/**
 * @file button_control.h
 * @brief Debounced physical button interface for ESP32-S3 Voice Recorder.
 */

#pragma once

#include "recorder_state.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize button GPIOs with internal pull-up resistors.
 * @return ESP_OK on success.
 */
esp_err_t button_control_init(void);

/**
 * @brief Poll the Record/Stop button with software debouncing.
 *
 * This function detects a single transition from RELEASED to PRESSED.
 * Holding down the button will not trigger repeated events.
 *
 * @return true if a valid, debounced button click was detected, false otherwise.
 */
bool button_control_poll_record_press(void);

#ifdef __cplusplus
}
#endif
