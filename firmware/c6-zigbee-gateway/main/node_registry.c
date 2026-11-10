#include "node_registry.h"

#include <inttypes.h>
#include <string.h>

#include "app_config.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

static const char *TAG = "node_registry";
static SemaphoreHandle_t s_mutex;
static node_record_t s_nodes[APP_MAX_TRACKED_NODES];
static size_t s_node_count;

static int find_node(uint32_t device_id)
{
    for (size_t i = 0; i < s_node_count; i++) {
        if (s_nodes[i].device_id == device_id) return (int)i;
    }
    return -1;
}

esp_err_t node_registry_init(void)
{
    s_mutex = xSemaphoreCreateMutex();
    return s_mutex ? ESP_OK : ESP_ERR_NO_MEM;
}

void node_registry_register(uint32_t device_id, uint16_t short_addr)
{
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    int index = find_node(device_id);
    if (index < 0 && s_node_count < APP_MAX_TRACKED_NODES) {
        index = (int)s_node_count++;
        s_nodes[index].device_id = device_id;
        ESP_LOGI(TAG, "Registered node=%08" PRIx32 " short=0x%04x total=%u",
                 device_id, short_addr, (unsigned)s_node_count);
    }
    if (index >= 0) {
        s_nodes[index].short_addr = short_addr;
        s_nodes[index].last_seen_s = (uint32_t)(esp_timer_get_time() / 1000000ULL);
    } else {
        ESP_LOGW(TAG, "Node table full; ignored node=%08" PRIx32, device_id);
    }
    xSemaphoreGive(s_mutex);
}

size_t node_registry_snapshot_sorted(node_record_t *nodes, size_t capacity)
{
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    size_t count = s_node_count < capacity ? s_node_count : capacity;
    memcpy(nodes, s_nodes, count * sizeof(nodes[0]));
    xSemaphoreGive(s_mutex);

    for (size_t i = 0; i + 1 < count; i++) {
        for (size_t j = i + 1; j < count; j++) {
            if (nodes[j].device_id < nodes[i].device_id) {
                node_record_t temp = nodes[i];
                nodes[i] = nodes[j];
                nodes[j] = temp;
            }
        }
    }
    return count;
}

bool node_registry_find_recent(uint32_t device_id, uint16_t *short_addr)
{
    const uint32_t now = (uint32_t)(esp_timer_get_time() / 1000000ULL);
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    int index = find_node(device_id);
    bool found = index >= 0 && now - s_nodes[index].last_seen_s <= 120;
    if (found) {
        *short_addr = s_nodes[index].short_addr;
        for (size_t i = 0; i < s_node_count; i++) {
            if ((int)i != index && s_nodes[i].short_addr == *short_addr &&
                now - s_nodes[i].last_seen_s <= 120) {
                found = false;
                break;
            }
        }
    }
    xSemaphoreGive(s_mutex);
    return found;
}

bool node_registry_mark_test_report(uint32_t device_id, uint16_t test_id)
{
    bool is_new = false;
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    int index = find_node(device_id);
    if (index >= 0 && s_nodes[index].last_test_id != test_id) {
        s_nodes[index].last_test_id = test_id;
        is_new = true;
    }
    xSemaphoreGive(s_mutex);
    return is_new;
}
