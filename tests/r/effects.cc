#define R_NO_REMAP
#include <R_ext/Rdynload.h>
#include <Rinternals.h>

#include <cstdint>
#include <cstdlib>
#include <thread>

namespace {
using Send = int (*)(const char*, const uint8_t*, size_t);
using Recv = int (*)(const char*, int64_t, uint8_t**, size_t*);

#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wcast-function-type-mismatch"
#elif defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wcast-function-type"
#endif
template <typename Effect>
auto GetEffect(const char* name) -> Effect {
  return reinterpret_cast<Effect>(R_GetCCallable("tsugumi", name));
}
#if defined(__clang__)
#pragma clang diagnostic pop
#elif defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
}  // namespace

extern "C" auto start_effects() -> SEXP {
  const auto send = GetEffect<Send>("send");
  const auto recv = GetEffect<Recv>("recv");
  std::thread([send, recv] {
    uint8_t* data = nullptr;
    size_t size = 0;
    if (recv("background", -1, &data, &size) == 0)
      send("background", data, size);
    std::free(data);
  }).detach();
  return R_NilValue;
}
