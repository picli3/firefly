#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
#include "emergency_protocol.h"

#define CSV_FILE_PATH "/telemetry/telemetry.csv"
#define CSV_MAX_BYTES (1400U * 1024U)

typedef struct {
    bool mounted;
    bool recording;
    bool full;
    size_t bytes;
    uint32_t boot_id;
    uint32_t queue_drops;
    uint32_t write_errors;
    uint32_t skipped;
} telemetry_csv_status_t;

esp_err_t telemetry_csv_init(void);
void telemetry_csv_enqueue(uint16_t short_addr, const emergency_telemetry_t *sample);
void telemetry_csv_status(telemetry_csv_status_t *status);
/* Snapshot size includes only completely synchronized rows. */
esp_err_t telemetry_csv_snapshot(size_t *bytes);
esp_err_t telemetry_csv_read(size_t offset, void *buffer, size_t length);
esp_err_t telemetry_csv_clear(void);
