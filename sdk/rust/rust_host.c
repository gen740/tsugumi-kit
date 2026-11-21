/* Keep the public entry and its version check tied to the vendored C ABI. */
#include <string.h>

#include "abi.h"

extern int tsugumi_rust_invoke(tsugumi_bytes input, void* host,
                               const tsugumi_effects* effects,
                               tsugumi_bytes* result);

__attribute__((visibility("default"))) int tsugumi_main(
    const tsugumi_call* call, tsugumi_bytes* result) {
  uint32_t version = 0;
  if (call) memcpy(&version, call, sizeof(version));
  if (version != TSUGUMI_ABI_VERSION || !result) return 1;

  *result = (tsugumi_bytes){0};
  return tsugumi_rust_invoke(call->input, call->host, call->effects, result);
}
