#pragma once

#include <stdio.h>
#include <stdlib.h>

#include "tsugumi/abi9/abi.h"

#define CHECK(condition)                                              \
  do {                                                                \
    if (!(condition)) {                                               \
      fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #condition); \
      abort();                                                        \
    }                                                                 \
  } while (0)

enum input_case {
  VALID,
  OPTIONAL_PORTS,
  MALFORMED_ENVELOPE,
  MALFORMED_PARAMS,
  MALFORMED_PORT,
  MISSING_INPUT,
  UNKNOWN_KIND,
  WRONG_KIND,
  UNKNOWN_PORT,
  DUPLICATE_PORT
};

#ifdef __cplusplus
extern "C" {
#endif

/* The returned input lives until the next call to make_input. */
tsugumi_bytes make_input(enum input_case test_case, int mode, int fd);
void check_success(tsugumi_bytes result, int optional_ports);
void check_input_failure(tsugumi_bytes result, enum input_case test_case);
void check_card_failure(tsugumi_bytes result, int mode);
void check_display(tsugumi_bytes display, int record, unsigned index);

#ifdef __cplusplus
}
#endif
