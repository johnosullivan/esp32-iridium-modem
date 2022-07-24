
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <assert.h>

#include "sdkconfig.h"

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

#include "utils.h"
#include "bt_manager.h"
#include "wifi_manager.h"

#include <pthread.h>

#include "iridium.h"

static const char *TAG_IRIDIUM = "esp32_iridium";

/*
    helper iridium functions 
*/
bool startsWith(const char *pre, const char *str)
{
    size_t lenpre = strlen(pre),
           lenstr = strlen(str);
    return lenstr < lenpre ? false : memcmp(pre, str, lenpre) == 0;
}

char** str_split(char* a_str, char a_delim)
{
    char** result    = 0;
    size_t count     = 0;
    char* tmp        = a_str;
    char* last_comma = 0;
    char delim[2];
    delim[0] = a_delim;
    delim[1] = 0;

    /* Count how many elements will be extracted. */
    while (*tmp)
    {
        if (a_delim == *tmp)
        {
            count++;
            last_comma = tmp;
        }
        tmp++;
    }

    /* Add space for trailing token. */
    count += last_comma < (a_str + strlen(a_str) - 1);

    /* Add space for terminating null string so caller
       knows where the list of returned strings ends. */
    count++;

    result = malloc(sizeof(char*) * count);

    if (result)
    {
        size_t idx  = 0;
        char* token = strtok(a_str, delim);

        while (token)
        {
            assert(idx < count);
            *(result + idx++) = strdup(token);
            token = strtok(0, delim);
        }
        assert(idx == count - 1);
        *(result + idx) = 0;
    }

    return result;
}

iridium_status_t iridium_satcom_process_result(iridium_t *satcom, char *command, char *data) {
    if (strcmp ("AT", command) == 0) { return SAT_OK; }
    if (strcmp ("AT&K0", command) == 0) { return SAT_OK; }
    if (strcmp ("AT&w0", command) == 0) { return SAT_OK; }
    if (strcmp ("AT&K0", command) == 0) { return SAT_OK; }
    if (startsWith("AT+SBDMTA", command)) { return SAT_OK; }

    if (strcmp ("AT+CGMI", command) == 0) {
        strcpy(satcom->manufacturer_identification, data);
        satcom->callback(satcom, AT_CGMI, SAT_OK);
        return SAT_OK;
    } 

    if (strcmp ("AT+CGMM", command) == 0) {
        strcpy(satcom->model_identification, data);
        satcom->callback(satcom, AT_CGMM, SAT_OK);
        return SAT_OK;
    } 

    if (strcmp ("AT+CSQ", command) == 0) {
        char** tokens = str_split(data, ':');
     
        satcom->signal_strength = atoi(*(tokens + 1));
        satcom->callback(satcom, AT_CSQ, SAT_OK);

        free(tokens);
        return SAT_OK;
    }

    if (strcmp ("AT+SBDSX", command) == 0) {
        char** tokens = str_split(data, ':');
        char** results = str_split(*(tokens + 1), ',');
     
        satcom->status_outbound = atoi(*(results));
        satcom->sequence_outbound = atoi(*(results + 1));
        satcom->status_inbound = atoi(*(results + 2));
        satcom->sequence_inbound = atoi(*(results + 3));
        satcom->bytes_received = atoi(*(results + 4));
        satcom->messages_waiting = atoi(*(results + 5));
        satcom->callback(satcom, AT_SBDSX, SAT_OK);

        free(tokens);
        free(results);
        return SAT_OK;
    }

    if (strcmp ("AT+SBDIX", command) == 0 || strcmp ("AT+SBDIXA", command) == 0) {
        char** tokens = str_split(data, ':');
        char** results = str_split(*(tokens + 1), ',');
     
        satcom->status_outbound = atoi(*(results));
        satcom->sequence_outbound = atoi(*(results + 1));
        satcom->status_inbound = atoi(*(results + 2));
        satcom->sequence_inbound = atoi(*(results + 3));
        satcom->bytes_received = atoi(*(results + 4));
        satcom->messages_waiting = atoi(*(results + 5));
        satcom->callback(satcom, AT_SBDSX, SAT_OK);

        free(tokens);
        free(results);
        return SAT_OK;
    }

    if (strcmp ("AT+SBDRT", command) == 0) {
        char** tokens = str_split(data, '+');

        iridium_message_t msg;
        strcpy(msg.data, *(tokens));
        msg.size = strlen(*(tokens));
        xQueueSend(satcom->message_queue, (void *)&msg, 10);

        free(tokens);
        return SAT_OK;
    }

    if (strcmp ("AT+CRIS", command) == 0) {
        char** tokens = str_split(data, ':');
        char** results = str_split(*(tokens + 1), ',');

        free(tokens);
        free(results);
        return SAT_OK;
    }

    return SAT_ERROR;
}


iridium_status_t iridium_update_iqs(iridium_t* satcom, iridium_queue_status_t status) {
    pthread_mutex_lock(&satcom->p_status_mutex);
    satcom->status = status;
    pthread_mutex_unlock(&satcom->p_status_mutex);
    return SAT_OK;
}

iridium_status_t iridium_update_p_nonce(iridium_t* satcom, int nonce) {
    pthread_mutex_lock(&satcom->p_nonce_mutex);
    satcom->p_nonce = nonce;
    pthread_mutex_unlock(&satcom->p_nonce_mutex);
    return SAT_OK;
}

iridium_queue_status_t iridium_get_iqs(iridium_t* satcom) {
    iridium_queue_status_t t_status = IQS_NONE;
    pthread_mutex_lock(&satcom->p_status_mutex);
    t_status = satcom->status;
    pthread_mutex_unlock(&satcom->p_status_mutex);
    return t_status;
}

iridium_status_t iridium_send_raw(iridium_t* satcom, char *data, int nonce) {
    if (satcom->status == IQS_WAITING) {
        // send iridium_message_t to buffer queue
        ESP_LOGI(TAG_IRIDIUM, "IN_BUFFER_QUEUE[%d]", nonce);
        iridium_message_t msg;
        strcpy(msg.data, data);
        msg.size = strlen(data);
        msg.nonce = nonce;
        xQueueSend(satcom->buffer_queue, (void *)&msg, 10);
        return SAT_OK;  
    }
    /* transmit data via UART */
    uart_write_bytes(satcom->uart_number, data, strlen(data));
    /* update IQS */
    iridium_update_iqs(satcom, IQS_WAITING);
    iridium_update_p_nonce(satcom, nonce);
    return SAT_OK;
}

iridium_result_t iridium_config_ring(iridium_t *satcom, bool enabled) {
    iridium_result_t result;

    /* enable/disable satcom ring */
    if (enabled) {
        result = iridium_send(satcom, AT_SBDMTA, "1", true, 500);
        if (result.status != SAT_OK) {
            return result;
        }
    } else {
        result = iridium_send(satcom, AT_SBDMTA, "0", true, 500);
        if (result.status != SAT_OK) {
            return result;
        }
    }
    vTaskDelay(pdMS_TO_TICKS(IRI_BUFF_DELAY));

    /* save config */
    result = iridium_send(satcom, AT_W0, "", true, 500);
    if (result.status != SAT_OK) {
        return result;
    }
    vTaskDelay(pdMS_TO_TICKS(IRI_BUFF_DELAY));

    /* turn off flow control */
    result = iridium_send(satcom, AT_K0, "", true, 500);
    if (result.status != SAT_OK) {
        return result;
    }
    vTaskDelay(pdMS_TO_TICKS(IRI_BUFF_DELAY));

    /* check ring status */
    result = iridium_send(satcom, AT_SBDMTAQ, "", true, 500);
    return result;
}

iridium_result_t iridium_tx_message(iridium_t *satcom, char *message) {
    iridium_result_t result;
    iridium_result_t r1 = iridium_send(satcom, AT_SBDWT, message, true, 500);

    /* failed to set outbound message buffer */
    if (r1.status != SAT_OK) {
        result.status = SAT_ERROR;
        return result;
    }

    int delays[6] = {2000,4000,20000,30000,300000,300000};

    /* short burst - send message - with adaptive retry */
    for (int i = 0; i < 5; i++){
        iridium_result_t r2 = iridium_send(satcom, AT_SBDIX, NULL, true, 500);
        if (r2.status != SAT_OK) {
            result.status = SAT_ERROR;
            break;
        }

        /* */
        if (satcom->status_outbound == MO_TRANSFERRED_SUCCESSFULLY ||
            satcom->status_outbound == MO_TRANSFERRED_SUCCESSFULLY_TOO_BIG ||
            satcom->status_outbound == MO_TRANSFERRED_SUCCESSFULLY_LOC_NOT_ACCEPTED) {
            break;
        } else {
            result.status = SAT_ERROR;
        }

        vTaskDelay(pdMS_TO_TICKS(delays[i]));
    } 

    return result;
}

/*
AT+SBDIX = +SBDIX:<MO status>,<MOMSN>,<MT status>,<MTMSN>,<MT length>,<MT queued>
*/

iridium_result_t iridium_send(iridium_t* satcom, iridium_command_t command, char *rdata, bool wait_response, int wait_interval) {
    iridium_result_t result;

    /* increment c_nonce */
    satcom->c_nonce++;
    int t_nonce = satcom->c_nonce;

    switch (command) {
        case AT:
            if (iridium_send_raw(satcom, "AT\r", t_nonce) != SAT_OK) {
                result.status = SAT_ERROR;
                return result;
            }
            break;
        case AT_CSQ:
            if (iridium_send_raw(satcom, "AT+CSQ\r", t_nonce) != SAT_OK) {
                result.status = SAT_ERROR;
                return result;
            }
            break;
        case AT_CGMI:
            if (iridium_send_raw(satcom, "AT+CGMI\r", t_nonce) != SAT_OK) {
                result.status = SAT_ERROR;
                return result;
            }
            break;
        case AT_CGMM:
            if (iridium_send_raw(satcom, "AT+CGMM\r", t_nonce) != SAT_OK) {
                result.status = SAT_ERROR;
                return result;
            }
            break;
        case AT_SBDIX:
            if (iridium_send_raw(satcom, "AT+SBDIX\r", t_nonce) != SAT_OK) {
                result.status = SAT_ERROR;
                return result;
            }
            break;
        case AT_SBDSX:
            if (iridium_send_raw(satcom, "AT+SBDSX\r", t_nonce) != SAT_OK) {
                result.status = SAT_ERROR;
                return result;
            }
            break;
        case AT_MSSTM:
            if (iridium_send_raw(satcom, "AT-MSSTM\r", t_nonce) != SAT_OK) {
                result.status = SAT_ERROR;
                return result;
            }
            break;
        case AT_SBDRT:
            if (iridium_send_raw(satcom, "AT+SBDRT\r", t_nonce) != SAT_OK) {
                result.status = SAT_ERROR;
                return result;
            }
            break;
        case AT_CRIS:
            if (iridium_send_raw(satcom, "AT+CRIS\r", t_nonce) != SAT_OK) {
                result.status = SAT_ERROR;
                return result;
            }
            break;
         case AT_SBDIXA:
            if (iridium_send_raw(satcom, "AT+SBDIXA\r", t_nonce) != SAT_OK) {
                result.status = SAT_ERROR;
                return result;
            }
            break;
        case AT_SBDMTAQ:
            if (iridium_send_raw(satcom, "AT+SBDMTA?\r", t_nonce) != SAT_OK) {
                result.status = SAT_ERROR;
                return result;
            }
            break;
        case AT_SBDWT: {
            char *message = (char*)malloc(50 * sizeof(char));
            sprintf(message, "AT+SBDWT=%s\r", rdata);
            result.status = iridium_send_raw(satcom, message, t_nonce);
            free(message);
            return result;
        }
        case AT_SBDMTA: {
            char *message = (char*)malloc(20 * sizeof(char));
            sprintf(message, "AT+SBDMTA=%s\r", rdata);
            result.status = iridium_send_raw(satcom, message, t_nonce);
            free(message);
            return result;
        }
        case AT_W0:
            if (iridium_send_raw(satcom, "AT&w0\r", t_nonce) != SAT_OK) {
                result.status = SAT_ERROR;
                return result;
            }
            break;
        case AT_K0:
            if (iridium_send_raw(satcom, "AT&K0\r", t_nonce) != SAT_OK) {
                result.status = SAT_ERROR;
                return result;
            }
            break;
        default:
            break;
    }

    if (wait_response) {
        iridium_queue_status_t t_status = IQS_NONE;
        while (t_status != IQS_OPEN && satcom->p_nonce == t_nonce) {
            vTaskDelay(pdMS_TO_TICKS(wait_interval));
            t_status = iridium_get_iqs(satcom);
        }

        strcpy(result.result, satcom->buffer_data);
        ESP_LOGD(TAG_IRIDIUM, "WAIT_DONE_NONCE = [%d]", t_nonce);
    }

    result.status = SAT_OK;
    return result;
}

void ring_satcom_task(void *pvParameters) { 
    iridium_t* satcom = (iridium_t *)pvParameters;
    vTaskDelay(pdMS_TO_TICKS(1000));

    iridium_result_t rcris = iridium_send(satcom, AT_CRIS, NULL, true, 500);
    if (rcris.status == SAT_OK) { }

    for(;;) {
        iridium_result_t r1 = iridium_send(satcom, AT_SBDIXA, "", true, 500);
        if (r1.status == SAT_OK) {
            ESP_LOGD(TAG_IRIDIUM, "RST_R1[%d] = %s", r1.status, r1.result);
        }
        if (satcom->status_outbound == MO_TRANSFERRED_SUCCESSFULLY ||
            satcom->status_outbound == MO_TRANSFERRED_SUCCESSFULLY_TOO_BIG ||
            satcom->status_outbound == MO_TRANSFERRED_SUCCESSFULLY_LOC_NOT_ACCEPTED) {
            if (satcom->messages_waiting == 0) {
                break;
            }
            vTaskDelay(pdMS_TO_TICKS(5000));
            iridium_result_t r2 = iridium_send(satcom, AT_SBDRT, NULL, true, 500);
            if (r2.status == SAT_OK) {
                ESP_LOGD(TAG_IRIDIUM, "RST_R2[%d] = %s", r2.status, r2.result);
            }
        }
        vTaskDelay(pdMS_TO_TICKS(10000));
    }

    iridium_result_t r2 = iridium_send(satcom, AT_SBDRT, NULL, true, 500);
    if (r2.status == SAT_OK) {
        ESP_LOGD(TAG_IRIDIUM, "RST_R3[%d] = %s", r2.status, r2.result);
    }

    satcom->ring_task_running = 0;
    vTaskDelete(NULL);
}

void uart_satcom_task(void *pvParameters) { 
    iridium_t* satcom = (iridium_t *)pvParameters;
    uint8_t* dtmp = (uint8_t*) malloc(IRI_RD_BUF_SIZE);
    uart_event_t event;

    struct stack_t *s = newStack();
    for(;;) {
        if(xQueueReceive(satcom->uart_queue, (void * )&event, (portTickType)portMAX_DELAY)) {
            bzero(dtmp, IRI_RD_BUF_SIZE);
            switch(event.type) {
                case UART_DATA:
                    uart_read_bytes(satcom->uart_number, dtmp, event.size, portMAX_DELAY);

                    ESP_LOGI(TAG_IRIDIUM, "R:%s-", dtmp);

                    char* pch = NULL;
                    pch = strtok((char*)dtmp, "\r\n");
                    while (pch != NULL) {
                        /* AT Command Check */
                        //printf("- D:[%s]\n", pch);

                        if (startsWith("AT", pch)) {
                            push(s, pch);
                        } else {
                            if (strcmp("SBDRING", pch) == 0) {
                                if (satcom->ring_task_running == 0) {
                                    satcom->ring_task_running = 1;
                                    xTaskCreate(&ring_satcom_task, 
                                                "ring_satcom_task", 
                                                4096,
                                                satcom, 
                                                12, NULL);
                                }
                                break;
                            }
                            if (strcmp ("ERROR", pch) == 0) {
                                continue;
                            }
                            if (strcmp ("OK", pch) == 0) {
                                // Process 
                                char data[100];
                                char command[20];

                                data[0] = '\0';

                                while (top(s) != NULL) {
                                    // Grab top value / pop
                                    char* tmp = top(s);

                                    ESP_LOGD(TAG_IRIDIUM, "TMP:[%s]", tmp); 
                                    if (startsWith("AT", tmp)) {
                                         strcpy(command, tmp);
                                    } else {
                                        strcat(data, tmp);
                                    }  
                                    pop(s);
                                }

                                ESP_LOGD(TAG_IRIDIUM, "P: %s = %s", command, data);
                                strcpy(satcom->buffer_data, data);

                                if (iridium_satcom_process_result(satcom, command, data) == SAT_OK) {
                                    ESP_LOGD(TAG_IRIDIUM, "OK_R[%d]: %s = %s ", satcom->p_nonce, command, pch); 
                                } else {
                                    ESP_LOGD(TAG_IRIDIUM, "ERROR_R[%d]: %s = %s ", satcom->p_nonce, command, pch); 
                                }
                                /* Clean up after AT processing */
                                clear_stack(s);
                                iridium_update_iqs(satcom, IQS_OPEN);
                            } else {
                                push(s, pch); 
                            }
                        }
                        pch = strtok(NULL, "\r\n");
                    }
                    break;
                case UART_FIFO_OVF:
                    uart_flush_input(satcom->uart_number);
                    xQueueReset(satcom->uart_queue);
                    break;
                case UART_BUFFER_FULL:
                    uart_flush_input(satcom->uart_number);
                    xQueueReset(satcom->uart_queue);
                    break;
                case UART_BREAK:
                    break;
                case UART_PARITY_ERR:
                    break;
                case UART_FRAME_ERR:
                    break;
                case UART_PATTERN_DET:
                    break;
                default:
                    break;
            }
        }
    }
    free(dtmp);
    dtmp = NULL;
    destroy_stack(&s);
    vTaskDelete(NULL);
}

void buffer_satcom_task(void *pvParameters) { 
    iridium_t* satcom = (iridium_t *)pvParameters;
    iridium_queue_status_t t_status = IQS_NONE;

    int delay_ms = satcom->buffer_delay_ms;
    for(;;) {
        /* waiting for buffer message event */
        t_status = iridium_get_iqs(satcom);
        if (t_status == IQS_OPEN) {
            iridium_message_t rcv_msg;
            if (xQueueReceive(satcom->buffer_queue, (void *)&rcv_msg, 0) == pdTRUE) {
                iridium_send_raw(satcom, rcv_msg.data, rcv_msg.nonce);
            }
        }
        vTaskDelay(pdMS_TO_TICKS(delay_ms));
    }
    vTaskDelete(NULL);
} 

void message_satcom_task(void *pvParameters) { 
    iridium_t* satcom = (iridium_t *)pvParameters;
    int delay_ms = satcom->buffer_delay_ms;

    for(;;) {
        iridium_message_t rcv_msg;
        if (xQueueReceive(satcom->message_queue, (void *)&rcv_msg, 0) == pdTRUE) {
           satcom->message_callback(satcom, rcv_msg.data);
        }
        vTaskDelay(pdMS_TO_TICKS(delay_ms));
    }
    vTaskDelete(NULL);
}  

iridium_t* iridium_default_configuration() {
    iridium_t *satcom = malloc(sizeof(iridium_t));
    satcom->buffer_size = 10; // item size
    satcom->buffer_delay_ms = 1000; // ms
    satcom->task_message_stack_depth = 4096;
    satcom->task_buffer_stack_depth = 2024;
    satcom->task_uart_stack_depth = 4096;
    return satcom;
}


iridium_status_t iridium_config(iridium_t *satcom) {
    /*
        Baud Rate = 19200 Data Bits = 8 Parity = N Stop Bits = 1
    */
    const int DEFAULT_BAUD_RATE = 19200;

    uart_config_t uart_config = {
        .baud_rate = DEFAULT_BAUD_RATE,
        .data_bits = UART_DATA_8_BITS,
        .parity    = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        //.source_clk = UART_SCLK_REF_TICK,
    };

    /* init pthread_mutex handles */
    pthread_mutex_init(&(satcom->p_status_mutex), NULL);
    pthread_mutex_init(&(satcom->p_nonce_mutex), NULL);

    if (satcom->buffer_delay_ms == 0) {
        satcom->buffer_delay_ms = 1000; // ms
    }

    satcom->c_nonce = 0;
    satcom->p_nonce = 0;
    satcom->ring_task_running = 0;
    satcom->status = IQS_OPEN;
    satcom->buffer_queue = xQueueCreate(satcom->buffer_size, sizeof(iridium_message_t));
    satcom->message_queue = xQueueCreate(20, sizeof(iridium_message_t));

    /* install uart drivers */
    if (uart_driver_install(satcom->uart_number, 
                            IRI_BUF_SIZE * 2, 
                            IRI_BUF_SIZE * 2, 20, 
                            &satcom->uart_queue, 0) != ESP_OK) {
        return SAT_ERROR;
    }

    if (uart_param_config(satcom->uart_number, &uart_config) != ESP_OK) {
        return SAT_ERROR;
    }

    /* set uart pin outs */
    if (uart_set_pin(satcom->uart_number, 
                    satcom->uart_txn_number,
                    satcom->uart_rxd_number, 
                    satcom->uart_rts_number, 
                    satcom->uart_cts_number) != ESP_OK) {
        return SAT_ERROR;
    }

    /* start message processing tasks */
    xTaskCreate(&message_satcom_task, 
                "message_satcom_task", 
                satcom->task_message_stack_depth, 
                satcom, 
                12, NULL);

    /* start uart processing tasks */
    xTaskCreate(&uart_satcom_task, 
                "uart_satcom_task", 
                satcom->task_uart_stack_depth,
                satcom, 
                12, NULL);

    /* start buffer processing tasks */
    xTaskCreate(&buffer_satcom_task, 
                "buffer_satcom_task", 
                satcom->task_buffer_stack_depth, 
                satcom, 
                12, NULL);

    /* 1000ms delay */
    vTaskDelay(pdMS_TO_TICKS(1000));

    /* AT check */
    iridium_result_t r;
    r = iridium_send(satcom, AT, NULL, true, 500);
    if (r.status != SAT_OK) {
        return r.status;
    }

    /* AT system details */
    r = iridium_send(satcom, AT_CGMI, NULL, true, 500);
    if (r.status != SAT_OK) {
        return r.status;
    }

    r = iridium_send(satcom, AT_CGMM, NULL, true, 500);
    if (r.status != SAT_OK) {
        return r.status;
    }

    return r.status;
}