/**
 * @file audio_capture.c
 * @brief Implementation of I2S microphone driver and PCM16 sample conversion.
 */

#include "audio_capture.h"
#include "recorder_state.h"
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/i2s_std.h"
#include "esp_log.h"

static const char *TAG = "audio_capture";

// Handle differences in macro naming across ESP-IDF v5.x versions
#ifndef I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG
#ifdef I2S_STD_PHILIP_SLOT_DEFAULT_CONFIG
#define I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG I2S_STD_PHILIP_SLOT_DEFAULT_CONFIG
#endif
#endif

static i2s_chan_handle_t s_rx_handle = NULL;
static bool s_is_running = false;

// Scratch buffer for raw 32-bit I2S reads
#define RAW_BATCH_CAPACITY 128
static int32_t s_raw_buffer[RAW_BATCH_CAPACITY];

static inline int16_t clamp_to_i16(int32_t val)
{
    if (val > 32767) return 32767;
    if (val < -32768) return -32768;
    return (int16_t)val;
}

esp_err_t audio_capture_init(void)
{
    if (s_rx_handle != NULL) {
        ESP_LOGW(TAG, "I2S channel already initialized.");
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Initializing I2S INMP441 Microphone (Master RX)...");
    ESP_LOGI(TAG, "  BCLK/SCK=GPIO%d, WS/LRCLK=GPIO%d, SD/DIN=GPIO%d",
             I2S_SCK_PIN, I2S_WS_PIN, I2S_SD_PIN);
    ESP_LOGI(TAG, "  Rate=%d Hz, Channels=%d (Mono Left Slot), FrameWidth=32-bit",
             AUDIO_SAMPLE_RATE, AUDIO_CHANNELS);

    // Channel configuration: RX only, Master role
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    chan_cfg.dma_desc_num  = I2S_DMA_BUF_COUNT;
    chan_cfg.dma_frame_num = I2S_DMA_BUF_LEN;

    esp_err_t ret = i2s_new_channel(&chan_cfg, NULL, &s_rx_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to allocate I2S channel: %s", esp_err_to_name(ret));
        return ret;
    }

    // Standard Philips I2S mode configuration
    i2s_std_config_t std_cfg = {
        .clk_cfg  = I2S_STD_CLK_DEFAULT_CONFIG(AUDIO_SAMPLE_RATE),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_32BIT,
                                                         I2S_SLOT_MODE_MONO),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = I2S_SCK_PIN,
            .ws   = I2S_WS_PIN,
            .dout = I2S_GPIO_UNUSED,
            .din  = I2S_SD_PIN,
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv   = false,
            },
        },
    };

    // INMP441 L/R pin tied to GND selects the LEFT slot
    std_cfg.slot_cfg.slot_mask = I2S_STD_SLOT_LEFT;

    ret = i2s_channel_init_std_mode(s_rx_handle, &std_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize standard I2S mode: %s", esp_err_to_name(ret));
        i2s_del_channel(s_rx_handle);
        s_rx_handle = NULL;
        return ret;
    }

    ESP_LOGI(TAG, "I2S microphone driver initialized successfully.");
    return ESP_OK;
}

esp_err_t audio_capture_start(void)
{
    if (s_rx_handle == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    if (s_is_running) {
        return ESP_OK;
    }

    esp_err_t ret = i2s_channel_enable(s_rx_handle);
    if (ret == ESP_OK) {
        s_is_running = true;
        ESP_LOGI(TAG, "I2S RX channel enabled.");
    } else {
        ESP_LOGE(TAG, "Failed to enable I2S RX channel: %s", esp_err_to_name(ret));
    }
    return ret;
}

esp_err_t audio_capture_stop(void)
{
    if (s_rx_handle == NULL || !s_is_running) {
        return ESP_OK;
    }

    esp_err_t ret = i2s_channel_disable(s_rx_handle);
    if (ret == ESP_OK) {
        s_is_running = false;
        ESP_LOGI(TAG, "I2S RX channel paused.");
    }
    return ret;
}

void audio_capture_deinit(void)
{
    if (s_rx_handle != NULL) {
        audio_capture_stop();
        i2s_del_channel(s_rx_handle);
        s_rx_handle = NULL;
        ESP_LOGI(TAG, "I2S channel destroyed.");
    }
}

esp_err_t audio_capture_read_pcm16(int16_t *out_pcm,
                                  size_t max_samples,
                                  size_t *out_samples_read,
                                  uint32_t timeout_ms)
{
    if (s_rx_handle == NULL || !s_is_running || out_pcm == NULL || out_samples_read == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    size_t samples_to_read = max_samples;
    if (samples_to_read > RAW_BATCH_CAPACITY) {
        samples_to_read = RAW_BATCH_CAPACITY;
    }

    size_t bytes_to_read = samples_to_read * sizeof(int32_t);
    size_t bytes_read = 0;

    esp_err_t ret = i2s_channel_read(s_rx_handle,
                                      s_raw_buffer,
                                      bytes_to_read,
                                      &bytes_read,
                                      pdMS_TO_TICKS(timeout_ms));

    if (ret != ESP_OK) {
        *out_samples_read = 0;
        return ret;
    }

    size_t actual_samples = bytes_read / sizeof(int32_t);

    /*
     * INMP441 Sample Conversion Logic:
     * -------------------------------------------------------------
     * The INMP441 outputs 24-bit 2's complement audio MSB-aligned
     * within a 32-bit I2S frame:
     *
     *   Bit 31 (Sign) .......... Bit 8 (LSB) | Bits 7..0 (Zero padding)
     *
     * To convert to a signed 16-bit PCM sample (-32768 to +32767):
     * We perform an arithmetic right-shift of 16 bits.
     * This shifts bits 31..16 down to bits 15..0, preserving the sign bit
     * and taking the top 16 bits of microphone data.
     */
    for (size_t i = 0; i < actual_samples; i++) {
        int32_t shifted = s_raw_buffer[i] >> 16;
        out_pcm[i] = clamp_to_i16(shifted);
    }

    *out_samples_read = actual_samples;
    return ESP_OK;
}

bool audio_capture_self_test(void)
{
    ESP_LOGI(TAG, "Running microphone self-test...");

    esp_err_t ret = audio_capture_start();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Self-test: Failed to start I2S capture.");
        return false;
    }

    const size_t test_samples = 256;
    int16_t samples[RAW_BATCH_CAPACITY];
    size_t total_samples_tested = 0;
    size_t non_zero_count = 0;
    int32_t min_val = 32767;
    int32_t max_val = -32768;

    while (total_samples_tested < test_samples) {
        size_t read_count = 0;
        ret = audio_capture_read_pcm16(samples, RAW_BATCH_CAPACITY, &read_count, 500);
        if (ret != ESP_OK || read_count == 0) {
            ESP_LOGW(TAG, "Self-test: Timed out waiting for I2S samples.");
            break;
        }

        for (size_t i = 0; i < read_count; i++) {
            int16_t s = samples[i];
            if (s != 0 && s != -1 && s != 1) {
                non_zero_count++;
            }
            if (s < min_val) min_val = s;
            if (s > max_val) max_val = s;
        }

        total_samples_tested += read_count;
    }

    audio_capture_stop();

    ESP_LOGI(TAG, "Self-test summary: %zu/%zu samples active | Dynamic Range: [%ld, %ld]",
             non_zero_count, total_samples_tested, (long)min_val, (long)max_val);

    if (non_zero_count > 0) {
        ESP_LOGI(TAG, "Microphone self-test PASSED (Audio activity detected).");
        return true;
    } else {
        ESP_LOGW(TAG, "Microphone self-test WARN: All samples were zero.");
        ESP_LOGW(TAG, "Check INMP441 wiring: VDD (3.3V), GND, SCK, WS, SD, L/R (to GND).");
        return false;
    }
}
