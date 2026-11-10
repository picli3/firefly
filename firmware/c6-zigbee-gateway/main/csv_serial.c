#include "csv_serial.h"

#include <inttypes.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "csv_format.h"
#include "driver/usb_serial_jtag.h"
#include "driver/usb_serial_jtag_vfs.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "sdkconfig.h"
#include "telemetry_csv.h"
#include "zigbee_gateway.h"

#if !CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG
#error "The gateway command console requires native USB Serial/JTAG"
#endif

#define SERIAL_TX_TIMEOUT pdMS_TO_TICKS(1000)

static SemaphoreHandle_t s_output;
static vprintf_like_t s_previous_print = vprintf;

/* Never wait behind a download in the Zigbee callback. Logs are discarded
 * while a command owns the console; CSV recording continues independently. */
static int log_print(const char *format, va_list args)
{
    if (!usb_serial_jtag_is_connected() || xSemaphoreTake(s_output, 0) != pdTRUE) return 0;
    int result = s_previous_print(format, args);
    xSemaphoreGive(s_output);
    return result;
}

static bool write_raw(const void *data, size_t size)
{
    return usb_serial_jtag_write_bytes(data, size, SERIAL_TX_TIMEOUT) == (int)size;
}

static bool reply(const char *text)
{
    return write_raw(text, strlen(text));
}

static void download(void)
{
    size_t size;
    if (telemetry_csv_snapshot(&size) != ESP_OK) {
        reply("@CSV ERROR storage_unavailable\n");
        return;
    }
    char line[100];
    snprintf(line, sizeof(line), "@CSV BEGIN %u\n", (unsigned)size);
    if (!reply(line)) return;
    uint8_t buffer[512];
    uint32_t crc = UINT32_MAX;
    for (size_t offset = 0; offset < size;) {
        uint8_t control;
        while (usb_serial_jtag_read_bytes(&control, 1, 0) == 1) {
            if (control == 3) {
                reply("\n@CSV ERROR aborted\n");
                return;
            }
        }
        size_t length = size - offset;
        if (length > sizeof(buffer)) length = sizeof(buffer);
        if (telemetry_csv_read(offset, buffer, length) != ESP_OK) {
            reply("\n@CSV ERROR read_failed\n");
            return;
        }
        crc = csv_crc32_update(crc, buffer, length);
        if (!write_raw(buffer, length)) return;
        offset += length;
        taskYIELD();
    }
    snprintf(line, sizeof(line), "\n@CSV END %u %08" PRIx32 "\n", (unsigned)size, crc ^ UINT32_MAX);
    reply(line);
}

static void command(const char *text)
{
    if (strncmp(text, "zigbee repeat ", 14) == 0) {
        const char *operation = text + 14;
        char line[140];
        esp_err_t err = ESP_ERR_INVALID_ARG;
        if (strcmp(operation, "status") == 0) {
            err = zigbee_gateway_repeat_status(line, sizeof(line));
        } else if (strcmp(operation, "stop") == 0) {
            err = zigbee_gateway_repeat_stop();
            snprintf(line, sizeof(line), "@REPEAT OK stopped\n");
        } else if (strncmp(operation, "start ", 6) == 0) {
            const char *id = operation + 6;
            if (strlen(id) == 8 && strspn(id, "0123456789abcdefABCDEF") == 8) {
                err = zigbee_gateway_repeat_start((uint32_t)strtoul(id, NULL, 16));
                snprintf(line, sizeof(line), "@REPEAT OK started node=%s\n", id);
            }
        }
        if (err != ESP_OK) snprintf(line, sizeof(line), "@REPEAT ERROR %s\n", esp_err_to_name(err));
        xSemaphoreTake(s_output, portMAX_DELAY);
        reply(line);
        usb_serial_jtag_wait_tx_done(SERIAL_TX_TIMEOUT);
        xSemaphoreGive(s_output);
        return;
    }
    if (strncmp(text, "zigbee test ", 12) == 0) {
        const char *id = text + 12;
        esp_err_t result = ESP_ERR_INVALID_ARG;
        if (strlen(id) == 8 && strspn(id, "0123456789abcdefABCDEF") == 8) {
            result = zigbee_gateway_start_manual_test((uint32_t)strtoul(id, NULL, 16));
        }
        if (result != ESP_OK) ESP_LOGW("csv_serial", "MANUAL_TEST error: %s", esp_err_to_name(result));
        return;
    }
    // Link diagnostics use ESP_LOG, so dispatch without reserving the USB console.
    if (strcmp(text, "zigbee links") == 0 || strncmp(text, "zigbee links ", 13) == 0) {
        esp_err_t result;
        if (!text[12]) {
            result = zigbee_gateway_log_links();
        } else {
            const char *addr = text + 13;
            char *end;
            unsigned long target = strtoul(addr, &end, 16);
            unsigned long start = 0;
            bool valid = end != addr && target > 0 && target < 0xfff8;
            if (*end == ' ') {
                const char *page = end + 1;
                start = strtoul(page, &end, 10);
                valid = valid && end != page && start <= UINT8_MAX;
            }
            result = valid && !*end ? zigbee_gateway_query_links(target, start) : ESP_ERR_INVALID_ARG;
        }
        if (result != ESP_OK) ESP_LOGW("csv_serial", "LINK command_error: %s", esp_err_to_name(result));
        return;
    }
    xSemaphoreTake(s_output, portMAX_DELAY);
    if (strcmp(text, "csv download") == 0) {
        download();
    } else if (strcmp(text, "csv status") == 0) {
        telemetry_csv_status_t status;
        telemetry_csv_status(&status);
        char line[320];
        snprintf(line, sizeof(line),
                 "@CSV STATUS mounted=%u recording=%u full=%u bytes=%u limit=%u boot=%" PRIu32
                 " queue_drops=%" PRIu32 " write_errors=%" PRIu32 " skipped=%" PRIu32 "\n",
                 status.mounted, status.recording, status.full, (unsigned)status.bytes, CSV_MAX_BYTES,
                 status.boot_id, status.queue_drops, status.write_errors, status.skipped);
        reply(line);
    } else if (strcmp(text, "csv clear CONFIRM") == 0) {
        reply(telemetry_csv_clear() == ESP_OK ? "@CSV OK cleared\n" : "@CSV ERROR clear_failed\n");
    } else {
        reply("@CSV HELP csv status | csv download | csv clear CONFIRM | zigbee links [0xADDR [START]] | zigbee test DEVICE_ID | zigbee repeat start DEVICE_ID / status / stop\n");
    }
    usb_serial_jtag_wait_tx_done(SERIAL_TX_TIMEOUT);
    xSemaphoreGive(s_output);
}

static void serial_task(void *arg)
{
    char line[80];
    size_t length = 0;
    bool overflow = false;
    for (;;) {
        uint8_t ch;
        if (usb_serial_jtag_read_bytes(&ch, 1, portMAX_DELAY) != 1) continue;
        if (ch == '\r' || ch == '\n') {
            if (length || overflow) {
                line[length] = 0;
                command(overflow ? "" : line);
            }
            length = 0;
            overflow = false;
        } else if (ch == 3) {
            length = 0;
            overflow = false;
        } else if (ch == 8 || ch == 127) {
            if (length) length--;
        } else if (ch >= 32 && ch < 127) {
            if (length < sizeof(line) - 1) line[length++] = ch;
            else overflow = true;
        }
    }
}

esp_err_t csv_serial_start(void)
{
    s_output = xSemaphoreCreateMutex();
    if (!s_output) return ESP_ERR_NO_MEM;
    fflush(stdout);
    usb_serial_jtag_driver_config_t config = {
        .rx_buffer_size = 2048,
        .tx_buffer_size = 4096,
    };
    esp_err_t err = usb_serial_jtag_driver_install(&config);
    if (err != ESP_OK) return err;
    usb_serial_jtag_vfs_use_driver();
    fflush(stdout);
    s_previous_print = esp_log_set_vprintf(log_print);
    return xTaskCreate(serial_task, "csv_serial", 4096, NULL, 2, NULL) == pdPASS ? ESP_OK : ESP_ERR_NO_MEM;
}
