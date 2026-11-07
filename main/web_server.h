#pragma once
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Call after Wi-Fi AP and flash filesystem are both initialized and tested. */
esp_err_t web_server_start(void);

#ifdef __cplusplus
}
#endif
