/**
 * @file flash_storage.c
 * @brief FATFS on the ESP32-S3 onboard SPI flash, using wear levelling.
 */

#include "flash_storage.h"

#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include "esp_log.h"
#include "esp_vfs_fat.h"
#include "wear_levelling.h"
#include "recorder_state.h"

static const char *TAG = "flash_storage";
static wl_handle_t s_wl_handle = WL_INVALID_HANDLE;
static bool s_is_mounted = false;

esp_err_t flash_storage_init(void)
{
    if (s_is_mounted) {
        ESP_LOGW(TAG, "Internal flash filesystem is already mounted.");
        return ESP_OK;
    }

    const esp_vfs_fat_mount_config_t mount_config = {
        .format_if_mount_failed = true,
        .max_files = 5,
        .allocation_unit_size = 4096,
    };

    ESP_LOGI(TAG, "Mounting internal flash FAT filesystem at %s...", FLASH_MOUNT_POINT);
    ESP_LOGW(TAG, "A blank storage partition will be formatted on first boot.");

    esp_err_t ret = esp_vfs_fat_spiflash_mount_rw_wl(
        FLASH_MOUNT_POINT,
        FLASH_PARTITION_LABEL,
        &mount_config,
        &s_wl_handle);

    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Internal flash filesystem mount failed: %s", esp_err_to_name(ret));
        s_wl_handle = WL_INVALID_HANDLE;
        return ret;
    }

    s_is_mounted = true;

    uint32_t free_mb = 0;
    if (flash_storage_get_free_space_mb(&free_mb) == ESP_OK) {
        ESP_LOGI(TAG, "Internal flash filesystem mounted. Free space: %lu MB",
                 (unsigned long)free_mb);
    }

    return ESP_OK;
}

void flash_storage_deinit(void)
{
    if (!s_is_mounted) {
        return;
    }

    esp_vfs_fat_spiflash_unmount_rw_wl(FLASH_MOUNT_POINT, s_wl_handle);
    s_wl_handle = WL_INVALID_HANDLE;
    s_is_mounted = false;
    ESP_LOGI(TAG, "Internal flash filesystem unmounted.");
}

esp_err_t flash_storage_get_next_filename(char *out_path, size_t max_len)
{
    if (!s_is_mounted || out_path == NULL || max_len == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    struct stat st;
    for (int i = 1; i <= REC_MAX_FILE_NUMBER; ++i) {
        int n = snprintf(out_path, max_len, "%s%04d%s",
                         REC_FILENAME_PREFIX, i, REC_FILENAME_EXT);
        if (n < 0 || (size_t)n >= max_len) {
            return ESP_ERR_INVALID_SIZE;
        }

        if (stat(out_path, &st) != 0) {
            ESP_LOGI(TAG, "Allocated recording file: %s", out_path);
            return ESP_OK;
        }
    }

    return ESP_ERR_NOT_FOUND;
}

/*
 * Diagnostic self-test using POSIX open/read/write instead of stdio fopen().
 * This helps determine whether the failure is specific to stdio or to the
 * mounted FAT filesystem/VFS path itself.
 */
esp_err_t flash_storage_test_rw(void)
{
    if (!s_is_mounted) {
        return ESP_ERR_INVALID_STATE;
    }

    const char *test_path = FLASH_MOUNT_POINT "/TEST.TMP";
    static const char test_data[] = "ESP32-S3 internal flash filesystem test\n";
    char readback[sizeof(test_data)];

    int fd = open(test_path, O_CREAT | O_WRONLY | O_TRUNC, 0666);
    if (fd < 0) {
        ESP_LOGE(TAG, "open() create failed: path=%s, errno=%d, reason=%s",
                 test_path, errno, strerror(errno));
        return ESP_FAIL;
    }

    ssize_t written = write(fd, test_data, sizeof(test_data));
    int write_errno = errno;
    int close_result = close(fd);

    if (written != (ssize_t)sizeof(test_data)) {
        ESP_LOGE(TAG, "write() failed/short write: written=%ld expected=%u errno=%d reason=%s",
                 (long)written, (unsigned)sizeof(test_data),
                 (int)write_errno, strerror(write_errno));
        unlink(test_path);
        return ESP_FAIL;
    }

    if (close_result != 0) {
        ESP_LOGE(TAG, "close() after write failed: errno=%d reason=%s",
                 errno, strerror(errno));
        unlink(test_path);
        return ESP_FAIL;
    }

    fd = open(test_path, O_RDONLY, 0);
    if (fd < 0) {
        ESP_LOGE(TAG, "open() read failed: path=%s, errno=%d, reason=%s",
                 test_path, errno, strerror(errno));
        unlink(test_path);
        return ESP_FAIL;
    }

    ssize_t received = read(fd, readback, sizeof(readback));
    int read_errno = errno;
    close(fd);
    unlink(test_path);

    if (received != (ssize_t)sizeof(test_data)) {
        ESP_LOGE(TAG, "read() failed/short read: received=%ld expected=%u errno=%d reason=%s",
                 (long)received, (unsigned)sizeof(test_data),
                 (int)read_errno, strerror(read_errno));
        return ESP_FAIL;
    }

    if (memcmp(readback, test_data, sizeof(test_data)) != 0) {
        ESP_LOGE(TAG, "Internal flash filesystem read-back data mismatch.");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Internal flash filesystem POSIX read/write test passed.");
    return ESP_OK;
}

esp_err_t flash_storage_get_free_space_mb(uint32_t *out_free_mb)
{
    if (!s_is_mounted || out_free_mb == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    uint64_t total_bytes = 0;
    uint64_t free_bytes = 0;
    esp_err_t ret = esp_vfs_fat_info(FLASH_MOUNT_POINT, &total_bytes, &free_bytes);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Could not query internal flash filesystem space: %s",
                 esp_err_to_name(ret));
        return ret;
    }

    (void)total_bytes;
    *out_free_mb = (uint32_t)(free_bytes / (1024ULL * 1024ULL));
    return ESP_OK;
}
