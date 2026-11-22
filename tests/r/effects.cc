#define R_NO_REMAP
#include <R_ext/Rdynload.h>
#include <Rinternals.h>
#include <unistd.h>

#include <string_view>
#include <thread>
#include <vector>

#include "tsugumi/protocol/card_generated.h"
#include "wire.h"

namespace {

using Display = int (*)(tsugumi_bytes, int);
using Cancelled = bool (*)();
using RemainingMs = double (*)();

std::thread background;

// R_GetCCallable uses an erased function pointer as its registry value.
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

extern "C" auto start_effects(SEXP input_fd) -> SEXP {
  const auto display = GetEffect<Display>("display");
  const auto cancelled = GetEffect<Cancelled>("cancelled");
  const auto remaining_ms = GetEffect<RemainingMs>("remaining_ms");
  const int fd = Rf_asInteger(input_fd);
  std::thread helper([=] {
    CHECK(!cancelled());
    CHECK(remaining_ms() == 500);
  });
  helper.join();
  background = std::thread([=] {
    char signal;
    CHECK(read(fd, &signal, 1) == 1);
    CHECK(cancelled());
    CHECK(remaining_ms() == 0);

    using namespace Tsugumi::Card;
    flatbuffers::FlatBufferBuilder buffer;
    const auto view = buffer.CreateString("tsugumi:text");
    const std::string_view config = R"({"text":"outside"})";
    const std::vector blobs{CreateCardDisplayBlob(
        buffer, buffer.CreateString("config"),
        buffer.CreateVector(reinterpret_cast<const uint8_t*>(config.data()),
                            config.size()))};
    buffer.Finish(CreateCardDisplay(buffer, view, buffer.CreateVector(blobs)));
    CHECK(display({buffer.GetBufferPointer(), buffer.GetSize()}, 0) == 0);
  });
  return R_NilValue;
}

extern "C" auto join_effects() -> SEXP {
  background.join();
  return R_NilValue;
}
