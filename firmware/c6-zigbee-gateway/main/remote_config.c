#include "remote_config.h"
#include "gateway_settings.h"

#ifdef CONFIG_GATEWAY_ETH_ENABLED
#include "cJSON.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "zigbee_gateway.h"
#include <inttypes.h>
#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

/* Integration dependency; also belongs in zigbee_gateway.h. */
extern esp_err_t zigbee_gateway_send_battery_config(uint32_t device_id,
                                                   const battery_config_request_t *request);

#define CONFIG_RX_MAX 1024
#define CONFIG_ACK_CACHE 16
#define CONFIG_COMMAND_QUEUE 8
static const char *TAG = "remote_config";
static esp_mqtt_client_handle_t s_client;
static QueueHandle_t s_jobs;
static char s_boot[65];
static char s_command_topic[128], s_ack_topic[128];
static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;

typedef struct {
    size_t length;
    char payload[CONFIG_RX_MAX + 1];
} config_job_t;
typedef struct {
    battery_config_ack_t ack;
    uint64_t sequence;
    uint64_t generation;
    bool pending;
} config_ack_entry_t;
static config_ack_entry_t s_acks[CONFIG_ACK_CACHE];
static uint64_t s_sequence;
static unsigned s_next_ack;
static struct {
    char data[CONFIG_RX_MAX + 1];
    int used, total, message_id;
    bool active;
} s_rx; /* Owned only by the serialized MQTT callback. */

static int hex_value(unsigned char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}
static bool uuid_bytes_valid(const uint8_t bytes[16]) {
    bool nil = true, max = true;
    for (int i = 0; i < 16; ++i) {
        nil = nil && bytes[i] == 0;
        max = max && bytes[i] == 255;
    }
    /* Same UUID acceptance as web zod v4: versions 1..8, RFC variant,
     * plus nil/max. No host-endian GUID conversion. */
    return nil || max || ((bytes[6] >> 4) >= 1 && (bytes[6] >> 4) <= 8 &&
                         (bytes[8] & 0xc0) == 0x80);
}
static bool parse_uuid(const char *s, uint8_t bytes[16]) {
    if (!s || strlen(s) != 36) return false;
    unsigned out = 0;
    for (unsigned i = 0; i < 36;) {
        if (i == 8 || i == 13 || i == 18 || i == 23) {
            if (s[i++] != '-') return false;
            continue;
        }
        int hi = hex_value((unsigned char)s[i++]);
        int lo = hex_value((unsigned char)s[i++]);
        if (hi < 0 || lo < 0) return false;
        bytes[out++] = (uint8_t)((hi << 4) | lo);
    }
    return uuid_bytes_valid(bytes);
}
static void format_uuid(const uint8_t bytes[16], char out[37]) {
    static const char hex[] = "0123456789abcdef";
    unsigned p = 0;
    for (unsigned i = 0; i < 16; ++i) {
        if (i == 4 || i == 6 || i == 8 || i == 10) out[p++] = '-';
        out[p++] = hex[bytes[i] >> 4];
        out[p++] = hex[bytes[i] & 15];
    }
    out[p] = 0;
}
static bool digit(char c) { return c >= '0' && c <= '9'; }
static bool whitespace(char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; }

/* cJSON accepts some non-JSON numbers/control whitespace. Check lexical rules
 * first, including embedded NULs and escaped NULs (C string key ambiguity).
 * Limit nesting before cJSON allocation/recursion. cJSON handles grammar. */
static bool json_lexical(const char *p, size_t n) {
    unsigned depth = 0;
    for (size_t i = 0; i < n;) {
        char c = p[i++];
        if (whitespace(c) || c == ',' || c == ':') continue;
        if (c == '{' || c == '[') { if (++depth > 4) return false; continue; }
        if (c == '}' || c == ']') { if (!depth) return false; --depth; continue; }
        if (c == '"') {
            bool closed = false;
            while (i < n) {
                unsigned char ch = (unsigned char)p[i++];
                if (ch == '"') { closed = true; break; }
                if (ch < 0x20) return false;
                if (ch != '\\') continue;
                if (i == n) return false;
                ch = (unsigned char)p[i++];
                if (ch == 'u') {
                    if (n - i < 4) return false;
                    unsigned value = 0;
                    for (int j = 0; j < 4; ++j) {
                        int h = hex_value((unsigned char)p[i++]);
                        if (h < 0) return false;
                        value = (value << 4) | (unsigned)h;
                    }
                    if (!value) return false;
                } else if (!strchr("\"\\/bfnrt", ch) || !ch) return false;
            }
            if (!closed) return false;
            continue;
        }
        if (c == '-' || digit(c)) {
            --i;
            if (p[i] == '-' && ++i == n) return false;
            if (p[i] == '0') ++i;
            else {
                if (p[i] < '1' || p[i] > '9') return false;
                while (i < n && digit(p[i])) ++i;
            }
            if (i < n && p[i] == '.') {
                if (++i == n || !digit(p[i])) return false;
                while (i < n && digit(p[i])) ++i;
            }
            if (i < n && (p[i] == 'e' || p[i] == 'E')) {
                if (++i < n && (p[i] == '+' || p[i] == '-')) ++i;
                if (i == n || !digit(p[i])) return false;
                while (i < n && digit(p[i])) ++i;
            }
            if (i < n && !whitespace(p[i]) && !strchr(",}]", p[i])) return false;
            continue;
        }
        const char *literal = c == 't' ? "true" : c == 'f' ? "false" : c == 'n' ? "null" : NULL;
        if (!literal) return false;
        size_t len = strlen(literal);
        --i;
        if (n - i < len || memcmp(p + i, literal, len)) return false;
        i += len;
    }
    return depth == 0;
}
/* Exact object shape also rejects duplicate keys after JSON escape decoding. */
static bool object_keys(const cJSON *object, const char *const *keys, unsigned count) {
    if (!cJSON_IsObject(object)) return false;
    unsigned seen = 0;
    for (const cJSON *item = object->child; item; item = item->next) {
        unsigned k;
        for (k = 0; k < count; ++k)
            if (item->string && !strcmp(item->string, keys[k])) break;
        if (k == count || (seen & (1U << k))) return false;
        seen |= 1U << k;
    }
    return seen == (1U << count) - 1;
}
static bool integer(const cJSON *object, const char *key, uint32_t max, uint32_t *out) {
    const cJSON *x = cJSON_GetObjectItemCaseSensitive(object, key);
    if (!cJSON_IsNumber(x) || !isfinite(x->valuedouble) || x->valuedouble < 1 ||
        x->valuedouble > max || floor(x->valuedouble) != x->valuedouble) return false;
    *out = (uint32_t)x->valuedouble;
    return true;
}
static void config_command(const config_job_t *job) {
    if (!json_lexical(job->payload, job->length)) return;
    const char *end;
    cJSON *json = cJSON_ParseWithLengthOpts(job->payload, job->length + 1, &end, true);
    if (!json) return;
    static const char *const root_keys[] = {
        "schema_version", "command_id", "device_id", "version", "thresholds"};
    static const char *const threshold_keys[] = {
        "attention_mv", "alert_mv", "critical_mv", "test_min_mv", "full_mv",
        "ac_empty_mv", "dc_empty_mv"};
    battery_config_request_t request = {.version = EMERGENCY_CONFIG_VERSION};
    uint32_t schema, revision;
    uint32_t node = 0;
    const cJSON *id = cJSON_GetObjectItemCaseSensitive(json, "command_id");
    const cJSON *device = cJSON_GetObjectItemCaseSensitive(json, "device_id");
    const cJSON *thresholds = cJSON_GetObjectItemCaseSensitive(json, "thresholds");
    bool valid = object_keys(json, root_keys, 5) && object_keys(thresholds, threshold_keys, 7) &&
                 integer(json, "schema_version", 1, &schema) &&
                 integer(json, "version", UINT32_MAX, &revision) &&
                 cJSON_IsString(id) && parse_uuid(id->valuestring, request.command_uuid) &&
                 cJSON_IsString(device) && strlen(device->valuestring) == 8;
    if (valid) {
        for (unsigned i = 0; i < 8; ++i) {
            int h = hex_value((unsigned char)device->valuestring[i]);
            if (h < 0) { valid = false; break; }
            node = (node << 4) | (unsigned)h;
        }
    }
    uint16_t values[7];
    for (unsigned i = 0; valid && i < 7; ++i) {
        uint32_t v;
        valid = integer(thresholds, threshold_keys[i], UINT16_MAX, &v);
        if (valid) values[i] = (uint16_t)v;
    }
    if (valid) {
        request.revision = revision;
        request.thresholds = (battery_thresholds_t){
            .attention_mv = values[0], .alert_mv = values[1], .critical_mv = values[2],
            .test_min_mv = values[3], .full_mv = values[4],
            .ac_empty_mv = values[5], .dc_empty_mv = values[6]};
        valid = battery_thresholds_valid(&request.thresholds);
    }
    if (valid) {
        /* Web retries MUST reach H2: only H2 knows persisted/active state. */
        esp_err_t err = zigbee_gateway_send_battery_config(node, &request);
        if (err != ESP_OK) ESP_LOGW(TAG, "Config send failed (%d); awaiting web retry", (int)err);
    }
    cJSON_Delete(json);
}

static bool publish_ack(const config_ack_entry_t *entry) {
    char uuid[37], device[9], message[128], unknown_reason[24];
    format_uuid(entry->ack.command_uuid, uuid);
    snprintf(device, sizeof(device), "%08" PRIx32, entry->ack.device_id);
    snprintf(message, sizeof(message), "%s-%s-%" PRIu64, uuid, s_boot, entry->sequence);
    const char *status = entry->ack.status == BATTERY_CONFIG_APPLIED ? "applied" :
                         entry->ack.status == BATTERY_CONFIG_DEFERRED ? "deferred" : "rejected";
    static const char *const reasons[] = {"none", "invalid", "stale", "conflict", "storage"};
    snprintf(unknown_reason, sizeof(unknown_reason), "h2_reason_%u", entry->ack.reason);
    const char *reason = entry->ack.reason < sizeof(reasons) / sizeof(reasons[0]) ?
                         reasons[entry->ack.reason] : unknown_reason;
    cJSON *j = cJSON_CreateObject();
    if (!j) return false;
    /* Never enqueue an incomplete ACK after an allocation failure. */
    bool ok = cJSON_AddNumberToObject(j, "schema_version", 1) &&
              cJSON_AddStringToObject(j, "message_id", message) &&
              cJSON_AddStringToObject(j, "command_id", uuid) &&
              cJSON_AddStringToObject(j, "device_id", device) &&
              cJSON_AddNumberToObject(j, "version", entry->ack.revision) &&
              cJSON_AddStringToObject(j, "status", status) &&
              cJSON_AddStringToObject(j, "reason", reason);
    char *raw = ok ? cJSON_PrintUnformatted(j) : NULL;
    ok = raw && esp_mqtt_client_enqueue(s_client, s_ack_topic, raw, 0, 1, 0, true) >= 0;
    cJSON_free(raw);
    cJSON_Delete(j);
    return ok;
}
static void flush_acks(void) {
    for (unsigned i = 0; i < CONFIG_ACK_CACHE; ++i) {
        config_ack_entry_t snapshot;
        portENTER_CRITICAL(&s_lock);
        snapshot = s_acks[i];
        portEXIT_CRITICAL(&s_lock);
        if (!snapshot.pending || !publish_ack(&snapshot)) continue;
        portENTER_CRITICAL(&s_lock);
        /* ACK insertion/reconnect may have happened during MQTT enqueue. */
        if (s_acks[i].sequence == snapshot.sequence &&
            s_acks[i].generation == snapshot.generation) s_acks[i].pending = false;
        portEXIT_CRITICAL(&s_lock);
    }
}
static void config_task(void *arg) {
    (void)arg;
    config_job_t job;
    for (;;) {
        if (xQueueReceive(s_jobs, &job, pdMS_TO_TICKS(1000)) == pdTRUE) config_command(&job);
        flush_acks();
    }
}
esp_err_t remote_config_init(esp_mqtt_client_handle_t client, const char *boot) {
    if (s_jobs) return ESP_ERR_INVALID_STATE;
    if (!client || !boot || !*boot || strlen(boot) >= sizeof(s_boot)) return ESP_ERR_INVALID_ARG;
    int a = snprintf(s_command_topic, sizeof(s_command_topic), "firefly/v1/gateways/%s/commands/config",
                     gateway_settings_active()->gateway_id);
    int b = snprintf(s_ack_topic, sizeof(s_ack_topic), "firefly/v1/gateways/%s/config-ack",
                     gateway_settings_active()->gateway_id);
    if (a < 0 || (size_t)a >= sizeof(s_command_topic) || b < 0 || (size_t)b >= sizeof(s_ack_topic))
        return ESP_ERR_INVALID_ARG;
    s_client = client;
    memcpy(s_boot, boot, strlen(boot) + 1);
    s_jobs = xQueueCreate(CONFIG_COMMAND_QUEUE, sizeof(config_job_t));
    if (!s_jobs) return ESP_ERR_NO_MEM;
    if (xTaskCreate(config_task, "remote_config", 6144, NULL, 4, NULL) != pdPASS) {
        vQueueDelete(s_jobs);
        s_jobs = NULL;
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}
void remote_config_mqtt(esp_mqtt_event_handle_t event) {
    if (!event || !s_jobs) return;
    if (event->event_id == MQTT_EVENT_DISCONNECTED || event->event_id == MQTT_EVENT_CONNECTED ||
        event->event_id == MQTT_EVENT_ERROR) { s_rx.active = false; return; }
    if (event->event_id != MQTT_EVENT_DATA) return;
    bool topic_matches = event->topic && event->topic_len == (int)strlen(s_command_topic) &&
                         !memcmp(event->topic, s_command_topic, (size_t)event->topic_len);
    if (event->current_data_offset == 0) {
        s_rx.active = false;
        if (event->retain || !topic_matches || event->total_data_len <= 0 ||
            event->total_data_len > CONFIG_RX_MAX) return;
        s_rx.used = 0;
        s_rx.total = event->total_data_len;
        s_rx.message_id = event->msg_id;
        s_rx.active = true;
    }
    if (!s_rx.active) return;
    if (event->retain || event->current_data_offset != s_rx.used ||
        event->total_data_len != s_rx.total || event->msg_id != s_rx.message_id ||
        event->data_len <= 0 || !event->data || event->data_len > s_rx.total - s_rx.used ||
        event->topic_len < 0 || (event->topic_len != 0 && !topic_matches)) {
        s_rx.active = false;
        return;
    }
    memcpy(s_rx.data + s_rx.used, event->data, (size_t)event->data_len);
    s_rx.used += event->data_len;
    if (s_rx.used == s_rx.total) {
        config_job_t job = {.length = (size_t)s_rx.used};
        memcpy(job.payload, s_rx.data, job.length);
        job.payload[job.length] = 0;
        s_rx.active = false;
        if (xQueueSend(s_jobs, &job, 0) != pdTRUE)
            ESP_LOGW(TAG, "Config queue full; awaiting web retry");
    }
}
void remote_config_ack(const battery_config_ack_t *ack) {
    if (!s_jobs || !ack || ack->version != EMERGENCY_CONFIG_VERSION || !ack->revision ||
        ack->status > BATTERY_CONFIG_REJECTED || !uuid_bytes_valid(ack->command_uuid)) return;
    portENTER_CRITICAL(&s_lock);
    for (unsigned i = 0; i < CONFIG_ACK_CACHE; ++i) {
        if (s_acks[i].sequence && !memcmp(&s_acks[i].ack, ack, sizeof(*ack))) {
            s_acks[i].pending = true;
            ++s_acks[i].generation;
            portEXIT_CRITICAL(&s_lock);
            return;
        }
    }
    bool evicted_pending = s_acks[s_next_ack].pending;
    s_acks[s_next_ack] = (config_ack_entry_t){
        .ack = *ack, .sequence = ++s_sequence, .generation = 1, .pending = true};
    s_next_ack = (s_next_ack + 1) % CONFIG_ACK_CACHE;
    portEXIT_CRITICAL(&s_lock);
    if (evicted_pending) ESP_LOGW(TAG, "Config ACK cache full; oldest pending ACK evicted");
}
void remote_config_reconnect(void) {
    if (!s_jobs) return;
    s_rx.active = false;
    portENTER_CRITICAL(&s_lock);
    for (unsigned i = 0; i < CONFIG_ACK_CACHE; ++i) {
        if (!s_acks[i].sequence) continue;
        s_acks[i].pending = true;
        ++s_acks[i].generation;
    }
    portEXIT_CRITICAL(&s_lock);
}
#endif
