#include "ads131m08.h"
#include "rtu_pins.h"

#include <string.h>
#include <math.h>
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_rom_sys.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_log.h"

static const char *TAG = "ads131";

bool adc_log_enabled = false;

static const char * const adc1_ch_names[ADC1_CH_COUNT] = {
    "ADC1_CH0 (AC_CURRENT1)",
    "ADC1_CH1 (AC_CURRENT2)",
    "ADC1_CH2 (AC_CURRENT3)",
    "ADC1_CH3 (AC_VOLTAGE1)",
    "ADC1_CH4 (AC_VOLTAGE2)",
    "ADC1_CH5 (AC_VOLTAGE3)",
    "ADC1_CH6 (COMMON1)",
    "ADC1_CH7 (COMMON2)"
};

static const char * const adc2_ch_names[ADC2_CH_COUNT] = {
    "ADC2_CH0 (COMMON3)",
    "ADC2_CH1 (COMMON4)",
    "ADC2_CH2 (COMMON5)",
    "ADC2_CH3 (COMMON6)",
    "ADC2_CH4 (RESERVED1)",
    "ADC2_CH5 (RESERVED2)",
    "ADC2_CH6 (RESERVED3)",
    "ADC2_CH7 (RESERVED4)"
};

static spi_device_handle_t adc1_spi = NULL;
static spi_device_handle_t adc2_spi = NULL;

// ADS131M08: 10-word frame (status + 8ch + CRC), 24-bit words
#define ADS131M08_FRAME_WORDS   10
#define ADS131M08_WORD_BITS     24
#define ADS131M08_FRAME_BYTES   (3 * ADS131M08_FRAME_WORDS)

static void ads131m08_reset(void)
{
    gpio_set_level(ADC_SYNC_PIN, 0);
    vTaskDelay(pdMS_TO_TICKS(2));
    gpio_set_level(ADC_SYNC_PIN, 1);
    vTaskDelay(pdMS_TO_TICKS(5));
    ESP_LOGI(TAG, "ADS131M08 reset pulse sent");
}

static void ads131m08_transfer_frame(spi_device_handle_t dev, gpio_num_t cs, const uint8_t *tx, uint8_t *rx)
{
    spi_transaction_t t = {
        .length = ADS131M08_WORD_BITS * ADS131M08_FRAME_WORDS,
        .tx_buffer = tx,
        .rx_buffer = rx,
    };
    gpio_set_level(cs, 0);
    esp_rom_delay_us(2);
    spi_device_transmit(dev, &t);
    esp_rom_delay_us(2);
    gpio_set_level(cs, 1);
}

static void ads131m08_write_reg(spi_device_handle_t dev, gpio_num_t cs, uint8_t addr, uint16_t val)
{
    uint8_t tx[ADS131M08_FRAME_BYTES] = {0};
    uint8_t rx[ADS131M08_FRAME_BYTES] = {0};

    uint16_t cmd = 0x6000 | ((addr & 0x3F) << 7);
    tx[0] = (cmd >> 8) & 0xFF;
    tx[1] = cmd & 0xFF;
    tx[2] = 0x00;

    tx[3] = (val >> 8) & 0xFF;
    tx[4] = val & 0xFF;
    tx[5] = 0x00;

    ads131m08_transfer_frame(dev, cs, tx, rx);
    esp_rom_delay_us(50);
}

static uint16_t ads131m08_read_reg(spi_device_handle_t dev, gpio_num_t cs, uint8_t addr)
{
    uint8_t tx[ADS131M08_FRAME_BYTES] = {0};
    uint8_t rx[ADS131M08_FRAME_BYTES] = {0};

    uint16_t cmd = 0xA000 | ((addr & 0x3F) << 7);
    tx[0] = (cmd >> 8) & 0xFF;
    tx[1] = cmd & 0xFF;
    tx[2] = 0x00;
    ads131m08_transfer_frame(dev, cs, tx, rx);
    esp_rom_delay_us(50);

    memset(tx, 0, sizeof(tx));
    ads131m08_transfer_frame(dev, cs, tx, rx);

    return ((uint16_t)rx[0] << 8) | rx[1];
}

static void ads131m08_unlock(spi_device_handle_t dev, gpio_num_t cs)
{
    uint8_t tx[ADS131M08_FRAME_BYTES] = {0};
    uint8_t rx[ADS131M08_FRAME_BYTES] = {0};
    tx[0] = 0x06;
    tx[1] = 0x55;
    tx[2] = 0x00;
    ads131m08_transfer_frame(dev, cs, tx, rx);
    esp_rom_delay_us(50);
}

static void ads131m08_clear_fifo(spi_device_handle_t dev, gpio_num_t cs)
{
    uint8_t tx[ADS131M08_FRAME_BYTES] = {0};
    uint8_t rx[ADS131M08_FRAME_BYTES] = {0};
    ads131m08_transfer_frame(dev, cs, tx, rx);
    ads131m08_transfer_frame(dev, cs, tx, rx);
}

static uint16_t ads131m08_osr_to_clock(uint16_t osr, bool channels_on)
{
    uint16_t osr_code = 5;
    switch (osr) {
        case 128:   osr_code = 0; break;
        case 256:   osr_code = 1; break;
        case 512:   osr_code = 2; break;
        case 1024:  osr_code = 3; break;
        case 2048:  osr_code = 4; break;
        case 4096:  osr_code = 5; break;
        case 8192:  osr_code = 6; break;
        case 16384: osr_code = 7; break;
        default:    osr_code = 5; break;
    }
    uint16_t val = (osr_code << 2);
    if (channels_on) val |= 0xFF00;
    return val;
}

static int32_t adc1_offset[ADS131M08_NUM_CH] = {0};
static int32_t adc2_offset[ADS131M08_NUM_CH] = {0};

static int32_t adc1_latest[ADS131M08_NUM_CH] = {0};
static int64_t adc1_sum[ADS131M08_NUM_CH] = {0};
static uint32_t adc1_sample_count = 0;
static int32_t adc1_avg[ADS131M08_NUM_CH] = {0};

static int32_t adc2_latest[ADS131M08_NUM_CH] = {0};
static int64_t adc2_sum[ADS131M08_NUM_CH] = {0};
static uint32_t adc2_sample_count = 0;
static int32_t adc2_avg[ADS131M08_NUM_CH] = {0};

static SemaphoreHandle_t drdy_sem = NULL;

#define POWER_PHASES        3
#define POWER_RING_SIZE     2048
#define POWER_SPS           (8192000 / (2 * PP_ADC_OSR))

typedef struct {
    int32_t v[POWER_PHASES];
    int32_t i[POWER_PHASES];
} power_sample_t;

static power_sample_t power_ring[POWER_RING_SIZE];
static volatile uint32_t power_wr = 0;

typedef struct {
    float vrms;
    float irms;
    float kw;       // active power
    float kvar;     // reactive power
    float kva;      // apparent power
    float pf;
    float kwh;      // energy accumulator
    uint32_t n;
} power_phase_t;

static power_phase_t power_result[POWER_PHASES];
static uint32_t power_sps_meas = 0;
static float freq_hz = 0.0f;   // estimated from phase 1 voltage zero-crossing

static volatile bool adc_paused = false;

static void IRAM_ATTR drdy_isr_handler(void *arg)
{
    BaseType_t hp_task_woken = pdFALSE;
    xSemaphoreGiveFromISR(drdy_sem, &hp_task_woken);
    if (hp_task_woken) {
        portYIELD_FROM_ISR();
    }
}

static void ads131m08_read_frame(spi_device_handle_t dev, gpio_num_t cs, int32_t *channels)
{
    uint8_t tx[ADS131M08_FRAME_BYTES] = {0};
    uint8_t rx[ADS131M08_FRAME_BYTES] = {0};

    ads131m08_transfer_frame(dev, cs, tx, rx);

    const int32_t *offsets = (cs == ADC1_SPI_CS) ? adc1_offset : adc2_offset;

    for (int ch = 0; ch < ADS131M08_NUM_CH; ch++) {
        int offset = 3 + ch * 3;
        int32_t raw = ((int32_t)rx[offset] << 16) |
                      ((int32_t)rx[offset + 1] << 8) |
                      rx[offset + 2];
        if (raw & 0x800000) {
            raw |= 0xFF000000;
        }
        channels[ch] = raw - offsets[ch];
    }
}

static void ads131m08_calibrate_reserved(uint16_t samples)
{
    if (samples == 0) return;

    int64_t sum[4] = {0};
    int32_t ch[ADS131M08_NUM_CH];

    for (int c = 4; c < 8; c++) adc2_offset[c] = 0;

    for (uint16_t i = 0; i < samples; i++) {
        ads131m08_read_frame(adc2_spi, ADC2_SPI_CS, ch);
        for (int c = 0; c < 4; c++) sum[c] += ch[4 + c];
        vTaskDelay(pdMS_TO_TICKS(5));
    }

    for (int c = 0; c < 4; c++) {
        adc2_offset[4 + c] = (int32_t)(sum[c] / samples);
    }

    ESP_LOGI(TAG, "RESERVED offset calibration done (%u samples)", samples);
    ESP_LOGI(TAG, "  RESERVED offsets: %ld %ld %ld %ld",
             adc2_offset[4], adc2_offset[5], adc2_offset[6], adc2_offset[7]);
}

void ads131m08_calibrate_measure(uint16_t samples)
{
    if (samples == 0) return;

    int64_t sum1[ADS131M08_NUM_CH] = {0};
    int64_t sum2[4] = {0};
    int32_t ch[ADS131M08_NUM_CH];

    adc_paused = true;
    vTaskDelay(pdMS_TO_TICKS(20));

    memset(adc1_offset, 0, sizeof(adc1_offset));
    for (int c = 0; c < 4; c++) adc2_offset[c] = 0;

    for (uint16_t i = 0; i < samples; i++) {
        ads131m08_read_frame(adc1_spi, ADC1_SPI_CS, ch);
        for (int c = 0; c < ADS131M08_NUM_CH; c++) sum1[c] += ch[c];

        ads131m08_read_frame(adc2_spi, ADC2_SPI_CS, ch);
        for (int c = 0; c < 4; c++) sum2[c] += ch[c];

        vTaskDelay(pdMS_TO_TICKS(5));
    }

    for (int c = 0; c < ADS131M08_NUM_CH; c++) {
        adc1_offset[c] = (int32_t)(sum1[c] / samples);
    }
    for (int c = 0; c < 4; c++) {
        adc2_offset[c] = (int32_t)(sum2[c] / samples);
    }

    adc_paused = false;

    ESP_LOGI(TAG, "Manual offset calibration done (%u samples)", samples);
    ESP_LOGI(TAG, "  ADC1 offsets: %ld %ld %ld %ld %ld %ld %ld %ld",
             adc1_offset[0], adc1_offset[1], adc1_offset[2], adc1_offset[3],
             adc1_offset[4], adc1_offset[5], adc1_offset[6], adc1_offset[7]);
    ESP_LOGI(TAG, "  ADC2 COMMON offsets: %ld %ld %ld %ld",
             adc2_offset[0], adc2_offset[1], adc2_offset[2], adc2_offset[3]);
}

void ads131m08_init(void)
{
    ads131m08_reset();
    vTaskDelay(pdMS_TO_TICKS(50));

    spi_device_interface_config_t adc_devcfg = {
        .command_bits = 0,
        .address_bits = 0,
        .dummy_bits = 0,
        .mode = 1,
        .clock_speed_hz = ADC_SPI_CLK_SPEED_HZ,
        .spics_io_num = -1,
        .queue_size = 4,
    };

    ESP_ERROR_CHECK(spi_bus_add_device(ADC_SPI_HOST, &adc_devcfg, &adc1_spi));
    ESP_ERROR_CHECK(spi_bus_add_device(ADC_SPI_HOST, &adc_devcfg, &adc2_spi));

    gpio_set_direction(ADC1_SPI_CS, GPIO_MODE_OUTPUT);
    gpio_set_level(ADC1_SPI_CS, 1);
    gpio_set_direction(ADC2_SPI_CS, GPIO_MODE_OUTPUT);
    gpio_set_level(ADC2_SPI_CS, 1);

    gpio_set_direction(ADC_SYNC_PIN, GPIO_MODE_OUTPUT);
    gpio_set_level(ADC_SYNC_PIN, 1);

    gpio_set_direction(ADC_DRDY_PIN, GPIO_MODE_INPUT);
    gpio_set_pull_mode(ADC_DRDY_PIN, GPIO_PULLUP_ONLY);

    ESP_LOGI(TAG, "ADS131M08 SPI devices added (ADC1 CS=%d, ADC2 CS=%d, DRDY=%d, SYNC=%d)",
             ADC1_SPI_CS, ADC2_SPI_CS, ADC_DRDY_PIN, ADC_SYNC_PIN);

    ads131m08_unlock(adc1_spi, ADC1_SPI_CS);
    ads131m08_unlock(adc2_spi, ADC2_SPI_CS);

    uint16_t id1 = ads131m08_read_reg(adc1_spi, ADC1_SPI_CS, 0x00);
    uint16_t id2 = ads131m08_read_reg(adc2_spi, ADC2_SPI_CS, 0x00);
    ESP_LOGI(TAG, "ADS131M08 ID: ADC1=0x%04X  ADC2=0x%04X", id1, id2);

    uint16_t clk_off = ads131m08_osr_to_clock(PP_ADC_OSR, false);
    uint16_t clk_on  = ads131m08_osr_to_clock(PP_ADC_OSR, true);

    ads131m08_write_reg(adc1_spi, ADC1_SPI_CS, 0x03, clk_off);
    ads131m08_write_reg(adc2_spi, ADC2_SPI_CS, 0x03, clk_off);

    ads131m08_write_reg(adc1_spi, ADC1_SPI_CS, 0x02, 0x0110);
    ads131m08_write_reg(adc2_spi, ADC2_SPI_CS, 0x02, 0x0110);

    ads131m08_write_reg(adc1_spi, ADC1_SPI_CS, 0x03, clk_on);
    ads131m08_write_reg(adc2_spi, ADC2_SPI_CS, 0x03, clk_on);

    ESP_LOGI(TAG, "ADS131M08 init done (OSR=%d)", PP_ADC_OSR);

    ads131m08_clear_fifo(adc1_spi, ADC1_SPI_CS);
    ads131m08_clear_fifo(adc2_spi, ADC2_SPI_CS);

    ads131m08_calibrate_reserved(64);

#if PP_ADC_USE_DRDY
    drdy_sem = xSemaphoreCreateBinary();
    gpio_set_intr_type(ADC_DRDY_PIN, GPIO_INTR_NEGEDGE);
    gpio_isr_handler_add(ADC_DRDY_PIN, drdy_isr_handler, NULL);
    ESP_LOGI(TAG, "ADS131M08 DRDY interrupt mode (GPIO%d, %d SPS)", ADC_DRDY_PIN, POWER_SPS);
#else
    ESP_LOGI(TAG, "ADS131M08 on-demand mode (no DRDY)");
#endif
}

static void task_ads131m08(void *arg)
{
    int32_t channels[ADS131M08_NUM_CH];
    uint32_t last_log_tick = xTaskGetTickCount();

    while (1) {
        if (adc_paused) {
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }
#if PP_ADC_USE_DRDY
        if (xSemaphoreTake(drdy_sem, pdMS_TO_TICKS(100)) != pdTRUE) {
            continue;
        }
#else
        vTaskDelay(pdMS_TO_TICKS(PP_ADC_SAMPLE_MS));
#endif

        ads131m08_read_frame(adc1_spi, ADC1_SPI_CS, channels);
        for (int ch = 0; ch < ADS131M08_NUM_CH; ch++) {
            adc1_sum[ch] += channels[ch];
            adc1_latest[ch] = channels[ch];
        }
        adc1_sample_count++;

        {
            power_sample_t s = {
                .v = {
                    channels[ADC1_AC_VOLTAGE1],
                    channels[ADC1_AC_VOLTAGE2],
                    channels[ADC1_AC_VOLTAGE3],
                },
                .i = {
                    channels[ADC1_AC_CURRENT1],
                    channels[ADC1_AC_CURRENT2],
                    channels[ADC1_AC_CURRENT3],
                },
            };
            power_ring[power_wr % POWER_RING_SIZE] = s;
            power_wr++;
        }

        ads131m08_read_frame(adc2_spi, ADC2_SPI_CS, channels);
        for (int ch = 0; ch < ADS131M08_NUM_CH; ch++) {
            adc2_sum[ch] += channels[ch];
            adc2_latest[ch] = channels[ch];
        }
        adc2_sample_count++;

        if ((xTaskGetTickCount() - last_log_tick) >= pdMS_TO_TICKS(1000)) {
            static bool red_on = false;
            red_on = !red_on;
            gpio_set_level(LED_STATUS_RED, red_on ? 1 : 0);

            if (adc1_sample_count > 0) {
                for (int ch = 0; ch < ADS131M08_NUM_CH; ch++) {
                    adc1_avg[ch] = (int32_t)(adc1_sum[ch] / adc1_sample_count);
                    adc1_sum[ch] = 0;
                }
                if (adc_log_enabled) {
                    ESP_LOGI(TAG, "  ==============================================");
                    for (int ch = 0; ch < ADS131M08_NUM_CH; ch++) {
                        ESP_LOGI(TAG, "  ADC1 %s: %ld", adc1_ch_names[ch], adc1_avg[ch]);
                        if (ch == 3) {
                            ESP_LOGI(TAG, "  ==============================================");
                        }
                    }
                }
                adc1_sample_count = 0;
            }
            if (adc2_sample_count > 0) {
                for (int ch = 0; ch < ADS131M08_NUM_CH; ch++) {
                    adc2_avg[ch] = (int32_t)(adc2_sum[ch] / adc2_sample_count);
                    adc2_sum[ch] = 0;
                }
                if (adc_log_enabled) {
                    for (int ch = 0; ch < ADS131M08_NUM_CH; ch++) {
                        ESP_LOGI(TAG, "  ADC2 %s: %ld", adc2_ch_names[ch], adc2_avg[ch]);
                        if (ch == 3) {
                            ESP_LOGI(TAG, "  ==============================================");
                        } else if (ch == 7) {
                            ESP_LOGI(TAG, "\n");
                        }
                    }
                }
                adc2_sample_count = 0;
            }

            last_log_tick = xTaskGetTickCount();
        }
    }
}

static void task_power_calc(void *arg)
{
    uint32_t last_wr = 0;
    uint32_t bems_acc_ms = 0;

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(PP_POWER_WINDOW_MS));

        uint32_t now = power_wr;
        uint32_t n = now - last_wr;
        if (n > POWER_RING_SIZE) n = POWER_RING_SIZE;
        power_sps_meas = (PP_POWER_WINDOW_MS > 0) ? (n * 1000 / PP_POWER_WINDOW_MS) : 0;

        if (n >= 8) {
            double sum_v2[POWER_PHASES] = {0};
            double sum_i2[POWER_PHASES] = {0};
            double sum_p[POWER_PHASES] = {0};
            int zc_count = 0;
            int32_t prev_v = 0;
            bool first = true;

            for (uint32_t k = now - n; k < now; k++) {
                const power_sample_t *s = &power_ring[k % POWER_RING_SIZE];
                for (int p = 0; p < POWER_PHASES; p++) {
                    double v = (double)s->v[p] * PP_V_PER_COUNT;
                    double i = (double)s->i[p] * PP_I_PER_COUNT;
                    sum_v2[p] += v * v;
                    sum_i2[p] += i * i;
                    sum_p[p] += v * i;
                }
                // Zero-crossing on phase 1 voltage for frequency
                int32_t v1 = s->v[0];
                if (!first && prev_v < 0 && v1 >= 0) zc_count++;
                prev_v = v1;
                first = false;
            }

            float dt_sec = (float)PP_POWER_WINDOW_MS / 1000.0f;
            if (zc_count >= 1 && dt_sec > 0) {
                freq_hz = (float)zc_count / dt_sec;
            }

            for (int p = 0; p < POWER_PHASES; p++) {
                float vrms = sqrtf((float)(sum_v2[p] / n));
                float irms = sqrtf((float)(sum_i2[p] / n));
                float p_w  = (float)(sum_p[p] / n);
                float s_va = vrms * irms;
                float q_var = 0.0f;
                if (s_va > fabsf(p_w)) {
                    q_var = sqrtf(s_va * s_va - p_w * p_w);
                    // Sign from lagging assumption (positive Q for inductive)
                    // Refine later with phase if needed
                }
                power_result[p].vrms = vrms;
                power_result[p].irms = irms;
                power_result[p].kw   = p_w / 1000.0f;
                power_result[p].kvar = q_var / 1000.0f;
                power_result[p].kva  = s_va / 1000.0f;
                power_result[p].pf   = (s_va > 1e-6f) ? (p_w / s_va) : 0.0f;
                power_result[p].n    = n;

                // Energy integration: kWh += kW * dt_hours
                power_result[p].kwh += power_result[p].kw * (dt_sec / 3600.0f);
            }
        }
        last_wr = now;

        bems_acc_ms += PP_POWER_WINDOW_MS;
        if (bems_acc_ms >= (uint32_t)PP_BEMS_PERIOD_SEC * 1000) {
            bems_acc_ms = 0;
            ESP_LOGI(TAG, "BEMS P1 Vrms=%.2f Irms=%.3f kW=%.3f PF=%.3f | "
                          "P2 %.2f/%.3f/%.3f/%.3f | P3 %.2f/%.3f/%.3f/%.3f (%lu sps)",
                     power_result[0].vrms, power_result[0].irms, power_result[0].kw, power_result[0].pf,
                     power_result[1].vrms, power_result[1].irms, power_result[1].kw, power_result[1].pf,
                     power_result[2].vrms, power_result[2].irms, power_result[2].kw, power_result[2].pf,
                     (unsigned long)power_sps_meas);
        }
    }
}

void ads131m08_start_tasks(void)
{
    xTaskCreate(task_ads131m08, "ads131m08", 4096, NULL, 8, NULL);
    xTaskCreate(task_power_calc, "power", 8192, NULL, 5, NULL);
}

void ads131m08_get_avg(int32_t adc1[ADS131M08_NUM_CH], int32_t adc2[ADS131M08_NUM_CH])
{
    if (adc1) memcpy(adc1, adc1_avg, sizeof(adc1_avg));
    if (adc2) memcpy(adc2, adc2_avg, sizeof(adc2_avg));
}

void ads131m08_get_power(float vrms[3], float irms[3], float kw[3], float pf[3], uint32_t *sps)
{
    for (int p = 0; p < 3; p++) {
        if (vrms) vrms[p] = power_result[p].vrms;
        if (irms) irms[p] = power_result[p].irms;
        if (kw)   kw[p]   = power_result[p].kw;
        if (pf)   pf[p]   = power_result[p].pf;
    }
    if (sps) *sps = power_sps_meas;
}

void ads131m08_get_power_ext(float kvar[3], float kva[3], float kwh[3], float *freq)
{
    for (int p = 0; p < 3; p++) {
        if (kvar) kvar[p] = power_result[p].kvar;
        if (kva)  kva[p]  = power_result[p].kva;
        if (kwh)  kwh[p]  = power_result[p].kwh;
    }
    if (freq) *freq = freq_hz;
}

const char *ads131m08_ch_name_adc1(int ch)
{
    if (ch < 0 || ch >= ADC1_CH_COUNT) return "?";
    return adc1_ch_names[ch];
}

const char *ads131m08_ch_name_adc2(int ch)
{
    if (ch < 0 || ch >= ADC2_CH_COUNT) return "?";
    return adc2_ch_names[ch];
}
