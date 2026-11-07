/**
 * @file button_control.c
 * @brief Implementation of software debounced button polling.
 */

#include "button_control.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "driver/gpio.h"

static const char *TAG = "button_control";

static int64_t s_last_press_time = 0;
static bool s_btn_was_down = false;

esp_err_t button_control_init(void)
{
    ESP_LOGI(TAG, "Configuring buttons (Record: GPIO %d, Spare: GPIO %d)...",
             BTN_RECORD_PIN, BTN_SPARE_PIN);

    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << BTN_RECORD_PIN) | (1ULL << BTN_SPARE_PIN),
        .mode         = GPIO_MODE_INPUT,
        .pull_up_en   = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };

    esp_err_t ret = gpio_config(&io_conf);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure button GPIOs (err: %s)", esp_err_to_name(ret));
        return ret;
    }

    s_last_press_time = 0;
    s_btn_was_down = false;

    ESP_LOGI(TAG, "Buttons initialized successfully (Active LOW with internal pull-up).");
    return ESP_OK;
}

bool button_control_poll_record_press(void)
{
    int level = gpio_get_level(BTN_RECORD_PIN);
    int64_t now_ms = esp_timer_get_time() / 1000;

    // Active LOW: 0 means physical button is closed to ground
    if (level == 0) {
        if (!s_btn_was_down) {
            // First time detecting down state
            if ((now_ms - s_last_press_time) >= BTN_DEBOUNCE_MS) {
                s_btn_was_down = true;
                s_last_press_time = now_ms;
                ESP_LOGI(TAG, "Record/Stop button click detected.");
                return true;
            }
        }
    } else {
        // Button released
        s_btn_was_down = false;
    }

    return false;
}
