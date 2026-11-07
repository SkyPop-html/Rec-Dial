/**
 * @file audio_capture.h
 * @brief I2S audio capture and INMP441 sample conversion for ESP32-S3.
 */

#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize I2S peripheral in Standard (Philips) RX Master mode for INMP441.
 * @return ESP_OK on success, or ESP-IDF error code.
 */
esp_err_t audio_capture_init(void);

/**
 * @brief Enable the I2S RX channel and begin DMA audio streaming.
 * @return ESP_OK on success.
 */
esp_err_t audio_capture_start(void);

/**
 * @brief Disable the I2S RX channel and pause DMA audio streaming.
 * @return ESP_OK on success.
 */
esp_err_t audio_capture_stop(void);

/**
 * @brief Release I2S channel and hardware resources.
 */
void audio_capture_deinit(void);

/**
 * @brief Read raw samples from I2S, convert 32-bit frames to signed 16-bit PCM,
 *        and place them into destination buffer.
 *
 * @param out_pcm Destination buffer for signed 16-bit PCM audio samples.
 * @param max_samples Maximum number of 16-bit samples the buffer can hold.
 * @param out_samples_read Pointer to store the actual number of samples converted.
 * @param timeout_ms FreeRTOS timeout in milliseconds.
 * @return ESP_OK if samples were read, or error code on failure/timeout.
 */
esp_err_t audio_capture_read_pcm16(int16_t *out_pcm,
                                  size_t max_samples,
                                  size_t *out_samples_read,
                                  uint32_t timeout_ms);

/**
 * @brief Perform a microphone self-test by sampling audio and verifying non-zero signals.
 * @return true if valid signals detected, false if completely dead/unconnected.
 */
bool audio_capture_self_test(void);

#ifdef __cplusplus
}
#endif
