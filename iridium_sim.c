#include "iridium_sim.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

static bool is_tx_line(const char *line)
{
    return iridium_parser_starts_with("AT", line);
}

static void trim_inplace(char *line)
{
    if (line == NULL) {
        return;
    }

    size_t len = strlen(line);
    while (len > 0 && isspace((unsigned char)line[len - 1])) {
        line[--len] = '\0';
    }

    size_t start = 0;
    while (line[start] != '\0' && isspace((unsigned char)line[start])) {
        start++;
    }

    if (start > 0) {
        memmove(line, line + start, strlen(line + start) + 1);
    }
}

static bool append_line(iridium_sim_response_t *response, const char *line)
{
    if (response == NULL || line == NULL ||
        response->line_count >= IRI_SIM_MAX_LINES) {
        return false;
    }

    iridium_parser_copy_string(response->lines[response->line_count],
                               sizeof(response->lines[response->line_count]),
                               line);
    response->line_count++;
    return true;
}

static void flush_exchange(iridium_sim_script_t *script,
                           char *current_tx,
                           iridium_sim_response_t *current_rx)
{
    if (current_tx[0] == '\0' ||
        script->exchange_count >= IRI_SIM_MAX_EXCHANGES) {
        return;
    }

    iridium_sim_exchange_t *exchange =
        &script->exchanges[script->exchange_count++];
    iridium_parser_copy_string(exchange->tx_command,
                               sizeof(exchange->tx_command),
                               current_tx);
    exchange->rx = *current_rx;
    current_tx[0] = '\0';
    *current_rx = (iridium_sim_response_t){0};
}

bool iridium_sim_load_fixture(const char *path, iridium_sim_script_t *script)
{
    if (path == NULL || script == NULL) {
        return false;
    }

    FILE *fp = fopen(path, "r");
    if (fp == NULL) {
        return false;
    }

    memset(script, 0, sizeof(*script));

    char line[IRI_SIM_MAX_LINE_LEN];
    char current_tx[IRI_SIM_MAX_LINE_LEN] = {0};
    iridium_sim_response_t current_rx = {0};

    while (fgets(line, sizeof(line), fp) != NULL) {
        trim_inplace(line);
        if (line[0] == '\0' || line[0] == '#') {
            continue;
        }

        if (is_tx_line(line)) {
            if (current_tx[0] != '\0') {
                flush_exchange(script, current_tx, &current_rx);
            } else if (current_rx.line_count > 0) {
                script->unsolicited = current_rx;
                current_rx = (iridium_sim_response_t){0};
            }

            iridium_parser_copy_string(current_tx, sizeof(current_tx), line);
            continue;
        }

        append_line(&current_rx, line);
    }

    fclose(fp);
    flush_exchange(script, current_tx, &current_rx);

    return script->exchange_count > 0 || script->unsolicited.line_count > 0;
}

void iridium_sim_state_init(iridium_sim_state_t *state)
{
    if (state != NULL) {
        memset(state, 0, sizeof(*state));
    }
}

void iridium_sim_uart_stack_reset(iridium_sim_uart_stack_t *stack)
{
    if (stack != NULL) {
        memset(stack, 0, sizeof(*stack));
    }
}

void iridium_sim_device_tx(iridium_sim_state_t *state, const char *command)
{
    if (state == NULL || command == NULL) {
        return;
    }

    iridium_parser_copy_string(state->last_tx, sizeof(state->last_tx), command);

    size_t used = strlen(state->tx_log);
    if (used + 2 < sizeof(state->tx_log)) {
        if (used > 0) {
            state->tx_log[used++] = '\n';
        }
        snprintf(state->tx_log + used, sizeof(state->tx_log) - used, "%s", command);
    }
}

static bool stack_push(iridium_sim_uart_stack_t *stack, const char *line)
{
    if (stack == NULL || line == NULL || stack->depth >= IRI_SIM_MAX_STACK) {
        return false;
    }

    iridium_parser_copy_string(stack->lines[stack->depth],
                               sizeof(stack->lines[stack->depth]),
                               line);
    stack->depth++;
    return true;
}

static bool stack_pop(iridium_sim_uart_stack_t *stack, char *out, size_t out_len)
{
    if (stack == NULL || stack->depth == 0) {
        return false;
    }

    stack->depth--;
    iridium_parser_copy_string(out, out_len, stack->lines[stack->depth]);
    return true;
}

static void infer_command(char *command, size_t command_len,
                          const char *data, const char *last_tx)
{
    if (command == NULL || command_len == 0) {
        return;
    }

    if (iridium_parser_starts_with("+SBDIX:", data) ||
        iridium_parser_starts_with("SBDIX:", data)) {
        iridium_parser_copy_string(command, command_len, "AT+SBDIX");
    } else if (iridium_parser_starts_with("+SBDSX:", data) ||
               iridium_parser_starts_with("SBDSX:", data)) {
        iridium_parser_copy_string(command, command_len, "AT+SBDSX");
    } else if (iridium_parser_starts_with("+CSQ:", data) ||
               iridium_parser_starts_with("CSQ:", data)) {
        iridium_parser_copy_string(command, command_len, "AT+CSQ");
    } else if (last_tx != NULL && iridium_parser_starts_with("AT+SBDRT", last_tx)) {
        iridium_parser_copy_string(command, command_len, "AT+SBDRT");
    } else if (last_tx != NULL) {
        iridium_parser_copy_string(command, command_len, last_tx);
    }
}

static bool apply_parsed_response(iridium_sim_state_t *state,
                                  const char *command,
                                  const char *data)
{
    iridium_sbd_session_t session;
    int csq = 0;

    iridium_parser_copy_string(state->last_command, sizeof(state->last_command), command);
    iridium_parser_copy_string(state->last_data, sizeof(state->last_data), data);

    if (strcmp(command, "AT+CSQ") == 0) {
        if (!iridium_parser_csq(data, &csq)) {
            state->parse_errors++;
            return false;
        }
        state->signal_strength = csq;
        return true;
    }

    if (strcmp(command, "AT+SBDSX") == 0 ||
        strcmp(command, "AT+SBDIX") == 0 ||
        strcmp(command, "AT+SBDIXA") == 0) {
        if (!iridium_parser_sbd_session(data, &session)) {
            state->parse_errors++;
            return false;
        }
        state->mo_status = session.mo_status;
        state->momsn = session.momsn;
        state->mt_status = session.mt_status;
        state->mtmsn = session.mtmsn;
        state->mt_length = session.mt_length;
        state->mt_queued = session.mt_queued;
        return true;
    }

    if (strcmp(command, "AT+SBDRT") == 0) {
        iridium_parser_copy_string(state->last_payload,
                                   sizeof(state->last_payload),
                                   data);
        return true;
    }

    if (strcmp(command, "AT") == 0 ||
        iridium_parser_starts_with("AT+SBDMTA", command) ||
        iridium_parser_starts_with("AT+SBDWT", command) ||
        iridium_parser_starts_with("AT+CRIS", command)) {
        return true;
    }

    if (data[0] == '\0') {
        /* Bare OK for commands we do not model field data for yet. */
        return true;
    }

    state->parse_errors++;
    return false;
}

static bool finalize_ok(iridium_sim_uart_stack_t *stack, iridium_sim_state_t *state)
{
    char command[IRI_PARSER_AT_CMD_MAX] = {0};
    char data[IRI_PARSER_RESPONSE_MAX] = {0};
    char tmp[IRI_SIM_MAX_LINE_LEN];

    while (stack_pop(stack, tmp, sizeof(tmp))) {
        if (iridium_parser_starts_with("AT", tmp)) {
            iridium_parser_copy_string(command, sizeof(command), tmp);
        } else {
            size_t current_len = strlen(data);
            if (current_len + 1 < sizeof(data)) {
                strncat(data, tmp, sizeof(data) - current_len - 1);
            }
        }
    }

    if (command[0] == '\0') {
        infer_command(command, sizeof(command), data, state->last_tx);
    }

    state->ok_count++;
    return apply_parsed_response(state, command, data);
}

bool iridium_sim_feed_line(iridium_sim_uart_stack_t *stack,
                           iridium_sim_state_t *state,
                           const char *line)
{
    if (stack == NULL || state == NULL || line == NULL) {
        return false;
    }

    if (line[0] == '\0') {
        return true;
    }

    if (iridium_parser_starts_with("AT", line)) {
        return stack_push(stack, line);
    }

    if (strcmp(line, "SBDRING") == 0) {
        state->ring_alerts++;
        return true;
    }

    if (strcmp(line, "ERROR") == 0) {
        state->parse_errors++;
        return true;
    }

    if (strcmp(line, "OK") == 0) {
        return finalize_ok(stack, state);
    }

    return stack_push(stack, line);
}

bool iridium_sim_feed_response(iridium_sim_uart_stack_t *stack,
                               iridium_sim_state_t *state,
                               const iridium_sim_response_t *response)
{
    if (stack == NULL || state == NULL || response == NULL) {
        return false;
    }

    for (size_t i = 0; i < response->line_count; i++) {
        if (!iridium_sim_feed_line(stack, state, response->lines[i])) {
            return false;
        }
    }

    return true;
}

bool iridium_sim_feed_bytes(iridium_sim_uart_stack_t *stack,
                            iridium_sim_state_t *state,
                            const char *bytes)
{
    if (stack == NULL || state == NULL || bytes == NULL) {
        return false;
    }

    char buffer[IRI_SIM_MAX_LINE_LEN];
    strncpy(buffer, bytes, sizeof(buffer) - 1);
    buffer[sizeof(buffer) - 1] = '\0';

    for (char *line = strtok(buffer, "\r\n"); line != NULL;
         line = strtok(NULL, "\r\n")) {
        trim_inplace(line);
        if (!iridium_sim_feed_line(stack, state, line)) {
            return false;
        }
    }

    return true;
}

bool iridium_sim_find_exchange(const iridium_sim_script_t *script,
                               const char *tx_command,
                               const iridium_sim_exchange_t **exchange_out)
{
    if (script == NULL || tx_command == NULL || exchange_out == NULL) {
        return false;
    }

    for (size_t i = 0; i < script->exchange_count; i++) {
        if (strcmp(script->exchanges[i].tx_command, tx_command) == 0) {
            *exchange_out = &script->exchanges[i];
            return true;
        }
    }

    return false;
}

size_t iridium_sim_format_serial_payload(const iridium_sim_response_t *response,
                                         char *out,
                                         size_t out_len)
{
    if (response == NULL || out == NULL || out_len == 0) {
        return 0;
    }

    out[0] = '\0';
    size_t used = 0;

    for (size_t i = 0; i < response->line_count; i++) {
        int written = snprintf(out + used, out_len - used, "%s%s",
                               i == 0 ? "" : "\r\n",
                               response->lines[i]);
        if (written < 0 || (size_t)written >= out_len - used) {
            break;
        }
        used += (size_t)written;
    }

    if (used + 2 < out_len) {
        out[used++] = '\r';
        out[used++] = '\n';
        out[used] = '\0';
    }

    return used;
}

bool iridium_sim_run_exchange(iridium_sim_uart_stack_t *stack,
                              iridium_sim_state_t *state,
                              const iridium_sim_script_t *script,
                              const char *tx_command)
{
    const iridium_sim_exchange_t *exchange = NULL;

    if (!iridium_sim_find_exchange(script, tx_command, &exchange)) {
        return false;
    }

    iridium_sim_device_tx(state, tx_command);
    return iridium_sim_feed_response(stack, state, &exchange->rx);
}

bool iridium_sim_replay_script(iridium_sim_uart_stack_t *stack,
                               iridium_sim_state_t *state,
                               const iridium_sim_script_t *script)
{
    if (stack == NULL || state == NULL || script == NULL) {
        return false;
    }

    iridium_sim_uart_stack_reset(stack);

    if (script->unsolicited.line_count > 0) {
        if (!iridium_sim_feed_response(stack, state, &script->unsolicited)) {
            return false;
        }
    }

    for (size_t i = 0; i < script->exchange_count; i++) {
        const iridium_sim_exchange_t *exchange = &script->exchanges[i];
        iridium_sim_device_tx(state, exchange->tx_command);
        if (!iridium_sim_feed_response(stack, state, &exchange->rx)) {
            return false;
        }
    }

    return true;
}
