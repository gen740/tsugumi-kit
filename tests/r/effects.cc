#define R_NO_REMAP
#include <R_ext/Rdynload.h>
#include <Rinternals.h>

#include <cstdint>
#include <cstdlib>
#include <thread>

#include "tsugumi/abi/card_abi.h"

namespace {
using OpenView = int (*)(const char*, uint32_t, uint32_t, uint32_t,
                         tsugumi_route*, tsugumi_view_session*);
using Send = int (*)(const tsugumi_view_session*, const uint8_t*, size_t);
using Recv = int (*)(int64_t, tsugumi_view_event*);

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
  const auto open_view = GetEffect<OpenView>("view_open");
  const auto send = GetEffect<Send>("send");
  const auto recv = GetEffect<Recv>("recv");
  std::thread([open_view, send, recv] {
    tsugumi_view_event event{};
    if (recv(-1, &event) == 0 && event.kind == 0) {
      tsugumi_route route{};
      tsugumi_view_session session{};
      if (open_view("background", TSUGUMI_VIEW_PANEL, 0, 0, &route, &session) ==
          0)
        send(&session, event.data.data, event.data.size);
    }
    std::free(const_cast<uint8_t*>(event.data.data));
  }).detach();
  return R_NilValue;
}
