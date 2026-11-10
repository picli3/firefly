#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

typedef struct {
    uint32_t device_id;
    uint16_t short_addr;
    uint16_t last_test_id;
    uint32_t last_seen_s;
} node_record_t;

esp_err_t node_registry_init(void);
void node_registry_register(uint32_t device_id, uint16_t short_addr);
bool node_registry_find_recent(uint32_t device_id, uint16_t *short_addr);
size_t node_registry_snapshot_sorted(node_record_t *nodes, size_t capacity);
bool node_registry_mark_test_report(uint32_t device_id, uint16_t test_id);
