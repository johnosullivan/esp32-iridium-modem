#ifndef IRIDIUM_H_INCLUDED
#define IRIDIUM_H_INCLUDED

/**
 * @author John O'Sullivan
 * @paragraph ESP32 Satcom Library (Iridium Network)
 * Language:  C/C++
 */

#ifdef __cplusplus
extern "C" {
#endif

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <assert.h>
#include <pthread.h>

#include "nvs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_log.h"
#include "esp_event.h"
#include "nvs_flash.h"
#include "esp_system.h"
#include "esp_spi_flash.h"
#include "driver/uart.h"
#include "driver/gpio.h"
#include "freertos/queue.h"

#include "stack.h"

#define IRI_BUF_SIZE    (4096)
#define IRI_RD_BUF_SIZE (IRI_BUF_SIZE)
#define IRI_BUFF_DELAY  (100)

/**
 * @brief the enum to represent the AT commands. 
 */
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

} iridium_command_t;

/**
 * @brief the iridium command status.
 */
typedef enum iridium_status {
    SAT_ERROR       = -1,
    SAT_OK          = 1 
} iridium_status_t;

/**
 * @brief the iridium UART queue status.
 */
typedef enum iridium_queue_status {
    IQS_NONE        = -1,
    IQS_OPEN        = 0,
    IQS_WAITING     = 1 
} iridium_queue_status_t;

typedef enum iridium_mt_status {
    /* <MT status> */
    MT_NO_SBD_MESSAGE_RECEIVED              = 0, // No SBD message to receive from the GSS.
    MT_SBD_MESSAGE_SUCCESSFULLY_RECEIVED    = 1, // SBD message successfully received from the GSS.
    MT_GSS_ERROR_OCCURRED                   = 2  // An error occurred while attempting to perform a mailbox check or receive a message from the GSS.
} iridium_mt_status_t;

typedef enum iridium_mo_status {
    /* <MO status> */
    MO_TRANSFERRED_SUCCESSFULLY                     = 0,  // MO message, if any, transferred successfully.
    MO_TRANSFERRED_SUCCESSFULLY_TOO_BIG             = 1,  // MO message, if any, transferred successfully, but the MT message in the queue was too big to be transferred.
    MO_TRANSFERRED_SUCCESSFULLY_LOC_NOT_ACCEPTED    = 2,  // MO message, if any, transferred successfully, but the requested Location Update was not accepted.
    MO_GSS_NOT_COMPLETED                            = 10, // GSS reported that the call did not complete in the allowed time.
    MO_GSS_MESSAGE_QUEUE_FULL                       = 11, // MO message queue at the GSS is full.
    MO_GSS_MESSAGE_MANY_SEQ                         = 12, // MO message has too many segments.
    MO_GSS_MESSAGE_SESSION_INCOMPLETE               = 13, // GSS reported that the session did not complete.
    MO_INVALID_SEQMENT_SIZE                         = 14, // Invalid segment size.
    MO_ACCESS_DENIED                                = 15, // Access is denied.
    MO_ISU_LOCKED                                   = 16, // ISU has been locked and may not make SBD calls (see +CULK command).
    MO_GATEWAY_NOT_RESPONDING                       = 17, // Gateway not responding (local session timeout).
    MO_CONNECTION_LOST                              = 18, // Connection lost (RF drop).
    MO_LINK_FAILURE                                 = 19, // Link failure (A protocol error caused termination of the call).
    MO_NO_NETWORK_SERVICE                           = 32, // No network service, unable to initiate call.
    MO_ANTENNA_FAULT                                = 33, // Antenna fault, unable to initiate call.
    MO_RADIO_DISABLED                               = 34, // Radio is disabled, unable to initiate call (see *Rn command).
    MO_ISU_IS_BUSY                                  = 35, // ISU is busy, unable to initiate call.
    MO_TRY_LATER_3_MIN                              = 36, // Try later, must wait 3 minutes since last registration.
    MO_SBD_SERVICE_TERMP_DISABLED                   = 37, // SBD service is temporarily disabled.
    MO_TRY_LATER_TRAFFIC_PERIOD                     = 38, // Try later, traffic management period (see +SBDLOE command)
    MO_BAND_VIOLATION                               = 64, // Band violation (attempt to transmit outside permitted frequency band).
    MO_PLL_LOCK_FAILURE                             = 65  // PLL lock failure; hardware error during attempted transmit.
} iridium_mo_status_t;

/**
 * @brief the core iridum struct with all configuration / status values.
 * 
 * @paragraph 
 * 
 * 0 - 4 = Transmit successful
 * 32    = No network service
 * MO    = Mobile Originated 
 * MT    = Mobile Terminated
 */
typedef struct iridium {
    QueueHandle_t uart_queue;
    QueueHandle_t buffer_queue;
    QueueHandle_t message_queue;
    /* signal */
    int signal_strength;
    /* messaging */
    int status_inbound;
    int status_outbound;
    int sequence_inbound;
    int sequence_outbound;
    int bytes_received;
    int messages_waiting;
    /* about */
    char manufacturer_identification[20];
    char model_identification[50];
    /* quene processing */
    int c_nonce;
    int p_nonce;
    int buffer_size;
    int buffer_delay_ms;
    int uart_number;
    int uart_txn_number;
    int uart_rxd_number;
    int uart_rts_number;
    int uart_cts_number;
    int ring_task_running;
    char buffer_data[100];
    iridium_queue_status_t status;
    pthread_mutex_t p_status_mutex;
    pthread_mutex_t p_nonce_mutex;
    /* stack sizes */
    int task_message_stack_depth;
    int task_buffer_stack_depth;
    int task_uart_stack_depth;
    /* callbacks */ 
    void (*callback) (struct iridium* satcom, iridium_command_t command, iridium_status_t status);
    void (*message_callback) (struct iridium* satcom, char* data);
} iridium_t;

/**
 * @brief the iridium message for the modem.
 */
typedef struct iridium_message {
  char data[50];
  int size;
  int nonce;
  int command;
} iridium_message_t;

/**
 * @brief the iridium result from the modem.
 */
typedef struct iridium_result {
    char result[50];
    iridium_status_t status;
} iridium_result_t;

/**
 * @brief callbacks required for message/event data.
 */
typedef void (*callback_t) (iridium_t* satcom, iridium_command_t command, iridium_status_t status);
typedef void (*message_callback_t) (iridium_t* satcom, char* data);

/**
 * @brief Process data returned to device from UART bus.
 * @param satcom the iridium_t struct pointer.
 * @param command the AT command being processed.
 * @param data the data returned to be parsed into iridium_t struct.
 * @return a iridium_status_t with SAT_OK or SAT_ERROR.
 */
iridium_status_t iridium_satcom_process_result(iridium_t *satcom, char *command, char *data);

/**
 * @brief Update the iridium queue status. 
 * @param satcom the iridium_t struct pointer.
 * @param status the current IQS status.
 * @return a iridium_status_t with SAT_OK or SAT_ERROR.
 */
iridium_status_t iridium_update_iqs(iridium_t* satcom, iridium_queue_status_t status);

/**
 * @brief Update the processing message nonce. 
 * @param satcom the iridium_t struct pointer.
 * @param nonce the message nonce int.
 * @return a iridium_status_t with SAT_OK or SAT_ERROR.
 */
iridium_status_t iridium_update_p_nonce(iridium_t* satcom, int nonce);

/**
 * @brief Retrieves the current queue status while processing a message nonce. 
 * @param satcom the iridium_t struct pointer.
 * @return a iridium_queue_status_t with IQS_NONE, IQS_OPEN or IQS_WAITING.
 */
iridium_queue_status_t iridium_get_iqs(iridium_t* satcom); 

/**
 * @brief Send data payload across the UART bus.
 * @param satcom the iridium_t struct pointer.
 * @param data the data to be sent. 
 * @param nonce the nonce used to track responses. 
 * @return a iridium_status_t with SAT_OK or SAT_ERROR.
 */
iridium_status_t iridium_send_raw(iridium_t* satcom, char *data, int nonce);

/**
 * @brief Send AT command with data.  
 * @param satcom the iridium_t struct pointer.
 * @param command the iridium modem AT command.
 * @param rdata the raw data. 
 * @param wait_response wait for a responce from the modem.
 * @param wait_interval the amount of time in ms for wait interval check.
 * @return a iridium_result_t with SAT_OK or SAT_ERROR with metadata.
 */
iridium_result_t iridium_send(iridium_t* satcom, iridium_command_t command, char *rdata, bool wait_response, int wait_interval);

/**
 * @brief Configure iridium modem via UART connection. 
 * @param satcom the iridium_t struct pointer.
 * @return a iridium_status_t with SAT_OK or SAT_ERROR.
 */
iridium_status_t iridium_config(iridium_t *satcom);

/**
 * @brief Enabled or disable the ring notification on the modem.  
 * @param satcom the iridium_t struct pointer.
 * @param enabled the ring notification.
 * @return a iridium_result_t with SAT_OK or SAT_ERROR with metadata.
 */
iridium_result_t iridium_config_ring(iridium_t *satcom, bool enabled);

/**
 * @brief Transmit a message to the iridium network.
 * @param satcom the iridium_t struct pointer.
 * @param message to be sent.
 * @return a iridium_result_t with SAT_OK or SAT_ERROR with metadata.
 */
iridium_result_t iridium_tx_message(iridium_t *satcom, char *message);

/**
 * @brief Create a default iridium configuration.
 * @return a valid iridium_t struct configuration.
 */
iridium_t* iridium_default_configuration();

#ifdef __cplusplus
}
#endif

#endif /* IRIDIUM_H_INCLUDED */