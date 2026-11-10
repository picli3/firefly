#include "csv_format.h"

#include <inttypes.h>
#include <stdio.h>

const char CSV_HEADER[] = "gateway_boot_id,received_ms,short_addr,device_id,sequence,voltage_mv,current_ma,power_mw,temperature_centi_c,soh_pct,flags,node_uptime_s\n";

int csv_format_row(char *buffer, size_t capacity, uint32_t boot_id, int64_t received_ms,
                   uint16_t short_addr, const emergency_telemetry_t *s)
{
    int n = snprintf(buffer, capacity,
                     "%" PRIu32 ",%" PRId64 ",0x%04x,%08" PRIx32 ",%u,%u,%d,%" PRId32 ",%d,%u,%u,%" PRIu32 "\n",
                     boot_id, received_ms, short_addr, s->device_id, s->sequence,
                     s->vpack_mv, s->ipack_ma, s->power_mw, s->temp_centi_c,
                     s->soh_pct, s->flags, s->uptime_s);
    return n >= 0 && (size_t)n < capacity ? n : -1;
}

/* IEEE CRC-32: initialize with UINT32_MAX, finish with XOR UINT32_MAX. */
uint32_t csv_crc32_update(uint32_t state, const void *data, size_t length)
{
    const uint8_t *p = data;
    while (length--) {
        state ^= *p++;
        for (unsigned bit = 0; bit < 8; bit++) {
            state = (state >> 1) ^ (0xedb88320U & (0U - (state & 1U)));
        }
    }
    return state;
}
