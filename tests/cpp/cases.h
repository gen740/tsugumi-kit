#pragma once

#include "../wire.h"

enum cpp_input_case {
  CORRUPT_ENVELOPE_OFFSET = DUPLICATE_PORT + 1,
  CORRUPT_PARAMS_OFFSET,
  CORRUPT_PORT_OFFSET,
  RESERVED_IDENTIFIERS,
  MERGE_TEMPERATURE,
  MERGE_READING,
  MERGE_BAD_BYTES,
  MERGE_EMPTY
};

#ifdef __cplusplus
extern "C" {
#endif

/* Accepts input_case and cpp_input_case values. The returned input lives
   until the next call to make_input or make_cpp_input. */
tsugumi_bytes make_cpp_input(int test_case, int mode, int fd);
void check_cpp_success(tsugumi_bytes result);
void check_merge(tsugumi_bytes result, int scenario);
void check_cpp_failure(tsugumi_bytes result, int mode);

#ifdef __cplusplus
}
#endif
