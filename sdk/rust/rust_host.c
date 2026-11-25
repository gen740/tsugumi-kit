/* Keep the public entry and its version check tied to the vendored C ABI. */
#include <string.h>

#include "card_abi.h"

extern int tsugumi_rust_invoke(const tsugumi_input* inputs, size_t input_count,
                               void* host, const tsugumi_effects* effects,
                               tsugumi_bytes* result);

__attribute__((visibility("default"))) int tsugumi_main(
    const tsugumi_call* call, tsugumi_bytes* result) {
  if (!call || !result) return 1;
  uint32_t version;
  memcpy(&version, call, sizeof(version));
  if (version != TSUGUMI_ABI_VERSION) return 1;

  *result = (tsugumi_bytes){0};
  if (!call->effects || !call->effects->result_alloc) return 1;
  return tsugumi_rust_invoke(call->inputs, call->input_count, call->host,
                             call->effects, result);
}
