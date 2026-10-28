#include "pulse.h"
#include "ads131m08.h"

#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

static const char *TAG = "pulse";

static volatile uint32_t pulse_counts[4] = {0};
static uint32_t pulse_last_edge_time[4] = {0};

static void IRAM_ATTR pulse_isr_handler(void *arg)
{
    int ch = (int)arg;
    uint32_t now = xTaskGetTickCountFromISR();
    if ((now - pulse_last_edge_time[ch]) > pdMS_TO_TICKS(5)) {
        pulse_counts[ch]++;
        pulse_last_edge_time[ch] = now;
    }
}

void pulse_init(void)
{
    gpio_config_t io_conf = {
        .intr_type = GPIO_INTR_POSEDGE,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
    };

    io_conf.pin_bit_mask = (1ULL << PULSE_INPUT_1) | (1ULL << PULSE_INPUT_2) |
                           (1ULL << PULSE_INPUT_3) | (1ULL << PULSE_INPUT_4);
    gpio_config(&io_conf);

    gpio_isr_handler_add(PULSE_INPUT_1, pulse_isr_handler, (void*)0);
    gpio_isr_handler_add(PULSE_INPUT_2, pulse_isr_handler, (void*)1);
    gpio_isr_handler_add(PULSE_INPUT_3, pulse_isr_handler, (void*)2);
    gpio_isr_handler_add(PULSE_INPUT_4, pulse_isr_handler, (void*)3);

    ESP_LOGI(TAG, "Pulse inputs initialized");
}

void pulse_get_counts(uint32_t out[4])
{
    if (!out) return;
    for (int i = 0; i < 4; i++) out[i] = pulse_counts[i];
}

static void task_pulse_data_collection(void *arg)
{
    while (1) {
        if (adc_log_enabled) {
            ESP_LOGI(TAG, "Pulses: %lu %lu %lu %lu",
                pulse_counts[0], pulse_counts[1], pulse_counts[2], pulse_counts[3]);
        }
        vTaskDelay(pdMS_TO_TICKS(10000));
    }
}

void pulse_start_task(void)
{
    xTaskCreate(task_pulse_data_collection, "pulse_collect", 4096, NULL, 5, NULL);
}
