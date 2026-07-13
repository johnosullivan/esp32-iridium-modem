#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include <pthread.h>

#include "esp_log.h"
#include "esp_attr.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/uart.h"
#include "driver/gpio.h"

#include "stack.h"
#include "iridium.h"
#include "iridium_parser.h"

static const char *TAG_IRIDIUM = "esp32_iridium";

void ring_satcom_task(void *pvParameters);
void uart_satcom_task(void *pvParameters);
void buffer_satcom_task(void *pvParameters);
void message_satcom_task(void *pvParameters);

static bool iridium_copy_bounded(char *dest, size_t dest_size, const char *src)
{
    if (!iridium_parser_copy_string(dest, dest_size, src)) {
        ESP_LOGW(TAG_IRIDIUM, "Truncated string to fit %u bytes", (unsigned)dest_size);
        return false;
    }
    return true;
}

static bool iridium_apply_sbd_session(const char *data, iridium_t *satcom)
{
    iridium_sbd_session_t session;

    if (!iridium_parser_sbd_session(data, &session)) {
        return false;
    }

    satcom->status_outbound = session.mo_status;
    satcom->sequence_outbound = session.momsn;
    satcom->status_inbound = session.mt_status;
    satcom->sequence_inbound = session.mtmsn;
    satcom->bytes_received = session.mt_length;
    satcom->messages_waiting = session.mt_queued;
    return true;
}

static void iridium_invoke_callback(iridium_t *satcom, iridium_command_t command,
                                    iridium_status_t status)
{
    if (satcom != NULL && satcom->callback != NULL) {
        satcom->callback(satcom, command, status);
    }
}

static void iridium_invoke_message_callback(iridium_t *satcom, const char *data, size_t size)
{
    if (satcom != NULL && satcom->message_callback != NULL) {
        satcom->message_callback(satcom, data, size);
    }
}

static bool iridium_wait_for_response(iridium_t *satcom, int nonce, int wait_interval_ms,
                                      char *result_out, size_t result_len)
{
    const int timeout_ms = satcom->response_timeout_ms > 0 ?
                           satcom->response_timeout_ms : IRI_DEFAULT_TIMEOUT_MS;
    int elapsed_ms = 0;

    while (elapsed_ms < timeout_ms) {
        if (iridium_get_iqs(satcom) == IQS_OPEN) {
            if (result_out != NULL && result_len > 0) {
                iridium_copy_bounded(result_out, result_len, satcom->buffer_data);
            }
            return true;
        }

        vTaskDelay(pdMS_TO_TICKS(wait_interval_ms));
        elapsed_ms += wait_interval_ms;
    }

    ESP_LOGW(TAG_IRIDIUM, "Timed out waiting for response (nonce=%d)", nonce);
    iridium_update_iqs(satcom, IQS_OPEN);
    return false;
}

static iridium_result_t iridium_make_error(void)
{
    iridium_result_t result = {
        .status = SAT_ERROR,
    };
    result.result[0] = '\0';
    return result;
}

static iridium_result_t iridium_make_busy(void)
{
    iridium_result_t result = {
        .status = SAT_BUSY,
    };
    result.result[0] = '\0';
    return result;
}

static iridium_result_t iridium_make_ok(const char *payload)
{
    iridium_result_t result = {
        .status = SAT_OK,
    };
    iridium_copy_bounded(result.result, sizeof(result.result), payload);
    return result;
}

static void iridium_try_start_ring_task(iridium_t *satcom);
static void ri_gpio_task(void *pvParameters);

static bool iridium_lock_send(iridium_t *satcom, int timeout_ms)
{
    if (timeout_ms < 0) {
        pthread_mutex_lock(&satcom->send_mutex);
        return true;
    }

    const int step_ms = 50;
    int elapsed_ms = 0;

    while (pthread_mutex_trylock(&satcom->send_mutex) != 0) {
        if (elapsed_ms >= timeout_ms) {
            return false;
        }
        vTaskDelay(pdMS_TO_TICKS(step_ms));
        elapsed_ms += step_ms;
    }

    return true;
}

static void iridium_unlock_send(iridium_t *satcom)
{
    pthread_mutex_unlock(&satcom->send_mutex);
}

bool iridium_is_busy(const iridium_t *satcom)
{
    if (satcom == NULL || !satcom->configured || satcom->shutdown_requested) {
        return true;
    }

    if (satcom->ring_task_running) {
        return true;
    }

    return iridium_get_iqs((iridium_t *)satcom) == IQS_WAITING;
}

bool iridium_uart_flow_control_enabled(const iridium_t *satcom)
{
    return satcom != NULL &&
           satcom->uart_rts_number != UART_PIN_NO_CHANGE &&
           satcom->uart_cts_number != UART_PIN_NO_CHANGE;
}

static void IRAM_ATTR iridium_ri_gpio_isr(void *arg)
{
    iridium_t *satcom = (iridium_t *)arg;
    BaseType_t hp = pdFALSE;
    uint8_t evt = 1;

    if (satcom != NULL && satcom->ri_gpio_queue != NULL) {
        xQueueSendFromISR(satcom->ri_gpio_queue, &evt, &hp);
        if (hp) {
            portYIELD_FROM_ISR();
        }
    }
}

iridium_status_t iridium_satcom_process_result(iridium_t *satcom, char *command, char *data)
{
    if (satcom == NULL || command == NULL) {
        return SAT_ERROR;
    }

    if (strcmp("AT", command) == 0 ||
        strcmp("AT&K0", command) == 0 ||
        strcmp("AT&K3", command) == 0 ||
        strcmp("AT&w0", command) == 0 ||
        iridium_parser_starts_with("AT+SBDMTA", command)) {
        return SAT_OK;
    }

    if (strcmp("AT+CGMI", command) == 0) {
        iridium_copy_bounded(satcom->manufacturer_identification,
                             sizeof(satcom->manufacturer_identification), data);
        iridium_invoke_callback(satcom, AT_CGMI, SAT_OK);
        return SAT_OK;
    }

    if (strcmp("AT+CGMM", command) == 0) {
        iridium_copy_bounded(satcom->model_identification,
                             sizeof(satcom->model_identification), data);
        iridium_invoke_callback(satcom, AT_CGMM, SAT_OK);
        return SAT_OK;
    }

    if (strcmp("AT+CSQ", command) == 0) {
        int csq = 0;
        if (!iridium_parser_csq(data, &csq)) {
            return SAT_ERROR;
        }
        satcom->signal_strength = csq;
        iridium_invoke_callback(satcom, AT_CSQ, SAT_OK);
        return SAT_OK;
    }

    if (strcmp("AT+SBDSX", command) == 0) {
        if (!iridium_apply_sbd_session(data, satcom)) {
            return SAT_ERROR;
        }
        iridium_invoke_callback(satcom, AT_SBDSX, SAT_OK);
        return SAT_OK;
    }

    if (strcmp("AT+SBDIX", command) == 0) {
        if (!iridium_apply_sbd_session(data, satcom)) {
            return SAT_ERROR;
        }
        iridium_invoke_callback(satcom, AT_SBDIX, SAT_OK);
        return SAT_OK;
    }

    if (strcmp("AT+SBDIXA", command) == 0) {
        if (!iridium_apply_sbd_session(data, satcom)) {
            return SAT_ERROR;
        }
        iridium_invoke_callback(satcom, AT_SBDIXA, SAT_OK);
        return SAT_OK;
    }

    if (strcmp("AT+SBDRT", command) == 0 ||
        (command[0] == '\0' && (iridium_parser_starts_with("+SBDRT", data) ||
                                iridium_parser_starts_with("SBDRT:", data)))) {
        char payload[IRI_RESPONSE_MAX];
        if (!iridium_parser_sbdrt_payload(data, payload, sizeof(payload))) {
            ESP_LOGW(TAG_IRIDIUM, "SBDRT payload truncated");
        }

        iridium_message_t msg = {0};
        iridium_copy_bounded(msg.data, sizeof(msg.data), payload);
        msg.size = (int)strlen(msg.data);
        if (xQueueSend(satcom->message_queue, &msg, pdMS_TO_TICKS(100)) != pdTRUE) {
            ESP_LOGW(TAG_IRIDIUM, "Message queue full, dropping inbound payload");
            return SAT_ERROR;
        }
        ESP_LOGI(TAG_IRIDIUM, "Inbound MT queued (%d bytes): %.*s",
                 msg.size, msg.size, msg.data);
        return SAT_OK;
    }

    if (strcmp("AT+CRIS", command) == 0) {
        return SAT_OK;
    }

    return SAT_ERROR;
}

iridium_status_t iridium_update_iqs(iridium_t *satcom, iridium_queue_status_t status)
{
    pthread_mutex_lock(&satcom->p_status_mutex);
    satcom->status = status;
    pthread_mutex_unlock(&satcom->p_status_mutex);
    return SAT_OK;
}

iridium_status_t iridium_update_p_nonce(iridium_t *satcom, int nonce)
{
    pthread_mutex_lock(&satcom->p_nonce_mutex);
    satcom->p_nonce = nonce;
    pthread_mutex_unlock(&satcom->p_nonce_mutex);
    return SAT_OK;
}

iridium_queue_status_t iridium_get_iqs(iridium_t *satcom)
{
    iridium_queue_status_t t_status = IQS_NONE;
    pthread_mutex_lock(&satcom->p_status_mutex);
    t_status = satcom->status;
    pthread_mutex_unlock(&satcom->p_status_mutex);
    return t_status;
}

iridium_status_t iridium_send_raw(iridium_t *satcom, char *data, int nonce)
{
    if (satcom == NULL || data == NULL || satcom->shutdown_requested) {
        return SAT_ERROR;
    }

    if (iridium_get_iqs(satcom) == IQS_WAITING) {
        iridium_message_t msg = {0};
        iridium_copy_bounded(msg.data, sizeof(msg.data), data);
        msg.size = (int)strlen(msg.data);
        msg.nonce = nonce;
        if (xQueueSend(satcom->buffer_queue, &msg, pdMS_TO_TICKS(100)) != pdTRUE) {
            ESP_LOGW(TAG_IRIDIUM, "Buffer queue full for nonce %d", nonce);
            return SAT_ERROR;
        }
        ESP_LOGD(TAG_IRIDIUM, "Queued command nonce=%d", nonce);
        return SAT_OK;
    }

    ESP_LOGD(TAG_IRIDIUM, "UART TX nonce=%d len=%u", nonce, (unsigned)strlen(data));
    if (uart_write_bytes(satcom->uart_number, data, strlen(data)) < 0) {
        return SAT_ERROR;
    }

    iridium_update_iqs(satcom, IQS_WAITING);
    iridium_update_p_nonce(satcom, nonce);
    return SAT_OK;
}

iridium_result_t iridium_config_ring(iridium_t *satcom, bool enabled)
{
    iridium_result_t result;

    result = iridium_send(satcom, AT_SBDMTA, enabled ? "1" : "0", true, 500);
    if (result.status != SAT_OK) {
        return result;
    }
    vTaskDelay(pdMS_TO_TICKS(IRI_BUFF_DELAY));

    result = iridium_send(satcom, AT_W0, "", true, 500);
    if (result.status != SAT_OK) {
        return result;
    }
    vTaskDelay(pdMS_TO_TICKS(IRI_BUFF_DELAY));

    result = iridium_send(satcom,
                          iridium_uart_flow_control_enabled(satcom) ? AT_K3 : AT_K0,
                          "", true, 500);
    if (result.status != SAT_OK) {
        return result;
    }
    vTaskDelay(pdMS_TO_TICKS(IRI_BUFF_DELAY));

    return iridium_send(satcom, AT_SBDMTAQ, "", true, 500);
}

iridium_result_t iridium_tx_message(iridium_t *satcom, const char *message)
{
    iridium_result_t result = iridium_make_error();

    if (satcom == NULL || message == NULL) {
        return result;
    }

    if (strlen(message) > IRI_SBD_MAX_BYTES) {
        ESP_LOGE(TAG_IRIDIUM, "Message exceeds SBD limit (%d bytes)", IRI_SBD_MAX_BYTES);
        return result;
    }

    iridium_result_t write_result = iridium_send(satcom, AT_SBDWT, (char *)message, true, 500);
    if (write_result.status != SAT_OK) {
        return write_result;
    }

    const int delays[] = {2000, 4000, 20000, 30000, 300000};

    for (int i = 0; i < 5; i++) {
        iridium_result_t session_result = iridium_send(satcom, AT_SBDIX, NULL, true, 500);
        if (session_result.status == SAT_BUSY) {
            return session_result;
        }
        if (session_result.status != SAT_OK) {
            return session_result;
        }

        if (iridium_parser_mo_transfer_ok(satcom->status_outbound)) {
            return iridium_make_ok(satcom->buffer_data);
        }

        vTaskDelay(pdMS_TO_TICKS(delays[i]));
    }

    ESP_LOGW(TAG_IRIDIUM, "MO transfer failed with status %d", satcom->status_outbound);
    result = iridium_make_error();
    iridium_copy_bounded(result.result, sizeof(result.result), satcom->buffer_data);
    return result;
}

iridium_result_t iridium_rx_message(iridium_t *satcom, char *out, size_t out_len,
                                    size_t *received_len)
{
    iridium_result_t result = iridium_make_error();

    if (satcom == NULL || out == NULL || out_len == 0) {
        return result;
    }

    iridium_result_t status_result = iridium_send(satcom, AT_SBDSX, NULL, true, 500);
    if (status_result.status != SAT_OK) {
        return status_result;
    }

    if (satcom->messages_waiting <= 0 && satcom->bytes_received <= 0) {
        if (received_len != NULL) {
            *received_len = 0;
        }
        out[0] = '\0';
        return iridium_make_ok("");
    }

    iridium_result_t read_result = iridium_send(satcom, AT_SBDRT, NULL, true, 500);
    if (read_result.status != SAT_OK) {
        return read_result;
    }

    iridium_copy_bounded(out, out_len, read_result.result);
    if (received_len != NULL) {
        *received_len = strlen(out);
    }

    return iridium_make_ok(out);
}

iridium_result_t iridium_send(iridium_t *satcom, iridium_command_t command, char *rdata,
                              bool wait_response, int wait_interval)
{
    iridium_result_t result = iridium_make_error();

    if (satcom == NULL || !satcom->configured || satcom->shutdown_requested) {
        return result;
    }

    if (wait_interval <= 0) {
        wait_interval = 500;
    }

    const int lock_timeout_ms = satcom->send_lock_timeout_ms != 0 ?
                                satcom->send_lock_timeout_ms : IRI_DEFAULT_SEND_LOCK_MS;
    if (!iridium_lock_send(satcom, lock_timeout_ms)) {
        ESP_LOGW(TAG_IRIDIUM, "Modem busy (command %d)", (int)command);
        return iridium_make_busy();
    }

    if (satcom->shutdown_requested) {
        iridium_unlock_send(satcom);
        return result;
    }

    satcom->c_nonce++;
    const int t_nonce = satcom->c_nonce;
    char cmd_buf[IRI_SBD_MAX_BYTES + IRI_AT_CMD_MAX + 8];

    switch (command) {
        case AT:
            result.status = iridium_send_raw(satcom, "AT\r", t_nonce);
            break;
        case AT_CSQ:
            result.status = iridium_send_raw(satcom, "AT+CSQ\r", t_nonce);
            break;
        case AT_CGMI:
            result.status = iridium_send_raw(satcom, "AT+CGMI\r", t_nonce);
            break;
        case AT_CGMM:
            result.status = iridium_send_raw(satcom, "AT+CGMM\r", t_nonce);
            break;
        case AT_SBDIX:
            result.status = iridium_send_raw(satcom, "AT+SBDIX\r", t_nonce);
            break;
        case AT_SBDSX:
            result.status = iridium_send_raw(satcom, "AT+SBDSX\r", t_nonce);
            break;
        case AT_MSSTM:
            result.status = iridium_send_raw(satcom, "AT-MSSTM\r", t_nonce);
            break;
        case AT_SBDRT:
            result.status = iridium_send_raw(satcom, "AT+SBDRT\r", t_nonce);
            break;
        case AT_CRIS:
            result.status = iridium_send_raw(satcom, "AT+CRIS\r", t_nonce);
            break;
        case AT_SBDIXA:
            result.status = iridium_send_raw(satcom, "AT+SBDIXA\r", t_nonce);
            break;
        case AT_SBDMTAQ:
            result.status = iridium_send_raw(satcom, "AT+SBDMTA?\r", t_nonce);
            break;
        case AT_SBDWT:
            if (rdata == NULL) {
                iridium_unlock_send(satcom);
                return iridium_make_error();
            }
            snprintf(cmd_buf, sizeof(cmd_buf), "AT+SBDWT=%s\r", rdata);
            result.status = iridium_send_raw(satcom, cmd_buf, t_nonce);
            break;
        case AT_SBDMTA:
            if (rdata == NULL) {
                iridium_unlock_send(satcom);
                return iridium_make_error();
            }
            snprintf(cmd_buf, sizeof(cmd_buf), "AT+SBDMTA=%s\r", rdata);
            result.status = iridium_send_raw(satcom, cmd_buf, t_nonce);
            break;
        case AT_W0:
            result.status = iridium_send_raw(satcom, "AT&w0\r", t_nonce);
            break;
        case AT_K0:
            result.status = iridium_send_raw(satcom, "AT&K0\r", t_nonce);
            break;
        case AT_K3:
            result.status = iridium_send_raw(satcom, "AT&K3\r", t_nonce);
            break;
        default:
            ESP_LOGW(TAG_IRIDIUM, "Unsupported command %d", command);
            iridium_unlock_send(satcom);
            return iridium_make_error();
    }

    if (result.status != SAT_OK) {
        iridium_unlock_send(satcom);
        return result;
    }

    if (!wait_response) {
        iridium_unlock_send(satcom);
        return iridium_make_ok("");
    }

    if (!iridium_wait_for_response(satcom, t_nonce, wait_interval,
                                    result.result, sizeof(result.result))) {
        iridium_unlock_send(satcom);
        return iridium_make_error();
    }

    result.status = SAT_OK;
    ESP_LOGD(TAG_IRIDIUM, "Response ready nonce=%d", t_nonce);
    iridium_unlock_send(satcom);
    return result;
}

static void iridium_clear_ring_task_state(iridium_t *satcom)
{
    pthread_mutex_lock(&satcom->ring_mutex);
    satcom->ring_task_running = 0;
    satcom->task_ring_handle = NULL;
    pthread_mutex_unlock(&satcom->ring_mutex);
}

static void iridium_try_start_ring_task(iridium_t *satcom)
{
    pthread_mutex_lock(&satcom->ring_mutex);
    if (!satcom->ring_task_running && !satcom->shutdown_requested) {
        satcom->ring_task_running = 1;
        if (xTaskCreate(ring_satcom_task,
                        "ring_satcom_task",
                        satcom->task_ring_stack_depth,
                        satcom,
                        12,
                        &satcom->task_ring_handle) != pdPASS) {
            satcom->ring_task_running = 0;
            satcom->task_ring_handle = NULL;
            ESP_LOGE(TAG_IRIDIUM, "Failed to create ring task");
        }
    }
    pthread_mutex_unlock(&satcom->ring_mutex);
}

void ri_gpio_task(void *pvParameters)
{
    iridium_t *satcom = (iridium_t *)pvParameters;
    uint8_t evt;

    for (;;) {
        if (satcom->ri_gpio_queue == NULL ||
            !xQueueReceive(satcom->ri_gpio_queue, &evt, portMAX_DELAY)) {
            continue;
        }

        if (satcom->shutdown_requested) {
            break;
        }

        ESP_LOGI(TAG_IRIDIUM, "RI pin asserted");
        iridium_try_start_ring_task(satcom);
    }

    vTaskDelete(NULL);
}

void ring_satcom_task(void *pvParameters)
{
    iridium_t *satcom = (iridium_t *)pvParameters;
    const int saved_lock_timeout = satcom->send_lock_timeout_ms;
    const int saved_response_timeout = satcom->response_timeout_ms;

    /* Ring owns the modem for the session; wait as long as needed for the lock. */
    satcom->send_lock_timeout_ms = IRI_SEND_LOCK_WAIT_FOREVER;

    vTaskDelay(pdMS_TO_TICKS(1000));

    iridium_send(satcom, AT_CRIS, NULL, true, 500);

    for (;;) {
        if (satcom->shutdown_requested) {
            break;
        }

        iridium_result_t session = iridium_send(satcom, AT_SBDIXA, "", true, 500);
        if (session.status == SAT_OK) {
            ESP_LOGI(TAG_IRIDIUM, "Ring session MO=%d MT=%d queued=%d",
                     satcom->status_outbound, satcom->status_inbound,
                     satcom->messages_waiting);
        }

        if (!iridium_parser_mo_transfer_ok(satcom->status_outbound)) {
            vTaskDelay(pdMS_TO_TICKS(10000));
            continue;
        }

        /* MT buffer may already hold a message even when the gateway queue is empty. */
        if (satcom->status_inbound == MT_SBD_MESSAGE_SUCCESSFULLY_RECEIVED ||
            satcom->bytes_received > 0 ||
            satcom->messages_waiting > 0) {
            vTaskDelay(pdMS_TO_TICKS(IRI_BUFF_DELAY));
            satcom->response_timeout_ms = IRI_SBDRT_TIMEOUT_MS;
            iridium_result_t read = iridium_send(satcom, AT_SBDRT, NULL, true, 500);
            satcom->response_timeout_ms = saved_response_timeout;
            if (read.status != SAT_OK) {
                ESP_LOGW(TAG_IRIDIUM, "Ring SBDRT failed after MT session");
            }
        }

        if (satcom->messages_waiting == 0) {
            break;
        }

        vTaskDelay(pdMS_TO_TICKS(5000));
    }

    satcom->send_lock_timeout_ms = saved_lock_timeout;
    satcom->response_timeout_ms = saved_response_timeout;
    iridium_clear_ring_task_state(satcom);
    vTaskDelete(NULL);
}

void uart_satcom_task(void *pvParameters)
{
    iridium_t *satcom = (iridium_t *)pvParameters;
    uint8_t *dtmp = (uint8_t *)malloc(IRI_RD_BUF_SIZE);
    struct stack_t *s = newStack();
    char line_accum[IRI_RESPONSE_MAX];
    size_t line_accum_len = 0;

    if (dtmp == NULL || s == NULL) {
        ESP_LOGE(TAG_IRIDIUM, "UART task failed to allocate resources");
        free(dtmp);
        destroy_stack(&s);
        vTaskDelete(NULL);
        return;
    }

    line_accum[0] = '\0';

    for (;;) {
        uart_event_t event;
        if (!xQueueReceive(satcom->uart_queue, &event, portMAX_DELAY)) {
            continue;
        }

        if (satcom->shutdown_requested) {
            break;
        }

        memset(dtmp, 0, IRI_RD_BUF_SIZE);
        switch (event.type) {
            case UART_DATA: {
                int read_len = uart_read_bytes(satcom->uart_number, dtmp, event.size,
                                               portMAX_DELAY);
                if (read_len <= 0) {
                    break;
                }

                ESP_LOGD(TAG_IRIDIUM, "UART RX: %.*s", read_len, dtmp);

                for (int i = 0; i < read_len; i++) {
                    char c = (char)dtmp[i];

                    if (c == '\r' || c == '\n') {
                        if (line_accum_len == 0) {
                            continue;
                        }

                        line_accum[line_accum_len] = '\0';
                        char *line = line_accum;

                        if (iridium_parser_starts_with("AT", line)) {
                            push(s, line);
                        } else if (strcmp("SBDRING", line) == 0) {
                            iridium_try_start_ring_task(satcom);
                        } else if (strcmp("ERROR", line) == 0) {
                            /* Keep waiting; do not clear the pending command stack. */
                        } else if (strcmp("OK", line) == 0) {
                            char data[IRI_RESPONSE_MAX] = {0};
                            char command[IRI_AT_CMD_MAX] = {0};
                            const char *lines[32];
                            char *owned[32] = {0};
                            size_t line_count = 0;

                            while (top(s) != NULL && line_count < 32) {
                                owned[line_count] = copyString(top(s));
                                if (owned[line_count] == NULL) {
                                    break;
                                }
                                lines[line_count] = owned[line_count];
                                line_count++;
                                pop(s);
                            }

                            if (iridium_uart_finalize_ok(lines, line_count, command,
                                                         sizeof(command), data,
                                                         sizeof(data))) {
                                iridium_copy_bounded(satcom->buffer_data,
                                                     sizeof(satcom->buffer_data), data);
                                if (iridium_satcom_process_result(satcom, command,
                                                                  data) == SAT_OK) {
                                    ESP_LOGD(TAG_IRIDIUM, "Parsed %s", command);
                                } else {
                                    ESP_LOGW(TAG_IRIDIUM, "Failed to parse %s = %s",
                                             command, data);
                                }
                            }

                            for (size_t n = 0; n < line_count; n++) {
                                free(owned[n]);
                            }
                            clear_stack(s);
                            iridium_update_iqs(satcom, IQS_OPEN);
                        } else {
                            push(s, line);
                        }

                        line_accum_len = 0;
                        line_accum[0] = '\0';
                    } else if (line_accum_len + 1 < sizeof(line_accum)) {
                        line_accum[line_accum_len++] = c;
                    } else {
                        ESP_LOGW(TAG_IRIDIUM, "UART line too long, dropping");
                        line_accum_len = 0;
                        line_accum[0] = '\0';
                    }
                }
                break;
            }
            case UART_FIFO_OVF:
            case UART_BUFFER_FULL:
                uart_flush_input(satcom->uart_number);
                xQueueReset(satcom->uart_queue);
                line_accum_len = 0;
                line_accum[0] = '\0';
                ESP_LOGW(TAG_IRIDIUM, "UART overflow, input flushed");
                break;
            default:
                break;
        }
    }

    free(dtmp);
    destroy_stack(&s);
    vTaskDelete(NULL);
}

void buffer_satcom_task(void *pvParameters)
{
    iridium_t *satcom = (iridium_t *)pvParameters;
    const int delay_ms = satcom->buffer_delay_ms;

    for (;;) {
        if (satcom->shutdown_requested) {
            break;
        }

        if (iridium_get_iqs(satcom) == IQS_OPEN) {
            iridium_message_t rcv_msg;
            if (xQueueReceive(satcom->buffer_queue, &rcv_msg, 0) == pdTRUE) {
                iridium_send_raw(satcom, rcv_msg.data, rcv_msg.nonce);
            }
        }

        vTaskDelay(pdMS_TO_TICKS(delay_ms));
    }

    vTaskDelete(NULL);
}

void message_satcom_task(void *pvParameters)
{
    iridium_t *satcom = (iridium_t *)pvParameters;
    const int delay_ms = satcom->buffer_delay_ms;

    for (;;) {
        if (satcom->shutdown_requested) {
            break;
        }

        iridium_message_t rcv_msg;
        if (xQueueReceive(satcom->message_queue, &rcv_msg, pdMS_TO_TICKS(delay_ms)) == pdTRUE) {
            iridium_invoke_message_callback(satcom, rcv_msg.data, (size_t)rcv_msg.size);
        }
    }

    vTaskDelete(NULL);
}

iridium_t *iridium_default_configuration(void)
{
    iridium_t *satcom = calloc(1, sizeof(iridium_t));
    if (satcom == NULL) {
        return NULL;
    }

    satcom->buffer_size = 10;
    satcom->message_queue_size = 20;
    satcom->buffer_delay_ms = 1000;
    satcom->response_timeout_ms = IRI_DEFAULT_TIMEOUT_MS;
    satcom->send_lock_timeout_ms = IRI_DEFAULT_SEND_LOCK_MS;
    satcom->baud_rate = IRI_DEFAULT_BAUD_RATE;
    satcom->task_message_stack_depth = 4096;
    satcom->task_buffer_stack_depth = 2048;
    satcom->task_uart_stack_depth = 8192;
    satcom->task_ring_stack_depth = 8192;
    satcom->gpio_sleep_pin_number = -1;
    satcom->gpio_net_pin_number = -1;
    satcom->gpio_ri_pin_number = -1;
    satcom->uart_rts_number = UART_PIN_NO_CHANGE;
    satcom->uart_cts_number = UART_PIN_NO_CHANGE;
    return satcom;
}

int iridium_is_available(iridium_t *satcom)
{
    if (satcom == NULL || satcom->gpio_net_pin_number == -1) {
        return -1;
    }
    return gpio_get_level(satcom->gpio_net_pin_number);
}

int iridium_is_ringing(iridium_t *satcom)
{
    if (satcom == NULL || satcom->gpio_ri_pin_number == -1) {
        return -1;
    }

    /* RockBLOCK RI is active low. */
    return gpio_get_level(satcom->gpio_ri_pin_number) == 0 ? 1 : 0;
}

iridium_status_t iridium_system_spec(iridium_t *satcom)
{
    iridium_result_t r = iridium_send(satcom, AT_CGMI, NULL, true, 500);
    if (r.status != SAT_OK) {
        return r.status;
    }

    r = iridium_send(satcom, AT_CGMM, NULL, true, 500);
    return r.status;
}

iridium_status_t iridium_modem_sleep(iridium_t *satcom)
{
    if (satcom == NULL || satcom->gpio_sleep_pin_number == -1) {
        return SAT_ERROR;
    }

    if (gpio_set_level(satcom->gpio_sleep_pin_number, IRI_GPIO_SLP_OFF) != ESP_OK) {
        return SAT_ERROR;
    }

    return SAT_OK;
}

iridium_status_t iridium_modem_wake(iridium_t *satcom)
{
    if (satcom == NULL || !satcom->configured) {
        return SAT_ERROR;
    }

    if (satcom->gpio_sleep_pin_number != -1) {
        if (gpio_set_level(satcom->gpio_sleep_pin_number, IRI_GPIO_SLP_ON) != ESP_OK) {
            return SAT_ERROR;
        }
        vTaskDelay(pdMS_TO_TICKS(IRI_GPIO_CONF_BUFF));
    }

    iridium_result_t probe = iridium_send(satcom, AT, NULL, true, 500);
    return probe.status;
}

static void iridium_stop_task(TaskHandle_t *handle)
{
    if (handle != NULL && *handle != NULL) {
        vTaskDelete(*handle);
        *handle = NULL;
    }
}

static void iridium_destroy_resources(iridium_t *satcom)
{
    if (satcom == NULL) {
        return;
    }

    satcom->shutdown_requested = true;

    iridium_stop_task(&satcom->task_ring_handle);
    iridium_stop_task(&satcom->task_ri_handle);
    iridium_stop_task(&satcom->task_uart_handle);
    iridium_stop_task(&satcom->task_buffer_handle);
    iridium_stop_task(&satcom->task_message_handle);

    if (satcom->uart_queue != NULL) {
        uart_driver_delete(satcom->uart_number);
        satcom->uart_queue = NULL;
    }

    if (satcom->buffer_queue != NULL) {
        vQueueDelete(satcom->buffer_queue);
        satcom->buffer_queue = NULL;
    }
    if (satcom->message_queue != NULL) {
        vQueueDelete(satcom->message_queue);
        satcom->message_queue = NULL;
    }
    if (satcom->ri_gpio_queue != NULL) {
        vQueueDelete(satcom->ri_gpio_queue);
        satcom->ri_gpio_queue = NULL;
    }
    if (satcom->gpio_ri_pin_number != -1) {
        gpio_isr_handler_remove((gpio_num_t)satcom->gpio_ri_pin_number);
    }
    satcom->uart_queue = NULL;

    pthread_mutex_destroy(&satcom->p_status_mutex);
    pthread_mutex_destroy(&satcom->p_nonce_mutex);
    pthread_mutex_destroy(&satcom->ring_mutex);
    pthread_mutex_destroy(&satcom->send_mutex);

    satcom->configured = false;
    satcom->ring_task_running = 0;
}

iridium_status_t iridium_deinit(iridium_t *satcom)
{
    if (satcom == NULL || !satcom->configured) {
        return SAT_ERROR;
    }

    iridium_destroy_resources(satcom);
    return SAT_OK;
}

static iridium_status_t iridium_configure_gpio(iridium_t *satcom)
{
    if (satcom->gpio_sleep_pin_number != -1) {
        gpio_config_t slp_conf = {
            .intr_type = GPIO_INTR_DISABLE,
            .mode = GPIO_MODE_OUTPUT,
            .pin_bit_mask = (1ULL << satcom->gpio_sleep_pin_number),
            .pull_down_en = GPIO_PULLDOWN_ENABLE,
            .pull_up_en = GPIO_PULLUP_DISABLE,
        };

        if (gpio_config(&slp_conf) != ESP_OK) {
            return SAT_ERROR;
        }

        vTaskDelay(pdMS_TO_TICKS(IRI_GPIO_CONF_BUFF));
        gpio_set_level(satcom->gpio_sleep_pin_number, IRI_GPIO_SLP_ON);
    }

    if (satcom->gpio_net_pin_number != -1) {
        gpio_config_t net_conf = {
            .intr_type = GPIO_INTR_DISABLE,
            .mode = GPIO_MODE_INPUT,
            .pin_bit_mask = (1ULL << satcom->gpio_net_pin_number),
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .pull_up_en = GPIO_PULLUP_DISABLE,
        };

        if (gpio_config(&net_conf) != ESP_OK) {
            return SAT_ERROR;
        }

        vTaskDelay(pdMS_TO_TICKS(IRI_GPIO_CONF_BUFF));
    }

    if (satcom->gpio_ri_pin_number != -1) {
        gpio_config_t ri_conf = {
            .intr_type = GPIO_INTR_NEGEDGE,
            .mode = GPIO_MODE_INPUT,
            .pin_bit_mask = (1ULL << satcom->gpio_ri_pin_number),
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .pull_up_en = GPIO_PULLUP_ENABLE,
        };

        if (gpio_config(&ri_conf) != ESP_OK) {
            return SAT_ERROR;
        }

        vTaskDelay(pdMS_TO_TICKS(IRI_GPIO_CONF_BUFF));
    }

    return SAT_OK;
}

static iridium_status_t iridium_start_ri_monitor(iridium_t *satcom)
{
    if (satcom == NULL || satcom->gpio_ri_pin_number == -1) {
        return SAT_OK;
    }

    satcom->ri_gpio_queue = xQueueCreate(4, sizeof(uint8_t));
    if (satcom->ri_gpio_queue == NULL) {
        return SAT_ERROR;
    }

    esp_err_t isr_err = gpio_install_isr_service(0);
    if (isr_err != ESP_OK && isr_err != ESP_ERR_INVALID_STATE) {
        return SAT_ERROR;
    }

    if (gpio_isr_handler_add((gpio_num_t)satcom->gpio_ri_pin_number,
                             iridium_ri_gpio_isr, satcom) != ESP_OK) {
        return SAT_ERROR;
    }

    if (xTaskCreate(ri_gpio_task,
                    "ri_gpio_task",
                    2048,
                    satcom,
                    12,
                    &satcom->task_ri_handle) != pdPASS) {
        gpio_isr_handler_remove((gpio_num_t)satcom->gpio_ri_pin_number);
        return SAT_ERROR;
    }

    ESP_LOGI(TAG_IRIDIUM, "RI GPIO monitor enabled on pin %d",
             satcom->gpio_ri_pin_number);
    return SAT_OK;
}

iridium_status_t iridium_config(iridium_t *satcom)
{
    if (satcom == NULL) {
        return SAT_ERROR;
    }

    if (satcom->configured) {
        ESP_LOGW(TAG_IRIDIUM, "Modem already configured");
        return SAT_OK;
    }

    if (iridium_configure_gpio(satcom) != SAT_OK) {
        return SAT_ERROR;
    }

    uart_hw_flowcontrol_t flow_ctrl = UART_HW_FLOWCTRL_DISABLE;
    if (iridium_uart_flow_control_enabled(satcom)) {
        flow_ctrl = UART_HW_FLOWCTRL_CTS_RTS;
        ESP_LOGI(TAG_IRIDIUM, "UART hardware flow control enabled (RTS/CTS)");
    }

    uart_config_t uart_config = {
        .baud_rate = satcom->baud_rate > 0 ? satcom->baud_rate : IRI_DEFAULT_BAUD_RATE,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = flow_ctrl,
        .source_clk = UART_SCLK_DEFAULT,
    };

    esp_log_level_set(TAG_IRIDIUM, ESP_LOG_INFO);

    pthread_mutex_init(&satcom->p_status_mutex, NULL);
    pthread_mutex_init(&satcom->p_nonce_mutex, NULL);
    pthread_mutex_init(&satcom->ring_mutex, NULL);
    pthread_mutex_init(&satcom->send_mutex, NULL);

    if (satcom->buffer_delay_ms <= 0) {
        satcom->buffer_delay_ms = 1000;
    }
    if (satcom->response_timeout_ms <= 0) {
        satcom->response_timeout_ms = IRI_DEFAULT_TIMEOUT_MS;
    }
    if (satcom->buffer_size <= 0) {
        satcom->buffer_size = 10;
    }
    if (satcom->message_queue_size <= 0) {
        satcom->message_queue_size = 20;
    }

    satcom->c_nonce = 0;
    satcom->p_nonce = 0;
    satcom->ring_task_running = 0;
    satcom->shutdown_requested = false;
    satcom->status = IQS_OPEN;

    satcom->buffer_queue = xQueueCreate(satcom->buffer_size, sizeof(iridium_message_t));
    satcom->message_queue = xQueueCreate(satcom->message_queue_size, sizeof(iridium_message_t));
    if (satcom->buffer_queue == NULL || satcom->message_queue == NULL) {
        iridium_destroy_resources(satcom);
        return SAT_ERROR;
    }

    if (uart_driver_install(satcom->uart_number,
                            IRI_BUF_SIZE * 2,
                            IRI_BUF_SIZE * 2,
                            20,
                            &satcom->uart_queue,
                            0) != ESP_OK) {
        iridium_destroy_resources(satcom);
        return SAT_ERROR;
    }

    if (uart_set_pin(satcom->uart_number,
                     satcom->uart_txn_number,
                     satcom->uart_rxd_number,
                     satcom->uart_rts_number,
                     satcom->uart_cts_number) != ESP_OK ||
        uart_param_config(satcom->uart_number, &uart_config) != ESP_OK) {
        iridium_destroy_resources(satcom);
        return SAT_ERROR;
    }

    if (xTaskCreate(message_satcom_task,
                    "message_satcom_task",
                    satcom->task_message_stack_depth,
                    satcom,
                    12,
                    &satcom->task_message_handle) != pdPASS ||
        xTaskCreate(uart_satcom_task,
                    "uart_satcom_task",
                    satcom->task_uart_stack_depth,
                    satcom,
                    12,
                    &satcom->task_uart_handle) != pdPASS ||
        xTaskCreate(buffer_satcom_task,
                    "buffer_satcom_task",
                    satcom->task_buffer_stack_depth,
                    satcom,
                    12,
                    &satcom->task_buffer_handle) != pdPASS) {
        iridium_destroy_resources(satcom);
        return SAT_ERROR;
    }

    satcom->configured = true;
    vTaskDelay(pdMS_TO_TICKS(1000));

    if (iridium_start_ri_monitor(satcom) != SAT_OK) {
        iridium_destroy_resources(satcom);
        return SAT_ERROR;
    }

    iridium_result_t probe = iridium_send(satcom, AT, NULL, true, 500);
    if (probe.status != SAT_OK) {
        iridium_destroy_resources(satcom);
        return SAT_ERROR;
    }

    return SAT_OK;
}
