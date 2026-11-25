#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
  const char* name;
  const char* schema;
  const uint8_t* data;
  size_t size;
} tsugumi_go_input;

tsugumi_go_input tsugumi_go_read_input(const void* inputs, size_t index);
int tsugumi_go_verify_input(size_t index, const uint8_t* data, size_t size);
int tsugumi_go_output(void* result, size_t index, const uint8_t* data,
                      size_t size);
int tsugumi_go_failure(void* result, uint16_t kind, const char* message,
                       size_t message_size, const char* source,
                       size_t source_size, uint32_t line, const char* cell,
                       size_t cell_size);

int tsugumi_go_view_open(const char* view, uint32_t mode, uint32_t flags,
                         uint32_t width, uint32_t height, uint8_t* route,
                         uint8_t* session);
int tsugumi_go_view_close(const uint8_t* session);
int tsugumi_go_route_register(const uint8_t* session, uint8_t* route);
int tsugumi_go_route_unregister(const uint8_t* route);
int tsugumi_go_send(const uint8_t* session, const uint8_t* data, size_t size,
                    int framed);
/* On success, the caller frees the returned data or path. */
int tsugumi_go_recv(int64_t timeout_ms, uint32_t* kind, uint8_t* route,
                    uint8_t* session, uint8_t** data, size_t* size);
int tsugumi_go_artifact_dir(int keep, uint8_t* id, char** path);
int tsugumi_go_artifact_upload(const char* path, int keep, uint8_t* id);
int tsugumi_go_artifact_open(const uint8_t* id, char** path);
int tsugumi_go_cancelled(void);
int64_t tsugumi_go_remaining_ms(void);

#ifdef __cplusplus
}
#endif
