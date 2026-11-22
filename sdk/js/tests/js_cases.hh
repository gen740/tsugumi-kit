#pragma once

#include "wire.h"

enum js_merge_case {
  MERGE_TEMPERATURE,
  MERGE_READING,
  MERGE_BAD_BYTES,
  MERGE_EMPTY
};

extern "C" {
/* The returned input lives until the next call to make_merge_input. */
auto make_merge_input(int scenario) -> tsugumi_bytes;
auto check_merge(tsugumi_bytes result, int scenario) -> void;
auto check_file_output(tsugumi_bytes result) -> void;
}
