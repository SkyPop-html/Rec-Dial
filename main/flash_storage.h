/**
 * @file flash_storage.h
 * @brief FAT filesystem storage on the ESP32-S3 module's onboard SPI flash.
 *
 * The FAT partition is accessed through ESP-IDF wear levelling. No SD card,
 * SD module, or external SPI bus is used.
 */
#pragma once

#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t flash_storage_init(void);
void flash_storage_deinit(void);
esp_err_t flash_storage_get_next_filename(char *out_path, size_t max_len);
esp_err_t flash_storage_test_rw(void);
esp_err_t flash_storage_get_free_space_mb(uint32_t *out_free_mb);

#ifdef __cplusplus
}
#endif
