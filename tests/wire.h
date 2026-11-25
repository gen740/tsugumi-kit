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
  VALID_LEGACY,
  MISSING_OUTPUT,
  MALFORMED_INPUTS,
  MALFORMED_PORT,
  MISSING_INPUT,
  MISSING_MERGE_SCHEMA,
  UNKNOWN_SCHEMA,
  UNKNOWN_SINGLE_SCHEMA,
  UNKNOWN_PORT,
  DUPLICATE_PORT,
  UNKNOWN_ARTIFACT
};

/* Match the maximum alignment of the generated FlatBuffers payloads. */
enum { WIRE_ALIGNMENT = 32 };

#ifdef __cplusplus
extern "C" {
#endif

/* The returned input array and its fields live until the next make_inputs. */
const tsugumi_input* make_inputs(enum input_case scenario, size_t* count);
void check_version(tsugumi_main_fn entry);
void check_result(tsugumi_bytes result, enum input_case scenario,
                  const tsugumi_artifact_id* expected, size_t count,
                  int offset);

#ifdef __cplusplus
}
#endif
