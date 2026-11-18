#include "remote_test.h"
#include "gateway_settings.h"
#ifdef CONFIG_GATEWAY_ETH_ENABLED
#include "cJSON.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "nvs.h"
#include "telemetry_log.h"
#include "test_campaign.h"
#include "zigbee_gateway.h"
#include <ctype.h>
#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>

#define RX_MAX 512
#define RECORD_VERSION 2
static const char *TAG = "remote_test";
static esp_mqtt_client_handle_t s_client;
static QueueHandle_t s_jobs;
static char s_boot[40];
static unsigned s_sequence;
typedef struct {
    uint32_t version;
    char command[37];
    uint32_t device;
    uint16_t test_id;
    int64_t issued_ms;
    uint8_t state; // 1=send uncertain, 2=sent to Zigbee, 3=rejected, 4=result
    battery_test_report_t report;
} record_v1_t;
/* Preserve the complete old record prefix for safe upgrade of dedup state. */
typedef struct {
    uint32_t version;
    char command[37];
    uint32_t device;
    uint16_t test_id;
    int64_t issued_ms;
    uint8_t state;
    battery_test_report_t report;
    uint32_t config_version, node_boot;
    bool has_meta;
} record_t;
static record_t s_record;
static int64_t s_highwater;
typedef struct {
    bool report;
    char payload[RX_MAX + 1];
    battery_test_report_t result;
    uint32_t config_version, node_boot;
    bool has_meta;
} job_t;
static bool store_record(void) {
    nvs_handle_t nvs;
    if (nvs_open("remote_test", NVS_READWRITE, &nvs) != ESP_OK)
        return false;
    esp_err_t e = nvs_set_blob(nvs, "last", &s_record, sizeof(s_record));
    if (e == ESP_OK)
        e = nvs_commit(nvs);
    nvs_close(nvs);
    return e == ESP_OK;
}
static void publish_json(const char *suffix, cJSON *json) {
    if (!json)
        return;
    char topic[128];
    snprintf(topic, sizeof(topic), "firefly/v1/gateways/%s/%s", gateway_settings_active()->gateway_id,
             suffix);
    char *raw = cJSON_PrintUnformatted(json);
    if (raw) {
        if (esp_mqtt_client_enqueue(s_client, topic, raw, 0, 1, 0, true) < 0)
            ESP_LOGW(TAG, "MQTT result could not be queued");
        cJSON_free(raw);
    }
    cJSON_Delete(json);
}
static void ack(const char *command, const char *device, int test_id, const char *status,
                const char *reason) {
    cJSON *j = cJSON_CreateObject();
    if (!j)
        return;
    char message[90];
    snprintf(message, sizeof(message), "%s-test-ack-%u", s_boot, ++s_sequence);
    cJSON_AddNumberToObject(j, "schema_version", 1);
    cJSON_AddStringToObject(j, "message_id", message);
    cJSON_AddStringToObject(j, "command_id", command);
    cJSON_AddStringToObject(j, "device_id", device);
    cJSON_AddNumberToObject(j, "test_id", test_id);
    cJSON_AddStringToObject(j, "status", status);
    if (reason)
        cJSON_AddStringToObject(j, "reason", reason);
    publish_json("test-ack", j);
}
static void publish_report(const battery_test_report_t *r, const char *command, bool live,
                           bool has_meta, uint32_t revision, uint32_t node_boot) {
    cJSON *j = cJSON_CreateObject();
    if (!j)
        return;
    char message[100], device[9];
    snprintf(device, sizeof(device), "%08" PRIx32, r->device_id);
    if (command)
        snprintf(message, sizeof(message), "%s-report", command);
    else
        snprintf(message, sizeof(message), "%s-test-report-%u", s_boot, ++s_sequence);
    cJSON_AddNumberToObject(j, "schema_version", 1);
    cJSON_AddStringToObject(j, "message_id", message);
    cJSON_AddStringToObject(j, "device_id", device);
    char boot[48];
    snprintf(boot, sizeof(boot), "h2-%08" PRIx32 "-%08" PRIx32, r->device_id, node_boot);
    cJSON_AddStringToObject(j, "boot_id", has_meta ? boot : s_boot);
    if (has_meta && revision) cJSON_AddNumberToObject(j, "config_version", revision);
    cJSON_AddBoolToObject(j, "live", live);
    if (command)
        cJSON_AddStringToObject(j, "command_id", command);
    cJSON_AddNumberToObject(j, "test_id", r->test_id);
    cJSON_AddStringToObject(j, "result", telemetry_log_test_result(r->result));
    cJSON_AddNumberToObject(j, "voltage_before_mv", r->voltage_before_mv);
    cJSON_AddNumberToObject(j, "voltage_min_mv", r->voltage_min_mv);
    cJSON_AddNumberToObject(j, "current_avg_ma", r->current_avg_ma);
    cJSON_AddNumberToObject(j, "current_peak_ma", r->current_peak_ma);
    cJSON_AddNumberToObject(j, "power_avg_mw", r->power_avg_mw);
    cJSON_AddNumberToObject(j, "temperature_max_centi_c", r->temperature_max_centi_c);
    cJSON_AddNumberToObject(j, "duration_s", r->duration_s);
    cJSON_AddNumberToObject(j, "sample_count", r->sample_count);
    cJSON_AddNumberToObject(j, "flags", r->flags);
    publish_json("test-report", j);
}
bool remote_test_ready(void) { return s_jobs && time(NULL) > 1704067200; }
static bool number(cJSON *j, const char *key, double *v) {
    cJSON *x = cJSON_GetObjectItemCaseSensitive(j, key);
    if (!cJSON_IsNumber(x) || !isfinite(x->valuedouble) || floor(x->valuedouble) != x->valuedouble)
        return false;
    *v = x->valuedouble;
    return true;
}
static bool uuid(const char *s) {
    if (!s || strlen(s) != 36)
        return false;
    for (int i = 0; i < 36; i++)
        if ((i == 8 || i == 13 || i == 18 || i == 23) ? s[i] != '-'
                                                      : !isxdigit((unsigned char)s[i]))
            return false;
    return true;
}
static void command(const char *payload) {
    cJSON *j = cJSON_Parse(payload);
    if (!j)
        return;
    cJSON *id = cJSON_GetObjectItemCaseSensitive(j, "command_id"),
          *device = cJSON_GetObjectItemCaseSensitive(j, "device_id");
    double version, test_id, duration, issued, expires;
    bool valid = number(j, "schema_version", &version) && version == 1 && cJSON_IsString(id) &&
                 uuid(id->valuestring) && cJSON_IsString(device) &&
                 strlen(device->valuestring) == 8 && number(j, "test_id", &test_id) &&
                 test_id >= 32768 && test_id <= 65535 && number(j, "duration_s", &duration) &&
                 duration == 30 && number(j, "issued_at_ms", &issued) && issued > 0 &&
                 issued < 9007199254740991.0 && number(j, "expires_at_ms", &expires) &&
                 expires > issued && expires - issued <= 31000;
    if (valid)
        for (int i = 0; i < 8; i++)
            if (!isxdigit((unsigned char)device->valuestring[i]))
                valid = false;
    if (!valid) {
        cJSON_Delete(j);
        return;
    }
    const char *cid = id->valuestring, *did = device->valuestring;
    uint32_t node = (uint32_t)strtoul(did, NULL, 16);
    if (!strcmp(cid, s_record.command)) {
        bool same = s_record.device == node && s_record.test_id == (uint16_t)test_id &&
                    s_record.issued_ms == (int64_t)issued;
        ack(cid, did, (int)test_id,
            same ? (s_record.state == 1 ? "unknown"
                                        : (s_record.state == 3 ? "rejected" : "accepted"))
                 : "rejected",
            same ? "Solicitud ya procesada; no se ejecuta otra vez"
                 : "UUID con contenido diferente");
        if (same && s_record.state == 4)
            publish_report(&s_record.report, s_record.command, false,
                           s_record.has_meta, s_record.config_version, s_record.node_boot);
        cJSON_Delete(j);
        return;
    }
    struct timeval tv;
    gettimeofday(&tv, NULL);
    int64_t now = (int64_t)tv.tv_sec * 1000 + tv.tv_usec / 1000;
    if (!remote_test_ready() || expires <= now || issued > now + 5000 || issued <= s_highwater) {
        ack(cid, did, (int)test_id, "rejected", "Orden vencida, antigua o reloj no sincronizado");
        cJSON_Delete(j);
        return;
    }
    nvs_handle_t gate;
    if (nvs_open("remote_test", NVS_READWRITE, &gate) != ESP_OK) {
        ack(cid, did, (int)test_id, "rejected", "NVS no disponible");
        cJSON_Delete(j);
        return;
    }
    esp_err_t saved = nvs_set_i64(gate, "highwater", (int64_t)issued);
    if (saved == ESP_OK)
        saved = nvs_commit(gate);
    nvs_close(gate);
    if (saved != ESP_OK) {
        ack(cid, did, (int)test_id, "rejected", "No se pudo registrar la orden");
        cJSON_Delete(j);
        return;
    }
    s_highwater = (int64_t)issued;
    if (s_record.state && s_record.issued_ms + 180000 > now &&
        (s_record.state == 1 || s_record.state == 2)) {
        ack(cid, did, (int)test_id, "rejected", "Otra prueba remota pendiente en este gateway");
        cJSON_Delete(j);
        return;
    }
    record_t previous = s_record;
    memset(&s_record, 0, sizeof(s_record));
    s_record.version = RECORD_VERSION;
    snprintf(s_record.command, sizeof(s_record.command), "%s", cid);
    s_record.device = node;
    s_record.test_id = (uint16_t)test_id;
    s_record.issued_ms = (int64_t)issued;
    s_record.state = 1;
    // Persist BEFORE sending. After a crash the order is uncertain, never replayed.
    if (!store_record()) {
        s_record = previous;
        ack(cid, did, (int)test_id, "rejected", "No se pudo persistir la solicitud");
        cJSON_Delete(j);
        return;
    }
    esp_err_t err = zigbee_gateway_start_remote_test(node, (uint16_t)test_id);
    s_record.state = err == ESP_OK ? 2 : 3;
    // If this commit fails, keep state=1 on disk so reboot stays fail-closed.
    (void)store_record();
    ack(cid, did, (int)test_id, err == ESP_OK ? "accepted" : "rejected",
        err == ESP_OK ? "Orden enviada por Zigbee; esperando resultado" : esp_err_to_name(err));
    cJSON_Delete(j);
}
static void task(void *arg) {
    (void)arg;
    if (s_record.state == 4)
        publish_report(&s_record.report, s_record.command, false,
                       s_record.has_meta, s_record.config_version, s_record.node_boot);
    job_t job;
    for (;;) {
        if (xQueueReceive(s_jobs, &job, portMAX_DELAY) != pdTRUE)
            continue;
        if (!job.report) {
            command(job.payload);
            continue;
        }
        bool match = s_record.state && s_record.state != 3 &&
                     s_record.device == job.result.device_id &&
                     s_record.test_id == job.result.test_id;
        if (match) {
            s_record.report = job.result;
            s_record.has_meta = job.has_meta;
            s_record.config_version = job.config_version;
            s_record.node_boot = job.node_boot;
            s_record.state = 4;
            (void)store_record();
            test_campaign_remote_done();
        }
        publish_report(&job.result, match ? s_record.command : NULL, true,
                       job.has_meta, job.config_version, job.node_boot);
    }
}
esp_err_t remote_test_init(esp_mqtt_client_handle_t client, const char *boot) {
    s_client = client;
    snprintf(s_boot, sizeof(s_boot), "%s", boot);
    nvs_handle_t nvs;
    size_t len = sizeof(s_record);
    if (nvs_open("remote_test", NVS_READONLY, &nvs) == ESP_OK) {
        esp_err_t loaded = nvs_get_blob(nvs, "last", &s_record, &len);
        bool legacy = loaded == ESP_OK && len == sizeof(record_v1_t) && s_record.version == 1;
        if (legacy) {
            s_record.version = RECORD_VERSION;
            s_record.has_meta = false;
            s_record.config_version = s_record.node_boot = 0;
        }
        if (loaded != ESP_OK || (!legacy && len != sizeof(s_record)) ||
            s_record.version != RECORD_VERSION || s_record.command[36] != 0 ||
            !uuid(s_record.command) || s_record.test_id < 32768 || s_record.state > 4)
            memset(&s_record, 0, sizeof(s_record));
        (void)nvs_get_i64(nvs, "highwater", &s_highwater);
        nvs_close(nvs);
    }
    if (s_record.state == 1 || s_record.state == 2)
        (void)test_campaign_reserve_remote(180);
    s_jobs = xQueueCreate(8, sizeof(job_t));
    if (!s_jobs)
        return ESP_ERR_NO_MEM;
    if (xTaskCreate(task, "remote_test", 6144, NULL, 4, NULL) != pdPASS) {
        vQueueDelete(s_jobs);
        s_jobs = NULL;
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}
void remote_test_mqtt(esp_mqtt_event_handle_t event) {
    static char rx[RX_MAX + 1];
    static int used, total;
    static bool receiving;
    char topic[128];
    snprintf(topic, sizeof(topic), "firefly/v1/gateways/%s/commands/test",
             gateway_settings_active()->gateway_id);
    if (!event || !s_jobs)
        return;
    if (event->current_data_offset == 0) {
        receiving = false;
        used = 0;
        total = event->total_data_len;
        if (event->retain || total <= 0 || total > RX_MAX ||
            event->topic_len != (int)strlen(topic) || memcmp(event->topic, topic, event->topic_len))
            return;
        receiving = true;
    }
    if (!receiving || event->current_data_offset != used || event->total_data_len != total ||
        event->data_len < 0 || used + event->data_len > total) {
        receiving = false;
        return;
    }
    memcpy(rx + used, event->data, event->data_len);
    used += event->data_len;
    if (used == total) {
        rx[used] = 0;
        job_t job = {0};
        memcpy(job.payload, rx, used + 1);
        if (xQueueSend(s_jobs, &job, 0) != pdTRUE)
            ESP_LOGW(TAG, "Command queue full; no test started");
        receiving = false;
    }
}
void remote_test_report(const battery_test_report_t *report) {
    if (!s_jobs || !report)
        return;
    job_t job = {.report = true, .result = *report};
    if (xQueueSend(s_jobs, &job, 0) != pdTRUE)
        ESP_LOGW(TAG, "Report queue full; CSV remains available");
}
void remote_test_report_v2(const battery_test_report_v2_t *report) {
    if (!s_jobs || !report) return;
    job_t job = {.report = true, .result = report->report, .has_meta = report->config_capable == 1,
        .config_version = report->config_version, .node_boot = report->boot_id};
    if (xQueueSend(s_jobs, &job, 0) != pdTRUE)
        ESP_LOGW(TAG, "Report queue full; result not forwarded");
}
#endif
