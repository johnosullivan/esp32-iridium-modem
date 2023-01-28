/*
 * SPDX-FileCopyrightText: 2010-2023 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */

#include <stdio.h>
#include <inttypes.h>
#include <pthread.h>

#include "sdkconfig.h"

#include "nvs.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

#include "esp_chip_info.h"
#include "esp_flash.h"
#include "esp_log.h"
#include "esp_event.h"
#include "nvs_flash.h"
#include "esp_system.h"

#include "spi_flash_mmap.h" // #include "esp_spi_flash.h"

#include "driver/uart.h"
#include "driver/gpio.h"

#include "../../../iridium.h"

static const char *TAG = "iridium_examples";

void system_monitoring_task(void *pvParameter) {
    ESP_LOGI(TAG, "System [system_monitoring_task]");

    for(;;) {
        vTaskDelay(pdMS_TO_TICKS(10000));
    }
}

void cb_satcom(iridium_t* satcom, iridium_command_t command, iridium_status_t status) { 
    if (status == SAT_OK) {
        switch (command) {
            case AT_CSQ:
                ESP_LOGI(TAG, "Signal Strength [0-5]: %d", satcom->signal_strength);
                break;
            case AT_CGMM:
                ESP_LOGI(TAG, "Model Identification: %s", satcom->model_identification);
                break;
            case AT_CGMI:
                ESP_LOGI(TAG, "Manufacturer Identification: %s", satcom->manufacturer_identification);
                break;
            default:
                break;
        }
    }
}

void cb_message(iridium_t* satcom, char* data) { 
    ESP_LOGI(TAG, "CALLBACK[INCOMING] %s", data);

    if (strcmp ("PING", data) == 0) {
        iridium_result_t r1 = iridium_tx_message(satcom, "PONG");
        if (r1.status != SAT_OK) {
            ESP_LOGI(TAG, "R[%d] TX Failed!", r1.status);
        } else {
            ESP_LOGI(TAG, "R[%d] = %s", r1.status, r1.result);
        }
    }
}

void app_main(void)
{
    /* Print chip information */
    esp_chip_info_t chip_info;
    uint32_t flash_size;
    esp_chip_info(&chip_info);
    printf("[%s] - %d CPU core(s), WiFi%s%s%s, ",
           CONFIG_IDF_TARGET,
           chip_info.cores,
           (chip_info.features & CHIP_FEATURE_BT) ? "/BT" : "",
           (chip_info.features & CHIP_FEATURE_BLE) ? "/BLE" : "",
           (chip_info.features & CHIP_FEATURE_IEEE802154) ? ", 802.15.4 (Zigbee/Thread)" : "");

    /* Initialize NVS */
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    unsigned major_rev = chip_info.revision / 100;
    unsigned minor_rev = chip_info.revision % 100;
    printf("Silicon Revision v%d.%d, ", major_rev, minor_rev);
    if(esp_flash_get_size(NULL, &flash_size) != ESP_OK) {
        return;
    }

    printf("%" PRIu32 "MB %s \n", flash_size / (uint32_t)(1024 * 1024),
           (chip_info.features & CHIP_FEATURE_EMB_FLASH) ? "Embedded" : "External");

    /* Create FreeRTOS Monitoring Task */
    xTaskCreatePinnedToCore(&system_monitoring_task, "system_monitoring_task", 2048, NULL, 1, NULL, 1);

    /* Configuration Iridium SatCom */
    iridium_t *satcom = iridium_default_configuration();
    satcom->callback = &cb_satcom;
    satcom->message_callback = &cb_message;
    /* UART Port Configuration */
    satcom->uart_number = UART_NUM_1;
    satcom->uart_txn_number = GPIO_NUM_17;
    satcom->uart_rxd_number = GPIO_NUM_18;
    satcom->uart_rts_number = UART_PIN_NO_CHANGE;
    satcom->uart_cts_number = UART_PIN_NO_CHANGE;
    satcom->gpio_sleep_pin_number = GPIO_NUM_46;
    satcom->gpio_net_pin_number = GPIO_NUM_21;
    
    /* Initialized */
    if (iridium_config(satcom) == SAT_OK) {
        ESP_LOGI(TAG, "Iridium Modem [Initialized]");
    }

    /* Allow Ring Triggers */
    iridium_result_t ring = iridium_config_ring(satcom, true);
    if (ring.status == SAT_OK) {
        ESP_LOGI(TAG, "Iridium Modem [Ring Enabled]");
    }

    /* Loop */          
    for(;;) {
        iridium_result_t r1 = iridium_send(satcom, AT_CSQ, "", true, 500);
        if (r1.status == SAT_OK) {
            ESP_LOGI(TAG, "R[%d] = %s", r1.status, r1.result);
        }   
        vTaskDelay(pdMS_TO_TICKS(60000));
    }
}
