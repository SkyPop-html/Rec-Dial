#pragma once
#include <stdint.h>
#include "esp_err.h"
esp_err_t oled_ui_init(void);
void oled_ui_show_ready(void);
void oled_ui_show_recording(uint32_t elapsed_seconds, const char *filename, uint8_t level);
void oled_ui_show_stopped(const char *filename);
void oled_ui_show_error(const char *message);
