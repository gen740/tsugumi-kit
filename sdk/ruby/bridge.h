#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ruby_bytes {
  const char* data;
  size_t size;
} ruby_bytes;

/* The caller owns the returned string and frees it with free(). */
char* tsugumi_ruby_invoke(ruby_bytes input);
int tsugumi_ruby_send(const char* view, ruby_bytes bytes);
int tsugumi_ruby_cancelled(int* value);
int tsugumi_ruby_remaining_ms(int64_t* value);

#ifdef __cplusplus
}
#endif
