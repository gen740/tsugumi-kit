#include "host.hh"

#include <dlfcn.h>
#include <jni.h>

#include <algorithm>
#include <array>
#include <exception>
#include <limits>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

namespace tsugumi::abi {
namespace {

using Tsugumi::Card::CardInput;
using Tsugumi::Card::CardResult;

auto ThrowEffectError(JNIEnv* env, const char* message) -> void {
  if (env->ExceptionCheck()) return;
  jclass type = env->FindClass("java/lang/IllegalStateException");
  if (type) env->ThrowNew(type, message);
}

auto ReadString(JNIEnv* env, jbyteArray data) -> std::string {
  const auto size = env->GetArrayLength(data);
  std::string result(size, '\0');
  if (size) {
    env->GetByteArrayRegion(data, 0, size,
                            reinterpret_cast<jbyte*>(result.data()));
  }
  return result;
}

auto CopyBytes(JNIEnv* env, const void* data, size_t size) -> jbyteArray {
  if (size > static_cast<size_t>(std::numeric_limits<jsize>::max())) {
    ThrowEffectError(env, "Effect result exceeds the Java array limit");
    return nullptr;
  }
  const auto length = static_cast<jsize>(size);
  auto result = env->NewByteArray(length);
  if (result && length) {
    env->SetByteArrayRegion(result, 0, length, static_cast<const jbyte*>(data));
  }
  return result;
}

template <class Id>
auto ReadViewId(JNIEnv* env, jbyteArray array, Id& id) -> bool {
  if (env->GetArrayLength(array) != static_cast<jsize>(sizeof(id.bytes))) {
    ThrowEffectError(env, "Expected a 16-byte view ID");
    return false;
  }
  env->GetByteArrayRegion(array, 0, sizeof(id.bytes),
                          reinterpret_cast<jbyte*>(id.bytes));
  return !env->ExceptionCheck();
}

auto ViewOpen(JNIEnv* env, jclass, jbyteArray view, jint mode, jlong width,
              jlong height, jbyteArray route, jbyteArray session) noexcept
    -> void {
  try {
    const auto name = ReadString(env, view);
    if (env->ExceptionCheck()) return;
    const auto opened = card::display::view_open(
        name, static_cast<uint32_t>(mode), static_cast<uint32_t>(width),
        static_cast<uint32_t>(height));
    env->SetByteArrayRegion(route, 0, sizeof(opened.route.bytes),
                            reinterpret_cast<const jbyte*>(opened.route.bytes));
    if (!env->ExceptionCheck())
      env->SetByteArrayRegion(
          session, 0, sizeof(opened.session.bytes),
          reinterpret_cast<const jbyte*>(opened.session.bytes));
  } catch (const std::exception& error) {
    ThrowEffectError(env, error.what());
  }
}

auto RouteRegister(JNIEnv* env, jclass, jbyteArray id) noexcept -> jbyteArray {
  try {
    tsugumi_view_session session{};
    if (!ReadViewId(env, id, session)) return nullptr;
    const auto route = card::display::route_register(session);
    return CopyBytes(env, route.bytes, sizeof(route.bytes));
  } catch (const std::exception& error) {
    ThrowEffectError(env, error.what());
    return nullptr;
  }
}

auto RouteUnregister(JNIEnv* env, jclass, jbyteArray id) noexcept -> void {
  try {
    tsugumi_route route{};
    if (ReadViewId(env, id, route)) card::display::route_unregister(route);
  } catch (const std::exception& error) {
    ThrowEffectError(env, error.what());
  }
}

auto Send(JNIEnv* env, jclass, jbyteArray id, jbyteArray payload,
          jboolean framed) noexcept -> void {
  try {
    tsugumi_view_session session{};
    if (!ReadViewId(env, id, session)) return;
    const auto size = env->GetArrayLength(payload);
    if (!framed && static_cast<size_t>(size) > TSUGUMI_MESSAGE_MAX) {
      ThrowEffectError(env, "View message exceeds the size limit");
      return;
    }
    std::vector<uint8_t> bytes(size);
    if (size) {
      env->GetByteArrayRegion(payload, 0, size,
                              reinterpret_cast<jbyte*>(bytes.data()));
      if (env->ExceptionCheck()) return;
    }
    const int status = framed
                           ? SendDisplay(session, {bytes.data(), bytes.size()})
                           : card::display::send(session, bytes);
    if (status != 0)
      ThrowEffectError(env, "The host could not accept the view message");
  } catch (const std::exception& error) {
    ThrowEffectError(env, error.what());
  }
}

auto Recv(JNIEnv* env, jclass, jlong timeout, jintArray status,
          jbyteArray route, jbyteArray session) noexcept -> jbyteArray {
  try {
    const auto received = card::display::recv(timeout);
    const jint code = static_cast<jint>(received.status);
    env->SetIntArrayRegion(status, 0, 1, &code);
    if (env->ExceptionCheck()) return nullptr;
    using card::display::ReceiveStatus;
    if (received.status == ReceiveStatus::kMessage ||
        received.status == ReceiveStatus::kClose) {
      env->SetByteArrayRegion(
          route, 0, sizeof(received.route.bytes),
          reinterpret_cast<const jbyte*>(received.route.bytes));
      if (env->ExceptionCheck()) return nullptr;
      env->SetByteArrayRegion(
          session, 0, sizeof(received.session.bytes),
          reinterpret_cast<const jbyte*>(received.session.bytes));
      if (env->ExceptionCheck()) return nullptr;
    }
    return CopyBytes(env, received.bytes.data(), received.bytes.size());
  } catch (const std::exception& error) {
    ThrowEffectError(env, error.what());
    return nullptr;
  }
}

auto ArtifactDir(JNIEnv* env, jclass, jbyteArray id, jboolean keep) noexcept
    -> jbyteArray {
  try {
    const auto [artifact, path] = card::Artifact::dir(keep == JNI_TRUE);
    env->SetByteArrayRegion(
        id, 0, 16,
        reinterpret_cast<const jbyte*>(artifact.id->bytes()->Data()));
    if (env->ExceptionCheck()) return nullptr;
    const auto& native = path.native();
    return CopyBytes(env, native.data(), native.size());
  } catch (...) {
    ThrowEffectError(env, "The host could not create an Artifact");
    return nullptr;
  }
}

auto ArtifactUpload(JNIEnv* env, jclass, jbyteArray source,
                    jboolean keep) noexcept -> jbyteArray {
  try {
    const auto path = ReadString(env, source);
    if (env->ExceptionCheck()) return nullptr;
    const auto artifact = card::Artifact::upload(path, keep == JNI_TRUE);
    return CopyBytes(env, artifact.id->bytes()->Data(), 16);
  } catch (...) {
    ThrowEffectError(env, "Could not upload the Artifact");
    return nullptr;
  }
}

auto ArtifactOpen(JNIEnv* env, jclass, jbyteArray id) noexcept -> jbyteArray {
  try {
    std::array<uint8_t, 16> bytes{};
    env->GetByteArrayRegion(id, 0, bytes.size(),
                            reinterpret_cast<jbyte*>(bytes.data()));
    if (env->ExceptionCheck()) return nullptr;
    card::Artifact artifact;
    artifact.id = std::make_unique<ArtifactId>(
        flatbuffers::span<const uint8_t, 16>(bytes));
    const auto path = artifact.open();
    return CopyBytes(env, path.native().data(), path.native().size());
  } catch (const std::exception& error) {
    ThrowEffectError(env, error.what());
    return nullptr;
  } catch (...) {
    ThrowEffectError(env, "The host could not open the Artifact");
    return nullptr;
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

extern "C" auto tsugumi_java_vm() -> JavaVM*;

auto NewCardLoader(JNIEnv* env) -> jobject {
  const auto file = env->FindClass("java/io/File");
  const auto uri = env->FindClass("java/net/URI");
  const auto url = env->FindClass("java/net/URL");
  const auto loader = env->FindClass("java/net/URLClassLoader");
  const auto base = env->FindClass("java/lang/ClassLoader");
  if (!file || !uri || !url || !loader || !base) return nullptr;

  const auto init = env->GetMethodID(file, "<init>", "(Ljava/lang/String;)V");
  const auto to_uri = env->GetMethodID(file, "toURI", "()Ljava/net/URI;");
  const auto to_url = env->GetMethodID(uri, "toURL", "()Ljava/net/URL;");
  const auto parent = env->GetStaticMethodID(base, "getPlatformClassLoader",
                                             "()Ljava/lang/ClassLoader;");
  const auto make = env->GetStaticMethodID(
      loader, "newInstance",
      "([Ljava/net/URL;Ljava/lang/ClassLoader;)Ljava/net/URLClassLoader;");
  if (!init || !to_uri || !to_url || !parent || !make) return nullptr;

  std::vector<std::string> paths;
  std::string_view classpath(TSUGUMI_JAVA_CLASSPATH);
  while (!classpath.empty()) {
    const auto separator = classpath.find(':');
    paths.emplace_back(classpath.substr(0, separator));
    if (separator == std::string_view::npos) break;
    classpath.remove_prefix(separator + 1);
  }
  const auto urls =
      env->NewObjectArray(static_cast<jsize>(paths.size()), url, nullptr);
  if (!urls) return nullptr;
  for (size_t i = 0; i < paths.size(); ++i) {
    const LocalFrame frame(env);
    if (!frame) return nullptr;
    const auto path = env->NewStringUTF(paths[i].c_str());
    if (!path) return nullptr;
    const auto location = env->NewObject(file, init, path);
    if (!location) return nullptr;
    const auto address = env->CallObjectMethod(location, to_uri);
    if (!address || env->ExceptionCheck()) return nullptr;
    const auto entry = env->CallObjectMethod(address, to_url);
    if (!entry || env->ExceptionCheck()) return nullptr;
    env->SetObjectArrayElement(urls, static_cast<jsize>(i), entry);
    if (env->ExceptionCheck()) return nullptr;
  }
  const auto platform = env->CallStaticObjectMethod(base, parent);
  if (!platform || env->ExceptionCheck()) return nullptr;
  return env->CallStaticObjectMethod(loader, make, urls, platform);
}

auto LoadClass(JNIEnv* env, jobject loader, const char* name) -> jclass {
  const auto type = env->FindClass("java/lang/ClassLoader");
  if (!type) return nullptr;
  const auto load = env->GetMethodID(type, "loadClass",
                                     "(Ljava/lang/String;)Ljava/lang/Class;");
  const auto class_name = env->NewStringUTF(name);
  if (!load || !class_name) return nullptr;
  return static_cast<jclass>(env->CallObjectMethod(loader, load, class_name));
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

    vm_ = tsugumi_java_vm();
    if (!vm_) {
      error_ = "Cannot start the embedded Java VM";
      return;
    }

    const Attachment attachment(vm_);
    auto* env = attachment.Env();
    if (!env) {
      error_ = "Cannot attach to the Java VM";
      return;
    }
    const LocalFrame frame(env);
    if (!frame || !Load(env)) {
      if (env->ExceptionCheck()) env->ExceptionClear();
      error_ = "Cannot load the Java Card adapter";
    }
  }

  auto Invoke(tsugumi_bytes bytes) const -> ResultBuffer {
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
    auto input = env->NewDirectByteBuffer(const_cast<uint8_t*>(bytes.data),
                                          static_cast<jlong>(bytes.size));
    if (!input) Failed(env, "Cannot borrow Java CardInput");

    auto output = static_cast<jbyteArray>(
        env->CallStaticObjectMethod(entry_, invoke_, input));
    if (!output || env->ExceptionCheck())
      Failed(env, "Cannot run the Java Card adapter");
    const auto length = env->GetArrayLength(output);
    ResultBuffer result(length);
    if (length)
      env->GetByteArrayRegion(output, 0, length,
                              reinterpret_cast<jbyte*>(result.data()));
    if (env->ExceptionCheck()) Failed(env, "Cannot copy Java CardResult");
    return result;
  }

 private:
  auto Load(JNIEnv* env) -> bool {
    const auto loader = NewCardLoader(env);
    if (!loader || env->ExceptionCheck()) return false;
    jclass effects = LoadClass(env, loader, "tsugumi.Effects");
    if (!effects) return false;
    const JNINativeMethod methods[] = {
        {const_cast<char*>("viewOpen"), const_cast<char*>("([BIJJ[B[B)V"),
         reinterpret_cast<void*>(ViewOpen)},
        {const_cast<char*>("routeRegister"), const_cast<char*>("([B)[B"),
         reinterpret_cast<void*>(RouteRegister)},
        {const_cast<char*>("routeUnregister"), const_cast<char*>("([B)V"),
         reinterpret_cast<void*>(RouteUnregister)},
        {const_cast<char*>("send"), const_cast<char*>("([B[BZ)V"),
         reinterpret_cast<void*>(&Send)},
        {const_cast<char*>("recv"), const_cast<char*>("(J[I[B[B)[B"),
         reinterpret_cast<void*>(&Recv)},
        {const_cast<char*>("artifactDir"), const_cast<char*>("([BZ)[B"),
         reinterpret_cast<void*>(&ArtifactDir)},
        {const_cast<char*>("artifactUpload"), const_cast<char*>("([BZ)[B"),
         reinterpret_cast<void*>(&ArtifactUpload)},
        {const_cast<char*>("artifactOpen"), const_cast<char*>("([B)[B"),
         reinterpret_cast<void*>(&ArtifactOpen)},
        {const_cast<char*>("cancelled"), const_cast<char*>("()Z"),
         reinterpret_cast<void*>(&Cancelled)},
        {const_cast<char*>("remainingMs"), const_cast<char*>("()J"),
         reinterpret_cast<void*>(&RemainingMs)},
    };
    if (env->RegisterNatives(effects, methods, std::size(methods)) != JNI_OK)
      return false;
    jclass entry = LoadClass(env, loader, "tsugumi.Entry");
    if (!entry) return false;
    invoke_ =
        env->GetStaticMethodID(entry, "invoke", "(Ljava/nio/ByteBuffer;)[B");
    if (!invoke_) return false;
    entry_ = static_cast<jclass>(env->NewGlobalRef(entry));
    return entry_ != nullptr;
  }

  JavaVM* vm_ = nullptr;
  jclass entry_ = nullptr;
  jmethodID invoke_ = nullptr;
  const char* error_ = nullptr;
};

auto CheckInputs(tsugumi_bytes bytes, std::span<const JavaInput> catalog)
    -> void {
  Input input(bytes);
  std::unordered_set<std::string_view> seen;
  for (const auto& candidate : catalog) {
    if (!seen.insert(candidate.name).second) continue;
    const auto port = input.ReadPort(candidate.name);
    if (!port)
      Reject(FailureKind::input_missing,
             "Missing input " + std::string(candidate.name));
    const auto match = std::ranges::find_if(catalog, [&](const auto& option) {
      return option.name == candidate.name && option.schema == port->schema;
    });
    if (match == catalog.end() || !match->verify(Buffer(port->bytes))) {
      Reject(FailureKind::input_invalid,
             "Invalid schema or FlatBuffer for " + std::string(candidate.name));
    }
  }
  input.CheckConsumed();
}

auto CheckOutputs(const CardResult& result, std::span<const JavaOutput> catalog)
    -> void {
  std::unordered_set<std::string_view> seen;
  if (const auto* ports = result.outputs()) {
    for (const auto* port : *ports) {
      const auto name = port->name()->string_view();
      const auto expected = std::ranges::find(catalog, name, &JavaOutput::name);
      if (expected == catalog.end() || !seen.insert(name).second)
        Reject(FailureKind::output_invalid,
               "Unexpected output " + std::string(name));
      const auto* bytes = port->bytes();
      if (!bytes || !expected->valid(Buffer({bytes->data(), bytes->size()})))
        Reject(FailureKind::output_invalid,
               "Invalid output " + std::string(name));
    }
  }
  for (const auto& port : catalog) {
    if (port.required && !seen.contains(port.name)) {
      Reject(FailureKind::output_invalid,
             "Missing output " + std::string(port.name));
    }
  }
}

}  // namespace

auto RunJava(tsugumi_bytes bytes, std::span<const JavaInput> inputs,
             std::span<const JavaOutput> outputs) -> ResultBuffer {
  CheckInputs(bytes, inputs);
  // JNI supports one JVM per process. Keep its classes and state warm.
  static const Jvm jvm;
  auto result_bytes = jvm.Invoke(bytes);
  const Buffer result_buffer(result_bytes.bytes());
  const auto* result = result_buffer.Verify<CardResult>();
  if (!result) Reject(FailureKind::worker_failed, "Invalid Java CardResult");
  if (!result->failure()) CheckOutputs(*result, outputs);
  return result_bytes;
}

}  // namespace tsugumi::abi
