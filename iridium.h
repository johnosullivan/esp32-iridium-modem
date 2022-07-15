#ifndef IRIDIUM_H_INCLUDED
#define IRIDIUM_H_INCLUDED

/*
 * Author: John O'Sullivan
 * Purpose: ESP32 Satcom Library (Iridium Network)
 * Language:  C
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

/*
    define buffer values
*/
#define IRI_BUF_SIZE    (4096)
#define IRI_RD_BUF_SIZE (IRI_BUF_SIZE)
#define IRI_BUFF_DELAY  (100)

/* 
    iridium command enum for AT commands
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

typedef enum iridium_status {
    SAT_ERROR       = -1,
    SAT_OK          = 1 
} iridium_status_t;

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

/*
    0 - 4 = Transmit successful
    32    = No network service
    MO    = Mobile Originated 
    MT    = Mobile Terminated
*/
typedef struct iridium_settings {
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
    /* callbacks */ 
    void (*callback) (struct iridium_settings* satcom, iridium_command_t command, iridium_status_t status);
    void (*message_callback) (struct iridium_settings* satcom, char* data);
} iridium_settings_t;

typedef struct iridium_message {
  char data[50];
  int size;
  int nonce;
  int command;
} iridium_message_t;

typedef struct iridium_result {
    char result[50];
    iridium_status_t status;
} iridium_result_t;

/* 
    iridum system callbacks
*/
typedef void (*callback_t) (iridium_settings_t* satcom, iridium_command_t command, iridium_status_t status);
typedef void (*message_callback_t) (iridium_settings_t* satcom, char* data);

iridium_status_t iridium_satcom_process_result(iridium_settings_t *satcom, char *command, char *data);

iridium_status_t iridium_update_iqs(iridium_settings_t* satcom, iridium_queue_status_t status);

iridium_status_t iridium_update_p_nonce(iridium_settings_t* satcom, int nonce);

iridium_queue_status_t iridium_get_iqs(iridium_settings_t* satcom); 

iridium_status_t iridium_send_raw(iridium_settings_t* satcom, char *data, int nonce);

iridium_result_t iridium_send(iridium_settings_t* satcom, iridium_command_t command, char *rdata, bool wait_response, int wait_interval);

iridium_status_t iridium_config(iridium_settings_t *satcom);

iridium_result_t iridium_config_ring(iridium_settings_t *satcom, bool enabled);

iridium_result_t iridium_tx_message(iridium_settings_t *satcom, char *message);

#ifdef __cplusplus
}
#endif

#endif /* IRIDIUM_H_INCLUDED */