#pragma once

#include <stdint.h>

#include "esp_err.h"

typedef struct {
    uint16_t voltage_mv;
    int16_t current_ma;
    int32_t power_mw;
} ina226_measurement_t;

esp_err_t ina226_init(void);
esp_err_t ina226_read_measurement(ina226_measurement_t *measurement);
