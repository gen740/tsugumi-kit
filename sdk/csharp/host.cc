// The CLR and its native library are owned by the worker process.
#include <coreclr_delegates.h>
#include <dlfcn.h>
#include <hostfxr.h>

#include <cstring>

#include "adapter.hh"

namespace {

using namespace tsugumi::abi;
using tsugumi::abi::ResultBuffer;
using Tsugumi::Card::CardResult;

auto ViewOpen(const char* view, uint32_t mode, uint32_t flags, uint32_t width,
              uint32_t height, uint8_t* route, uint8_t* session) noexcept
    -> int {
  try {
    const auto opened =
        card::display::view_open(view, mode, flags, width, height);
    std::memcpy(route, opened.route.bytes, sizeof(opened.route.bytes));
    std::memcpy(session, opened.session.bytes, sizeof(opened.session.bytes));
    return 0;
  } catch (...) {
    return -1;
  }
}

template <class Id>
auto ReadViewId(const uint8_t* bytes) -> Id {
  Id id{};
  std::memcpy(id.bytes, bytes, sizeof(id.bytes));
  return id;
}

auto ViewClose(const uint8_t* session) noexcept -> int {
  try {
    card::display::view_close(ReadViewId<tsugumi_view_session>(session));
    return 0;
  } catch (...) {
    return -1;
  }
}

auto RouteRegister(const uint8_t* session, uint8_t* out) noexcept -> int {
  try {
    const auto route = card::display::route_register(
        ReadViewId<tsugumi_view_session>(session));
    std::memcpy(out, route.bytes, sizeof(route.bytes));
    return 0;
  } catch (const card::display::WindowClosed&) {
    return TSUGUMI_VIEW_WINDOW_CLOSED;
  } catch (...) {
    return -1;
  }
}

auto RouteUnregister(const uint8_t* route) noexcept -> int {
  try {
    card::display::route_unregister(ReadViewId<tsugumi_route>(route));
    return 0;
  } catch (...) {
    return -1;
  }
}

auto Send(const uint8_t* id, const uint8_t* data, size_t size,
          int framed) noexcept -> int {
  try {
    const auto session = ReadViewId<tsugumi_view_session>(id);
    return framed ? SendDisplay(session, {data, size})
                  : card::display::send(session, {data, size});
  } catch (...) {
    return -1;
  }
}

auto Recv(int64_t timeout, uint8_t* route, uint8_t* session,
          tsugumi_bytes* bytes) noexcept -> int {
  try {
    static thread_local std::vector<uint8_t> payload;
    auto received = card::display::recv(timeout);
    using card::display::ReceiveStatus;
    if (received.status == ReceiveStatus::kMessage ||
        received.status == ReceiveStatus::kClose) {
      std::memcpy(route, received.route.bytes, sizeof(received.route.bytes));
      std::memcpy(session, received.session.bytes,
                  sizeof(received.session.bytes));
    }
    payload = std::move(received.bytes);
    *bytes = {payload.data(), payload.size()};
    return static_cast<int>(received.status);
  } catch (...) {
    return -1;
  }
}

auto ArtifactPath(const std::filesystem::path& path, tsugumi_bytes* out)
    -> void {
  static thread_local std::string storage;
  storage = path.native();
  *out = {reinterpret_cast<const uint8_t*>(storage.data()), storage.size()};
}

auto ArtifactDir(int keep, uint8_t* id, tsugumi_bytes* out) noexcept -> int {
  try {
    const auto [artifact, path] = card::Artifact::dir(keep != 0);
    ArtifactPath(path, out);
    std::memcpy(id, artifact.id->bytes()->Data(), 16);
    return 0;
  } catch (...) {
    return -1;
  }
}

auto ArtifactUpload(const char* path, int keep, uint8_t* id) noexcept -> int {
  try {
    const auto artifact = card::Artifact::upload(path, keep != 0);
    std::memcpy(id, artifact.id->bytes()->Data(), 16);
    return 0;
  } catch (...) {
    return -1;
  }
}

auto ArtifactOpen(const uint8_t* id, tsugumi_bytes* out) noexcept -> int {
  try {
    card::Artifact artifact;
    artifact.id = std::make_unique<tsugumi::ArtifactId>(
        flatbuffers::span<const uint8_t, 16>(id, 16));
    ArtifactPath(artifact.open(), out);
    return 0;
  } catch (...) {
    return -1;
  }
}

auto Cancelled() noexcept -> int { return card::cancelled(); }

auto RemainingMs() noexcept -> int64_t { return card::remaining_ms(); }

// Only input descriptors cross into managed code. The sink copies while the
// managed result is pinned, and no callback can unwind through the CLR.
struct ManagedEffects {
  decltype(&ViewOpen) view_open;
  decltype(&ViewClose) view_close;
  decltype(&RouteRegister) route_register;
  decltype(&RouteUnregister) route_unregister;
  decltype(&Send) send;
  decltype(&Recv) recv;
  decltype(&Cancelled) cancelled;
  decltype(&RemainingMs) remaining;
  decltype(&ArtifactDir) artifact_dir;
  decltype(&ArtifactUpload) artifact_upload;
  decltype(&ArtifactOpen) artifact_open;
};

constexpr ManagedEffects effects{
    ViewOpen, ViewClose, RouteRegister, RouteUnregister, Send, Recv,
    Cancelled, RemainingMs, ArtifactDir, ArtifactUpload, ArtifactOpen};
using Sink = int (*)(void*, const uint8_t*, size_t);
using Entry = int (*)(const tsugumi_input*, size_t, const ManagedEffects*, void*,
                      Sink);

auto LoadCard() -> Entry {
  const auto library = ::dlopen(TSUGUMI_HOSTFXR, RTLD_NOW | RTLD_LOCAL);
  if (!library) return nullptr;

  const auto initialize =
      reinterpret_cast<hostfxr_initialize_for_runtime_config_fn>(
          ::dlsym(library, "hostfxr_initialize_for_runtime_config"));
  const auto get_delegate = reinterpret_cast<hostfxr_get_runtime_delegate_fn>(
      ::dlsym(library, "hostfxr_get_runtime_delegate"));
  const auto close =
      reinterpret_cast<hostfxr_close_fn>(::dlsym(library, "hostfxr_close"));
  if (!initialize || !get_delegate || !close) return nullptr;

  const hostfxr_initialize_parameters options{sizeof(options), nullptr,
                                              TSUGUMI_DOTNET_ROOT};
  hostfxr_handle handle = nullptr;
  const int status =
      initialize(TSUGUMI_CSHARP_RUNTIMECONFIG, &options, &handle);
  if (status < 0 || !handle) return nullptr;
  void* loader = nullptr;
  const int delegate_status =
      get_delegate(handle, hdt_load_assembly_and_get_function_pointer, &loader);
  close(handle);
  if (delegate_status != 0 || !loader) return nullptr;

  const auto load =
      reinterpret_cast<load_assembly_and_get_function_pointer_fn>(loader);
  void* entry = nullptr;
  if (load(TSUGUMI_CSHARP_ASSEMBLY, "Tsugumi.Entry, Card", "Execute",
           UNMANAGEDCALLERSONLY_METHOD, nullptr, &entry) != 0)
    return nullptr;
  return reinterpret_cast<Entry>(entry);
}

auto ReceiveResult(void* context, const uint8_t* data, size_t size) noexcept
    -> int {
  try {
    const Buffer buffer({data, size});
    const auto* result = buffer.Verify<CardResult>();
    if (!result) return 1;
    *static_cast<ResultBuffer*>(context) = ResultBuffer({data, size});
    return 0;
  } catch (...) {
    return 1;
  }
}

auto Run(const tsugumi_call& call) -> ResultBuffer {
  static const Entry entry = LoadCard();
  if (!entry)
    Reject(FailureKind::worker_failed, "Could not load the .NET Card");

  ResultBuffer result;
  if (entry(call.inputs, call.input_count, &effects, &result, ReceiveResult) !=
      0)
    Reject(FailureKind::worker_failed,
           "Could not build the managed CardResult");
  return result;
}

}  // namespace

extern "C" __attribute__((visibility("default"))) auto tsugumi_main(
    const tsugumi_call* call, tsugumi_bytes* result) -> int {
  return Invoke(call, result, Run);
}
