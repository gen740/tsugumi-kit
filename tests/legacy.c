#include <stdint.h>
#include <string.h>

// The version and phase prefix of the ABI 8 call. The ABI 8 entry has one
// argument. The rest of the old call is irrelevant to this fixture.
struct abi8_call {
  uint32_t abi_version;
  uint32_t phase;
};

int tsugumi_main(const struct abi8_call* call) {
  uint32_t version;
  memcpy(&version, call, sizeof(version));
  return version != 8u || call->phase != 3u;
}
