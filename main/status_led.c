/**
 * @file status_led.c
 * @brief Implementation of non-blocking LED status control using esp_timer.
 */

#include "status_led.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "driver/gpio.h"

static const char *TAG = "status_led";

// Timer handle for error blinking pattern
static esp_timer_handle_t s_led_timer = NULL;
static uint8_t s_error_step = 0;
static recorder_state_t s_current_led_state = RECORDER_IDLE;

static void led_timer_callback(void *arg);

void status_led_set_level(bool on)
{
    int level = on ? (LED_ACTIVE_LEVEL ? 1 : 0) : (LED_ACTIVE_LEVEL ? 0 : 1);
    gpio_set_level(LED_PIN, level);
}

static void schedule_next_blink_step(void)
{
    if (s_current_led_state != RECORDER_ERROR) {
        return;
    }

    uint64_t delay_us = 150000; // default 150ms

    switch (s_error_step) {
        case 0: // Blink 1 ON
            status_led_set_level(true);
            delay_us = 150000;
            s_error_step = 1;
            break;
        case 1: // Blink 1 OFF
            status_led_set_level(false);
            delay_us = 150000;
            s_error_step = 2;
            break;
        case 2: // Blink 2 ON
            status_led_set_level(true);
            delay_us = 150000;
            s_error_step = 3;
            break;
        case 3: // Blink 2 OFF
            status_led_set_level(false);
            delay_us = 150000;
            s_error_step = 4;
            break;
        case 4: // Blink 3 ON
            status_led_set_level(true);
            delay_us = 150000;
            s_error_step = 5;
            break;
        case 5: // Blink 3 OFF (Pause period)
        default:
            status_led_set_level(false);
            delay_us = 800000; // 800ms pause
            s_error_step = 0;
            break;
    }

    esp_timer_start_once(s_led_timer, delay_us);
}

static void led_timer_callback(void *arg)
{
    schedule_next_blink_step();
}

esp_err_t status_led_init(void)
{
    ESP_LOGI(TAG, "Configuring Status LED on GPIO %d (Active %s)...",
             LED_PIN, LED_ACTIVE_LEVEL ? "HIGH" : "LOW");

    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << LED_PIN),
        .mode         = GPIO_MODE_OUTPUT,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };

    esp_err_t ret = gpio_config(&io_conf);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure LED GPIO (err: %s)", esp_err_to_name(ret));
        return ret;
    }

    // Default to OFF
    status_led_set_level(false);

    // Create background one-shot timer for error pattern
    const esp_timer_create_args_t timer_args = {
        .callback = &led_timer_callback,
        .name     = "led_error_timer",
    };

    ret = esp_timer_create(&timer_args, &s_led_timer);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create LED timer (err: %s)", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(TAG, "Status LED initialized successfully.");
    return ESP_OK;
}

void status_led_set_state(recorder_state_t state)
{
    s_current_led_state = state;

    // Stop any active timer
    if (s_led_timer != NULL) {
        esp_timer_stop(s_led_timer);
    }

    switch (state) {
        case RECORDER_IDLE:
            status_led_set_level(false);
            break;

        case RECORDER_RECORDING:
            status_led_set_level(true);
            break;

        case RECORDER_ERROR:
            s_error_step = 0;
            schedule_next_blink_step();
            break;
    }
}
