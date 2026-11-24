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

typedef struct ruby_view_event {
  uint32_t kind;
  uint8_t route[16];
  uint8_t session[16];
  uint8_t* data;
  size_t size;
} ruby_view_event;

/* The caller owns the returned string and frees it with free(). */
char* tsugumi_ruby_invoke(ruby_bytes input);
int tsugumi_ruby_view_open(const char* view, uint32_t mode, uint32_t width,
                           uint32_t height, uint8_t* route, uint8_t* session);
int tsugumi_ruby_route_register(const uint8_t* session, uint8_t* route);
int tsugumi_ruby_route_unregister(const uint8_t* route);
int tsugumi_ruby_send(const uint8_t* session, ruby_bytes bytes, int framed);
int tsugumi_ruby_cancelled(int* value);
int tsugumi_ruby_remaining_ms(int64_t* value);
/* The caller frees any received event data with free(). */
int tsugumi_ruby_recv(int64_t timeout_ms, ruby_view_event* event);
char* tsugumi_ruby_artifact_dir(int keep, uint8_t* id);
int tsugumi_ruby_artifact_upload(const char* path, int keep, uint8_t* id);
/* Sets *path to the directory, or on failure to the message or NULL; the
   caller frees it. */
int tsugumi_ruby_artifact_open(const uint8_t* id, char** path);

#ifdef __cplusplus
}
#endif
