/**
 * @file recorder_state.h
 * @brief Common types, configurations, state definitions, and GPIO mappings
 *        for ESP32-S3 Voice Recorder Firmware V1.
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "driver/gpio.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 *  SYSTEM STATE MACHINE
 * ============================================================================ */

typedef enum {
    RECORDER_IDLE,       /**< System ready, internal flash mounted, waiting for user button */
    RECORDER_RECORDING,  /**< Actively capturing I2S audio and writing to internal flash */
    RECORDER_ERROR       /**< Hardware fault (SD/I2S fail), LED blinking error pattern */
} recorder_state_t;

/* ============================================================================
 *  GPIO PIN DEFINITIONS (ESP32-S3 N8R2)
 *
 *  CRITICAL PIN SAFETY AUDIT FOR ESP32-S3 N8R2:
 *  - GPIO 26 to 37: PERMANENTLY RESERVED for internal Octal/Quad SPI Flash
 *    and PSRAM on the N8R2 module. DO NOT CONNECT ANYTHING TO THESE PINS.
 *  - GPIO 19, 20: Dedicated USB D- / D+ (native USB CDC / JTAG).
 *  - GPIO 43, 44: UART0 TX / RX (console log and serial flashing).
 *  - GPIO 0: Boot strapping pin.
 *  - GPIO 45, 46: VDD_SPI and boot logging strapping pins.
 *
 *  The selected pins below are completely outside the flash/PSRAM ranges,
 *  have standard GPIO muxing capability, and do not conflict with boot straps.
 * ============================================================================ */

// --- I2S Digital Microphone (INMP441) ---
#define I2S_SCK_PIN         GPIO_NUM_4    /**< INMP441 SCK (Bit Clock / BCLK) */
#define I2S_WS_PIN          GPIO_NUM_5    /**< INMP441 WS  (Word Select / LRCLK) */
#define I2S_SD_PIN          GPIO_NUM_6    /**< INMP441 SD  (Serial Data In to S3) */

// --- Push Buttons ---
#define BTN_RECORD_PIN      GPIO_NUM_1    /**< Record/Stop button (Active LOW) */
#define BTN_SPARE_PIN       GPIO_NUM_2    /**< Spare button reserved for V2 */
#define BTN_DEBOUNCE_MS     250           /**< Minimum debounce lockout period */

// --- External Status LED ---
#define LED_PIN             GPIO_NUM_21   /**< External LED anode via 330 ohm resistor */
#define LED_ACTIVE_LEVEL    1             /**< 1 = HIGH turns LED on, 0 = Inverted */

/* ============================================================================
 *  AUDIO & STORAGE SPECIFICATIONS
 * ============================================================================ */

#define AUDIO_SAMPLE_RATE       16000     /**< 16 kHz sample rate (voice standard) */
#define AUDIO_CHANNELS          1         /**< 1 channel (Mono) */
#define AUDIO_BITS_PER_SAMPLE   16        /**< 16-bit signed PCM output */

// I2S DMA Ring Buffering (8 buffers of 256 samples = ~128ms of hardware buffer)
#define I2S_DMA_BUF_COUNT       8
#define I2S_DMA_BUF_LEN         256

// Software write buffer size (4096 bytes = 2048 samples = 128ms of audio)
#define FLASH_WRITE_BUF_SIZE    4096

// Internal flash FAT partition. Label must match partitions.csv.
#define FLASH_MOUNT_POINT       "/flash"
#define FLASH_PARTITION_LABEL  "storage"
#define REC_FILENAME_PREFIX    FLASH_MOUNT_POINT "/REC"
#define REC_FILENAME_EXT       ".WAV"
#define REC_MAX_FILE_NUMBER    9999

#ifdef __cplusplus
}
#endif
