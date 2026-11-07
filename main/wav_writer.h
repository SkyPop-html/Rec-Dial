/**
 * @file wav_writer.h
 * @brief WAV (RIFF/WAVE) file creation, audio streaming, and header finalization.
 */

#pragma once

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#pragma pack(push, 1)
/**
 * @brief Standard 44-byte uncompressed PCM RIFF/WAVE file header.
 */
typedef struct {
    // RIFF chunk descriptor
    char     riff_tag[4];        /**< "RIFF" */
    uint32_t riff_size;          /**< Total file size minus 8 bytes */
    char     wave_tag[4];        /**< "WAVE" */

    // fmt sub-chunk
    char     fmt_tag[4];         /**< "fmt " */
    uint32_t fmt_size;           /**< Sub-chunk size = 16 for PCM */
    uint16_t audio_format;       /**< 1 = Linear PCM */
    uint16_t num_channels;       /**< 1 = Mono, 2 = Stereo */
    uint32_t sample_rate;        /**< Sampling rate in Hz (e.g. 16000) */
    uint32_t byte_rate;          /**< sample_rate * channels * (bits/8) */
    uint16_t block_align;        /**< channels * (bits/8) */
    uint16_t bits_per_sample;    /**< 16 for 16-bit PCM */

    // data sub-chunk
    char     data_tag[4];        /**< "data" */
    uint32_t data_size;          /**< Total raw audio data bytes */
} wav_header_t;
#pragma pack(pop)

/**
 * @brief Context structure for an active WAV file writer session.
 */
typedef struct {
    FILE    *file;               /**< Open file handle */
    uint32_t audio_bytes;        /**< Total PCM bytes written */
    char     filepath[64];       /**< Full path of current file */
    bool     is_open;            /**< Flag indicating open session */
} wav_writer_t;

/**
 * @brief Create a new WAV file on the filesystem and write the placeholder 44-byte header.
 *
 * @param writer Pointer to allocated wav_writer_t structure.
 * @param filepath Destination filesystem path (e.g. "/flash/REC0001.WAV").
 * @return ESP_OK on success, or ESP_FAIL if file creation failed.
 */
esp_err_t wav_writer_create(wav_writer_t *writer, const char *filepath);

/**
 * @brief Write a chunk of raw 16-bit PCM audio samples to the WAV file.
 *
 * @param writer Pointer to active wav_writer_t context.
 * @param data Buffer containing raw 16-bit PCM audio samples.
 * @param size_bytes Number of bytes to write.
 * @return ESP_OK if all bytes written, or ESP_FAIL on disk error.
 */
esp_err_t wav_writer_write_samples(wav_writer_t *writer, const void *data, size_t size_bytes);

/**
 * @brief Finalize the WAV file header with actual recorded sizes, flush buffers, and close.
 *
 * Seeks to offset 4 (riff_size) and offset 40 (data_size) to update byte counts.
 *
 * @param writer Pointer to active wav_writer_t context.
 * @return ESP_OK on success, or ESP_FAIL on seek/write error.
 */
esp_err_t wav_writer_close(wav_writer_t *writer);

#ifdef __cplusplus
}
#endif
