/**
 * @file status_led.h
 * @brief Non-blocking LED status indicator for ESP32-S3 Voice Recorder.
 */

#pragma once

#include "recorder_state.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize the status LED GPIO and background timer.
 * @return ESP_OK on success, or appropriate error code.
 */
esp_err_t status_led_init(void);

/**
 * @brief Update the LED behavior based on current recorder state.
 *
 * - RECORDER_IDLE: LED OFF
 * - RECORDER_RECORDING: LED Solid ON
 * - RECORDER_ERROR: Non-blocking 3 short flashes followed by pause
 *
 * @param state The target state to reflect on the LED.
 */
void status_led_set_state(recorder_state_t state);

/**
 * @brief Directly turn LED on or off respecting polarity.
 * @param on True to illuminate LED, false to turn off.
 */
void status_led_set_level(bool on);

#ifdef __cplusplus
}
#endif
