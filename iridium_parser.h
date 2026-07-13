/**
 * Pure parsing helpers for Iridium AT/SBD responses.
 * No FreeRTOS or ESP-IDF dependencies — safe to compile on the host for unit tests.
 */

#ifndef IRIDIUM_PARSER_H_INCLUDED
#define IRIDIUM_PARSER_H_INCLUDED

#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define IRI_PARSER_AT_CMD_MAX     (64)
#define IRI_PARSER_RESPONSE_MAX   (512)

typedef struct {
    int mo_status;
    int momsn;
    int mt_status;
    int mtmsn;
    int mt_length;
    int mt_queued;
} iridium_sbd_session_t;

/** Copy src into dest with null termination; returns false if truncated. */
bool iridium_parser_copy_string(char *dest, size_t dest_size, const char *src);

bool iridium_parser_starts_with(const char *prefix, const char *str);

/** Parse +SBDIX / +SBDSX / +SBDIXA status lines. */
bool iridium_parser_sbd_session(const char *response_line, iridium_sbd_session_t *out);

/** Parse +CSQ signal strength lines. */
bool iridium_parser_csq(const char *response_line, int *csq_out);

/** True when MO status indicates a successful mobile-originated transfer. */
bool iridium_parser_mo_transfer_ok(int mo_status);

/**
 * Apply the UART task's OK-handler logic to a stack snapshot.
 * @param stack_top_first Lines in pop order (top of stack / newest first).
 *   Data lines are concatenated oldest-first for multi-line responses.
 */
bool iridium_uart_finalize_ok(const char **stack_top_first, size_t line_count,
                              char *command, size_t command_len,
                              char *data, size_t data_len);

/**
 * Extract the MT text payload from an AT+SBDRT response body.
 * Strips a leading "+SBDRT:" header and surrounding whitespace.
 */
bool iridium_parser_sbdrt_payload(const char *response, char *out, size_t out_len);

#ifdef __cplusplus
}
#endif

#endif /* IRIDIUM_PARSER_H_INCLUDED */
