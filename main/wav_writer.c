/**
 * @file wav_writer.c
 * @brief Implementation of standard 44-byte WAV audio file creation and finalization.
 */

#include "wav_writer.h"
#include "recorder_state.h"
#include <string.h>
#include <sys/stat.h>
#include "esp_log.h"

static const char *TAG = "wav_writer";

esp_err_t wav_writer_create(wav_writer_t *writer, const char *filepath)
{
    if (writer == NULL || filepath == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    writer->file = fopen(filepath, "wb");
    if (writer->file == NULL) {
        ESP_LOGE(TAG, "Failed to create WAV file at %s", filepath);
        return ESP_FAIL;
    }

    wav_header_t hdr;
    memset(&hdr, 0, sizeof(wav_header_t));

    // RIFF Descriptor
    memcpy(hdr.riff_tag, "RIFF", 4);
    hdr.riff_size = 0; // Placeholder: updated on close
    memcpy(hdr.wave_tag, "WAVE", 4);

    // fmt sub-chunk
    memcpy(hdr.fmt_tag, "fmt ", 4);
    hdr.fmt_size        = 16;
    hdr.audio_format    = 1; // PCM Linear uncompressed
    hdr.num_channels    = AUDIO_CHANNELS;
    hdr.sample_rate     = AUDIO_SAMPLE_RATE;
    hdr.byte_rate       = AUDIO_SAMPLE_RATE * AUDIO_CHANNELS * (AUDIO_BITS_PER_SAMPLE / 8);
    hdr.block_align     = AUDIO_CHANNELS * (AUDIO_BITS_PER_SAMPLE / 8);
    hdr.bits_per_sample = AUDIO_BITS_PER_SAMPLE;

    // data sub-chunk
    memcpy(hdr.data_tag, "data", 4);
    hdr.data_size = 0; // Placeholder: updated on close

    size_t written = fwrite(&hdr, 1, sizeof(wav_header_t), writer->file);
    if (written != sizeof(wav_header_t)) {
        ESP_LOGE(TAG, "Failed to write 44-byte WAV header placeholder.");
        fclose(writer->file);
        writer->file = NULL;
        return ESP_FAIL;
    }

    writer->audio_bytes = 0;
    strncpy(writer->filepath, filepath, sizeof(writer->filepath) - 1);
    writer->filepath[sizeof(writer->filepath) - 1] = '\0';
    writer->is_open = true;

    ESP_LOGI(TAG, "Created WAV file: %s (44-byte header initialized).", filepath);
    return ESP_OK;
}

esp_err_t wav_writer_write_samples(wav_writer_t *writer, const void *data, size_t size_bytes)
{
    if (writer == NULL || !writer->is_open || writer->file == NULL || data == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    if (size_bytes == 0) {
        return ESP_OK;
    }

    size_t written = fwrite(data, 1, size_bytes, writer->file);
    if (written != size_bytes) {
        ESP_LOGE(TAG, "Filesystem write error! Expected %zu bytes, wrote %zu.", size_bytes, written);
        return ESP_FAIL;
    }

    writer->audio_bytes += (uint32_t)written;
    return ESP_OK;
}

esp_err_t wav_writer_close(wav_writer_t *writer)
{
    if (writer == NULL || !writer->is_open || writer->file == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    ESP_LOGI(TAG, "Finalizing WAV header for %s...", writer->filepath);
    ESP_LOGI(TAG, "  Total audio data written: %lu bytes", (unsigned long)writer->audio_bytes);

    // riff_size = (Total File Size) - 8
    //           = (audio_bytes + sizeof(wav_header_t)) - 8
    //           = audio_bytes + 44 - 8 = audio_bytes + 36
    uint32_t riff_size = writer->audio_bytes + 36;
    uint32_t data_size = writer->audio_bytes;

    // Seek to offset 4: RIFF chunk size
    if (fseek(writer->file, 4, SEEK_SET) != 0) {
        ESP_LOGE(TAG, "Failed to seek to offset 4 (riff_size).");
        fclose(writer->file);
        writer->is_open = false;
        return ESP_FAIL;
    }

    if (fwrite(&riff_size, 1, sizeof(riff_size), writer->file) != sizeof(riff_size)) {
        ESP_LOGE(TAG, "Failed to write updated riff_size.");
        fclose(writer->file);
        writer->is_open = false;
        return ESP_FAIL;
    }

    // Seek to offset 40: data sub-chunk size
    if (fseek(writer->file, 40, SEEK_SET) != 0) {
        ESP_LOGE(TAG, "Failed to seek to offset 40 (data_size).");
        fclose(writer->file);
        writer->is_open = false;
        return ESP_FAIL;
    }

    if (fwrite(&data_size, 1, sizeof(data_size), writer->file) != sizeof(data_size)) {
        ESP_LOGE(TAG, "Failed to write updated data_size.");
        fclose(writer->file);
        writer->is_open = false;
        return ESP_FAIL;
    }

    // Flush and close the file
    fflush(writer->file);
    fclose(writer->file);
    writer->file = NULL;
    writer->is_open = false;

    uint32_t total_file_size = writer->audio_bytes + sizeof(wav_header_t);
    float seconds = (float)writer->audio_bytes /
                    (AUDIO_SAMPLE_RATE * AUDIO_CHANNELS * (AUDIO_BITS_PER_SAMPLE / 8));

    ESP_LOGI(TAG, "WAV closed successfully: %s", writer->filepath);
    ESP_LOGI(TAG, "  Final file size: %lu bytes | Duration: %.2f seconds",
             (unsigned long)total_file_size, seconds);

    // Verify on-disk file size matches expected size
    struct stat st;
    if (stat(writer->filepath, &st) == 0) {
        if ((uint32_t)st.st_size == total_file_size) {
            ESP_LOGI(TAG, "File size verification PASSED.");
        } else {
            ESP_LOGW(TAG, "File size mismatch! Expected %lu, filesystem reports %ld.",
                     (unsigned long)total_file_size, (long)st.st_size);
        }
    }

    return ESP_OK;
}
