/**
 * BEMS RTU - ESP32-S3
 *
 * Thin main: only app_main orchestration.
 * Feature code lives in separate modules (ads131m08, pulse, pca9555, eth_app, webserver, console, modbus_rtu, board).
 */

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "driver/gpio.h"
#include <stdio.h>

#include "project.h"
#include "board.h"
#include "eth_w5500.h"
#include "eth_app.h"
#include "webserver.h"
#include "pulse.h"
#include "pca9555.h"
#include "ads131m08.h"
#include "modbus_rtu.h"
#include "console.h"

static const char *TAG = "RTU_MAIN";

#if PP_ENABLE_MODBUS
static void rtu_modbus_snapshot(modbus_meas_t *out)
{
    if (!out) return;

    float vrms[3], irms[3], kw[3], pf[3];
    ads131m08_get_power(vrms, irms, kw, pf, NULL);

    float kvar[3], kva[3], kwh[3], freq;
    ads131m08_get_power_ext(kvar, kva, kwh, &freq);

    for (int p = 0; p < 3; p++) {
        out->vrms[p] = vrms[p];
        out->irms[p] = irms[p];
        out->kw[p]   = kw[p];
        out->kvar[p] = kvar[p];
        out->kva[p]  = kva[p];
        out->pf[p]   = pf[p];
        out->kwh[p]  = kwh[p];
    }
    out->freq_hz = freq;

    pulse_get_counts(out->pulse);

    int32_t adc1[8], adc2[8];
    ads131m08_get_avg(adc1, adc2);
    for (int i = 0; i < 8; i++) {
        out->adc1[i] = adc1[i];
        out->adc2[i] = adc2[i];
    }
}
#endif

#if PP_ENABLE_HEARTBEAT
static void task_heartbeat(void *arg)
{
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(10000));
    }
}
#endif

void app_main(void)
{
    ESP_LOGI(TAG, "BEMS RTU starting...");

    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    webserver_nvs_load();

    gpio_install_isr_service(0);

    board_init_gpios();

#if PP_ENABLE_RELAY
    pca9555_init();
#endif

#if PP_ENABLE_PULSE_INPUTS
    pulse_init();
    pulse_start_task();
#endif

#if PP_ENABLE_RS485
    board_init_rs485(RS485_UART_NUM_1, RS485_TX_PIN_1, RS485_RX_PIN_1, RS485_DE_RE_PIN_1);
    board_init_rs485(RS485_UART_NUM_2, RS485_TX_PIN_2, RS485_RX_PIN_2, RS485_DE_RE_PIN_2);
#endif

    board_init_spi2();

#if PP_ENABLE_ETHERNET
    esp_eth_handle_t eth_handle;
    ESP_ERROR_CHECK(w5500_init(&eth_handle));
    eth_app_init(eth_handle);
#else
    ESP_LOGI(TAG, "Ethernet disabled (PP_ENABLE_ETHERNET=0)");
#endif

#if PP_ENABLE_RS485 && PP_ENABLE_MODBUS
    // After TCP/IP stack so Modbus TCP socket() succeeds
    modbus_start(RS485_UART_NUM_1, RS485_DE_RE_PIN_1,
                 RS485_UART_NUM_2, RS485_DE_RE_PIN_2,
                 rtu_modbus_snapshot);
#endif

#if PP_ENABLE_ADS131M08
    // Restore common channel modes and current source selection from NVS
    {
        nvs_handle_t h;
        if (nvs_open("rtu_cfg", NVS_READONLY, &h) == ESP_OK) {
            for (int c = 1; c <= 6; c++) {
                char key[8];
                snprintf(key, sizeof(key), "cm%d", c);
                uint8_t val = 0;
                if (nvs_get_u8(h, key, &val) == ESP_OK) {
                    di_sel_t di = (di_sel_t)((val >> 4) & 0xF);
                    cv_sel_t cv = (cv_sel_t)(val & 0xF);
                    pca9555_config_common((common_sel_t)c, di, cv, R_SEL_49K9);
                    ESP_LOGI(TAG, "Restored common%d: di=%d cv=%d", c, di, cv);
                }
            }
            nvs_close(h);
        }
    }
    ads131m08_nvs_load();
    ads131m08_init();
    ads131m08_start_tasks();
#endif

#if PP_ENABLE_HEARTBEAT
    xTaskCreate(task_heartbeat, "heartbeat", 4096, NULL, 2, NULL);
#endif

    console_start();

    ESP_LOGI(TAG, "RTU ready......");
}
