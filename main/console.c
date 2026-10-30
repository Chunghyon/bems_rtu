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
    ESP_LOGI(TAG, "  z/Z : toggle phase log");
    ESP_LOGI(TAG, "  p/P : set web period (1~300s, NVS)");
    ESP_LOGI(TAG, "  c/C : manual ADC calibration");
    ESP_LOGI(TAG, "  m/M : set phase mode (1=single, 3=three, NVS)");
    ESP_LOGI(TAG, "  k/K : config common channel (c/v/d, NVS)");
    ESP_LOGI(TAG, "  i/I : set current source channels (0-15, NVS)");
    ESP_LOGI(TAG, "  v/V : set voltage source channels (0-15, NVS)");
    ESP_LOGI(TAG, "  /   : show this help");
    ESP_LOGI(TAG, "========================");
}

static bool read_line_esc(char *buf, int buflen)
{
    int idx = 0;
    printf("> ");
    fflush(stdout);
    while (1) {
        int ch = getchar();
        if (ch == EOF) {
            vTaskDelay(pdMS_TO_TICKS(20));
            continue;
        }
        if (ch == 0x1B) {
            putchar('\n');
            fflush(stdout);
            ESP_LOGI(TAG, "Cancelled");
            return false;
        }
        if (ch == '\n' || ch == '\r') {
            putchar('\n');
            fflush(stdout);
            buf[idx] = '\0';
            return true;
        }
        if (ch == 0x08 || ch == 0x7F) {
            if (idx > 0) {
                idx--;
                buf[idx] = '\0';
                printf("\b \b");
                fflush(stdout);
            }
            continue;
        }
        if (ch >= 32 && idx < buflen - 1) {
            buf[idx++] = (char)ch;
            putchar(ch);
            fflush(stdout);
        }
    }
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
                ESP_LOGI(TAG, "Current web update period: %d seconds", web_update_period_sec);
                ESP_LOGI(TAG, "Enter new period (1~300) or ESC to cancel:");
                char numbuf[8] = {0};
                if (!read_line_esc(numbuf, sizeof(numbuf))) break;
                if (numbuf[0] == '\0') {
                    ESP_LOGI(TAG, "No input. Period unchanged (%ds)", web_update_period_sec);
                    break;
                }
                int val = atoi(numbuf);
                if (val >= 1 && val <= 300) {
                    web_update_period_sec = val;
                    webserver_nvs_save_period(web_update_period_sec);
                    ESP_LOGI(TAG, "Web update period set to %d seconds (saved to NVS)", web_update_period_sec);
                } else {
                    ESP_LOGI(TAG, "Invalid value '%s' (must be 1~300). Period unchanged (%ds)", numbuf, web_update_period_sec);
                }
                break;
            }
            case 'c':
            case 'C':
                ESP_LOGI(TAG, "Starting manual calibration (measure channels)...");
                ads131m08_calibrate_measure(64);
                break;
            case 'm':
            case 'M': {
                power_phase_mode_t cur = ads131m08_get_phase_mode();
                ESP_LOGI(TAG, "Current phase mode: %s", (cur == POWER_MODE_SINGLE) ? "SINGLE" : "THREE");
                ESP_LOGI(TAG, "Enter 1 (single) or 3 (three), or ESC to cancel:");
                char buf[8] = {0};
                if (!read_line_esc(buf, sizeof(buf))) break;
                if (buf[0] == '1') {
                    ads131m08_set_phase_mode(POWER_MODE_SINGLE);
                } else if (buf[0] == '3') {
                    ads131m08_set_phase_mode(POWER_MODE_THREE);
                } else {
                    ESP_LOGI(TAG, "Invalid input. Mode unchanged.");
                }
                break;
            }
            case 'z':
            case 'Z':
                phase_log_enabled = !phase_log_enabled;
                ESP_LOGI(TAG, "Phase logging %s", phase_log_enabled ? "ENABLED" : "DISABLED");
                break;
            case 'k':
            case 'K': {
                ESP_LOGI(TAG, "=== Common Channel Settings ===");
                nvs_handle_t h;
                if (nvs_open("rtu_cfg", NVS_READONLY, &h) == ESP_OK) {
                    for (int c = 1; c <= 6; c++) {
                        char key[8];
                        snprintf(key, sizeof(key), "cm%d", c);
                        uint8_t val = 0;
                        const char *desc = "(not set)";
                        if (nvs_get_u8(h, key, &val) == ESP_OK) {
                            di_sel_t di = (di_sel_t)((val >> 4) & 0xF);
                            cv_sel_t cv = (cv_sel_t)(val & 0xF);
                            if (di == DI_SEL_DIGITAL) desc = "DI";
                            else if (cv == CV_SEL_VOLTAGE) desc = "VOLTAGE";
                            else desc = "CURRENT";
                        }
                        ESP_LOGI(TAG, "  common%d : %s", c, desc);
                    }
                    nvs_close(h);
                } else {
                    ESP_LOGI(TAG, "  (no NVS data)");
                }
                ESP_LOGI(TAG, "Enter '<1-6> <c|v|d>' or ESC to cancel");
                char buf[16] = {0};
                if (!read_line_esc(buf, sizeof(buf))) break;
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
            case 'i':
            case 'I': {
                int c0, c1, c2;
                ads131m08_get_current_channels(&c0, &c1, &c2);
                ESP_LOGI(TAG, "Current source channels: %d, %d, %d", c0, c1, c2);
                ESP_LOGI(TAG, "Enter 3 channel codes (0-7=ADC1, 8-15=ADC2, -1=unused) or ESC to cancel");
                ESP_LOGI(TAG, "Example: 0 1 8");
                char buf[32] = {0};
                if (!read_line_esc(buf, sizeof(buf))) break;
                if (sscanf(buf, "%d %d %d", &c0, &c1, &c2) >= 1) {
                    ads131m08_set_current_source(CURRENT_SRC_CUSTOM, c0, c1, c2);
                    ESP_LOGI(TAG, "Current source set to channels %d, %d, %d (saved to NVS)", c0, c1, c2);
                } else {
                    ESP_LOGI(TAG, "Invalid input. Unchanged.");
                }
                break;
            }
            case 'v':
            case 'V': {
                int v0, v1, v2;
                ads131m08_get_voltage_channels(&v0, &v1, &v2);
                ESP_LOGI(TAG, "Voltage source channels: %d, %d, %d", v0, v1, v2);
                ESP_LOGI(TAG, "Enter 3 channel codes (0-7=ADC1, 8-15=ADC2, -1=unused) or ESC to cancel");
                ESP_LOGI(TAG, "Example: 3 4 5");
                char buf[32] = {0};
                if (!read_line_esc(buf, sizeof(buf))) break;
                if (sscanf(buf, "%d %d %d", &v0, &v1, &v2) >= 1) {
                    ads131m08_set_voltage_channels(v0, v1, v2);
                    ESP_LOGI(TAG, "Voltage source set to channels %d, %d, %d (saved to NVS)", v0, v1, v2);
                } else {
                    ESP_LOGI(TAG, "Invalid input. Unchanged.");
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
                ESP_LOGI(TAG, "Unknown key '%c' (0x%02X). Use a,w,p,c,m,z,k,i,v or / for help", c, c);
                break;
        }
    }
}

void console_start(void)
{
    xTaskCreate(task_console, "console", 4096, NULL, 3, NULL);
}
