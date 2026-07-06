#include "iridium_parser.h"

#include <stdio.h>
#include <string.h>

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

    for (size_t i = 0; i < line_count; i++) {
        const char *line = stack_top_first[i];
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
