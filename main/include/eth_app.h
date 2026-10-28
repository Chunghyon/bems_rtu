#pragma once

#include "esp_eth.h"
#include "rtu_pins.h"

#ifdef __cplusplus
extern "C" {
#endif

void eth_app_init(esp_eth_handle_t eth_handle);
void eth_app_recover(void);

#ifdef __cplusplus
}
#endif
