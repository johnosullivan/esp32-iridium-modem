/**
 * Host-side Iridium modem serial traffic simulator.
 * Replays fixture transcripts as \r\n-delimited UART payloads for testing.
 */

#ifndef IRIDIUM_SIM_H_INCLUDED
#define IRIDIUM_SIM_H_INCLUDED

#include <stddef.h>
#include <stdbool.h>

#include "iridium_parser.h"

#ifdef __cplusplus
extern "C" {
#endif

#define IRI_SIM_MAX_LINES       (32)
#define IRI_SIM_MAX_LINE_LEN    (512)
#define IRI_SIM_MAX_EXCHANGES   (16)
#define IRI_SIM_MAX_STACK       (16)
#define IRI_SIM_TX_LOG_LEN      (4096)

typedef struct {
    char lines[IRI_SIM_MAX_LINES][IRI_SIM_MAX_LINE_LEN];
    size_t line_count;
} iridium_sim_response_t;

typedef struct {
    char tx_command[IRI_SIM_MAX_LINE_LEN];
    iridium_sim_response_t rx;
} iridium_sim_exchange_t;

typedef struct {
    iridium_sim_response_t unsolicited;
    iridium_sim_exchange_t exchanges[IRI_SIM_MAX_EXCHANGES];
    size_t exchange_count;
} iridium_sim_script_t;

typedef struct {
    char lines[IRI_SIM_MAX_STACK][IRI_SIM_MAX_LINE_LEN];
    size_t depth;
} iridium_sim_uart_stack_t;

typedef struct {
    char last_tx[IRI_SIM_MAX_LINE_LEN];
    char last_command[IRI_PARSER_AT_CMD_MAX];
    char last_data[IRI_PARSER_RESPONSE_MAX];
    char last_payload[IRI_PARSER_RESPONSE_MAX];
    char tx_log[IRI_SIM_TX_LOG_LEN];

    int signal_strength;
    int mo_status;
    int momsn;
    int mt_status;
    int mtmsn;
    int mt_length;
    int mt_queued;
    int ring_alerts;
    int ok_count;
    int parse_errors;
} iridium_sim_state_t;

/** Load a fixture file (see test/host/fixtures/ for format). */
bool iridium_sim_load_fixture(const char *path, iridium_sim_script_t *script);

void iridium_sim_state_init(iridium_sim_state_t *state);
void iridium_sim_uart_stack_reset(iridium_sim_uart_stack_t *stack);

/** Record a device-side AT command (modem TX log). */
void iridium_sim_device_tx(iridium_sim_state_t *state, const char *command);

/**
 * Feed one modem response line through the UART RX state machine.
 * Blank lines are ignored.
 */
bool iridium_sim_feed_line(iridium_sim_uart_stack_t *stack,
                           iridium_sim_state_t *state,
                           const char *line);

/** Feed a full modem transcript block (multiple lines). */
bool iridium_sim_feed_response(iridium_sim_uart_stack_t *stack,
                               iridium_sim_state_t *state,
                               const iridium_sim_response_t *response);

/** Feed raw serial bytes (\\r\\n delimited) as the modem would send them. */
bool iridium_sim_feed_bytes(iridium_sim_uart_stack_t *stack,
                            iridium_sim_state_t *state,
                            const char *bytes);

/**
 * Run one device TX + scripted modem RX exchange from a loaded fixture.
 * @param tx_command Command without trailing \\r (e.g. "AT+SBDIX").
 */
bool iridium_sim_run_exchange(iridium_sim_uart_stack_t *stack,
                              iridium_sim_state_t *state,
                              const iridium_sim_script_t *script,
                              const char *tx_command);

/** Replay every exchange in a fixture; returns false if any step fails. */
bool iridium_sim_replay_script(iridium_sim_uart_stack_t *stack,
                               iridium_sim_state_t *state,
                               const iridium_sim_script_t *script);

/** Find the modem response for a TX command in a loaded script. */
bool iridium_sim_find_exchange(const iridium_sim_script_t *script,
                               const char *tx_command,
                               const iridium_sim_exchange_t **exchange_out);

/** Format modem response lines as a serial payload (lines joined with \\r\\n). */
size_t iridium_sim_format_serial_payload(const iridium_sim_response_t *response,
                                         char *out,
                                         size_t out_len);

#ifdef __cplusplus
}
#endif

#endif /* IRIDIUM_SIM_H_INCLUDED */
