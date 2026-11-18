#include "at_provisioning.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "esp_timer.h"
#include "gateway_settings.h"
#include "ethernet_mqtt.h"
#include "mbedtls/base64.h"
#include "mbedtls/sha256.h"

#define CHUNK_SIZE 192U
#define MAX_ENCODED (((GATEWAY_CONFIG_MAX_JSON + 2U) / 3U) * 4U)
#define MAX_CHUNKS ((MAX_ENCODED + CHUNK_SIZE - 1U) / CHUNK_SIZE)
#define TRANSACTION_TIMEOUT_US INT64_C(60000000)
static struct {
    char *data;
    unsigned count, next;
    size_t used, last_offset, last_length;
    int64_t touched;
} s_rx;

void at_provisioning_abort(void)
{
    if (s_rx.data) { memset(s_rx.data, 0, MAX_ENCODED + 1); free(s_rx.data); }
    memset(&s_rx, 0, sizeof(s_rx));
}
void at_provisioning_poll(void)
{
    if (s_rx.data && esp_timer_get_time() - s_rx.touched > TRANSACTION_TIMEOUT_US)
        at_provisioning_abort();
}
static bool decimal(const char **p, unsigned *out)
{
    if (**p < '0' || **p > '9') return false;
    unsigned v = 0;
    while (**p >= '0' && **p <= '9') {
        if (v > 100000) return false;
        v = v * 10 + (unsigned)(*(*p)++ - '0');
    }
    *out = v;
    return true;
}
static int hex_digit(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}
bool at_provisioning_command(const char *line, char *reply, size_t size)
{
    if (strncmp(line, "AT", 2)) return false;
    const char *error = NULL;
    int64_t now = esp_timer_get_time();
    bool expired = s_rx.data && now - s_rx.touched > TRANSACTION_TIMEOUT_US;
    if (expired) at_provisioning_abort();
    if (!strcmp(line, "AT")) snprintf(reply, size, "OK\r\n");
    else if (!strcmp(line, "ATI"))
        snprintf(reply, size, "+FFAT: firefly-at/v1,fragment=192,max_json=%u,apply=reboot\r\nOK\r\n", GATEWAY_CONFIG_MAX_JSON);
    else if (!strcmp(line, "AT+FFPROV?")) {
        if (gateway_settings_summary(reply, size) != ESP_OK) error = "STORAGE";
    } else if (!strcmp(line, "AT+FFSTATUS?")) ethernet_mqtt_status(reply, size);
    else if (!strcmp(line, "AT+FFPROVABORT")) { at_provisioning_abort(); snprintf(reply, size, "OK\r\n"); }
    else if (!strncmp(line, "AT+FFPROVBEGIN=", 15)) {
        const char *p = line + 15;
        unsigned count;
        if (!decimal(&p, &count) || *p || !count || count > MAX_CHUNKS) error = "ARGUMENT";
        else {
            at_provisioning_abort();
            s_rx.data = calloc(1, MAX_ENCODED + 1);
            if (!s_rx.data) error = "NO_MEMORY";
            else { s_rx.count = count; s_rx.touched = now; snprintf(reply, size, "OK\r\n"); }
        }
    } else if (!strncmp(line, "AT+FFPROVDATA=", 14)) {
        const char *p = line + 14;
        unsigned index;
        if (!s_rx.data) error = expired ? "TIMEOUT" : "NO_TRANSACTION";
        else if (!decimal(&p, &index) || *p++ != ',' || *p++ != '"') error = "ARGUMENT";
        else {
            size_t n = strlen(p);
            if (n < 2 || p[n-1] != '"' || n-1 > CHUNK_SIZE) error = "ARGUMENT";
            else {
                --n;
                if (n % 4 || strspn(p, "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/=") != n) error = "BASE64";
                else if (s_rx.next && index == s_rx.next - 1 && n == s_rx.last_length &&
                         !memcmp(s_rx.data + s_rx.last_offset, p, n)) {
                    s_rx.touched = now; snprintf(reply, size, "OK\r\n"); /* lost OK: retry last fragment */
                } else if (index != s_rx.next || index >= s_rx.count || s_rx.used + n > MAX_ENCODED ||
                           (index + 1 < s_rx.count && n != CHUNK_SIZE)) error = "ORDER";
                else {
                    s_rx.last_offset = s_rx.used; s_rx.last_length = n;
                    memcpy(s_rx.data + s_rx.used, p, n); s_rx.used += n;
                    ++s_rx.next; s_rx.touched = now; snprintf(reply, size, "OK\r\n");
                }
            }
        }
    } else if (!strncmp(line, "AT+FFPROVCOMMIT=", 16)) {
        const char *p = line + 16;
        unsigned char expected[32], actual[32];
        if (!s_rx.data) error = expired ? "TIMEOUT" : "NO_TRANSACTION";
        else if (s_rx.next != s_rx.count) error = "INCOMPLETE";
        else if (strlen(p) != 66 || p[0] != '"' || p[65] != '"') error = "ARGUMENT";
        else {
            for (unsigned i = 0; i < 32; ++i) {
                int hi = hex_digit(p[1+i*2]), lo = hex_digit(p[2+i*2]);
                if (hi < 0 || lo < 0) { error = "ARGUMENT"; break; }
                expected[i] = (unsigned char)((hi << 4) | lo);
            }
            if (!error) {
                unsigned char *json = calloc(1, GATEWAY_CONFIG_MAX_JSON + 1);
                if (!json) error = "NO_MEMORY";
                else {
                    size_t length = 0;
                    int decoded = mbedtls_base64_decode(json, GATEWAY_CONFIG_MAX_JSON, &length,
                                                        (const unsigned char *)s_rx.data, s_rx.used);
                    if (decoded || !length) error = "BASE64";
                    else if (mbedtls_sha256(json, length, actual, 0) || memcmp(actual, expected, 32)) error = "CHECKSUM";
                    else {
                        esp_err_t e = gateway_settings_commit((const char *)json, length);
                        if (e != ESP_OK) error = e == ESP_ERR_INVALID_ARG ? "CONFIG" : "STORAGE";
                        else snprintf(reply, size, "+FFPROV: SAVED,RESTART_REQUIRED\r\nOK\r\n");
                    }
                    memset(json, 0, GATEWAY_CONFIG_MAX_JSON + 1); free(json);
                }
            }
        }
        at_provisioning_abort(); /* success and failure both close the transaction */
    } else error = "UNSUPPORTED";
    if (error) {
        /* Fail closed: no ambiguous fragments survive an error. */
        at_provisioning_abort();
        snprintf(reply, size, "ERROR:%s\r\n", error);
    }
    return true;
}
