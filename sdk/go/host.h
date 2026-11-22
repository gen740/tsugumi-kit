#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

int tsugumi_go_verify_input(size_t index, const uint8_t* data, size_t size);
int tsugumi_go_output(void* result, size_t index, const uint8_t* data,
                      size_t size);
int tsugumi_go_failure(void* result, uint16_t kind, const char* message,
                       size_t message_size, const char* source,
                       size_t source_size, uint32_t line, const char* cell,
                       size_t cell_size);

int tsugumi_go_send(const char* view, const uint8_t* data, size_t size);
int tsugumi_go_cancelled(void);
int64_t tsugumi_go_remaining_ms(void);

#ifdef __cplusplus
}
#endif
