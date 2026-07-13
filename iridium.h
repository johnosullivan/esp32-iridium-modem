/**
Copyright 2024 John O'Sullivan <john@osullivan.dev>

Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files (the "Software"), to deal in the Software without restriction, including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do so, subject to the following conditions:
The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.
THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/

/**
 * @author John O'Sullivan <john@osullivan.dev>
 * @paragraph ESP32 Satcom Library (Iridium Network)
 * Supported Hardware
 * - RockBLOCK 9603 https://cdn-shop.adafruit.com/product-files/4521/RockBLOCK-9603-Data-Sheet-Small.pdf
 * Language:  C/C++
 */

#ifndef IRIDIUM_H_INCLUDED
#define IRIDIUM_H_INCLUDED

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "driver/uart.h"
#include "driver/gpio.h"
#include "pthread.h"

#define IRI_BUF_SIZE            (4096)
#define IRI_RD_BUF_SIZE         (IRI_BUF_SIZE)
#define IRI_SBD_MAX_BYTES       (340)
#define IRI_AT_CMD_MAX          (64)
#define IRI_RESPONSE_MAX        (512)
#define IRI_BUFF_DELAY          (100)
#define IRI_GPIO_CONF_BUFF      (100)
#define IRI_GPIO_SLP_ON         (1)
#define IRI_GPIO_SLP_OFF        (0)
#define IRI_DEFAULT_BAUD_RATE       (19200)
#define IRI_DEFAULT_TIMEOUT_MS      (30000)
#define IRI_DEFAULT_SEND_LOCK_MS    (2000)
#define IRI_SBDRT_TIMEOUT_MS        (5000)
#define IRI_SEND_LOCK_WAIT_FOREVER  (-1)

typedef enum iridium_command {
    SBDRING         = -1,
    AT              = 0,
    AT_CSQ          = 1,
    AT_SBDSX        = 2,
    AT_CGMI         = 3,
    AT_CGMM         = 4,
    AT_SBDRT        = 5,
    AT_SBDWT        = 6,
    AT_SBDIX        = 7,
    AT_MSSTM        = 8,
    AT_SBDMTA       = 9,
    AT_W0           = 10,
    AT_CRIS         = 11,
    AT_SBDIXA       = 12,
    AT_K0           = 13,
    AT_SBDMTAQ      = 14,
    AT_K3           = 15,
} iridium_command_t;

typedef enum iridium_status {
    SAT_ERROR       = -1,
    SAT_BUSY        = 0,
    SAT_OK          = 1
} iridium_status_t;

typedef enum iridium_queue_status {
    IQS_NONE        = -1,
    IQS_OPEN        = 0,
    IQS_WAITING     = 1
} iridium_queue_status_t;

typedef enum iridium_mt_status {
    MT_NO_SBD_MESSAGE_RECEIVED              = 0,
    MT_SBD_MESSAGE_SUCCESSFULLY_RECEIVED    = 1,
    MT_GSS_ERROR_OCCURRED                   = 2
} iridium_mt_status_t;

typedef enum iridium_mo_status {
    MO_TRANSFERRED_SUCCESSFULLY                     = 0,
    MO_TRANSFERRED_SUCCESSFULLY_TOO_BIG             = 1,
    MO_TRANSFERRED_SUCCESSFULLY_LOC_NOT_ACCEPTED    = 2,
    MO_GSS_NOT_COMPLETED                            = 10,
    MO_GSS_MESSAGE_QUEUE_FULL                       = 11,
    MO_GSS_MESSAGE_MANY_SEQ                         = 12,
    MO_GSS_MESSAGE_SESSION_INCOMPLETE               = 13,
    MO_INVALID_SEQMENT_SIZE                         = 14,
    MO_ACCESS_DENIED                                = 15,
    MO_ISU_LOCKED                                   = 16,
    MO_GATEWAY_NOT_RESPONDING                       = 17,
    MO_CONNECTION_LOST                              = 18,
    MO_LINK_FAILURE                                 = 19,
    MO_NO_NETWORK_SERVICE                           = 32,
    MO_ANTENNA_FAULT                                = 33,
    MO_RADIO_DISABLED                               = 34,
    MO_ISU_IS_BUSY                                  = 35,
    MO_TRY_LATER_3_MIN                              = 36,
    MO_SBD_SERVICE_TERMP_DISABLED                   = 37,
    MO_TRY_LATER_TRAFFIC_PERIOD                     = 38,
    MO_BAND_VIOLATION                               = 64,
    MO_PLL_LOCK_FAILURE                             = 65
} iridium_mo_status_t;

typedef struct iridium iridium_t;

typedef void (*iridium_event_callback_t)(iridium_t *satcom, iridium_command_t command, iridium_status_t status);
typedef void (*iridium_message_callback_t)(iridium_t *satcom, const char *data, size_t size);

typedef struct iridium {
    QueueHandle_t uart_queue;
    QueueHandle_t buffer_queue;
    QueueHandle_t message_queue;

    int signal_strength;
    int status_inbound;
    int status_outbound;
    int sequence_inbound;
    int sequence_outbound;
    int bytes_received;
    int messages_waiting;

    char manufacturer_identification[32];
    char model_identification[64];

    int c_nonce;
    int p_nonce;
    int buffer_size;
    int message_queue_size;
    int buffer_delay_ms;
    int response_timeout_ms;
    int send_lock_timeout_ms;
    int baud_rate;

    int uart_number;
    int uart_txn_number;
    int uart_rxd_number;
    int uart_rts_number;
    int uart_cts_number;

    int gpio_sleep_pin_number;
    int gpio_net_pin_number;
    int gpio_ri_pin_number;

    int task_message_stack_depth;
    int task_buffer_stack_depth;
    int task_uart_stack_depth;
    int task_ring_stack_depth;

    char buffer_data[IRI_RESPONSE_MAX];
    iridium_queue_status_t status;

    iridium_event_callback_t callback;
    iridium_message_callback_t message_callback;

    pthread_mutex_t p_status_mutex;
    pthread_mutex_t p_nonce_mutex;
    pthread_mutex_t ring_mutex;
    pthread_mutex_t send_mutex;

    volatile int ring_task_running;
    volatile bool configured;
    volatile bool shutdown_requested;

    TaskHandle_t task_uart_handle;
    TaskHandle_t task_buffer_handle;
    TaskHandle_t task_message_handle;
    TaskHandle_t task_ring_handle;
    TaskHandle_t task_ri_handle;
    QueueHandle_t ri_gpio_queue;
} iridium_t;

typedef struct iridium_message {
    char data[IRI_SBD_MAX_BYTES + IRI_AT_CMD_MAX];
    int size;
    int nonce;
    int command;
} iridium_message_t;

typedef struct iridium_result {
    char result[IRI_RESPONSE_MAX];
    iridium_status_t status;
} iridium_result_t;

iridium_t *iridium_default_configuration(void);

iridium_status_t iridium_config(iridium_t *satcom);
iridium_status_t iridium_deinit(iridium_t *satcom);

iridium_result_t iridium_send(iridium_t *satcom, iridium_command_t command, char *rdata,
                              bool wait_response, int wait_interval);
iridium_result_t iridium_config_ring(iridium_t *satcom, bool enabled);
iridium_result_t iridium_tx_message(iridium_t *satcom, const char *message);
iridium_result_t iridium_rx_message(iridium_t *satcom, char *out, size_t out_len, size_t *received_len);

iridium_status_t iridium_system_spec(iridium_t *satcom);
iridium_status_t iridium_modem_sleep(iridium_t *satcom);
iridium_status_t iridium_modem_wake(iridium_t *satcom);

int iridium_is_available(iridium_t *satcom);
int iridium_is_ringing(iridium_t *satcom);
bool iridium_is_busy(const iridium_t *satcom);
bool iridium_uart_flow_control_enabled(const iridium_t *satcom);

iridium_status_t iridium_satcom_process_result(iridium_t *satcom, char *command, char *data);
iridium_status_t iridium_update_iqs(iridium_t *satcom, iridium_queue_status_t status);
iridium_status_t iridium_update_p_nonce(iridium_t *satcom, int nonce);
iridium_queue_status_t iridium_get_iqs(iridium_t *satcom);
iridium_status_t iridium_send_raw(iridium_t *satcom, char *data, int nonce);

#ifdef __cplusplus
}
#endif

#endif /* IRIDIUM_H_INCLUDED */
