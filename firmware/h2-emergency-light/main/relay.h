#pragma once

#include <stdbool.h>

#include "esp_err.h"

esp_err_t relay_init(void);
bool relay_ac_present(void);
bool relay_test_button_pressed(void);
void relay_set_test_active(bool active);
