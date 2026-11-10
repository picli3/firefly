#pragma once

#include <stdint.h>

#include "esp_err.h"

esp_err_t telemetry_init(uint32_t device_id);
esp_err_t telemetry_start(void);
void telemetry_wake(void);
