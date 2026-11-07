#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_log.h"
#include "nvs_flash.h"

#include "wifi_ap.h"

static const char *TAG = "WIFI_AP";

esp_err_t wifi_ap_start(void)
{
    esp_err_t ret = nvs_flash_init();

    if (ret == ESP_ERR_NVS_NO_FREE_PAGES ||
        ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }

    ESP_ERROR_CHECK(ret);

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    esp_netif_create_default_wifi_ap();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    wifi_config_t wifi_config = {
        .ap = {
            .ssid = "VoiceRecorder",
            .password = "12345678",
            .ssid_len = 0,
            .channel = 1,
            .max_connection = 2,
            .authmode = WIFI_AUTH_WPA2_PSK,
        },
    };

    ESP_ERROR_CHECK(
        esp_wifi_set_mode(WIFI_MODE_AP)
    );

    ESP_ERROR_CHECK(
        esp_wifi_set_config(WIFI_IF_AP, &wifi_config)
    );

    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "Wi-Fi hotspot started!");
    ESP_LOGI(TAG, "Network: RecDial");
    ESP_LOGI(TAG, "Password: 12345678");
    ESP_LOGI(TAG, "IP: 192.168.4.1");

    return ESP_OK;
}