#include "iridium_parser.h"

#include <stdio.h>
#include <string.h>
#include <stdint.h>

static const char *skip_prefix(const char *data)
{
    const char *colon = strchr(data, ':');
    return colon != NULL ? colon + 1 : data;
}

bool iridium_parser_copy_string(char *dest, size_t dest_size, const char *src)
{
    if (dest == NULL || dest_size == 0) {
        return false;
    }

    if (src == NULL) {
        dest[0] = '\0';
        return true;
    }

    size_t src_len = strlen(src);
    if (src_len >= dest_size) {
        memcpy(dest, src, dest_size - 1);
        dest[dest_size - 1] = '\0';
        return false;
    }

    memcpy(dest, src, src_len + 1);
    return true;
}

bool iridium_parser_starts_with(const char *prefix, const char *str)
{
    if (prefix == NULL || str == NULL) {
        return false;
    }

    size_t prefix_len = strlen(prefix);
    size_t str_len = strlen(str);
    return str_len >= prefix_len && memcmp(prefix, str, prefix_len) == 0;
}

bool iridium_parser_sbd_session(const char *response_line, iridium_sbd_session_t *out)
{
    if (response_line == NULL || out == NULL) {
        return false;
    }

    int mo = 0;
    int momsn = 0;
    int mt = 0;
    int mtmsn = 0;
    int mt_len = 0;
    int mt_queued = 0;

    if (sscanf(skip_prefix(response_line), " %d,%d,%d,%d,%d,%d",
               &mo, &momsn, &mt, &mtmsn, &mt_len, &mt_queued) != 6) {
        return false;
    }

    out->mo_status = mo;
    out->momsn = momsn;
    out->mt_status = mt;
    out->mtmsn = mtmsn;
    out->mt_length = mt_len;
    out->mt_queued = mt_queued;
    return true;
}

bool iridium_parser_csq(const char *response_line, int *csq_out)
{
    if (response_line == NULL || csq_out == NULL) {
        return false;
    }

    return sscanf(skip_prefix(response_line), " %d", csq_out) == 1;
}

bool iridium_parser_mo_transfer_ok(int mo_status)
{
    return mo_status == 0 || mo_status == 1 || mo_status == 2;
}

bool iridium_parser_mo_try_later(int mo_status)
{
    return mo_status == 36 || mo_status == 38;
}

int iridium_parser_mo_retry_delay_ms(int mo_status, int attempt_index)
{
    static const int default_delays[] = {2000, 4000, 20000, 30000, 300000};
    static const int traffic_delays[] = {5000, 15000, 45000, 90000, 300000};
    const int max_idx = 4;
    int idx = attempt_index;

    if (idx < 0) {
        idx = 0;
    }
    if (idx > max_idx) {
        idx = max_idx;
    }

    if (mo_status == 36) {
        return 180000;
    }
    if (mo_status == 38) {
        return traffic_delays[idx];
    }
    return default_delays[idx];
}

bool iridium_parser_cris(const char *response_line, int *telephony_out, int *sbd_out)
{
    if (response_line == NULL || telephony_out == NULL || sbd_out == NULL) {
        return false;
    }

    int tri = 0;
    int sri = 0;
    if (sscanf(skip_prefix(response_line), " %d,%d", &tri, &sri) != 2) {
        return false;
    }

    *telephony_out = tri;
    *sbd_out = sri;
    return true;
}

bool iridium_parser_msstm(const char *response_line, char *out, size_t out_len)
{
    if (out == NULL || out_len == 0) {
        return false;
    }

    out[0] = '\0';
    if (response_line == NULL) {
        return false;
    }

    const char *value = skip_prefix(response_line);
    while (*value == ' ' || *value == '\t') {
        value++;
    }

    return iridium_parser_copy_string(out, out_len, value);
}

uint16_t iridium_parser_sbd_checksum(const uint8_t *data, size_t len)
{
    uint32_t sum = 0;
    if (data == NULL) {
        return 0;
    }

    for (size_t i = 0; i < len; i++) {
        sum += data[i];
    }
    return (uint16_t)(sum & 0xFFFFu);
}

bool iridium_parser_sbdrb_frame(const uint8_t *frame, size_t frame_len,
                                const uint8_t **payload_out, size_t *payload_len_out)
{
    if (frame == NULL || frame_len < 4 || payload_out == NULL || payload_len_out == NULL) {
        return false;
    }

    size_t payload_len = ((size_t)frame[0] << 8) | (size_t)frame[1];
    if (payload_len > 340 || frame_len < payload_len + 4) {
        return false;
    }

    const uint8_t *payload = frame + 2;
    uint16_t expected = iridium_parser_sbd_checksum(payload, payload_len);
    uint16_t actual = ((uint16_t)frame[2 + payload_len] << 8) | (uint16_t)frame[3 + payload_len];
    if (expected != actual) {
        return false;
    }

    *payload_out = payload;
    *payload_len_out = payload_len;
    return true;
}

bool iridium_uart_finalize_ok(const char **stack_top_first, size_t line_count,
                              char *command, size_t command_len,
                              char *data, size_t data_len)
{
    if (command == NULL || command_len == 0 || data == NULL || data_len == 0) {
        return false;
    }

    command[0] = '\0';
    data[0] = '\0';

    if (stack_top_first == NULL) {
        return line_count == 0;
    }

    /* stack_top_first is newest-first; walk oldest-first for stable multi-line data. */
    for (size_t n = line_count; n > 0; n--) {
        const char *line = stack_top_first[n - 1];
        if (line == NULL) {
            continue;
        }

        if (iridium_parser_starts_with("AT", line)) {
            iridium_parser_copy_string(command, command_len, line);
        } else {
            size_t current_len = strlen(data);
            if (current_len + 1 < data_len) {
                strncat(data, line, data_len - current_len - 1);
            }
        }
    }

    return true;
}

bool iridium_parser_sbdrt_payload(const char *response, char *out, size_t out_len)
{
    if (out == NULL || out_len == 0) {
        return false;
    }

    out[0] = '\0';
    if (response == NULL) {
        return true;
    }

    const char *payload = response;
    if (iridium_parser_starts_with("+SBDRT:", payload)) {
        payload += strlen("+SBDRT:");
    } else if (iridium_parser_starts_with("SBDRT:", payload)) {
        payload += strlen("SBDRT:");
    }

    while (*payload == ' ' || *payload == '\t' || *payload == '\r' || *payload == '\n') {
        payload++;
    }

    size_t len = strlen(payload);
    while (len > 0 && (payload[len - 1] == ' ' || payload[len - 1] == '\t' ||
                       payload[len - 1] == '\r' || payload[len - 1] == '\n')) {
        len--;
    }

    if (len >= out_len) {
        memcpy(out, payload, out_len - 1);
        out[out_len - 1] = '\0';
        return false;
    }

    memcpy(out, payload, len);
    out[len] = '\0';
    return true;
}
