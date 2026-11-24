#include "node_card.hh"

#include <dlfcn.h>
#include <node.h>
#include <uv.h>

#include <array>
#include <condition_variable>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "process.hh"

namespace tsugumi::abi {
namespace {

using Value = v8::Local<v8::Value>;
using CallbackInfo = v8::FunctionCallbackInfo<v8::Value>;

auto ThrowError(v8::Isolate* isolate, const char* message) -> void {
  isolate->ThrowException(v8::Exception::Error(
      v8::String::NewFromUtf8(isolate, message).ToLocalChecked()));
}

auto Cancelled(const CallbackInfo& args) -> void {
  try {
    args.GetReturnValue().Set(card::cancelled());
  } catch (...) {
    ThrowError(args.GetIsolate(), "The host cancellation query failed");
  }
}

auto RemainingMs(const CallbackInfo& args) -> void {
  try {
    args.GetReturnValue().Set(static_cast<double>(card::remaining_ms()));
  } catch (...) {
    ThrowError(args.GetIsolate(), "The host deadline query failed");
  }
}

auto Text(v8::Isolate* isolate, const char* value) -> v8::Local<v8::String> {
  return v8::String::NewFromUtf8(isolate, value).ToLocalChecked();
}

auto ViewName(v8::Isolate* isolate, Value value) -> std::string {
  if (!value->IsString()) throw std::invalid_argument("Expected a string");
  v8::String::Utf8Value string(isolate, value);
  if (!*string || std::memchr(*string, 0, string.length()))
    throw std::invalid_argument("Expected a string without NUL bytes");
  return std::string(*string, string.length());
}

auto CopyBytes(v8::Isolate* isolate, std::span<const uint8_t> bytes)
    -> v8::Local<v8::Uint8Array> {
  const auto buffer = v8::ArrayBuffer::New(isolate, bytes.size());
  if (!bytes.empty())
    std::memcpy(buffer->GetBackingStore()->Data(), bytes.data(), bytes.size());
  return v8::Uint8Array::New(buffer, 0, bytes.size());
}

template <class Id>
auto ReadViewId(Value value) -> Id {
  Id id{};
  if (!value->IsUint8Array())
    throw std::invalid_argument("Expected a 16-byte view ID");
  const auto bytes = value.As<v8::Uint8Array>();
  if (bytes->ByteLength() != sizeof(id.bytes) ||
      bytes->CopyContents(id.bytes, sizeof(id.bytes)) != sizeof(id.bytes))
    throw std::invalid_argument("Expected a 16-byte view ID");
  return id;
}

auto ViewOpen(const CallbackInfo& args) -> void {
  auto* isolate = args.GetIsolate();
  const auto context = isolate->GetCurrentContext();
  try {
    if (!args[1]->IsUint32() || !args[2]->IsUint32() || !args[3]->IsUint32() ||
        !args[4]->IsUint32())
      throw std::invalid_argument(
          "Expected an unsigned 32-bit mode, flags and dimensions");
    const auto opened = card::display::view_open(
        ViewName(isolate, args[0]), args[1].As<v8::Uint32>()->Value(),
        args[2].As<v8::Uint32>()->Value(), args[3].As<v8::Uint32>()->Value(),
        args[4].As<v8::Uint32>()->Value());
    const auto result = v8::Object::New(isolate);
    result
        ->Set(context, Text(isolate, "route"),
              CopyBytes(isolate, opened.route.bytes))
        .Check();
    result
        ->Set(context, Text(isolate, "session"),
              CopyBytes(isolate, opened.session.bytes))
        .Check();
    args.GetReturnValue().Set(result);
  } catch (const std::exception& error) {
    ThrowError(isolate, error.what());
  }
}

auto RouteRegister(const CallbackInfo& args) -> void {
  auto* isolate = args.GetIsolate();
  try {
    const auto route = card::display::route_register(
        ReadViewId<tsugumi_view_session>(args[0]));
    args.GetReturnValue().Set(CopyBytes(isolate, route.bytes));
  } catch (const std::exception& error) {
    ThrowError(isolate, error.what());
  }
}

auto RouteUnregister(const CallbackInfo& args) -> void {
  try {
    card::display::route_unregister(ReadViewId<tsugumi_route>(args[0]));
  } catch (const std::exception& error) {
    ThrowError(args.GetIsolate(), error.what());
  }
}

auto Send(const CallbackInfo& args) -> void {
  auto* isolate = args.GetIsolate();
  try {
    const auto session = ReadViewId<tsugumi_view_session>(args[0]);
    if (!args[1]->IsUint8Array())
      throw std::invalid_argument("Expected Uint8Array view data");
    const auto array = args[1].As<v8::Uint8Array>();
    if (array->HasBuffer() && array->Buffer()->WasDetached())
      throw std::invalid_argument("View data has a detached buffer");
    const bool framed = args[2]->IsTrue();
    if (!framed && array->ByteLength() > TSUGUMI_MESSAGE_MAX) {
      args.GetReturnValue().Set(-1);
      return;
    }
    std::vector<uint8_t> bytes(array->ByteLength());
    if (!bytes.empty() &&
        array->CopyContents(bytes.data(), bytes.size()) != bytes.size())
      throw std::runtime_error("Cannot copy the view data");
    args.GetReturnValue().Set(
        framed ? SendDisplay(session, {bytes.data(), bytes.size()})
               : card::display::send(session, bytes));
  } catch (const std::exception& error) {
    ThrowError(isolate, error.what());
  }
}

auto Receive(const CallbackInfo& args) -> void {
  auto* isolate = args.GetIsolate();
  const auto context = isolate->GetCurrentContext();
  try {
    const auto timeout = args[0]->IntegerValue(context).FromMaybe(-1);
    const auto message = card::display::recv(timeout);
    using card::display::ReceiveStatus;
    const char* status = "";
    switch (message.status) {
      case ReceiveStatus::kMessage:
        status = "message";
        break;
      case ReceiveStatus::kClose:
        status = "close";
        break;
      case ReceiveStatus::kTimeout:
        status = "timeout";
        break;
      case ReceiveStatus::kCancelled:
        status = "cancelled";
        break;
    }
    const auto result = v8::Object::New(isolate);
    result->Set(context, Text(isolate, "status"), Text(isolate, status))
        .Check();
    result
        ->Set(context, Text(isolate, "bytes"),
              CopyBytes(isolate, message.bytes))
        .Check();
    if (message.status == ReceiveStatus::kMessage ||
        message.status == ReceiveStatus::kClose) {
      result
          ->Set(context, Text(isolate, "route"),
                CopyBytes(isolate, message.route.bytes))
          .Check();
      result
          ->Set(context, Text(isolate, "session"),
                CopyBytes(isolate, message.session.bytes))
          .Check();
    }
    args.GetReturnValue().Set(result);
  } catch (const std::exception& error) {
    ThrowError(isolate, error.what());
  }
}

auto Artifact(const CallbackInfo& args) -> void {
  auto* isolate = args.GetIsolate();
  const auto context = isolate->GetCurrentContext();
  try {
    const auto operation = ViewName(isolate, args[0]);
    card::Artifact artifact;
    std::filesystem::path path;
    const bool keep = args[2]->BooleanValue(isolate);
    if (operation == "dir") {
      auto [created, directory] = card::Artifact::dir(keep);
      artifact = std::move(created);
      path = std::move(directory);
    } else if (operation == "upload") {
      artifact = card::Artifact::upload(ViewName(isolate, args[1]), keep);
    } else if (operation == "open" && args[1]->IsUint8Array()) {
      std::array<uint8_t, 16> id{};
      const auto bytes = args[1].As<v8::Uint8Array>();
      if (bytes->ByteLength() != id.size() ||
          bytes->CopyContents(id.data(), id.size()) != id.size())
        throw std::invalid_argument("Expected a 16-byte Artifact ID");
      artifact.id = std::make_unique<tsugumi::ArtifactId>(
          flatbuffers::span<const uint8_t, 16>(id));
      path = artifact.open();
    } else {
      throw std::invalid_argument("Unknown Artifact operation");
    }
    const auto result = v8::Array::New(isolate, 2);
    result
        ->Set(context, 0,
              CopyBytes(isolate, {artifact.id->bytes()->Data(), 16}))
        .Check();
    result->Set(context, 1, Text(isolate, path.c_str())).Check();
    args.GetReturnValue().Set(result);
  } catch (const std::exception& error) {
    ThrowError(isolate, error.what());
  }
}

auto BindEffects(v8::Local<v8::Object> exports, Value,
                 v8::Local<v8::Context> context, void*) -> void {
  auto* isolate = v8::Isolate::GetCurrent();
  const auto set = [&](const char* name, v8::FunctionCallback method) {
    const auto key = v8::String::NewFromUtf8(isolate, name).ToLocalChecked();
    exports
        ->Set(context, key, v8::Function::New(context, method).ToLocalChecked())
        .Check();
  };
  set("viewOpen", ViewOpen);
  set("routeRegister", RouteRegister);
  set("routeUnregister", RouteUnregister);
  set("send", Send);
  set("recv", Receive);
  set("artifact", Artifact);
  set("cancelled", Cancelled);
  set("remainingMs", RemainingMs);
}

class Runtime {
 public:
  Runtime() : thread_([this] { RunLoop(); }) {}

  Runtime(const Runtime&) = delete;
  auto operator=(const Runtime&) -> Runtime& = delete;

  auto Call(tsugumi_bytes bytes) -> ResultBuffer {
    Request request{.input = bytes};
    std::unique_lock lock(mutex_);
    changed_.wait(lock, [&] { return ready_ || stopped_; });
    if (stopped_) std::rethrow_exception(stop_error_);
    request_ = &request;
    if (uv_async_send(&wake_) != 0) {
      request_ = nullptr;
      throw std::runtime_error("Cannot wake the Node.js runtime");
    }
    changed_.wait(lock, [&] { return request_ == nullptr || stopped_; });
    request_ = nullptr;
    if (stopped_) std::rethrow_exception(stop_error_);
    return std::move(request.result);
  }

 private:
  static auto Stop(void* runtime) -> void {
    auto& self = *static_cast<Runtime*>(runtime);
    const std::lock_guard lock(self.mutex_);
    if (!self.stopped_) node::Stop(self.setup_->env());
  }

  static auto Join(void* runtime) -> bool {
    auto& self = *static_cast<Runtime*>(runtime);
    // A host effect may exit the process while on this thread.
    if (std::this_thread::get_id() == self.thread_.get_id()) return false;
    self.thread_.join();
    return true;
  }

  struct Request {
    tsugumi_bytes input;
    ResultBuffer result{};
  };

  auto Complete(ResultBuffer bytes) -> void {
    const std::lock_guard lock(mutex_);
    request_->result = std::move(bytes);
    request_ = nullptr;
    changed_.notify_all();
  }

  // A failure in the bootstrap or native bridge leaves the environment
  // unusable. Card errors take the regular CardResult path.
  auto Fail(std::exception_ptr error) -> void {
    execution_error_ = std::move(error);
    node::Stop(setup_->env());
  }

  static auto Self(const CallbackInfo& args) -> Runtime& {
    return *static_cast<Runtime*>(args.Data().As<v8::External>()->Value());
  }

  static auto Resolve(const CallbackInfo& args) -> void {
    auto& self = Self(args);
    try {
      if (!args[0]->IsUint8Array()) {
        throw std::runtime_error("Node.js SDK returned a non-byte result");
      }
      const auto array = args[0].As<v8::Uint8Array>();
      const auto size = array->ByteLength();
      ResultBuffer bytes(size);
      if (size) array->CopyContents(bytes.data(), size);
      self.Complete(std::move(bytes));
    } catch (...) {
      self.Fail(std::current_exception());
    }
  }

  static auto RejectPromise(const CallbackInfo& args) -> void {
    auto& self = Self(args);
    try {
      const v8::String::Utf8Value message(args.GetIsolate(), args[0]);
      throw std::runtime_error(*message ? *message
                                        : "Node.js SDK promise rejected");
    } catch (...) {
      self.Fail(std::current_exception());
    }
  }

  auto Invoke() -> void {
    auto* isolate = setup_->isolate();
    const v8::HandleScope scope(isolate);
    const auto context = setup_->context();
    v8::TryCatch caught(isolate);
    tsugumi_bytes input;
    {
      const std::lock_guard lock(mutex_);
      input = request_->input;
    }
    const auto array = v8::ArrayBuffer::New(isolate, input.size);
    std::memcpy(array->GetBackingStore()->Data(), input.data, input.size);
    std::array<Value, 4> args = {v8::Uint8Array::New(array, 0, input.size),
                                 entry_.Get(isolate), resolve_.Get(isolate),
                                 reject_.Get(isolate)};
    if (node::MakeCallback(isolate, context->Global(), invoke_.Get(isolate),
                           args.size(), args.data(), {0, 0})
            .IsEmpty()) {
      const v8::String::Utf8Value message(isolate, caught.Exception());
      throw std::runtime_error(*message ? *message
                                        : "Cannot invoke the Node.js Card");
    }
  }

  static auto Wake(uv_async_t* wake) -> void {
    auto& self = *static_cast<Runtime*>(wake->data);
    try {
      self.Invoke();
    } catch (...) {
      self.Fail(std::current_exception());
    }
  }

  auto SetUpCallbacks(v8::Local<v8::Context> context) -> void {
    auto* isolate = setup_->isolate();
    const auto self = v8::External::New(isolate, this);
    entry_.Reset(isolate,
                 v8::String::NewFromUtf8Literal(isolate, TSUGUMI_JS_ENTRY));
    resolve_.Reset(isolate,
                   v8::Function::New(context, Resolve, self).ToLocalChecked());
    reject_.Reset(
        isolate,
        v8::Function::New(context, RejectPromise, self).ToLocalChecked());
  }

  auto RunEnvironment(node::InitializationResult& initialization) -> void {
    std::vector<std::string> errors;
    using namespace node::EnvironmentFlags;
    constexpr auto flags = static_cast<Flags>(
        kNoCreateInspector | kNoGlobalSearchPaths | kNoStartDebugSignalHandler);
    auto args = initialization.args();
    args.emplace_back(TSUGUMI_JS_BOOTSTRAP);
    setup_ = node::CommonEnvironmentSetup::Create(
        initialization.platform(), &errors, args, initialization.exec_args(),
        flags);
    if (!setup_) {
      throw std::runtime_error(errors.empty()
                                   ? "Cannot create Node.js environment"
                                   : errors.front());
    }
    auto* isolate = setup_->isolate();
    const v8::Locker locker(isolate);
    const v8::Isolate::Scope isolate_scope(isolate);
    const v8::HandleScope handles(isolate);
    const auto context = setup_->context();
    const v8::Context::Scope context_scope(context);
    node::AddLinkedBinding(setup_->env(), "tsugumi_card", BindEffects, nullptr);
    node::SetProcessExitHandler(
        setup_->env(), [](node::Environment* env, int) { node::Stop(env); });
    {
      v8::TryCatch caught(isolate);
      Value loaded;
      if (!node::LoadEnvironment(
               setup_->env(),
               "return require('node:module').createRequire(process."
               "argv[1])(process.argv[1])")
               .ToLocal(&loaded) ||
          !loaded->IsFunction()) {
        const v8::String::Utf8Value message(isolate, caught.Exception());
        throw std::runtime_error(
            *message ? *message : "Cannot load the Node.js SDK bootstrap");
      }
      invoke_.Reset(isolate, loaded.As<v8::Function>());
    }
    SetUpCallbacks(context);
    if (uv_async_init(setup_->event_loop(), &wake_, Wake) != 0) {
      throw std::runtime_error(
          "Cannot create the Node.js event loop wake handle");
    }
    wake_.data = this;
    RegisterNodeEnvironment(this, Stop, Join);
    {
      const std::lock_guard lock(mutex_);
      ready_ = true;
      changed_.notify_all();
    }
    node::SpinEventLoop(setup_->env());
    uv_close(reinterpret_cast<uv_handle_t*>(&wake_), nullptr);
  }

  auto RunLoop() -> void {
    std::exception_ptr error;
    try {
      RunEnvironment(NodeProcess());
      if (execution_error_) std::rethrow_exception(execution_error_);
      throw std::runtime_error("The Node.js event loop stopped");
    } catch (...) {
      error = std::current_exception();
    }
    {
      const std::lock_guard lock(mutex_);
      stopped_ = true;
      stop_error_ = error;
      changed_.notify_all();
    }
    if (setup_) {
      const v8::Locker locker(setup_->isolate());
      invoke_.Reset();
      resolve_.Reset();
      reject_.Reset();
      entry_.Reset();
    }
    setup_.reset();
  }

  std::mutex mutex_;
  std::condition_variable changed_;
  Request* request_ = nullptr;
  bool ready_ = false;
  bool stopped_ = false;
  std::exception_ptr stop_error_;
  std::exception_ptr execution_error_;
  uv_async_t wake_{};
  std::unique_ptr<node::CommonEnvironmentSetup> setup_;
  v8::Global<v8::Function> invoke_, resolve_, reject_;
  v8::Global<v8::String> entry_;
  std::thread thread_;
};

// The libuv loop can dispatch a host effect even between calls. Keep the
// adapter and its runtime loaded until process exit. In particular, joining
// the Node thread during dlclose can deadlock on the dynamic loader lock.
auto StartRuntime() -> Runtime* {
  Dl_info card;
  if (!dladdr(reinterpret_cast<const void*>(&RunNodeCard), &card) ||
      !dlopen(card.dli_fname, RTLD_NOW | RTLD_LOCAL)) {
    throw std::runtime_error("Cannot retain the Node.js Card library");
  }
  return new Runtime;
}

}  // namespace

auto RunNodeCard(tsugumi_bytes input) -> ResultBuffer {
  static auto* runtime = StartRuntime();
  return runtime->Call(input);
}

}  // namespace tsugumi::abi
