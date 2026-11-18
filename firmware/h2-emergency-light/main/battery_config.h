#pragma once
#include <stdbool.h>
#include "emergency_protocol.h"
#include "esp_err.h"

typedef struct {
    battery_thresholds_t thresholds;
    uint32_t revision;
} battery_config_snapshot_t;
typedef esp_err_t (*battery_config_sender_t)(const battery_config_ack_t *);
esp_err_t battery_config_init(uint32_t device_id, battery_config_sender_t sender);
esp_err_t battery_config_receive(const battery_config_request_t *request);
void battery_config_snapshot(battery_config_snapshot_t *snapshot);
void battery_config_begin_test(battery_config_snapshot_t *snapshot);
void battery_config_end_test(void);
uint32_t battery_config_boot_id(void);
