/* Keep the public entry and its version check tied to the vendored C ABI. */
#include <stdlib.h>
#include <string.h>

#include "card_abi.h"

int tsugumi_rust_result(tsugumi_bytes* result, const uint8_t* data,
                        size_t size) {
  uint8_t* owned = malloc(size);
  if (!owned) return 1;
  memcpy(owned, data, size);
  *result = (tsugumi_bytes){owned, size};
  return 0;
}

extern int tsugumi_rust_invoke(tsugumi_bytes input, void* host,
                               const tsugumi_effects* effects,
                               tsugumi_bytes* result);

__attribute__((visibility("default"))) int tsugumi_main(
    const tsugumi_call* call, tsugumi_bytes* result) {
  if (!call || !result) return 1;
  uint32_t version;
  memcpy(&version, call, sizeof(version));
  if (version != TSUGUMI_ABI_VERSION) return 1;

  *result = (tsugumi_bytes){0};
  return tsugumi_rust_invoke(call->input, call->host, call->effects, result);
}
