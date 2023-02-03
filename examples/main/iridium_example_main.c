/*
 * SPDX-FileCopyrightText: 2010-2023 Espressif Systems (Shanghai) CO LTD AUTO-GEN
 *
 * SPDX-License-Identifier: MIT
 * 
 * 2022-2023 John O'Sullivan
 * 
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

#include "spi_flash_mmap.h" // or #include "esp_spi_flash.h"

#include "driver/uart.h"
#include "driver/gpio.h"
#include "driver/rmt_tx.h"

#include "led_strip_encoder.h"

#include "../../../iridium.h"

// 10MHz resolution, 1 tick = 0.1us (led needs a high resolution)
#define RMT_LED_STRIP_RESOLUTION_HZ 10000000
#define RMT_LED_STRIP_GPIO_NUM      48

static uint8_t led_pixels[3];

rmt_channel_handle_t led_channel = NULL;
rmt_encoder_handle_t led_encoder = NULL;
rmt_transmit_config_t tx_config = {
    .loop_count = 0,
};

static const char *TAG = "iridium_examples";

static void configure_led(void) {
    rmt_tx_channel_config_t tx_chan_config = {
        .clk_src = RMT_CLK_SRC_DEFAULT, 
        .gpio_num = RMT_LED_STRIP_GPIO_NUM,
        .mem_block_symbols = 64, 
        .resolution_hz = RMT_LED_STRIP_RESOLUTION_HZ,
        .trans_queue_depth = 4,
    };
    ESP_ERROR_CHECK(rmt_new_tx_channel(&tx_chan_config, &led_channel));

    led_strip_encoder_config_t encoder_config = {
        .resolution = RMT_LED_STRIP_RESOLUTION_HZ,
    };
    ESP_ERROR_CHECK(rmt_new_led_strip_encoder(&encoder_config, &led_encoder));
    ESP_ERROR_CHECK(rmt_enable(led_channel));
}

void update_led_pixels(uint8_t green, uint8_t red, uint8_t blue) {
    led_pixels[0] = green;    // green
    led_pixels[1] = red;      // red
    led_pixels[2] = blue;     // blue
}

void cb_satcom(iridium_t* satcom, iridium_command_t command, iridium_status_t status) { 
    if (status == SAT_OK) {
        switch (command) {
            case AT_CSQ:
                ESP_LOGI(TAG, "Signal Strength [0-5]: %d", satcom->signal_strength);
                switch (satcom->signal_strength) {
                case 1:
                    update_led_pixels(128, 255, 0);
                    break;
                case 2:
                    update_led_pixels(255, 255, 0);
                    break;
                case 3:
                    update_led_pixels(255, 128, 0);
                    break;
                case 4:
                    update_led_pixels(255, 0, 255);
                    break;
                case 5:
                    update_led_pixels(255, 0, 0);
                    break;
                default:
                    memset(led_pixels, 0, sizeof(led_pixels)); // reset the led_pixels  
                    break;
                }
                ESP_ERROR_CHECK(rmt_transmit(led_channel, led_encoder, led_pixels, sizeof(led_pixels), &tx_config));
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
}

void system_monitoring_task(void *pvParameter) {
    ESP_LOGI(TAG, "System [system_monitoring_task]");
    
    for(;;) {
        vTaskDelay(pdMS_TO_TICKS(10000));
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

    /* Setup Built-In Addressable RGB LED, driven by GPIO38. */ 
    configure_led();

    /* Loop */          
    for(;;) {
        iridium_result_t r1 = iridium_send(satcom, AT_CSQ, "", true, 500);
        if (r1.status == SAT_OK) {
            ESP_LOGI(TAG, "R[%d] = %s", r1.status, r1.result);
        }   
        vTaskDelay(pdMS_TO_TICKS(30000));
    }
}
