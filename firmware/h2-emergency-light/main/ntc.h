#pragma once

#include <stdint.h>

#include "esp_err.h"

esp_err_t ntc_init(void);
esp_err_t ntc_read_centi_c(int16_t *temperature);
