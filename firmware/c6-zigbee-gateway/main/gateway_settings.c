#include "gateway_settings.h"
#include <ctype.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cJSON.h"
#include "mbedtls/sha256.h"
#include "mbedtls/x509_crt.h"
#include "nvs.h"
#include "sdkconfig.h"

typedef struct __attribute__((packed)) {
    uint32_t magic, version, length;
    unsigned char sha256[32];
    char json[];
} saved_config_t;
#define SETTINGS_MAGIC 0x46504647U
static gateway_settings_t s_active;
static bool s_from_nvs, s_pending, s_storage_error;

const gateway_settings_t *gateway_settings_active(void) { return &s_active; }
static bool string_field(const cJSON *j, const char *key, char *out, size_t size)
{
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(j, key);
    if (!cJSON_IsString(v) || !v->valuestring[0] || strlen(v->valuestring) >= size) return false;
    for (const unsigned char *p = (const unsigned char *)v->valuestring; *p; ++p)
        if (*p < 32 || *p > 126) return false;
    strcpy(out, v->valuestring);
    return true;
}
static bool equal_field(const cJSON *j, const char *key, const char *value)
{
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(j, key);
    return cJSON_IsString(v) && !strcmp(v->valuestring, value);
}
static bool integer_field(const cJSON *j, const char *key, unsigned min, unsigned max, unsigned *out)
{
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(j, key);
    if (!cJSON_IsNumber(v) || !isfinite(v->valuedouble) || v->valuedouble < min ||
        v->valuedouble > max || floor(v->valuedouble) != v->valuedouble) return false;
    *out = (unsigned)v->valuedouble;
    return true;
}
static bool identifier(const char *s)
{
    if (!*s) return false;
    for (; *s; ++s) if (!isalnum((unsigned char)*s) && *s != '_' && *s != '-') return false;
    return true;
}
static bool host_valid(const char *s)
{
    if (!*s || !strcmp(s, "localhost") || !strncmp(s, "127.", 4) ||
        !strcmp(s, "::1") || !strcmp(s, "::") || !strcmp(s, "0.0.0.0")) return false;
    for (; *s; ++s) if (!isalnum((unsigned char)*s) && !strchr(".:-", *s)) return false;
    return true;
}
/* Reject duplicate decoded keys anywhere, bounded nesting and escaped NULs
 * before C string comparisons. Raw NULs are rejected before parsing. */
static bool unique_keys(const cJSON *j, unsigned depth)
{
    if (depth > 8) return false;
    for (const cJSON *a = j->child; a; a = a->next) {
        if (a->string) for (const cJSON *b = a->next; b; b = b->next)
            if (b->string && !strcmp(a->string, b->string)) return false;
        if (!unique_keys(a, depth + 1)) return false;
    }
    return true;
}
static esp_err_t parse_settings(const char *raw, size_t length, gateway_settings_t *out)
{
    if (!raw || !length || length > GATEWAY_CONFIG_MAX_JSON || memchr(raw, 0, length)) return ESP_ERR_INVALID_ARG;
    /* Bound nesting before cJSON recursion, ignoring braces inside strings. */
    unsigned depth = 0;
    bool quoted = false, escaped = false;
    for (size_t i = 0; i < length; ++i) {
        char c = raw[i];
        if (quoted) {
            if (escaped) { escaped = false; continue; }
            if (c == '\\') escaped = true;
            else if (c == '"') quoted = false;
        } else if (c == '"') quoted = true;
        else if (c == '{' || c == '[') { if (++depth > 8) return ESP_ERR_INVALID_ARG; }
        else if (c == '}' || c == ']') { if (!depth--) return ESP_ERR_INVALID_ARG; }
    }
    if (depth || quoted) return ESP_ERR_INVALID_ARG;
    for (size_t i = 0; i + 5 < length; ++i)
        if (raw[i] == '\\' && raw[i+1] == 'u' && !memcmp(raw+i+2, "0000", 4)) return ESP_ERR_INVALID_ARG;
    const char *end = NULL;
    cJSON *root = cJSON_ParseWithLengthOpts(raw, length, &end, false);
    if (!root) return ESP_ERR_INVALID_ARG;
    while (end < raw + length && (*end == ' ' || *end == '\r' || *end == '\n' || *end == '\t')) ++end;
    if (end != raw + length) { cJSON_Delete(root); return ESP_ERR_INVALID_ARG; }
    esp_err_t err = ESP_ERR_INVALID_ARG;
    if (!unique_keys(root, 0)) goto done;
    const cJSON *j = cJSON_GetObjectItemCaseSensitive(root, "config");
    if (!j) j = root; /* accepts both exported wrapper and bare AT config */
    const cJSON *net = cJSON_GetObjectItemCaseSensitive(j, "network");
    const cJSON *clock = cJSON_GetObjectItemCaseSensitive(j, "time");
    const cJSON *m = cJSON_GetObjectItemCaseSensitive(j, "mqtt");
    const cJSON *servers = cJSON_GetObjectItemCaseSensitive(clock, "ntp_servers");
    unsigned schema, qos;
    memset(out, 0, sizeof(*out));
    if (!integer_field(j, "schema_version", 1, 1, &schema) ||
        !string_field(j, "gateway_id", out->gateway_id, sizeof(out->gateway_id)) || !identifier(out->gateway_id) ||
        !equal_field(net, "interface", "ethernet") || !equal_field(net, "addressing", "dhcp") ||
        !cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(m, "tls")) ||
        !cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(m, "verify_certificate")) ||
        !equal_field(m, "protocol_version", "3.1.1") || !integer_field(m, "qos", 1, 1, &qos) ||
        !string_field(m, "uri", out->uri, sizeof(out->uri)) ||
        !string_field(m, "host", out->host, sizeof(out->host)) || !host_valid(out->host) ||
        !integer_field(m, "port", 1, 65535, &out->port) ||
        !string_field(m, "username", out->username, sizeof(out->username)) || strcmp(out->username, out->gateway_id) ||
        !string_field(m, "password", out->password, sizeof(out->password)) ||
        !string_field(m, "client_id", out->client_id, sizeof(out->client_id)) || !identifier(out->client_id) ||
        !integer_field(m, "keepalive_s", 5, 3600, &out->keepalive_s) ||
        !integer_field(m, "heartbeat_interval_s", 5, 86400, &out->heartbeat_s)) goto done;
    char uri[300];
    snprintf(uri, sizeof(uri), strchr(out->host, ':') ? "mqtts://[%s]:%u" : "mqtts://%s:%u", out->host, out->port);
    size_t expected = strlen(uri);
    if (strcmp(uri, out->uri) && !(strlen(out->uri) == expected + 1 &&
        !memcmp(uri, out->uri, expected) && out->uri[expected] == '/')) goto done;
    if (!cJSON_IsArray(servers)) goto done;
    int count = cJSON_GetArraySize(servers);
    if (count < 1 || count > GATEWAY_CONFIG_MAX_NTP) goto done;
#ifdef CONFIG_LWIP_SNTP_MAX_SERVERS
    if (count > CONFIG_LWIP_SNTP_MAX_SERVERS) goto done;
#endif
    out->ntp_count = count;
    for (int i = 0; i < count; ++i) {
        const cJSON *v = cJSON_GetArrayItem(servers, i);
        if (!cJSON_IsString(v) || !host_valid(v->valuestring) || strlen(v->valuestring) >= sizeof(out->ntp[i])) goto done;
        strcpy(out->ntp[i], v->valuestring);
    }
    const cJSON *ca = cJSON_GetObjectItemCaseSensitive(m, "ca_pem");
    if (!cJSON_IsString(ca) || strlen(ca->valuestring) >= sizeof(out->ca_pem) ||
        !strstr(ca->valuestring, "-----BEGIN CERTIFICATE-----") || strstr(ca->valuestring, "PRIVATE KEY")) goto done;
    strcpy(out->ca_pem, ca->valuestring);
    mbedtls_x509_crt cert;
    mbedtls_x509_crt_init(&cert);
    int parsed = mbedtls_x509_crt_parse(&cert, (const unsigned char *)out->ca_pem, strlen(out->ca_pem) + 1);
    mbedtls_x509_crt_free(&cert);
    if (parsed != 0) goto done;
    char prefix[100];
    snprintf(prefix, sizeof(prefix), "firefly/v1/gateways/%s", out->gateway_id);
    if (!equal_field(m, "topic_prefix", prefix)) goto done;
    const cJSON *topics = cJSON_GetObjectItemCaseSensitive(m, "topics");
    static const char *keys[] = {"status", "telemetry", "test_report", "configuration_ack",
        "configuration_command", "individual_test_command", "individual_test_ack"};
    static const char *suffixes[] = {"status", "telemetry", "test-report", "config-ack",
        "commands/config", "commands/test", "test-ack"};
    for (unsigned i = 0; i < 7; ++i) {
        if (i >= 5 && !cJSON_GetObjectItemCaseSensitive(topics, keys[i])) continue; /* older exports */
        char topic[128];
        snprintf(topic, sizeof(topic), "%s/%s", prefix, suffixes[i]);
        if (!equal_field(topics, keys[i], topic)) goto done;
    }
    out->tls = true;
    err = ESP_OK;
done:
    cJSON_Delete(root);
    return err;
}
static void compiled_defaults(void)
{
#ifdef CONFIG_GATEWAY_ETH_ENABLED
    snprintf(s_active.gateway_id, sizeof(s_active.gateway_id), "%s", CONFIG_GATEWAY_MQTT_GATEWAY_ID);
    snprintf(s_active.uri, sizeof(s_active.uri), "%s", CONFIG_GATEWAY_MQTT_URI);
    snprintf(s_active.client_id, sizeof(s_active.client_id), "%s", CONFIG_GATEWAY_MQTT_CLIENT_ID);
    snprintf(s_active.username, sizeof(s_active.username), "%s", CONFIG_GATEWAY_MQTT_USERNAME);
    snprintf(s_active.password, sizeof(s_active.password), "%s", CONFIG_GATEWAY_MQTT_PASSWORD);
    s_active.keepalive_s = CONFIG_GATEWAY_MQTT_KEEPALIVE_SECONDS;
    s_active.heartbeat_s = CONFIG_GATEWAY_MQTT_HEARTBEAT_SECONDS;
    s_active.ntp_count = 1;
#ifdef CONFIG_GATEWAY_MQTT_TLS
    extern const char gateway_mqtt_ca[] asm("_binary_gateway_mqtt_ca_start");
    snprintf(s_active.ca_pem, sizeof(s_active.ca_pem), "%s", gateway_mqtt_ca);
    s_active.tls = true;
    snprintf(s_active.ntp[0], sizeof(s_active.ntp[0]), "%s", CONFIG_GATEWAY_NTP_SERVER);
#else
    strcpy(s_active.ntp[0], "pool.ntp.org");
#endif
#endif
}
static esp_err_t read_saved(saved_config_t **out)
{
    nvs_handle_t nvs;
    esp_err_t e = nvs_open("gateway_prov", NVS_READONLY, &nvs);
    if (e != ESP_OK) return e;
    size_t length = 0;
    e = nvs_get_blob(nvs, "config", NULL, &length);
    saved_config_t *v = NULL;
    if (e == ESP_OK) {
        if (length <= sizeof(*v) || length > sizeof(*v) + GATEWAY_CONFIG_MAX_JSON) e = ESP_ERR_INVALID_SIZE;
        else {
            v = calloc(1, length + 1);
            e = v ? nvs_get_blob(nvs, "config", v, &length) : ESP_ERR_NO_MEM;
        }
    }
    nvs_close(nvs);
    unsigned char sha[32];
    if (e == ESP_OK && (v->magic != SETTINGS_MAGIC || v->version != 1 ||
        v->length != length - sizeof(*v))) e = ESP_ERR_INVALID_SIZE;
    if (e == ESP_OK && (mbedtls_sha256((const unsigned char *)v->json, v->length, sha, 0) ||
        memcmp(sha, v->sha256, sizeof(sha)))) e = ESP_ERR_INVALID_CRC;
    if (e != ESP_OK) free(v);
    else *out = v;
    return e;
}
esp_err_t gateway_settings_init(void)
{
    saved_config_t *v = NULL;
    esp_err_t e = read_saved(&v);
    if (e == ESP_ERR_NVS_NOT_FOUND) { compiled_defaults(); return ESP_OK; }
    if (e == ESP_OK) {
        e = parse_settings(v->json, v->length, &s_active);
        free(v);
        s_from_nvs = e == ESP_OK;
    }
    if (e != ESP_OK) { memset(&s_active, 0, sizeof(s_active)); s_storage_error = true; }
    return e; /* preserve damaged settings; USB/Zigbee still start */
}
esp_err_t gateway_settings_commit(const char *json, size_t length)
{
    gateway_settings_t *checked = calloc(1, sizeof(*checked));
    if (!checked) return ESP_ERR_NO_MEM;
    esp_err_t e = parse_settings(json, length, checked);
    memset(checked, 0, sizeof(*checked)); free(checked);
    if (e != ESP_OK) return e;
    saved_config_t *v = calloc(1, sizeof(*v) + length);
    if (!v) return ESP_ERR_NO_MEM;
    v->magic = SETTINGS_MAGIC; v->version = 1; v->length = length;
    memcpy(v->json, json, length);
    if (mbedtls_sha256((const unsigned char *)json, length, v->sha256, 0)) e = ESP_FAIL;
    nvs_handle_t nvs;
    if (e == ESP_OK) {
        e = nvs_open("gateway_prov", NVS_READWRITE, &nvs);
        if (e == ESP_OK) {
            /* Exact same imported bytes do not wear NVS again. */
            saved_config_t *old = NULL;
            bool same = read_saved(&old) == ESP_OK && old->length == length && !memcmp(old->sha256, v->sha256, 32);
            free(old);
            if (!same) {
                e = nvs_set_blob(nvs, "config", v, sizeof(*v) + length);
                if (e == ESP_OK) e = nvs_commit(nvs);
            }
            nvs_close(nvs);
        }
    }
    memset(v, 0, sizeof(*v) + length); free(v);
    if (e == ESP_OK) s_pending = true;
    return e;
}
esp_err_t gateway_settings_summary(char *out, size_t size)
{
    gateway_settings_t *saved = calloc(1, sizeof(*saved));
    if (!saved) return ESP_ERR_NO_MEM;
    saved_config_t *v = NULL;
    esp_err_t e = read_saved(&v);
    const gateway_settings_t *cfg = &s_active;
    if (e == ESP_OK) {
        e = parse_settings(v->json, v->length, saved);
        if (e == ESP_OK) cfg = saved;
    }
    if (e == ESP_ERR_NVS_NOT_FOUND) e = ESP_OK;
    if (e == ESP_OK) {
        int n = snprintf(out, size,
            "+FFPROV: source=%s,pending_restart=%u,gateway_id=%s,uri=%s,client_id=%s,network=ethernet/dhcp,tls=%u,ntp_servers=%u,keepalive=%u,heartbeat=%u,password_set=%u\r\nOK\r\n",
            (v || s_from_nvs) ? "nvs" : (s_storage_error ? "invalid" : "compiled"),
            s_pending, cfg->gateway_id, cfg->uri, cfg->client_id, cfg->tls, cfg->ntp_count,
            cfg->keepalive_s, cfg->heartbeat_s, !!cfg->password[0]);
        if (n < 0 || (size_t)n >= size) e = ESP_ERR_INVALID_SIZE;
    }
    if (v) { memset(v->json, 0, v->length); free(v); }
    memset(saved, 0, sizeof(*saved)); free(saved);
    return e;
}
