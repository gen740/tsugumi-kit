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
void check_display(const char* view, tsugumi_bytes bytes, unsigned index);

#ifdef __cplusplus
}
#endif

#ifdef __cplusplus
#include <span>
#include <string_view>

inline auto ReadDisplayConfig(std::span<const uint8_t>& bytes, bool binary)
    -> std::string_view {
  size_t size = bytes.size();
  if (binary) {
    CHECK(bytes.size() >= 4);
    size = uint32_t{bytes[0]} | (uint32_t{bytes[1]} << 8) |
           (uint32_t{bytes[2]} << 16) | (uint32_t{bytes[3]} << 24);
    bytes = bytes.subspan(4);
    CHECK(size <= bytes.size());
  }
  const std::string_view config(reinterpret_cast<const char*>(bytes.data()),
                                size);
  bytes = bytes.subspan(size);
  return config;
}
#endif
