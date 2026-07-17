/**
 * Pure parsing helpers for Iridium AT/SBD responses.
 * No FreeRTOS or ESP-IDF dependencies — safe to compile on the host for unit tests.
 */

#ifndef IRIDIUM_PARSER_H_INCLUDED
#define IRIDIUM_PARSER_H_INCLUDED

#include <stddef.h>
#include <stdint.h>
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

/** True when MO status asks the FA to retry later (36 / 38). */
bool iridium_parser_mo_try_later(int mo_status);

/** Suggested retry delay in ms for an MO status (0 if not a retryable wait). */
int iridium_parser_mo_retry_delay_ms(int mo_status, int attempt_index);

/** Parse +CRIS:<tri>,<sri> ring indication status. */
bool iridium_parser_cris(const char *response_line, int *telephony_out, int *sbd_out);

/**
 * Parse -MSSTM:<system_time>.
 * Copies the token after the colon into out (e.g. hex time or "no network service").
 */
bool iridium_parser_msstm(const char *response_line, char *out, size_t out_len);

/** Least-significant 16 bits of the sum of `len` message bytes (SBD checksum). */
uint16_t iridium_parser_sbd_checksum(const uint8_t *data, size_t len);

/**
 * Parse an SBDRB frame: 2-byte BE length + payload + 2-byte BE checksum.
 * @return true when the frame is well-formed and the checksum matches.
 */
bool iridium_parser_sbdrb_frame(const uint8_t *frame, size_t frame_len,
                                const uint8_t **payload_out, size_t *payload_len_out);

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
