#pragma once

#include <stdio.h>
#include <stdlib.h>

#include "tsugumi/abi/card_abi.h"

#define CHECK(condition)                                              \
  do {                                                                \
    if (!(condition)) {                                               \
      fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #condition); \
      abort();                                                        \
    }                                                                 \
  } while (0)

enum input_case {
  VALID_NUMBER,
  VALID_TEXT,
  MISSING_OUTPUT,
  MALFORMED_ENVELOPE,
  MALFORMED_PORT,
  MISSING_INPUT,
  UNKNOWN_SCHEMA,
  UNKNOWN_PORT,
  DUPLICATE_PORT,
  UNKNOWN_ARTIFACT
};

#ifdef __cplusplus
extern "C" {
#endif

/* The returned input lives until the next call to make_input. */
tsugumi_bytes make_input(enum input_case scenario);
void check_result(tsugumi_bytes result, enum input_case scenario,
                  const tsugumi_artifact_id* expected, size_t count);

#ifdef __cplusplus
}
#endif
