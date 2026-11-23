#include <stdint.h>

#include "wire.h"

/* A version probe can supply only the prefix of tsugumi_call. */
void check_version(tsugumi_main_fn entry) {
  _Alignas(tsugumi_call) const uint32_t version = TSUGUMI_ABI_VERSION + 1;
  tsugumi_bytes result = {NULL, 123};
  CHECK(entry((const tsugumi_call*)&version, &result) != 0);
  CHECK(!result.data && result.size == 123);
  CHECK(entry(NULL, &result) != 0);
}
