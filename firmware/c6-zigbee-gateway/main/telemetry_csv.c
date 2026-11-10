#include "telemetry_csv.h"

#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include "csv_format.h"
#include "esp_log.h"
#include "esp_partition.h"
#include "esp_spiffs.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "nvs.h"

typedef struct {
    int64_t received_ms;
    uint32_t generation;
    uint16_t short_addr;
    emergency_telemetry_t sample;
} csv_event_t;

static const char *TAG = "telemetry_csv";
static SemaphoreHandle_t s_lock;
static QueueHandle_t s_queue;
static telemetry_csv_status_t s_status;
static uint32_t s_generation;
static bool s_worker_started;
static uint32_t s_drops;

static esp_err_t create_file(void)
{
    s_status.recording = false;
    s_status.bytes = 0;
    FILE *f = fopen(CSV_FILE_PATH, "w");
    if (!f) return ESP_FAIL;
    size_t length = strlen(CSV_HEADER);
    bool ok = fwrite(CSV_HEADER, 1, length, f) == length && fflush(f) == 0 && fsync(fileno(f)) == 0;
    if (fclose(f) != 0) ok = false;
    if (!ok) return ESP_FAIL;
    s_status.bytes = length;
    s_status.full = false;
    s_status.recording = s_worker_started;
    return ESP_OK;
}

static void writer_task(void *arg)
{
    csv_event_t event;
    char row[256];
    for (;;) {
        xQueueReceive(s_queue, &event, portMAX_DELAY);
        int length = csv_format_row(row, sizeof(row), s_status.boot_id, event.received_ms,
                                    event.short_addr, &event.sample);
        xSemaphoreTake(s_lock, portMAX_DELAY);
        if (event.generation != __atomic_load_n(&s_generation, __ATOMIC_RELAXED)) {
            xSemaphoreGive(s_lock);
            continue;
        }
        if (!s_status.recording) {
            s_status.skipped++;
            xSemaphoreGive(s_lock);
            continue;
        }
        if (length > 0 && s_status.bytes + length > CSV_MAX_BYTES) {
            s_status.full = true;
            s_status.recording = false;
            s_status.skipped++;
            xSemaphoreGive(s_lock);
            ESP_LOGW(TAG, "CSV full; download and explicitly clear it to resume recording");
            continue;
        }
        FILE *f = length > 0 ? fopen(CSV_FILE_PATH, "a") : NULL;
        bool ok = f && fwrite(row, 1, length, f) == (size_t)length && fflush(f) == 0 && fsync(fileno(f)) == 0;
        if (f && fclose(f) != 0) ok = false;
        if (ok) {
            s_status.bytes += length;
        } else {
            /* Preserve all previously committed rows, including on a partial write. */
            FILE *repair = fopen(CSV_FILE_PATH, "r+");
            if (repair) {
                if (ftruncate(fileno(repair), s_status.bytes) == 0) fsync(fileno(repair));
                fclose(repair);
            }
            s_status.recording = false;
            s_status.write_errors++;
            s_status.skipped++;
        }
        xSemaphoreGive(s_lock);
        if (!ok) ESP_LOGE(TAG, "CSV write failed; recording stopped, existing data retained");
    }
}

static esp_err_t mount_storage(void)
{
    const esp_partition_t *partition = esp_partition_find_first(ESP_PARTITION_TYPE_DATA,
                                                               ESP_PARTITION_SUBTYPE_DATA_SPIFFS, "telemetry");
    if (!partition) return ESP_ERR_NOT_FOUND;
    esp_vfs_spiffs_conf_t cfg = {
        .base_path = "/telemetry", .partition_label = "telemetry", .max_files = 3,
        .format_if_mount_failed = false,
    };
    esp_err_t err = esp_vfs_spiffs_register(&cfg);
    if (err == ESP_OK) return ESP_OK;
    /* A failed mount alone never authorizes erasing data. Check every byte. */
    uint8_t buffer[512];
    for (size_t offset = 0; offset < partition->size; offset += sizeof(buffer)) {
        if (esp_partition_read(partition, offset, buffer, sizeof(buffer)) != ESP_OK) return err;
        for (size_t i = 0; i < sizeof(buffer); i++) {
            if (buffer[i] != 0xff) return err;
        }
    }
    err = esp_spiffs_format("telemetry");
    return err == ESP_OK ? esp_vfs_spiffs_register(&cfg) : err;
}

esp_err_t telemetry_csv_init(void)
{
    s_lock = xSemaphoreCreateMutex();
    s_queue = xQueueCreate(64, sizeof(csv_event_t));
    if (!s_lock || !s_queue) return ESP_ERR_NO_MEM;
    nvs_handle_t nvs;
    esp_err_t err = nvs_open("csv_logger", NVS_READWRITE, &nvs);
    if (err != ESP_OK) return err;
    uint32_t previous = 0;
    err = nvs_get_u32(nvs, "boot_id", &previous);
    if (err == ESP_ERR_NVS_NOT_FOUND) err = ESP_OK;
    if (err == ESP_OK && previous == UINT32_MAX) err = ESP_ERR_INVALID_STATE;
    s_status.boot_id = previous + 1;
    if (err == ESP_OK) err = nvs_set_u32(nvs, "boot_id", s_status.boot_id);
    if (err == ESP_OK) err = nvs_commit(nvs);
    nvs_close(nvs);
    if (err != ESP_OK) return err;
    err = mount_storage();
    if (err != ESP_OK) return err;
    s_status.mounted = true;
    if (xTaskCreate(writer_task, "csv_writer", 4096, NULL, 3, NULL) != pdPASS) return ESP_ERR_NO_MEM;
    s_worker_started = true;
    FILE *f = fopen(CSV_FILE_PATH, "r");
    if (!f) {
        /* Only create on ENOENT, never truncate an inaccessible existing file. */
        if (errno != ENOENT) return ESP_FAIL;
        err = create_file();
    } else {
        char header[256];
        bool ok = fgets(header, sizeof(header), f) && strcmp(header, CSV_HEADER) == 0;
        if (fseek(f, 0, SEEK_END) != 0) ok = false;
        long size = ftell(f);
        if (size <= 0) ok = false;
        bool incomplete = false;
        if (ok && (fseek(f, -1, SEEK_END) != 0 || fgetc(f) != '\n')) {
            /* Keep an interrupted append untouched, but allow downloading the
             * complete prefix. Recording stays stopped until explicit clear. */
            incomplete = true;
            char block[256];
            long end = size;
            size = 0;
            while (end > 0 && !size) {
                size_t count = end > (long)sizeof(block) ? sizeof(block) : (size_t)end;
                long start = end - count;
                if (fseek(f, start, SEEK_SET) != 0 || fread(block, 1, count, f) != count) break;
                for (size_t i = count; i > 0; i--) {
                    if (block[i - 1] == '\n') {
                        size = start + i;
                        break;
                    }
                }
                end = start;
            }
            if (!size) ok = false;
        }
        fclose(f);
        if (!ok) return ESP_ERR_INVALID_STATE;
        s_status.bytes = (size_t)size;
        if (incomplete) {
            s_status.write_errors++;
            return ESP_ERR_INVALID_STATE;
        }
    }
    if (err != ESP_OK) return err;
    s_status.full = s_status.bytes >= CSV_MAX_BYTES;
    s_status.recording = !s_status.full;
    ESP_LOGI(TAG, "CSV ready: %u bytes, boot=%lu", (unsigned)s_status.bytes, (unsigned long)s_status.boot_id);
    return ESP_OK;
}

void telemetry_csv_enqueue(uint16_t short_addr, const emergency_telemetry_t *sample)
{
    csv_event_t event = {
        .received_ms = esp_timer_get_time() / 1000,
        .generation = __atomic_load_n(&s_generation, __ATOMIC_RELAXED),
        .short_addr = short_addr, .sample = *sample,
    };
    if (!s_queue || !s_worker_started || xQueueSend(s_queue, &event, 0) != pdTRUE) {
        __atomic_fetch_add(&s_drops, 1, __ATOMIC_RELAXED);
    }
}

void telemetry_csv_status(telemetry_csv_status_t *status)
{
    if (s_lock) xSemaphoreTake(s_lock, portMAX_DELAY);
    *status = s_status;
    status->queue_drops = __atomic_load_n(&s_drops, __ATOMIC_RELAXED);
    if (s_lock) xSemaphoreGive(s_lock);
}

esp_err_t telemetry_csv_snapshot(size_t *bytes)
{
    telemetry_csv_status_t status;
    telemetry_csv_status(&status);
    if (!status.mounted || !status.bytes) return ESP_ERR_INVALID_STATE;
    *bytes = status.bytes;
    return ESP_OK;
}

esp_err_t telemetry_csv_read(size_t offset, void *buffer, size_t length)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    FILE *f = fopen(CSV_FILE_PATH, "r");
    bool ok = f && fseek(f, offset, SEEK_SET) == 0 && fread(buffer, 1, length, f) == length;
    if (f) fclose(f);
    xSemaphoreGive(s_lock);
    return ok ? ESP_OK : ESP_FAIL;
}

esp_err_t telemetry_csv_clear(void)
{
    if (!s_lock) return ESP_ERR_INVALID_STATE;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    esp_err_t err = ESP_ERR_INVALID_STATE;
    if (s_status.mounted) {
        s_status.recording = false;
        __atomic_fetch_add(&s_generation, 1, __ATOMIC_RELAXED);
        xQueueReset(s_queue);
        err = create_file();
        if (err != ESP_OK) s_status.write_errors++;
    }
    xSemaphoreGive(s_lock);
    return err;
}
