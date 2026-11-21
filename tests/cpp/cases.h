#pragma once

#include "../wire.h"

enum cpp_input_case {
  CORRUPT_ENVELOPE_OFFSET = DUPLICATE_PORT + 1,
  CORRUPT_PARAMS_OFFSET,
  CORRUPT_PORT_OFFSET,
  RESERVED_IDENTIFIERS
};

#ifdef __cplusplus
extern "C" {
#endif

/* Accepts input_case and cpp_input_case values. The returned input lives
   until the next call to make_input or make_cpp_input. */
tsugumi_bytes make_cpp_input(int test_case, int mode, int fd);
void check_cpp_success(tsugumi_bytes result);
void check_cpp_failure(tsugumi_bytes result, int mode);

#ifdef __cplusplus
}
#endif
