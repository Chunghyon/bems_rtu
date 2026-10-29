#include "project.h"
#include "board.h"
#include "console.h"
#include "ads131m08.h"
#include "webserver.h"
#include "eth_app.h"

#include <stdio.h>
#include <stdlib.h>
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

static const char *TAG = "console";

static void cmd_dummy_1(void)
{
    ESP_LOGI(TAG, "[DUMMY1] Test command 1 executed");
}

static void cmd_dummy_2(void)
{
    ESP_LOGI(TAG, "[DUMMY2] Test command 2 executed");
}

static void cmd_dummy_3(void)
{
    ESP_LOGI(TAG, "[DUMMY3] Test command 3 executed");
}

static void task_console(void *arg)
{
    ESP_LOGI(TAG, "Console ready. Commands: a, w, p, i, h, c, 1/2/3");
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
            case '1':
                cmd_dummy_1();
                break;
            case '2':
                cmd_dummy_2();
                break;
            case '3':
                cmd_dummy_3();
                break;
            case '\n':
            case '\r':
                break;
            default:
                ESP_LOGI(TAG, "Unknown key '%c' (0x%02X). Use a, w, p, i, h, c, 1, 2, 3", c, c);
                break;
        }
    }
}

void console_start(void)
{
    xTaskCreate(task_console, "console", 4096, NULL, 3, NULL);
}
