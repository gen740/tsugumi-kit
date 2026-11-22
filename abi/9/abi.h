/* ABI 9. Based on gen740/Tsugumi c935f278d9471105785c08e2cce820ac22ad3c89
 * (sdk/abi9/include/tsugumi/abi9/abi.h), with the 2026-11-22 send effect. */
#ifndef TSUGUMI_ABI9_ABI_H
#define TSUGUMI_ABI9_ABI_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define TSUGUMI_ABI_VERSION 9u

typedef struct tsugumi_bytes {
  const uint8_t* data;
  size_t size;
} tsugumi_bytes;

typedef struct tsugumi_effects {
  /* Process-lifetime callback, callable from Card threads between calls.
   * The host context for send is valid for the process lifetime. The host
   * must consume or copy view and bytes before returning. A nonzero return
   * means only that the host could not accept the message. The core forwards
   * the view name unchecked. The UI shows an error for an undeclared view.
   * The host does not record messages.
   */
  int (*send)(void* host, const char* view, const uint8_t* bytes, size_t size);
  /* These callbacks and their context are valid only during the call.
   * They must not reenter the SDK's cancellation or deadline queries.
   */
  int (*cancelled)(void* host);
  /* Remaining milliseconds, or -1 when there is no deadline. */
  int64_t (*remaining_ms)(void* host);
} tsugumi_effects;

typedef struct tsugumi_call {
  uint32_t abi_version; /* Checked before reading the rest of the layout. */
  tsugumi_bytes input;
  void* host;
  const tsugumi_effects* effects;
} tsugumi_call;

/* The host calls serially. A zero return writes a borrowed CardResult,
 * including for encoded Card failures. The bytes last until the next
 * call. A nonzero return makes no result available.
 */
typedef int (*tsugumi_main_fn)(const tsugumi_call* call, tsugumi_bytes* result);
int tsugumi_main(const tsugumi_call* call, tsugumi_bytes* result);

#ifdef __cplusplus
}
#endif

#endif /* TSUGUMI_ABI9_ABI_H */
