#include <dlfcn.h>

#include <cstring>
#include <filesystem>

#include "adapter.hh"

extern "C" __attribute__((visibility("default"))) auto tsugumi_main(
    const tsugumi_call* call, tsugumi_bytes* result) -> int;

namespace {

auto LoadCard() noexcept -> tsugumi_main_fn {
  try {
    Dl_info info{};
    if (!dladdr(reinterpret_cast<const void*>(&tsugumi_main), &info)) {
      return nullptr;
    }
    const auto path =
        std::filesystem::path(info.dli_fname).parent_path() / "go-card.so";
    // The Go runtime and any Card goroutines live until the worker exits.
    void* library = dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
    return library ? reinterpret_cast<tsugumi_main_fn>(
                         dlsym(library, "tsugumi_go_entry"))
                   : nullptr;
  } catch (...) {
    return nullptr;
  }
}

auto LoadFailure(const tsugumi_call&) -> Tsugumi::Card::CardResultT {
  tsugumi::abi::Reject(tsugumi::abi::FailureKind::worker_failed,
                        "Could not load the Go Card library");
}

}  // namespace

extern "C" auto tsugumi_main(const tsugumi_call* call, tsugumi_bytes* result)
    -> int {
  // A different ABI can pass only the leading version. The Go shared
  // library's constructor starts the runtime, so load it after this gate.
  if (!call || !result) return 1;
  uint32_t version;
  std::memcpy(&version, call, sizeof(version));
  if (version != TSUGUMI_ABI_VERSION) return 1;

  static const auto entry = LoadCard();
  return entry ? entry(call, result)
               : tsugumi::abi::Invoke(call, result, LoadFailure);
}
