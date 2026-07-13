/*
 * SPDX-FileCopyrightText: 2022-2026 John O'Sullivan
 *
 * SPDX-License-Identifier: MIT
 *
 * Example firmware demonstrating the ESP32 Iridium SBD driver:
 *   - modem init + identity (CGMI/CGMM)
 *   - ring indicator enable
 *   - periodic CSQ + NET availability
 *   - button-triggered MO send (iridium_tx_message)
 *   - periodic mailbox poll (iridium_rx_message)
 *   - inbound delivery via message_callback
 *   - optional sleep/wake long-press demo
 *   - optional RGB status LED mapped to signal strength
 */

#include <stdio.h>
#include <string.h>
#include <inttypes.h>

#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_system.h"
#include "nvs_flash.h"
#include "driver/gpio.h"

#if CONFIG_EXAMPLE_STATUS_LED
#include "driver/rmt_tx.h"
#include "led_strip_encoder.h"
#endif

#include "iridium.h"
#include "iridium_parser.h"

static const char *TAG = "iridium_example";

#define BUTTON_GPIO                 CONFIG_EXAMPLE_BUTTON_GPIO_NUM
#define CSQ_INTERVAL_MS             CONFIG_EXAMPLE_CSQ_INTERVAL_MS
#define MAILBOX_POLL_INTERVAL_MS    CONFIG_EXAMPLE_MAILBOX_POLL_INTERVAL_MS
#define BUTTON_DEBOUNCE_MS          50
#define BUTTON_LONG_PRESS_MS        2000
/* iridium_send() keeps large AT buffers on-stack; keep this roomy */
#define DEMO_TASK_STACK             8192
#define DEMO_TASK_PRIORITY          5
#define MO_QUEUE_LEN                4
#define MO_PAYLOAD_MAX              64

#if CONFIG_EXAMPLE_STATUS_LED
#define RMT_LED_STRIP_RESOLUTION_HZ CONFIG_RMT_LED_STRIP_RESOLUTION_HZ
#define RMT_LED_STRIP_GPIO_NUM      CONFIG_RMT_LED_STRIP_GPIO_NUM
#define RMT_LED_STRIP_COUNT         CONFIG_RMT_LED_STRIP_COUNT

static uint8_t led_pixels[RMT_LED_STRIP_COUNT * 3];
static rmt_channel_handle_t led_channel = NULL;
static rmt_encoder_handle_t led_encoder = NULL;
static const rmt_transmit_config_t led_tx_config = {
    .loop_count = 0,
};
#endif

static iridium_t *satcom;
static uint32_t tx_count;
static QueueHandle_t mo_queue;

typedef struct {
    char data[MO_PAYLOAD_MAX];
} mo_job_t;

static int uart_pin_or_nc(int pin)
{
    return (pin < 0) ? UART_PIN_NO_CHANGE : pin;
}

static void log_pin_config(void)
{
    ESP_LOGI(TAG, "UART TX=%d RX=%d RTS=%d CTS=%d",
             CONFIG_UART_TX_GPIO_NUM, CONFIG_UART_RX_GPIO_NUM,
             CONFIG_UART_RTS_GPIO_NUM, CONFIG_UART_CTS_GPIO_NUM);
    ESP_LOGI(TAG, "GPIO SLP=%d NET=%d RI=%d",
             CONFIG_UART_SLEEP_GPIO_NUM, CONFIG_UART_NET_GPIO_NUM,
             CONFIG_UART_RI_GPIO_NUM);
}

#if CONFIG_EXAMPLE_STATUS_LED
static void configure_led(void)
{
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

static void update_led_pixels(uint8_t green, uint8_t red, uint8_t blue)
{
    led_pixels[0] = green;
    led_pixels[1] = red;
    led_pixels[2] = blue;
}

static void show_signal_led(int signal_strength)
{
    switch (signal_strength) {
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
            memset(led_pixels, 0, sizeof(led_pixels));
            break;
    }
    ESP_ERROR_CHECK(rmt_transmit(led_channel, led_encoder, led_pixels,
                                 sizeof(led_pixels), &led_tx_config));
}
#endif /* CONFIG_EXAMPLE_STATUS_LED */

static const char *mo_status_str(int mo_status)
{
    if (iridium_parser_mo_transfer_ok(mo_status)) {
        return "MO ok";
    }
    switch (mo_status) {
        case MO_NO_NETWORK_SERVICE:
            return "no network service";
        case MO_ISU_IS_BUSY:
            return "ISU busy";
        case MO_TRY_LATER_3_MIN:
            return "try later (3 min)";
        case MO_CONNECTION_LOST:
            return "connection lost";
        default:
            return "MO failed";
    }
}

static void log_session_status(const iridium_t *modem)
{
    ESP_LOGI(TAG,
             "Session MO=%d (%s) seq=%d | MT=%d seq=%d bytes=%d waiting=%d",
             modem->status_outbound, mo_status_str(modem->status_outbound),
             modem->sequence_outbound, modem->status_inbound,
             modem->sequence_inbound, modem->bytes_received,
             modem->messages_waiting);
}

static void cb_satcom(iridium_t *modem, iridium_command_t command, iridium_status_t status)
{
    if (status != SAT_OK) {
        ESP_LOGW(TAG, "Command %d failed", (int)command);
        return;
    }

    switch (command) {
        case AT_CSQ:
            ESP_LOGI(TAG, "Signal strength [0-5]: %d", modem->signal_strength);
#if CONFIG_EXAMPLE_STATUS_LED
            show_signal_led(modem->signal_strength);
#endif
            break;
        case AT_CGMI:
            ESP_LOGI(TAG, "Manufacturer: %s", modem->manufacturer_identification);
            break;
        case AT_CGMM:
            ESP_LOGI(TAG, "Model: %s", modem->model_identification);
            break;
        case AT_SBDIX:
        case AT_SBDIXA:
        case AT_SBDSX:
            log_session_status(modem);
            break;
        default:
            break;
    }
}

static void cb_message(iridium_t *modem, const char *data, size_t size)
{
    (void)modem;
    ESP_LOGI(TAG, "========== INBOUND MESSAGE ==========");
    ESP_LOGI(TAG, "Received %u byte(s):", (unsigned)size);
    ESP_LOGI(TAG, "%.*s", (int)size, data);
    ESP_LOGI(TAG, "=====================================");
    printf("\n*** INBOUND MT (%u bytes): %.*s ***\n\n",
           (unsigned)size, (int)size, data);
}

static void poll_signal_and_net(iridium_t *modem)
{
    if (iridium_is_busy(modem)) {
        ESP_LOGI(TAG, "Skipping CSQ/NET poll — modem busy");
        return;
    }

    iridium_result_t csq = iridium_send(modem, AT_CSQ, "", true, 500);
    if (csq.status == SAT_BUSY) {
        ESP_LOGI(TAG, "CSQ deferred — modem busy");
        return;
    }
    if (csq.status != SAT_OK) {
        ESP_LOGW(TAG, "CSQ failed");
    }

    int available = iridium_is_available(modem);
    if (available < 0) {
        ESP_LOGI(TAG, "NET pin not configured");
    } else {
        ESP_LOGI(TAG, "Network available: %s", available ? "yes" : "no");
    }

    int ringing = iridium_is_ringing(modem);
    if (ringing < 0) {
        ESP_LOGI(TAG, "RI pin not configured");
    } else {
        ESP_LOGI(TAG, "Ring indicator (RI): %s", ringing ? "asserted" : "idle");
    }
}

static void poll_mailbox(iridium_t *modem)
{
    if (iridium_is_busy(modem)) {
        ESP_LOGI(TAG, "Skipping mailbox poll — modem busy");
        return;
    }

    char mt_buf[IRI_SBD_MAX_BYTES + 1];
    size_t received = 0;

    iridium_result_t rx = iridium_rx_message(modem, mt_buf, sizeof(mt_buf), &received);
    if (rx.status == SAT_BUSY) {
        ESP_LOGI(TAG, "Mailbox poll deferred — modem busy");
        return;
    }
    if (rx.status != SAT_OK) {
        ESP_LOGW(TAG, "Mailbox poll failed");
        return;
    }

    if (received == 0) {
        ESP_LOGI(TAG, "Mailbox empty (waiting=%d)", modem->messages_waiting);
        return;
    }

    ESP_LOGI(TAG, "Mailbox read (%u bytes):", (unsigned)received);
    ESP_LOGI(TAG, "%.*s", (int)received, mt_buf);
    printf("\n*** MAILBOX MT (%u bytes): %.*s ***\n\n",
           (unsigned)received, (int)received, mt_buf);
}

static bool enqueue_mo(const char *payload)
{
    if (mo_queue == NULL || payload == NULL) {
        return false;
    }

    mo_job_t job = {0};
    strncpy(job.data, payload, sizeof(job.data) - 1);

    if (xQueueSend(mo_queue, &job, 0) != pdTRUE) {
        ESP_LOGW(TAG, "MO queue full — dropping: %s", payload);
        return false;
    }

    ESP_LOGI(TAG, "MO queued for later TX (%u waiting): %s",
             (unsigned)uxQueueMessagesWaiting(mo_queue), payload);
    return true;
}

static void drain_mo_queue(iridium_t *modem)
{
    if (mo_queue == NULL || iridium_is_busy(modem)) {
        return;
    }

    mo_job_t job;
    while (xQueueReceive(mo_queue, &job, 0) == pdTRUE) {
        ESP_LOGI(TAG, "Draining queued MO: %s", job.data);
        iridium_result_t tx = iridium_tx_message(modem, job.data);
        if (tx.status == SAT_BUSY) {
            xQueueSendToFront(mo_queue, &job, 0);
            ESP_LOGI(TAG, "Modem became busy — leaving MO queued");
            return;
        }
        if (tx.status == SAT_OK) {
            ESP_LOGI(TAG, "Queued MO transfer succeeded");
        } else {
            ESP_LOGW(TAG, "Queued MO failed (MO status %d: %s)",
                     modem->status_outbound, mo_status_str(modem->status_outbound));
        }

        if (iridium_is_busy(modem)) {
            return;
        }
    }
}

static void send_demo_message(iridium_t *modem)
{
    char payload[MO_PAYLOAD_MAX];
    tx_count++;
    snprintf(payload, sizeof(payload), "esp32-iridium demo #%lu",
             (unsigned long)tx_count);

    if (iridium_is_busy(modem)) {
        ESP_LOGI(TAG, "Modem busy — queueing MO: %s", payload);
        enqueue_mo(payload);
        return;
    }

    ESP_LOGI(TAG, "Sending MO: %s", payload);
    iridium_result_t tx = iridium_tx_message(modem, payload);
    if (tx.status == SAT_OK) {
        ESP_LOGI(TAG, "MO transfer succeeded");
    } else if (tx.status == SAT_BUSY) {
        ESP_LOGI(TAG, "MO deferred — queueing: %s", payload);
        enqueue_mo(payload);
    } else {
        ESP_LOGW(TAG, "MO transfer failed (MO status %d: %s)",
                 modem->status_outbound, mo_status_str(modem->status_outbound));
    }
}

#if CONFIG_EXAMPLE_ENABLE_SLEEP_DEMO
static void toggle_sleep(iridium_t *modem, bool *asleep)
{
    if (*asleep) {
        ESP_LOGI(TAG, "Waking modem");
        if (iridium_modem_wake(modem) == SAT_OK) {
            *asleep = false;
            ESP_LOGI(TAG, "Modem awake");
        } else {
            ESP_LOGW(TAG, "Wake failed (is SLP GPIO configured?)");
        }
        return;
    }

    ESP_LOGI(TAG, "Putting modem to sleep");
    if (iridium_modem_sleep(modem) == SAT_OK) {
        *asleep = true;
        ESP_LOGI(TAG, "Modem asleep — long-press again to wake");
    } else {
        ESP_LOGW(TAG, "Sleep failed (is SLP GPIO configured?)");
    }
}
#endif

static void demo_task(void *pvParameters)
{
    iridium_t *modem = (iridium_t *)pvParameters;
    TickType_t last_csq = xTaskGetTickCount();
    TickType_t last_mailbox = xTaskGetTickCount();
    bool button_down = false;
    TickType_t button_down_at = 0;
    bool long_press_handled = false;
#if CONFIG_EXAMPLE_ENABLE_SLEEP_DEMO
    bool asleep = false;
#endif

    /* Heavy AT traffic runs here — main task stack is too small for it. */
    if (iridium_system_spec(modem) != SAT_OK) {
        ESP_LOGW(TAG, "Could not read manufacturer/model");
    }

    iridium_result_t ring = iridium_config_ring(modem, true);
    if (ring.status == SAT_OK) {
        ESP_LOGI(TAG, "Ring indicator enabled");
    } else {
        ESP_LOGW(TAG, "Failed to enable ring indicator");
    }

    gpio_config_t button_conf = {
        .intr_type = GPIO_INTR_DISABLE,
        .mode = GPIO_MODE_INPUT,
        .pin_bit_mask = (1ULL << BUTTON_GPIO),
        .pull_down_en = GPIO_PULLDOWN_ENABLE,
        .pull_up_en = GPIO_PULLUP_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&button_conf));

    ESP_LOGI(TAG, "Demo ready — short-press GPIO %d (active-high) to send SBD",
             BUTTON_GPIO);
#if CONFIG_EXAMPLE_ENABLE_SLEEP_DEMO
    ESP_LOGI(TAG, "Long-press GPIO %d to sleep/wake modem", BUTTON_GPIO);
#endif

    poll_signal_and_net(modem);

    for (;;) {
        const bool pressed = gpio_get_level(BUTTON_GPIO) == 1;
        const TickType_t now = xTaskGetTickCount();

        drain_mo_queue(modem);

        if (pressed && !button_down) {
            button_down = true;
            button_down_at = now;
            long_press_handled = false;
        } else if (pressed && button_down && !long_press_handled) {
#if CONFIG_EXAMPLE_ENABLE_SLEEP_DEMO
            if ((now - button_down_at) >= pdMS_TO_TICKS(BUTTON_LONG_PRESS_MS)) {
                toggle_sleep(modem, &asleep);
                long_press_handled = true;
            }
#else
            (void)long_press_handled;
#endif
        } else if (!pressed && button_down) {
            const TickType_t held = now - button_down_at;
            button_down = false;
            if (!long_press_handled &&
                held >= pdMS_TO_TICKS(BUTTON_DEBOUNCE_MS)) {
#if CONFIG_EXAMPLE_ENABLE_SLEEP_DEMO
                if (!asleep) {
                    send_demo_message(modem);
                } else {
                    ESP_LOGW(TAG, "Modem asleep — long-press to wake before TX");
                }
#else
                send_demo_message(modem);
#endif
            }
        }

        if ((now - last_csq) >= pdMS_TO_TICKS(CSQ_INTERVAL_MS)) {
            last_csq = now;
#if CONFIG_EXAMPLE_ENABLE_SLEEP_DEMO
            if (!asleep) {
                poll_signal_and_net(modem);
            }
#else
            poll_signal_and_net(modem);
#endif
        }

        if ((now - last_mailbox) >= pdMS_TO_TICKS(MAILBOX_POLL_INTERVAL_MS)) {
            last_mailbox = now;
#if CONFIG_EXAMPLE_ENABLE_SLEEP_DEMO
            if (!asleep) {
                poll_mailbox(modem);
            }
#else
            poll_mailbox(modem);
#endif
        }

        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

void app_main(void)
{
    ESP_LOGI(TAG, "Iridium SBD example starting on %s", CONFIG_IDF_TARGET);

    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    mo_queue = xQueueCreate(MO_QUEUE_LEN, sizeof(mo_job_t));
    if (mo_queue == NULL) {
        ESP_LOGE(TAG, "Failed to create MO queue");
        return;
    }

#if CONFIG_EXAMPLE_STATUS_LED
    configure_led();
#endif

    satcom = iridium_default_configuration();
    if (satcom == NULL) {
        ESP_LOGE(TAG, "Failed to allocate modem config");
        return;
    }

    satcom->callback = cb_satcom;
    satcom->message_callback = cb_message;
    satcom->uart_number = CONFIG_UART_NUMBER;
    satcom->uart_txn_number = CONFIG_UART_TX_GPIO_NUM;
    satcom->uart_rxd_number = CONFIG_UART_RX_GPIO_NUM;
    satcom->uart_rts_number = uart_pin_or_nc(CONFIG_UART_RTS_GPIO_NUM);
    satcom->uart_cts_number = uart_pin_or_nc(CONFIG_UART_CTS_GPIO_NUM);
    satcom->gpio_sleep_pin_number = CONFIG_UART_SLEEP_GPIO_NUM;
    satcom->gpio_net_pin_number = CONFIG_UART_NET_GPIO_NUM;
    satcom->gpio_ri_pin_number = CONFIG_UART_RI_GPIO_NUM;

    log_pin_config();
    if (iridium_uart_flow_control_enabled(satcom)) {
        ESP_LOGI(TAG, "Hardware flow control enabled (modem will use AT&K3)");
    } else {
        ESP_LOGI(TAG, "Hardware flow control disabled (modem will use AT&K0)");
    }

    if (iridium_config(satcom) != SAT_OK) {
        ESP_LOGE(TAG, "Modem init failed — check UART wiring and power");
        return;
    }
    ESP_LOGI(TAG, "Modem initialized");

    BaseType_t ok = xTaskCreate(demo_task, "iridium_demo", DEMO_TASK_STACK, satcom,
                                DEMO_TASK_PRIORITY, NULL);
    if (ok != pdPASS) {
        ESP_LOGE(TAG, "Failed to start demo task");
        iridium_deinit(satcom);
    }
}
