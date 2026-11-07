/**
 * @file app_main.c
 * @brief Main application entry point, state machine, and audio recording loop
 *        for ESP32-S3 Voice Recorder Firmware V1.
 *
 * Framework: ESP-IDF (Native Embedded C)
 * Hardware:  ESP32-S3 N8R2, INMP441 I2S Mic, onboard flash storage, Button, LED
 */

#include <stdio.h>
#include <string.h>
#include <stdbool.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_log.h"
#include "esp_err.h"
#include "esp_system.h"
#include "esp_chip_info.h"
#include "esp_timer.h"

#include "recorder_state.h"
#include "status_led.h"
#include "button_control.h"
#include "flash_storage.h"
#include "audio_capture.h"
#include "wav_writer.h"
#include "oled_ui.h"
#include "wifi_ap.h"
#include "web_server.h"

static const char *TAG = "app_main";

// ============================================================================
//  APPLICATION GLOBAL STATE
// ============================================================================

static recorder_state_t s_recorder_state = RECORDER_IDLE;

// WAV Writer session handle
static wav_writer_t s_wav_session;

// flash write cluster accumulation buffer (4096 bytes = 2048 samples at 16-bit)
// Writing in 4 KB blocks batches filesystem operations and reduces overhead.
static uint8_t  s_flash_write_buffer[FLASH_WRITE_BUF_SIZE];
static uint16_t s_flash_write_index = 0;

// Progress logging timestamp
static int64_t s_last_progress_log_ms = 0;
static char s_current_filepath[64] = {0};

// ============================================================================
//  FORWARD DECLARATIONS
// ============================================================================

static void print_system_banner(void);
static void start_recording_session(void);
static void stop_recording_session(void);
static void handle_recording_loop(void);
static void enter_system_error(const char *reason);

// ============================================================================
//  MAIN APPLICATION ENTRY POINT
// ============================================================================

void app_main(void)
{
    // Brief settling time for power rail and UART console
    vTaskDelay(pdMS_TO_TICKS(500));

    print_system_banner();
    ESP_LOGI(TAG, "Starting Wi-Fi hotspot...");

    if (wifi_ap_start() != ESP_OK) {
        ESP_LOGE(TAG, "Wi-Fi initialization failed!");
    }

    // OLED is optional: failure must not prevent recording.
    if (oled_ui_init() != ESP_OK) {
        ESP_LOGW(TAG, "OLED unavailable; continuing without display");
    }

    // ------------------------------------------------------------------------
    // STAGE 1: Status LED
    // ------------------------------------------------------------------------
    ESP_LOGI(TAG, "[STAGE 1] Initializing Status LED...");
    if (status_led_init() != ESP_OK) {
        enter_system_error("Status LED hardware initialization failed");
        return;
    }
    status_led_set_state(RECORDER_IDLE);

    // ------------------------------------------------------------------------
    // STAGE 2: Button Control
    // ------------------------------------------------------------------------
    ESP_LOGI(TAG, "[STAGE 2] Initializing Button Input...");
    if (button_control_init() != ESP_OK) {
        enter_system_error("Button GPIO initialization failed");
        return;
    }

    // ------------------------------------------------------------------------
    // STAGE 3: Internal Flash Filesystem Mount & Self-Test
    // ------------------------------------------------------------------------
    ESP_LOGI(TAG, "[STAGE 3] Mounting internal flash filesystem...");
    if (flash_storage_init() != ESP_OK) {
        enter_system_error("Internal flash filesystem mount failed");
        return;
    }

    if (flash_storage_test_rw() != ESP_OK) {
        enter_system_error("Internal flash filesystem self-test failed");
        return;
    }
    // Start HTTP web server
    if (web_server_start() != ESP_OK) {
        enter_system_error("Web server startup failed");
        return;
    }
    // ------------------------------------------------------------------------
    // STAGE 4: I2S INMP441 Microphone Driver & Self-Test
    // ------------------------------------------------------------------------
    ESP_LOGI(TAG, "[STAGE 4] Initializing I2S INMP441 Microphone...");
    if (audio_capture_init() != ESP_OK) {
        enter_system_error("I2S peripheral initialization failed");
        return;
    }

    audio_capture_self_test();

    // ------------------------------------------------------------------------
    // SYSTEM READY
    // ------------------------------------------------------------------------
    s_recorder_state = RECORDER_IDLE;
    status_led_set_state(RECORDER_IDLE);
    oled_ui_show_ready();

    ESP_LOGI(TAG, "==========================================================");
    ESP_LOGI(TAG, "  ALL HARDWARE READY! Press Record button (GPIO 1) to start.");
    ESP_LOGI(TAG, "==========================================================");

    // ------------------------------------------------------------------------
    // MAIN EVENT LOOP
    // ------------------------------------------------------------------------
    while (1) {
        switch (s_recorder_state) {
            case RECORDER_IDLE:
                // Poll for button press
                if (button_control_poll_record_press()) {
                    start_recording_session();
                }
                vTaskDelay(pdMS_TO_TICKS(10));
                break;

            case RECORDER_RECORDING:
                // Check if user pressed button to STOP
                if (button_control_poll_record_press()) {
                    stop_recording_session();
                } else {
                    // Capture and stream audio
                    handle_recording_loop();
                    taskYIELD();
                }
                break;

            case RECORDER_ERROR:
                // Device is in error state; LED blinks in background
                if (button_control_poll_record_press()) {
                    ESP_LOGW(TAG, "Device in ERROR state. Resolve hardware issue and press RST.");
                }
                vTaskDelay(pdMS_TO_TICKS(200));
                break;
        }
    }
}

// ============================================================================
//  RECORDING SESSION CONTROL
// ============================================================================

static void start_recording_session(void)
{
    ESP_LOGI(TAG, "--------------------------------------------------");
    ESP_LOGI(TAG, "Starting new voice recording session...");

    // 1. Verify available storage
    uint32_t free_mb = 0;
    if (flash_storage_get_free_space_mb(&free_mb) == ESP_OK && free_mb < 1) {
        ESP_LOGE(TAG, "Insufficient storage space on internal flash (< 1 MB free)!");
        enter_system_error("Internal flash full");
        return;
    }

    // 2. Determine next filename
    char filepath[64];
    if (flash_storage_get_next_filename(filepath, sizeof(filepath)) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to allocate next recording filename.");
        enter_system_error("Filename index overflow");
        return;
    }

    snprintf(s_current_filepath, sizeof(s_current_filepath), "%s", filepath);

    // 3. Create WAV file with placeholder header
    if (wav_writer_create(&s_wav_session, filepath) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create WAV file: %s", filepath);
        enter_system_error("WAV file creation failed");
        return;
    }

    // 4. Start I2S audio stream
    if (audio_capture_start() != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start I2S capture.");
        wav_writer_close(&s_wav_session);
        enter_system_error("I2S capture start failed");
        return;
    }

    // 5. Reset RAM cluster buffer and metrics
    s_flash_write_index = 0;
    s_last_progress_log_ms = esp_timer_get_time() / 1000;

    // 6. Transition state and turn on LED
    s_recorder_state = RECORDER_RECORDING;
    status_led_set_state(RECORDER_RECORDING);
    oled_ui_show_recording(0, s_current_filepath, 0);

    ESP_LOGI(TAG, ">>> RECORDING IN PROGRESS: %s <<<", filepath);
}

static void handle_recording_loop(void)
{
    // Read batch of PCM16 samples (64 samples = 128 bytes)
    #define SAMPLES_BATCH 64
    int16_t pcm_samples[SAMPLES_BATCH];
    size_t samples_read = 0;

    esp_err_t ret = audio_capture_read_pcm16(pcm_samples, SAMPLES_BATCH, &samples_read, 100);
    if (ret != ESP_OK || samples_read == 0) {
        return;
    }

    // Append 16-bit PCM samples to the internal-flash write buffer
    for (size_t i = 0; i < samples_read; i++) {
        int16_t sample = pcm_samples[i];
        s_flash_write_buffer[s_flash_write_index++] = (uint8_t)(sample & 0xFF);
        s_flash_write_buffer[s_flash_write_index++] = (uint8_t)((sample >> 8) & 0xFF);

        // When the 4 KB buffer is full, flush it to internal flash
        if (s_flash_write_index >= FLASH_WRITE_BUF_SIZE) {
            ret = wav_writer_write_samples(&s_wav_session, s_flash_write_buffer, s_flash_write_index);
            if (ret != ESP_OK) {
                ESP_LOGE(TAG, "Critical write failure while recording!");
                stop_recording_session();
                enter_system_error("flash write failed during recording");
                return;
            }
            s_flash_write_index = 0;
        }
    }

    // Periodic progress report (every 1 second)
    int64_t now_ms = esp_timer_get_time() / 1000;
    if (now_ms - s_last_progress_log_ms >= 1000) {
        uint32_t total_bytes = s_wav_session.audio_bytes + s_flash_write_index;
        float seconds = (float)total_bytes /
                        (AUDIO_SAMPLE_RATE * AUDIO_CHANNELS * (AUDIO_BITS_PER_SAMPLE / 8));
        ESP_LOGI(TAG, "[REC PROGRESS] %lu bytes captured (%.1f sec)",
                 (unsigned long)total_bytes, seconds);
        // Update OLED once per second to avoid excessive I2C traffic.
        oled_ui_show_recording((uint32_t)seconds, s_current_filepath, 3);
        s_last_progress_log_ms = now_ms;
    }
}

static void stop_recording_session(void)
{
    ESP_LOGI(TAG, "Stopping voice recording session...");

    // 1. Pause I2S capture
    audio_capture_stop();

    // 2. Flush any remaining audio in RAM cluster buffer
    if (s_flash_write_index > 0) {
        wav_writer_write_samples(&s_wav_session, s_flash_write_buffer, s_flash_write_index);
        s_flash_write_index = 0;
    }

    // 3. Finalize WAV header (updates sizes) and close file
    if (wav_writer_close(&s_wav_session) != ESP_OK) {
        ESP_LOGW(TAG, "Warning: WAV header finalization reported an issue.");
    }

    // 4. Return to IDLE state and turn off LED
    s_recorder_state = RECORDER_IDLE;
    status_led_set_state(RECORDER_IDLE);
    oled_ui_show_stopped(s_current_filepath);
    vTaskDelay(pdMS_TO_TICKS(700));
    oled_ui_show_ready();

    ESP_LOGI(TAG, "Recording finalized successfully. System returned to IDLE.");
    ESP_LOGI(TAG, "--------------------------------------------------");
}

static void enter_system_error(const char *reason)
{
    oled_ui_show_error(reason);
    ESP_LOGE(TAG, "==========================================================");
    ESP_LOGE(TAG, "  SYSTEM ERROR: %s", reason);
    ESP_LOGE(TAG, "  Status LED will blink error pattern (3 flashes + pause).");
    ESP_LOGE(TAG, "  Resolve the issue and press RST on the ESP32-S3 board.");
    ESP_LOGE(TAG, "==========================================================");

    s_recorder_state = RECORDER_ERROR;
    status_led_set_state(RECORDER_ERROR);
}

static void print_system_banner(void)
{
    esp_chip_info_t chip_info;
    esp_chip_info(&chip_info);

    ESP_LOGI(TAG, "==========================================================");
    ESP_LOGI(TAG, "  ESP32-S3 Voice Recorder Firmware V1 (ESP-IDF C)");
    ESP_LOGI(TAG, "  Build Date: %s %s", __DATE__, __TIME__);
    ESP_LOGI(TAG, "  Silicon Revision: v%d.%d | Cores: %d",
             chip_info.revision / 100, chip_info.revision % 100, chip_info.cores);
    ESP_LOGI(TAG, "  Framework: ESP-IDF Native C99/C11 (No Arduino, No C++)");
    ESP_LOGI(TAG, "==========================================================");
}
