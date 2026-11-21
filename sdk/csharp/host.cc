// The CLR and its native library are owned by the worker process.
#include <coreclr_delegates.h>
#include <dlfcn.h>
#include <hostfxr.h>

#include "adapter.hh"

namespace {

using namespace tsugumi::abi9;
using Tsugumi::Card::CardResult;
using Tsugumi::Card::CardResultT;

auto Display(const uint8_t* data, size_t size, int record) noexcept -> int {
  try {
    return SendDisplay({data, size}, record != 0);
  } catch (...) {
    return 1;
  }
}

auto Cancelled() noexcept -> int { return card::cancelled(); }

auto RemainingMs() noexcept -> int64_t { return card::remaining_ms(); }

// No worker structs cross into managed code. The sink copies while the
// managed result is pinned, and no callback can unwind through the CLR.
using Sink = int (*)(void*, const uint8_t*, size_t);
using Entry = int (*)(const uint8_t*, size_t, decltype(&Display),
                      decltype(&Cancelled), decltype(&RemainingMs), void*,
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
    result->UnPackTo(static_cast<CardResultT*>(context));
    return 0;
  } catch (...) {
    return 1;
  }
}

auto Run(tsugumi_bytes bytes) -> CardResultT {
  if (!Buffer(bytes).Verify<Tsugumi::Card::CardInput>())
    Reject(FailureKind::invalid_request, "Invalid CardInput");
  static const Entry entry = LoadCard();
  if (!entry)
    Reject(FailureKind::worker_failed, "Could not load the .NET Card");

  CardResultT result;
  if (entry(bytes.data, bytes.size, Display, Cancelled, RemainingMs, &result,
            ReceiveResult) != 0)
    Reject(FailureKind::worker_failed,
           "Could not build the managed CardResult");
  return result;
}

}  // namespace

extern "C" __attribute__((visibility("default"))) auto tsugumi_main(
    const tsugumi_call* call, tsugumi_bytes* result) -> int {
  return Invoke(call, result, Run);
}
