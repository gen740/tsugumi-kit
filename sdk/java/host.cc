#include "host.hh"

#include <dlfcn.h>
#include <jni.h>

#include <algorithm>
#include <cstdlib>
#include <limits>
#include <string>
#include <unordered_set>
#include <vector>

namespace tsugumi::abi {
namespace {

using Tsugumi::Card::CardInput;
using Tsugumi::Card::CardResult;
using Tsugumi::Card::CardResultT;

auto ThrowEffectError(JNIEnv* env, const char* message) -> void {
  if (env->ExceptionCheck()) return;
  jclass type = env->FindClass("java/lang/IllegalStateException");
  if (type) env->ThrowNew(type, message);
}

auto Send(JNIEnv* env, jclass, jbyteArray view, jbyteArray payload) noexcept
    -> void {
  try {
    const auto view_size = env->GetArrayLength(view);
    std::string name(view_size, '\0');
    env->GetByteArrayRegion(view, 0, view_size,
                            reinterpret_cast<jbyte*>(name.data()));
    if (env->ExceptionCheck()) return;
    const auto size = env->GetArrayLength(payload);
    std::vector<uint8_t> bytes(size);
    if (size) {
      env->GetByteArrayRegion(payload, 0, size,
                              reinterpret_cast<jbyte*>(bytes.data()));
      if (env->ExceptionCheck()) return;
    }
    if (SendDisplay(name.c_str(), {bytes.data(), bytes.size()}) != 0) {
      ThrowEffectError(env, "The host could not accept the view message");
    }
  } catch (...) {
    ThrowEffectError(env, "Cannot send the view message");
  }
}

auto Cancelled(JNIEnv* env, jclass) noexcept -> jboolean {
  try {
    return card::cancelled();
  } catch (...) {
    ThrowEffectError(env, "The host cancellation query failed");
    return JNI_TRUE;
  }
}

auto RemainingMs(JNIEnv* env, jclass) noexcept -> jlong {
  try {
    return card::remaining_ms();
  } catch (...) {
    ThrowEffectError(env, "The host deadline query failed");
    return 0;
  }
}

class Attachment {
 public:
  explicit Attachment(JavaVM* vm) : vm_(vm) {
    void* env = nullptr;
    if (vm_->GetEnv(&env, JNI_VERSION_1_8) == JNI_EDETACHED) {
      attached_ = vm_->AttachCurrentThreadAsDaemon(&env, nullptr) == JNI_OK;
      if (!attached_) env = nullptr;
    }
    env_ = static_cast<JNIEnv*>(env);
  }

  ~Attachment() {
    if (attached_) vm_->DetachCurrentThread();
  }

  Attachment(const Attachment&) = delete;
  auto operator=(const Attachment&) -> Attachment& = delete;
  auto Env() const -> JNIEnv* { return env_; }

 private:
  JavaVM* vm_;
  JNIEnv* env_;
  bool attached_ = false;
};

class LocalFrame {
 public:
  explicit LocalFrame(JNIEnv* env)
      : env_(env), active_(env->PushLocalFrame(8) == JNI_OK) {}
  ~LocalFrame() {
    if (active_) env_->PopLocalFrame(nullptr);
  }
  LocalFrame(const LocalFrame&) = delete;
  auto operator=(const LocalFrame&) -> LocalFrame& = delete;
  explicit operator bool() const { return active_; }

 private:
  JNIEnv* env_;
  bool active_;
};

[[noreturn]] auto Failed(JNIEnv* env, const char* message) -> void {
  if (env->ExceptionCheck()) env->ExceptionClear();
  Reject(FailureKind::worker_failed, message);
}

class Jvm {
 public:
  Jvm() {
    // Java threads can call the registered natives for the life of the process.
    // Retain the image and its dependencies even if the loader closes its
    // handle.
    Dl_info image{};
    if (!dladdr(reinterpret_cast<const void*>(&Send), &image) ||
        !dlopen(image.dli_fname, RTLD_NOW | RTLD_LOCAL)) {
      error_ = "Cannot retain the Java Card library";
      return;
    }

    const char* scratch = std::getenv("TMPDIR");
    const std::string temporary = std::string("-Djava.io.tmpdir=") +
                                  (scratch && *scratch ? scratch : ".");
    const char* home_path = std::getenv("HOME");
    const std::string home = std::string("-Duser.home=") +
                             (home_path && *home_path ? home_path : ".");
    JavaVMOption options[] = {
        {const_cast<char*>("-Djava.class.path=" TSUGUMI_JAVA_CLASSPATH),
         nullptr},
        {const_cast<char*>("-Djava.awt.headless=true"), nullptr},
        {const_cast<char*>("-Xrs"), nullptr},
        {const_cast<char*>("-XX:-UsePerfData"), nullptr},
        {const_cast<char*>(temporary.c_str()), nullptr},
        {const_cast<char*>(home.c_str()), nullptr},
    };
    JavaVMInitArgs args{JNI_VERSION_1_8, std::size(options), options,
                        JNI_FALSE};
    void* env = nullptr;
    if (JNI_CreateJavaVM(&vm_, &env, &args) != JNI_OK) {
      error_ = "Cannot start the embedded Java VM";
      return;
    }

    auto* jni = static_cast<JNIEnv*>(env);
    if (!Load(jni)) {
      if (jni->ExceptionCheck()) jni->ExceptionClear();
      error_ = "Cannot load the Java Card adapter";
    }
    // The native worker thread may change between serial calls.
    vm_->DetachCurrentThread();
  }

  auto Invoke(tsugumi_bytes bytes) const -> std::vector<uint8_t> {
    if (error_) Reject(FailureKind::worker_failed, error_);
    const Attachment attachment(vm_);
    auto* env = attachment.Env();
    if (!env)
      Reject(FailureKind::worker_failed, "Cannot attach to the Java VM");
    const LocalFrame frame(env);
    if (!frame) Failed(env, "Cannot allocate Java JNI references");
    if (bytes.size > static_cast<size_t>(std::numeric_limits<jsize>::max())) {
      Reject(FailureKind::resource_limit,
             "CardInput exceeds the Java array limit");
    }
    const auto size = static_cast<jsize>(bytes.size);
    auto input = env->NewByteArray(size);
    if (!input) Failed(env, "Cannot allocate Java CardInput");
    env->SetByteArrayRegion(input, 0, size,
                            reinterpret_cast<const jbyte*>(bytes.data));
    if (env->ExceptionCheck()) Failed(env, "Cannot copy Java CardInput");

    auto output = static_cast<jbyteArray>(
        env->CallStaticObjectMethod(entry_, invoke_, input));
    if (!output || env->ExceptionCheck())
      Failed(env, "Cannot run the Java Card adapter");
    const auto length = env->GetArrayLength(output);
    std::vector<uint8_t> result(length);
    if (length)
      env->GetByteArrayRegion(output, 0, length,
                              reinterpret_cast<jbyte*>(result.data()));
    if (env->ExceptionCheck()) Failed(env, "Cannot copy Java CardResult");
    return result;
  }

 private:
  auto Load(JNIEnv* env) -> bool {
    jclass effects = env->FindClass("tsugumi/Effects");
    if (!effects) return false;
    const JNINativeMethod methods[] = {
        {const_cast<char*>("send"), const_cast<char*>("([B[B)V"),
         reinterpret_cast<void*>(&Send)},
        {const_cast<char*>("cancelled"), const_cast<char*>("()Z"),
         reinterpret_cast<void*>(&Cancelled)},
        {const_cast<char*>("remainingMs"), const_cast<char*>("()J"),
         reinterpret_cast<void*>(&RemainingMs)},
    };
    if (env->RegisterNatives(effects, methods, std::size(methods)) != JNI_OK)
      return false;
    jclass entry = env->FindClass("tsugumi/Entry");
    if (!entry) return false;
    invoke_ = env->GetStaticMethodID(entry, "invoke", "([B)[B");
    if (!invoke_) return false;
    entry_ = static_cast<jclass>(env->NewGlobalRef(entry));
    return entry_ != nullptr;
  }

  JavaVM* vm_ = nullptr;
  jclass entry_ = nullptr;
  jmethodID invoke_ = nullptr;
  const char* error_ = nullptr;
};

// Input ports are absent unless connected, so only required outputs can be
// missing. The Java Card decides which inputs it needs.
auto CheckPorts(const auto* ports, std::span<const JavaPort> catalog,
                FailureKind invalid) -> void {
  std::unordered_set<std::string_view> seen;
  if (ports) {
    for (const auto* port : *ports) {
      const auto name = port->name()->string_view();
      const auto expected = std::ranges::find(catalog, name, &JavaPort::name);
      if (expected == catalog.end() || port->kind() != expected->kind ||
          !seen.insert(name).second) {
        Reject(invalid, "Unexpected port " + std::string(name));
      }
      const auto* data = port->bytes();
      if (expected->kind == PortKind::Path) {
        if (!port->path() || port->path()->size() == 0 ||
            (data && data->size())) {
          Reject(invalid, "Invalid Path for " + std::string(name));
        }
      } else if (port->path() || !data ||
                 !expected->verify(Buffer({data->data(), data->size()}))) {
        Reject(invalid, "Invalid FlatBuffer for " + std::string(name));
      }
    }
  }
  for (const auto& port : catalog) {
    if (port.required && !seen.contains(port.name)) {
      Reject(invalid, "Missing port " + std::string(port.name));
    }
  }
}

}  // namespace

auto RunJava(tsugumi_bytes bytes, Verify params,
             std::span<const JavaPort> inputs,
             std::span<const JavaPort> outputs) -> CardResultT {
  const Buffer input_buffer(bytes);
  const auto* input = input_buffer.Verify<CardInput>();
  if (!input) Reject(FailureKind::invalid_request, "Invalid CardInput");
  const auto* param_bytes = input->params();
  if (!param_bytes ||
      !params(Buffer({param_bytes->data(), param_bytes->size()}))) {
    Reject(FailureKind::invalid_request, "Invalid FlatBuffer for params");
  }
  CheckPorts(input->inputs(), inputs, FailureKind::invalid_request);

  // JNI supports one JVM per process. Keep its classes and state warm.
  static const Jvm jvm;
  const auto result_bytes = jvm.Invoke(bytes);
  const Buffer result_buffer({result_bytes.data(), result_bytes.size()});
  const auto* result = result_buffer.Verify<CardResult>();
  if (!result) Reject(FailureKind::worker_failed, "Invalid Java CardResult");
  if (!result->failure()) {
    CheckPorts(result->outputs(), outputs, FailureKind::output_invalid);
  }
  CardResultT value;
  result->UnPackTo(&value);
  return value;
}

}  // namespace tsugumi::abi
