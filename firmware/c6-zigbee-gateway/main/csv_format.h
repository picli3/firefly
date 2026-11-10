#pragma once

#include <stddef.h>
#include <stdint.h>
#include "emergency_protocol.h"

extern const char CSV_HEADER[];
/* Returns bytes excluding NUL, or -1 if the buffer is too small. */
int csv_format_row(char *buffer, size_t capacity, uint32_t boot_id, int64_t received_ms,
                   uint16_t short_addr, const emergency_telemetry_t *sample);
uint32_t csv_crc32_update(uint32_t state, const void *data, size_t length);
