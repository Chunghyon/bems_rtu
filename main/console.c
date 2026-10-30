#include "project.h"
#include "board.h"
#include "console.h"
#include "ads131m08.h"
#include "webserver.h"
#include "eth_app.h"
#include "pca9555.h"

#include <stdio.h>
#include <stdlib.h>
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "nvs.h"

static const char *TAG = "console";

static void print_console_help(void)
{
    ESP_LOGI(TAG, "=== Console Commands ===");
    ESP_LOGI(TAG, "  a/A : toggle ADC log");
    ESP_LOGI(TAG, "  w/W : toggle web update");
    ESP_LOGI(TAG, "  p/P : set web period (1~300s, NVS)");
    ESP_LOGI(TAG, "  i/I : read ETH INT pin");
    ESP_LOGI(TAG, "  h/H : ETH recover");
    ESP_LOGI(TAG, "  c/C : manual ADC calibration");
    ESP_LOGI(TAG, "  s/S : single-phase mode");
    ESP_LOGI(TAG, "  t/T : three-phase mode");
    ESP_LOGI(TAG, "  m/M : show phase mode");
    ESP_LOGI(TAG, "  z/Z : toggle phase log");
    ESP_LOGI(TAG, "  k/K : config common channel (e.g. 1 c, 3 v, 5 d)  [c=current, v=voltage, d=DI]");
    ESP_LOGI(TAG, "  j/J : set current source channels (e.g. 0 1 8)  [0-7=ADC1, 8-15=ADC2]");
    ESP_LOGI(TAG, "  /   : show this help");
    ESP_LOGI(TAG, "========================");
}

static void task_console(void *arg)
{
    print_console_help();
    while (1) {
        int c = getchar();
        if (c == EOF) {
            vTaskDelay(pdMS_TO_TICKS(50));
            continue;
        }
        switch (c) {
            case 'a':
            case 'A':
                adc_log_enabled = !adc_log_enabled;
                ESP_LOGI(TAG, "ADC logging %s", adc_log_enabled ? "ENABLED" : "DISABLED");
                break;
            case 'w':
            case 'W':
                web_update_enabled = !web_update_enabled;
                ESP_LOGI(TAG, "Web update %s (%ds)", web_update_enabled ? "ENABLED" : "DISABLED", web_update_period_sec);
                break;
            case 'p':
            case 'P': {
                ESP_LOGI(TAG, "Enter web update period in seconds (1~300), then Enter:");
                char numbuf[8] = {0};
                int idx = 0;
                printf("> ");
                fflush(stdout);

                while (1) {
                    int ch = getchar();
                    if (ch == EOF) {
                        vTaskDelay(pdMS_TO_TICKS(20));
                        continue;
                    }
                    if (ch == '\n' || ch == '\r') {
                        putchar('\n');
                        fflush(stdout);
                        break;
                    }
                    if (ch == 0x08 || ch == 0x7F) {
                        if (idx > 0) {
                            idx--;
                            numbuf[idx] = '\0';
                            printf("\b \b");
                            fflush(stdout);
                        }
                        continue;
                    }
                    if (ch >= '0' && ch <= '9' && idx < (int)sizeof(numbuf) - 1) {
                        numbuf[idx++] = (char)ch;
                        numbuf[idx] = '\0';
                        putchar(ch);
                        fflush(stdout);
                    }
                }

                if (idx > 0) {
                    int val = atoi(numbuf);
                    if (val >= 1 && val <= 300) {
                        web_update_period_sec = val;
                        webserver_nvs_save_period(web_update_period_sec);
                        ESP_LOGI(TAG, "Web update period set to %d seconds (saved to NVS)", web_update_period_sec);
                    } else {
                        ESP_LOGI(TAG, "Invalid value '%s' (must be 1~300). Period unchanged (%ds)", numbuf, web_update_period_sec);
                    }
                } else {
                    ESP_LOGI(TAG, "No number entered. Period unchanged (%ds)", web_update_period_sec);
                }
                break;
            }
            case 'i':
            case 'I': {
                int level = gpio_get_level(ETH_INT_PIN);
                ESP_LOGI(TAG, "INT pin (GPIO%d) = %d (%s)", ETH_INT_PIN, level,
                         level ? "released/idle" : "asserted/LOW");
                break;
            }
            case 'h':
            case 'H':
                eth_app_recover();
                break;
            case 'c':
            case 'C':
                ESP_LOGI(TAG, "Starting manual calibration (measure channels)...");
                ads131m08_calibrate_measure(64);
                break;
            case 's':
            case 'S':
                ads131m08_set_phase_mode(POWER_MODE_SINGLE);
                break;
            case 't':
            case 'T':
                ads131m08_set_phase_mode(POWER_MODE_THREE);
                break;
            case 'm':
            case 'M':
                ESP_LOGI(TAG, "Current power mode: %s",
                         (ads131m08_get_phase_mode() == POWER_MODE_SINGLE) ? "SINGLE" : "THREE");
                break;
            case 'z':
            case 'Z':
                phase_log_enabled = !phase_log_enabled;
                ESP_LOGI(TAG, "Phase logging %s", phase_log_enabled ? "ENABLED" : "DISABLED");
                break;
            case 'k':
            case 'K': {
                ESP_LOGI(TAG, "Common channel config: enter '<1-6> <c|v|d>' then Enter  (c=current, v=voltage, d=DI)");
                printf("> ");
                fflush(stdout);
                char buf[16] = {0};
                int idx = 0;
                while (1) {
                    int ch = getchar();
                    if (ch == EOF) { vTaskDelay(pdMS_TO_TICKS(20)); continue; }
                    if (ch == '\n' || ch == '\r') { putchar('\n'); fflush(stdout); break; }
                    if (idx < (int)sizeof(buf)-1 && ch >= 32) { buf[idx++] = (char)ch; putchar(ch); fflush(stdout); }
                }
                int common = 0;
                char mode = 0;
                if (sscanf(buf, "%d %c", &common, &mode) == 2 && common >= 1 && common <= 6) {
                    cv_sel_t cv = CV_SEL_CURRENT;
                    di_sel_t di = DI_SEL_ANALOG;
                    if (mode == 'v' || mode == 'V') cv = CV_SEL_VOLTAGE;
                    else if (mode == 'd' || mode == 'D') { di = DI_SEL_DIGITAL; cv = CV_SEL_CURRENT; }
                    else if (mode != 'c' && mode != 'C') {
                        ESP_LOGI(TAG, "Invalid mode. Use c/v/d");
                        break;
                    }
                    if (common <= 2 && (mode == 'd' || mode == 'D')) {
                        ESP_LOGI(TAG, "common1~2 do not support DI");
                        break;
                    }
                    pca9555_config_common((common_sel_t)common, di, cv, R_SEL_49K9);
                    nvs_handle_t h;
                    if (nvs_open("rtu_cfg", NVS_READWRITE, &h) == ESP_OK) {
                        char key[8];
                        snprintf(key, sizeof(key), "cm%d", common);
                        uint8_t val = (uint8_t)((di << 4) | cv);
                        nvs_set_u8(h, key, val);
                        nvs_commit(h);
                        nvs_close(h);
                    }
                    ESP_LOGI(TAG, "common%d set to %s (saved to NVS)", common,
                             (mode=='c'||mode=='C') ? "CURRENT" : (mode=='v'||mode=='V') ? "VOLTAGE" : "DI");
                } else {
                    ESP_LOGI(TAG, "Format: <1-6> <c|v|d>");
                }
                break;
            }
            case 'j':
            case 'J': {
                ESP_LOGI(TAG, "Set current channels for 3 phases. Codes: 0-7=ADC1, 8-15=ADC2, -1=unused");
                ESP_LOGI(TAG, "Example: 0 1 8   then Enter");
                printf("> ");
                fflush(stdout);
                char buf[32] = {0};
                int idx = 0;
                while (1) {
                    int ch = getchar();
                    if (ch == EOF) { vTaskDelay(pdMS_TO_TICKS(20)); continue; }
                    if (ch == '\n' || ch == '\r') { putchar('\n'); fflush(stdout); break; }
                    if (idx < (int)sizeof(buf)-1 && ch >= 32) { buf[idx++] = (char)ch; putchar(ch); fflush(stdout); }
                }
                int c0 = -1, c1 = -1, c2 = -1;
                if (sscanf(buf, "%d %d %d", &c0, &c1, &c2) >= 1) {
                    ads131m08_set_current_source(CURRENT_SRC_CUSTOM, c0, c1, c2);
                    ESP_LOGI(TAG, "Current source set to channels %d, %d, %d (saved to NVS)", c0, c1, c2);
                }
                break;
            }
            case '/':
                print_console_help();
                break;
            case '\n':
            case '\r':
                break;
            default:
                ESP_LOGI(TAG, "Unknown key '%c' (0x%02X). Use a,w,p,i,h,c,s,t,m,z,k,j or / for help", c, c);
                break;
        }
    }
}

void console_start(void)
{
    xTaskCreate(task_console, "console", 4096, NULL, 3, NULL);
}
