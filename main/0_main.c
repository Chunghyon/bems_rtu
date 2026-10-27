/**
 * BEMS RTU - ESP32-S3 Implementation
 * 
 * Based on design report: 한국형_전력_수도_온수_태양광_RTU_ADC_설계_리포트_개정.docx
 * Architecture: Communication-first (RS-485/Modbus) + Pulse capture + Analog + (optional) high-speed power sampling
 * 
 * Features:
 * - W5500 Ethernet initialization (SPI)
 * - Basic HTTP web server showing status
 * - Pulse inputs (GPIO interrupt)
 * - RS-485 UART init
 * - Basic ADC
 * 
 * Pin mappings are in rtu_pins.h — update them to match the schematic.
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "esp_system.h"
#include "esp_log.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "driver/gpio.h"
#include "driver/uart.h"
#include "driver/spi_master.h"
#include "esp_rom_sys.h"
#include "driver/i2c_master.h"
#include "esp_eth.h"
#include "esp_eth_mac.h"
#include "esp_eth_phy.h"
#include "esp_http_server.h"

#include "rtu_pins.h"
#include "eth_w5500.h"

// ========== ADC Channel & PCA9555 Port Enums ==========
// ADC1 channels (ADS131M08 #1) — names linked to schematic nets / function
typedef enum {
    ADC1_AC_CURRENT1 = 0,   // AIN0
    ADC1_AC_CURRENT2,       // AIN1
    ADC1_AC_CURRENT3,       // AIN2
    ADC1_AC_VOLTAGE1,       // AIN3
    ADC1_AC_VOLTAGE2,       // AIN4
    ADC1_AC_VOLTAGE3,       // AIN5
    ADC1_COMMON1,           // AIN6
    ADC1_COMMON2,           // AIN7
    ADC1_CH_COUNT
} adc1_channel_t;

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

// ADC2 channels (ADS131M08 #2)
typedef enum {
    ADC2_COMMON3 = 0,       // AIN8
    ADC2_COMMON4,           // AIN9
    ADC2_COMMON5,           // AIN10
    ADC2_COMMON6,           // AIN11
    ADC2_RESERVED1,         // AIN12
    ADC2_RESERVED2,         // AIN13
    ADC2_RESERVED3,         // AIN14
    ADC2_RESERVED4,         // AIN15
    ADC2_CH_COUNT
} adc2_channel_t;

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

/******************************************************************
입력 ──[49.9 kΩ 릴레이]────[49.9 kΩ 고정]────[5 kΩ]── INA
            │                            │
        (릴레이로 short)               [10 kΩ]
                                         │
                                        GND
*******************************************************************/
// PCA9555PWR ports (16-bit expander, Port0=bits0-7, Port1=bits8-15)
// Used to drive relay coils for analog/pulse path selection
typedef enum {
    PCA_10V_SEL1 = 0,   // PCA_PORT0_BIT0, 0 : Low Voltage input, 1 : High Voltage input
    PCA_10V_SEL2,       // PCA_PORT0_BIT1, 0 : Low Voltage input, 1 : High Voltage input
    PCA_10V_SEL3,       // PCA_PORT0_BIT2, 0 : Low Voltage input, 1 : High Voltage input
    PCA_10V_SEL4,       // PCA_PORT0_BIT3, 0 : Low Voltage input, 1 : High Voltage input
    PCA_10V_SEL5,       // PCA_PORT0_BIT4, 0 : Low Voltage input, 1 : High Voltage input
    PCA_10V_SEL6,       // PCA_PORT0_BIT5, 0 : Low Voltage input, 1 : High Voltage input

    PCA_DI_SEL1,        // PCA_PORT0_BIT6, 0 : Pulse input, 1 : Analog input
    PCA_DI_SEL2,        // PCA_PORT0_BIT7, 0 : Pulse input, 1 : Analog input

    PCA_CUR_SEL1,       // PCA_PORT1_BIT0, 0 : Current input, 1 : Voltage input
    PCA_CUR_SEL2,       // PCA_PORT1_BIT1, 0 : Current input, 1 : Voltage input
    PCA_CUR_SEL3,       // PCA_PORT1_BIT2, 0 : Current input, 1 : Voltage input
    PCA_CUR_SEL4,       // PCA_PORT1_BIT3, 0 : Current input, 1 : Voltage input
    PCA_CUR_SEL5,       // PCA_PORT1_BIT4, 0 : Current input, 1 : Voltage input
    PCA_CUR_SEL6,       // PCA_PORT1_BIT5, 0 : Current input, 1 : Voltage input

    PCA_DI_SEL3,        // PCA_PORT1_BIT6, 0 : Pulse input, 1 : Analog input
    PCA_DI_SEL4,        // PCA_PORT1_BIT7, 0 : Pulse input, 1 : Analog input
    PCA_PORT_COUNT
} pca9555_port_t;

static const char * const pca_port_names[PCA_PORT_COUNT] = {
    "PCA_P0.0 (10V_SEL1)",
    "PCA_P0.1 (10V_SEL2)",
    "PCA_P0.2 (10V_SEL3)",
    "PCA_P0.3 (10V_SEL4)",
    "PCA_P0.4 (10V_SEL5)",
    "PCA_P0.5 (10V_SEL6)",
    "PCA_P0.6 (DI_SEL1)",
    "PCA_P0.7 (DI_SEL2)",
    "PCA_P1.0 (CUR_SEL1)",
    "PCA_P1.1 (CUR_SEL2)",
    "PCA_P1.2 (CUR_SEL3)",
    "PCA_P1.3 (CUR_SEL4)",
    "PCA_P1.4 (CUR_SEL5)",
    "PCA_P1.5 (CUR_SEL6)",
    "PCA_P1.6 (DI_SEL3)",
    "PCA_P1.7 (DI_SEL4)"
};

static const char *TAG = "RTU_MAIN";

static esp_eth_handle_t g_eth_handle = NULL;
static esp_netif_t *g_eth_netif = NULL;
static esp_eth_netif_glue_handle_t g_eth_glue = NULL;
static void start_webserver(void);
// ========== Pulse counters ==========
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

static void init_pulse_inputs(void)
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

// ========== RS-485 ==========
static void init_rs485(uart_port_t uart_num, int tx, int rx, int de_re)
{
    uart_config_t uart_config = {
        .baud_rate = 9600,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    uart_driver_install(uart_num, 1024, 1024, 0, NULL, 0);
    uart_param_config(uart_num, &uart_config);
    uart_set_pin(uart_num, tx, rx, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);

    if (de_re >= 0) {
        gpio_set_direction(de_re, GPIO_MODE_OUTPUT);
        gpio_set_level(de_re, 0);
    }
    ESP_LOGI(TAG, "RS-485 UART%d initialized", uart_num);
}

// Note: DRDY is shared on schematic but net to ADC2 was cut.
// Only ADC1 DRDY (ADC_DRDY_PIN) is used.
static spi_device_handle_t adc1_spi = NULL;
static spi_device_handle_t adc2_spi = NULL;

static void ads131m08_reset(void)
{
    // SYNC/RESET low for > 2 us, then high
    gpio_set_level(ADC_SYNC_PIN, 0);
    vTaskDelay(pdMS_TO_TICKS(2));
    gpio_set_level(ADC_SYNC_PIN, 1);
    vTaskDelay(pdMS_TO_TICKS(5));
    ESP_LOGI(TAG, "ADS131M08 reset pulse sent");
}

// ADS131M08: 10-word frame (status + 8ch + CRC), 24-bit words
// All SPI traffic MUST be a full 30-byte frame with CS held low the entire time.
#define ADS131M08_FRAME_WORDS   10
#define ADS131M08_WORD_BITS     24
#define ADS131M08_FRAME_BYTES   (3 * ADS131M08_FRAME_WORDS)

// Full 30-byte frame transfer with manual CS (matches reference library)
static void ads131m08_transfer_frame(spi_device_handle_t dev, gpio_num_t cs, const uint8_t *tx, uint8_t *rx)
{
    spi_transaction_t t = {
        .length = ADS131M08_WORD_BITS * ADS131M08_FRAME_WORDS,
        .tx_buffer = tx,
        .rx_buffer = rx,
    };
    gpio_set_level(cs, 0);
    esp_rom_delay_us(2);          // tCSS
    spi_device_transmit(dev, &t);
    esp_rom_delay_us(2);          // tCSH
    gpio_set_level(cs, 1);
}

// WREG: command + data in one continuous 30-byte frame
static void ads131m08_write_reg(spi_device_handle_t dev, gpio_num_t cs, uint8_t addr, uint16_t val)
{
    uint8_t tx[ADS131M08_FRAME_BYTES] = {0};
    uint8_t rx[ADS131M08_FRAME_BYTES] = {0};

    // Word 0: WREG command 011a aaaa a000 0000 (n=0 → write 1 register)
    uint16_t cmd = 0x6000 | ((addr & 0x3F) << 7);
    tx[0] = (cmd >> 8) & 0xFF;
    tx[1] = cmd & 0xFF;
    tx[2] = 0x00;

    // Word 1: register data, MSB-aligned (16-bit value in upper 16 bits of 24-bit word)
    tx[3] = (val >> 8) & 0xFF;
    tx[4] = val & 0xFF;
    tx[5] = 0x00;

    ads131m08_transfer_frame(dev, cs, tx, rx);
    esp_rom_delay_us(50);
}

// RREG: send command frame, then NULL frame to clock out the response
static uint16_t ads131m08_read_reg(spi_device_handle_t dev, gpio_num_t cs, uint8_t addr)
{
    uint8_t tx[ADS131M08_FRAME_BYTES] = {0};
    uint8_t rx[ADS131M08_FRAME_BYTES] = {0};

    // First frame: RREG command
    uint16_t cmd = 0xA000 | ((addr & 0x3F) << 7);
    tx[0] = (cmd >> 8) & 0xFF;
    tx[1] = cmd & 0xFF;
    tx[2] = 0x00;
    ads131m08_transfer_frame(dev, cs, tx, rx);
    esp_rom_delay_us(50);

    // Second frame: NULL (all zeros) → response appears in word 0
    memset(tx, 0, sizeof(tx));
    ads131m08_transfer_frame(dev, cs, tx, rx);

    return ((uint16_t)rx[0] << 8) | rx[1];
}

// UNLOCK command (required before register writes on some revisions)
static void ads131m08_unlock(spi_device_handle_t dev, gpio_num_t cs)
{
    uint8_t tx[ADS131M08_FRAME_BYTES] = {0};
    uint8_t rx[ADS131M08_FRAME_BYTES] = {0};
    // UNLOCK = 0x0655
    tx[0] = 0x06;
    tx[1] = 0x55;
    tx[2] = 0x00;
    ads131m08_transfer_frame(dev, cs, tx, rx);
    esp_rom_delay_us(50);
}

static void ads131m08_clear_fifo(spi_device_handle_t dev, gpio_num_t cs)
{
    // Datasheet: two dummy (NULL) frames clear the 2-deep FIFO
    uint8_t tx[ADS131M08_FRAME_BYTES] = {0};
    uint8_t rx[ADS131M08_FRAME_BYTES] = {0};
    ads131m08_transfer_frame(dev, cs, tx, rx);
    ads131m08_transfer_frame(dev, cs, tx, rx);
}

static uint16_t ads131m08_osr_to_clock(uint16_t osr, bool channels_on)
{
    // CLOCK[4:2] = OSR code
    uint16_t osr_code = 5;  // default 4096
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
    uint16_t val = (osr_code << 2);          // bits [4:2]
    if (channels_on) val |= 0xFF00;          // all channels enable
    return val;
}


#define ADS131M08_NUM_CH    8

// Per-channel offset (subtracted from every sample). Filled by ads131m08_calibrate().
static int32_t adc1_offset[ADS131M08_NUM_CH] = {0};
static int32_t adc2_offset[ADS131M08_NUM_CH] = {0};

static void ads131m08_calibrate_reserved(uint16_t samples);
static void ads131m08_calibrate_measure(uint16_t samples);

static void init_ads131m08(void)
{
    ads131m08_reset();
    vTaskDelay(pdMS_TO_TICKS(50));

    spi_device_interface_config_t adc_devcfg = {
        .command_bits = 0,
        .address_bits = 0,
        .dummy_bits = 0,
        .mode = 1,                          // ADS131M08 uses SPI mode 1
        .clock_speed_hz = ADC_SPI_CLK_SPEED_HZ,  // 8 MHz
        .spics_io_num = -1,                 // Manual CS (driver CS timing is unreliable in mode 1)
        .queue_size = 4,
    };

    // ADC1
    ESP_ERROR_CHECK(spi_bus_add_device(ADC_SPI_HOST, &adc_devcfg, &adc1_spi));

    // ADC2 (CS only; DRDY net was cut)
    ESP_ERROR_CHECK(spi_bus_add_device(ADC_SPI_HOST, &adc_devcfg, &adc2_spi));

    // Manual CS pins (active low, idle high)
    gpio_set_direction(ADC1_SPI_CS, GPIO_MODE_OUTPUT);
    gpio_set_level(ADC1_SPI_CS, 1);
    gpio_set_direction(ADC2_SPI_CS, GPIO_MODE_OUTPUT);
    gpio_set_level(ADC2_SPI_CS, 1);

    // SYNC / RESET pin
    gpio_set_direction(ADC_SYNC_PIN, GPIO_MODE_OUTPUT);
    gpio_set_level(ADC_SYNC_PIN, 1);

    // DRDY pin (ADC1 only)
    gpio_set_direction(ADC_DRDY_PIN, GPIO_MODE_INPUT);
    gpio_set_pull_mode(ADC_DRDY_PIN, GPIO_PULLUP_ONLY);

    ESP_LOGI(TAG, "ADS131M08 SPI devices added (ADC1 CS=%d, ADC2 CS=%d, DRDY=%d, SYNC=%d)",
             ADC1_SPI_CS, ADC2_SPI_CS, ADC_DRDY_PIN, ADC_SYNC_PIN);

    // UNLOCK interface so register writes are accepted
    ads131m08_unlock(adc1_spi, ADC1_SPI_CS);
    ads131m08_unlock(adc2_spi, ADC2_SPI_CS);

    // Verify communication with ID register (bits[15:12] should be 0x2)
    uint16_t id1 = ads131m08_read_reg(adc1_spi, ADC1_SPI_CS, 0x00);
    uint16_t id2 = ads131m08_read_reg(adc2_spi, ADC2_SPI_CS, 0x00);
    ESP_LOGI(TAG, "ADS131M08 ID: ADC1=0x%04X  ADC2=0x%04X", id1, id2);

    uint16_t clk_off = ads131m08_osr_to_clock(PP_ADC_OSR, false);
    uint16_t clk_on  = ads131m08_osr_to_clock(PP_ADC_OSR, true);

    // 1. CLOCK: channels off + OSR
    ads131m08_write_reg(adc1_spi, ADC1_SPI_CS, 0x03, clk_off);
    ads131m08_write_reg(adc2_spi, ADC2_SPI_CS, 0x03, clk_off);

    // 2. MODE: clear RESET, 24-bit word, SPI timeout enabled
    ads131m08_write_reg(adc1_spi, ADC1_SPI_CS, 0x02, 0x0110);
    ads131m08_write_reg(adc2_spi, ADC2_SPI_CS, 0x02, 0x0110);

    // 3. CLOCK: all channels on + OSR
    ads131m08_write_reg(adc1_spi, ADC1_SPI_CS, 0x03, clk_on);
    ads131m08_write_reg(adc2_spi, ADC2_SPI_CS, 0x03, clk_on);

    ESP_LOGI(TAG, "ADS131M08 init done (OSR=%d)", PP_ADC_OSR);

    // Clear FIFO so first reads are fresh
    ads131m08_clear_fifo(adc1_spi, ADC1_SPI_CS);
    ads131m08_clear_fifo(adc2_spi, ADC2_SPI_CS);

    // Boot: calibrate RESERVED channels only (hard-tied to GND)
    ads131m08_calibrate_reserved(64);

#if PP_ADC_USE_DRDY
    drdy_sem = xSemaphoreCreateBinary();
    gpio_set_intr_type(ADC_DRDY_PIN, GPIO_INTR_NEGEDGE);
    gpio_isr_handler_add(ADC_DRDY_PIN, drdy_isr_handler, NULL);
    ESP_LOGI(TAG, "ADS131M08 DRDY interrupt mode");
#else
    ESP_LOGI(TAG, "ADS131M08 on-demand mode (no DRDY)");
#endif
}

// ========== ADS131M08 DRDY interrupt + 1s average ==========
static int32_t adc1_latest[ADS131M08_NUM_CH] = {0};
static int64_t adc1_sum[ADS131M08_NUM_CH] = {0};
static uint32_t adc1_sample_count = 0;
static int32_t adc1_avg[ADS131M08_NUM_CH] = {0};

static int32_t adc2_latest[ADS131M08_NUM_CH] = {0};
static int64_t adc2_sum[ADS131M08_NUM_CH] = {0};
static uint32_t adc2_sample_count = 0;
static int32_t adc2_avg[ADS131M08_NUM_CH] = {0};
static SemaphoreHandle_t drdy_sem = NULL;

// Full 10-word frame, CS held for entire transaction
static void ads131m08_read_frame(spi_device_handle_t dev, gpio_num_t cs, int32_t *channels)
{
    uint8_t tx[ADS131M08_FRAME_BYTES] = {0};   // NULL command
    uint8_t rx[ADS131M08_FRAME_BYTES] = {0};

    ads131m08_transfer_frame(dev, cs, tx, rx);

    const int32_t *offsets = (cs == ADC1_SPI_CS) ? adc1_offset : adc2_offset;

    // word0 = status/response, word1-8 = channel data, word9 = CRC
    for (int ch = 0; ch < ADS131M08_NUM_CH; ch++) {
        int offset = 3 + ch * 3;
        int32_t raw = ((int32_t)rx[offset] << 16) |
                      ((int32_t)rx[offset + 1] << 8) |
                      rx[offset + 2];
        if (raw & 0x800000) {
            raw |= 0xFF000000;   // sign-extend 24-bit two's complement
        }
        channels[ch] = raw - offsets[ch];
    }
}

// Pause flag so the sampling task does not touch SPI during manual calibration
static volatile bool adc_paused = false;

// Boot-time: only RESERVED channels (ADC2 CH4-7, hard-tied to GND)
static void ads131m08_calibrate_reserved(uint16_t samples)
{
    if (samples == 0) return;

    int64_t sum[4] = {0};
    int32_t ch[ADS131M08_NUM_CH];

    // Clear only RESERVED offsets so we measure raw
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

// Manual: calibrate measurement channels (ADC1 all + ADC2 CH0-3)
// Call when those inputs are at their zero condition.
static void ads131m08_calibrate_measure(uint16_t samples)
{
    if (samples == 0) return;

    int64_t sum1[ADS131M08_NUM_CH] = {0};
    int64_t sum2[4] = {0};
    int32_t ch[ADS131M08_NUM_CH];

    // Pause background sampling to avoid SPI contention
    adc_paused = true;
    vTaskDelay(pdMS_TO_TICKS(20));

    // Clear offsets of channels we are about to calibrate (keep RESERVED)
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

static void IRAM_ATTR drdy_isr_handler(void *arg)
{
    BaseType_t hp_task_woken = pdFALSE;
    xSemaphoreGiveFromISR(drdy_sem, &hp_task_woken);
    if (hp_task_woken) {
        portYIELD_FROM_ISR();
    }
}

bool adc_log_enabled = false;
bool web_update_enabled = false;
int web_update_period_sec = 5;   // seconds (1~300)

// ---------- NVS persistence for web update period ----------
#define NVS_NAMESPACE   "rtu_cfg"
#define NVS_KEY_PERIOD  "web_period"

static void nvs_save_web_period(int period)
{
    nvs_handle_t handle;
    if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle) == ESP_OK) {
        nvs_set_i32(handle, NVS_KEY_PERIOD, period);
        nvs_commit(handle);
        nvs_close(handle);
    }
}

static void nvs_load_web_period(void)
{
    nvs_handle_t handle;
    int32_t val = 0;
    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle) == ESP_OK) {
        if (nvs_get_i32(handle, NVS_KEY_PERIOD, &val) == ESP_OK) {
            if (val >= 1 && val <= 300) {
                web_update_period_sec = (int)val;
                ESP_LOGI(TAG, "Loaded web update period from NVS: %d s", web_update_period_sec);
            }
        }
        nvs_close(handle);
    }
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
        // Wait for DRDY interrupt
        if (xSemaphoreTake(drdy_sem, pdMS_TO_TICKS(100)) != pdTRUE) {
            continue;
        }
#else
        // On-demand: read at fixed interval (no DRDY)
        vTaskDelay(pdMS_TO_TICKS(PP_ADC_SAMPLE_MS));
#endif

        ads131m08_read_frame(adc1_spi, ADC1_SPI_CS, channels);
        for (int ch = 0; ch < ADS131M08_NUM_CH; ch++) {
            adc1_sum[ch] += channels[ch];
            adc1_latest[ch] = channels[ch];
        }
        adc1_sample_count++;

        ads131m08_read_frame(adc2_spi, ADC2_SPI_CS, channels);
        for (int ch = 0; ch < ADS131M08_NUM_CH; ch++) {
            adc2_sum[ch] += channels[ch];
            adc2_latest[ch] = channels[ch];
        }
        adc2_sample_count++;

        // Compute 1s average always; log only if enabled
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
                    //ESP_LOGI(TAG, "ADC1 1s avg (%lu samples):", adc1_sample_count);
                    ESP_LOGI(TAG, "  ==============================================");
                    for (int ch = 0; ch < ADS131M08_NUM_CH; ch++) {
                        ESP_LOGI(TAG, "  ADC1 %s: %ld", adc1_ch_names[ch], adc1_avg[ch]);
                        if(ch == 2 || ch == 5) {
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
                    //ESP_LOGI(TAG, "ADC2 1s avg (%lu samples):", adc2_sample_count);
                    for (int ch = 0; ch < ADS131M08_NUM_CH; ch++) {
                        ESP_LOGI(TAG, "  ADC2 %s: %ld", adc2_ch_names[ch], adc2_avg[ch]);
                        if(ch == 3) {
                            ESP_LOGI(TAG, "  ==============================================");
                        }
                        else if(ch == 7) {
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

// ========== W5500 Ethernet ==========
static httpd_handle_t web_server = NULL;

static void stop_webserver(void)
{
    if (web_server) {
        httpd_stop(web_server);
        web_server = NULL;
        ESP_LOGI(TAG, "Web server stopped");
    }
}

/** Event handler for Ethernet events */
static void eth_event_handler(void *arg, esp_event_base_t event_base,
    int32_t event_id, void *event_data)
{
    uint8_t mac_addr[6] = {0};
    /* we can get the ethernet driver handle from event data */
    esp_eth_handle_t eth_handle = *(esp_eth_handle_t *)event_data;

    switch (event_id) {
    case ETHERNET_EVENT_CONNECTED:
        esp_eth_ioctl(eth_handle, ETH_CMD_G_MAC_ADDR, mac_addr);
        ESP_LOGI(TAG, "Ethernet Link Up");
        ESP_LOGI(TAG, "Ethernet HW Addr %02x:%02x:%02x:%02x:%02x:%02x",
        mac_addr[0], mac_addr[1], mac_addr[2], mac_addr[3], mac_addr[4], mac_addr[5]);
        break;
    case ETHERNET_EVENT_DISCONNECTED:
        ESP_LOGI(TAG, "Ethernet Link Down");
        break;
    case ETHERNET_EVENT_START:
        ESP_LOGI(TAG, "Ethernet Started");
        break;
    case ETHERNET_EVENT_STOP:
        ESP_LOGI(TAG, "Ethernet Stopped");
        break;
    default:
        break;
    }
}

/** Event handler for IP_EVENT_ETH_GOT_IP */
static void got_ip_event_handler(void *arg, esp_event_base_t event_base,
       int32_t event_id, void *event_data)
{
    ip_event_got_ip_t *event = (ip_event_got_ip_t *) event_data;
    const esp_netif_ip_info_t *ip_info = &event->ip_info;

    ESP_LOGI(TAG, "Ethernet Got IP Address");
    ESP_LOGI(TAG, "~~~~~~~~~~~");
    ESP_LOGI(TAG, "ETHIP:" IPSTR, IP2STR(&ip_info->ip));
    ESP_LOGI(TAG, "ETHMASK:" IPSTR, IP2STR(&ip_info->netmask));
    ESP_LOGI(TAG, "ETHGW:" IPSTR, IP2STR(&ip_info->gw));
    ESP_LOGI(TAG, "~~~~~~~~~~~");

    start_webserver();
}

static void init_spi2_host(void)
{
    // Always initialize SPI2 for ADC (fixed pins)
    spi_bus_config_t adc_buscfg = {
        .miso_io_num = ADC_SPI_MISO,
        .mosi_io_num = ADC_SPI_MOSI,
        .sclk_io_num = ADC_SPI_CLK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
    };
    ESP_ERROR_CHECK(spi_bus_initialize(SPI2_HOST, &adc_buscfg, SPI_DMA_CH_AUTO));
    ESP_LOGI(TAG, "ADC SPI bus ready (SPI2_HOST, MOSI=%d MISO=%d CLK=%d)",
             ADC_SPI_MOSI, ADC_SPI_MISO, ADC_SPI_CLK);
}

// ========== Simple Web Server ==========
static esp_err_t root_get_handler(httpd_req_t *req)
{
    // Use heap to avoid httpd stack overflow
    char *buf = malloc(2048);
    if (!buf) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    char refresh_tag[64] = "";
    char update_status[40] = "OFF";
    if (web_update_enabled && web_update_period_sec >= 1) {
        snprintf(refresh_tag, sizeof(refresh_tag),
                 "<meta http-equiv='refresh' content='%d'>", web_update_period_sec);
        snprintf(update_status, sizeof(update_status), "ON (%ds)", web_update_period_sec);
    }

    int len = snprintf(buf, 2048,
        "<!DOCTYPE html><html><head><meta charset='utf-8'>%s"
        "<title>BEMS RTU</title>"
        "<style>"
        "body{font-family:sans-serif;margin:1em;}"
        ".row{display:flex;gap:1em;flex-wrap:nowrap;}"
        ".box{border:1px solid #ccc;padding:0.5em 1em;flex:1;min-width:0;}"
        "table{border-collapse:collapse;width:100%%;}"
        "td,th{border:1px solid #ddd;padding:2px 6px;text-align:right;font-size:0.9em;}"
        "h1{margin:0 0 0.3em 0;font-size:1.3em;}"
        "h2{margin:0 0 0.3em 0;font-size:1em;}"
        "</style></head>"
        "<body><h1>BEMS RTU Status</h1>"
        "<p style='margin:0 0 0.5em 0;'><small>Web update: %s</small></p>"
        "<div class='row'>"
        "<div class='box'><h2>Pulse Counters</h2>"
        "<table><tr><th>CH</th><th>Count</th></tr>"
        "<tr><td>1</td><td>%lu</td></tr><tr><td>2</td><td>%lu</td></tr>"
        "<tr><td>3</td><td>%lu</td></tr><tr><td>4</td><td>%lu</td></tr></table></div>"
        "<div class='box'><h2>ADC1 (1s avg)</h2><table><tr><th>CH</th><th>Value</th></tr>",
        refresh_tag,
        update_status,
        pulse_counts[0], pulse_counts[1], pulse_counts[2], pulse_counts[3]);

    for (int ch = 0; ch < ADS131M08_NUM_CH && len < 2048 - 80; ch++) {
        len += snprintf(buf + len, 2048 - len,
                        "<tr><td>%s</td><td>%ld</td></tr>", adc1_ch_names[ch], adc1_avg[ch]);
    }
    len += snprintf(buf + len, 2048 - len, "</table></div>"
        "<div class='box'><h2>ADC2 (1s avg)</h2><table><tr><th>CH</th><th>Value</th></tr>");

    for (int ch = 0; ch < ADS131M08_NUM_CH && len < 2048 - 80; ch++) {
        len += snprintf(buf + len, 2048 - len,
                        "<tr><td>%s</td><td>%ld</td></tr>", adc2_ch_names[ch], adc2_avg[ch]);
    }
    len += snprintf(buf + len, 2048 - len,
        "</table></div></div>"
        "<p><small>ESP32-S3 RTU</small></p></body></html>");

    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, buf, HTTPD_RESP_USE_STRLEN);
    free(buf);
    return ESP_OK;
}

static void start_webserver(void)
{
    if (web_server) {
        ESP_LOGI(TAG, "Web server already running");
        return;
    }

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = 80;
    config.stack_size = 8192;

    if (httpd_start(&web_server, &config) == ESP_OK) {
        httpd_uri_t uri_get = {
            .uri = "/",
            .method = HTTP_GET,
            .handler = root_get_handler,
            .user_ctx = NULL
        };
        httpd_register_uri_handler(web_server, &uri_get);
        ESP_LOGI(TAG, "Web server started on port 80");
    } else {
        ESP_LOGE(TAG, "Failed to start web server");
        web_server = NULL;
    }
}

// ========== Periodic task ==========
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

// ========== I2C / PCA9555PWR (Relay control) ==========
// Address: base 0x20 + (A2<<2 | A1<<1 | A0) = 0x20 + 0x01 = 0x21
#define PCA9555_ADDR            0x21
#define PCA9555_REG_OUTPUT0     0x02
#define PCA9555_REG_OUTPUT1     0x03
#define PCA9555_REG_CONFIG0     0x06
#define PCA9555_REG_CONFIG1     0x07

static i2c_master_bus_handle_t i2c_bus = NULL;
static i2c_master_dev_handle_t pca9555_dev = NULL;

static esp_err_t pca9555_write_reg(uint8_t reg, uint8_t val)
{
    if (!pca9555_dev) return ESP_ERR_INVALID_STATE;
    uint8_t buf[2] = {reg, val};
    return i2c_master_transmit(pca9555_dev, buf, 2, pdMS_TO_TICKS(100));
}

static void init_i2c_pca9555(void)
{
    i2c_master_bus_config_t bus_cfg = {
        .i2c_port = I2C_NUM_0,
        .sda_io_num = I2C_SDA_PIN,
        .scl_io_num = I2C_SCL_PIN,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    ESP_ERROR_CHECK(i2c_new_master_bus(&bus_cfg, &i2c_bus));

    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = PCA9555_ADDR,
        .scl_speed_hz = 100000,
    };
    ESP_ERROR_CHECK(i2c_master_bus_add_device(i2c_bus, &dev_cfg, &pca9555_dev));

    // All pins as outputs (0 = output)
    ESP_ERROR_CHECK(pca9555_write_reg(PCA9555_REG_CONFIG0, 0x00));
    ESP_ERROR_CHECK(pca9555_write_reg(PCA9555_REG_CONFIG1, 0x00));

    // All relays off
    ESP_ERROR_CHECK(pca9555_write_reg(PCA9555_REG_OUTPUT0, 0x00));
    ESP_ERROR_CHECK(pca9555_write_reg(PCA9555_REG_OUTPUT1, 0x00));

    ESP_LOGI(TAG, "PCA9555PWR initialized (addr=0x%02X, new I2C master driver)", PCA9555_ADDR);
}

static volatile bool relay_test_enabled = false;

static void task_relay_test(void *arg)
{
    uint8_t relay = 0;
    while (1) {
        if (!relay_test_enabled) {
            // Ensure all relays off when disabled
            pca9555_write_reg(PCA9555_REG_OUTPUT0, 0x00);
            pca9555_write_reg(PCA9555_REG_OUTPUT1, 0x00);
            vTaskDelay(pdMS_TO_TICKS(200));
            continue;
        }

        uint8_t port0 = 0, port1 = 0;
        if (relay < 8) {
            port0 = (1 << relay);
        } else {
            port1 = (1 << (relay - 8));
        }

        pca9555_write_reg(PCA9555_REG_OUTPUT0, port0);
        pca9555_write_reg(PCA9555_REG_OUTPUT1, port1);

        ESP_LOGI(TAG, "Relay %d (%s) ON (port0=0x%02X port1=0x%02X)", 
                 relay, pca_port_names[relay], port0, port1);

        relay = (relay + 1) % 16;
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

// ========== Console command handlers ==========
static void cmd_relay_toggle(void)
{
    relay_test_enabled = !relay_test_enabled;
    ESP_LOGI(TAG, "Relay test %s", relay_test_enabled ? "ENABLED" : "DISABLED");
    if (!relay_test_enabled) {
        pca9555_write_reg(PCA9555_REG_OUTPUT0, 0x00);
        pca9555_write_reg(PCA9555_REG_OUTPUT1, 0x00);
    }
}

static void cmd_dummy_1(void)
{
    ESP_LOGI(TAG, "[DUMMY1] Test command 1 executed");
    static bool toggle = false;
    pca9555_write_reg(PCA9555_REG_OUTPUT0, toggle ? 0x55 : 0xAA);
    toggle = !toggle;
}

static void cmd_dummy_2(void)
{
    ESP_LOGI(TAG, "[DUMMY2] Test command 2 executed");
    static bool toggle = false;
    pca9555_write_reg(PCA9555_REG_OUTPUT1, toggle ? 0x55 : 0xAA);
    toggle = !toggle;
}

static void cmd_dummy_3(void)
{
    ESP_LOGI(TAG, "[DUMMY3] Test command 3 executed");
}

static void eth_full_recover(void)
{
    if (!g_eth_handle) {
        ESP_LOGE(TAG, "No Ethernet handle to recover");
        return;
    }

    // Tear down netif glue first so the old driver can be uninstalled
    if (g_eth_glue) {
        esp_eth_del_netif_glue(g_eth_glue);
        g_eth_glue = NULL;
    }
    if (g_eth_netif) {
        esp_netif_destroy(g_eth_netif);
        g_eth_netif = NULL;
    }

    esp_err_t err = w5500_reinstall_driver(&g_eth_handle);
    if (err != ESP_OK || !g_eth_handle) {
        ESP_LOGE(TAG, "Driver reinstall failed: %s", esp_err_to_name(err));
        return;
    }

    // Recreate netif + glue and start
    esp_netif_config_t cfg = ESP_NETIF_DEFAULT_ETH();
    g_eth_netif = esp_netif_new(&cfg);
    g_eth_glue = esp_eth_new_netif_glue(g_eth_handle);
    if (!g_eth_netif || !g_eth_glue) {
        ESP_LOGE(TAG, "Failed to recreate netif/glue");
        return;
    }
    ESP_ERROR_CHECK(esp_netif_attach(g_eth_netif, g_eth_glue));

    err = esp_eth_start(g_eth_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_eth_start after reinstall failed: %s", esp_err_to_name(err));
        return;
    }
    ESP_LOGI(TAG, "Full recover done. Wait for Link Up / IP...");
}

static void task_heartbeat(void *arg)
{
    uint32_t sec = 0;
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(10000));
        sec += 10;
        //ESP_LOGI(TAG, "[HB] uptime %lu s", (unsigned long)sec);
    }
}

static void task_console(void *arg)
{
    ESP_LOGI(TAG, "Console ready. Commands: r, a, w, p, i=INT pin, h=hard reset, c, 1/2/3");
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
                // Echo prompt
                printf("> ");
                fflush(stdout);

                while (1) {
                    int ch = getchar();
                    if (ch == EOF) {
                        vTaskDelay(pdMS_TO_TICKS(20));
                        continue;
                    }
                    // Enter
                    if (ch == '\n' || ch == '\r') {
                        putchar('\n');
                        fflush(stdout);
                        break;
                    }
                    // Backspace / Delete
                    if (ch == 0x08 || ch == 0x7F) {
                        if (idx > 0) {
                            idx--;
                            numbuf[idx] = '\0';
                            // Erase last character on terminal
                            printf("\b \b");
                            fflush(stdout);
                        }
                        continue;
                    }
                    // Digit
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
                        nvs_save_web_period(web_update_period_sec);
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
                // GPIO only. No SPI, does not touch the Ethernet driver.
                int level = gpio_get_level(ETH_INT_PIN);
                ESP_LOGI(TAG, "INT pin (GPIO%d) = %d (%s)", ETH_INT_PIN, level,
                         level ? "released/idle" : "asserted/LOW");
                break;
            }
            case 'h':
            case 'H': {
                eth_full_recover();
                break;
            }
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
                ESP_LOGI(TAG, "Unknown key '%c' (0x%02X). Use r, a, w, p, i, h, c, 1, 2, 3", c, c);
                break;
        }
    }
}
static void init_leds_gpio(void)
{
    gpio_reset_pin(ETH_SPI_CS);
    gpio_reset_pin(ETH_RST_PIN);
    gpio_reset_pin(ETH_INT_PIN);
    gpio_set_direction(ETH_SPI_CS, GPIO_MODE_OUTPUT);
    gpio_set_direction(ETH_RST_PIN, GPIO_MODE_OUTPUT);
    gpio_set_level(ETH_SPI_CS, 1);
    gpio_set_level(ETH_RST_PIN, 1);

    gpio_reset_pin(ADC_SYNC_PIN);
    gpio_reset_pin(ADC_DRDY_PIN);

    gpio_reset_pin(ADC1_SPI_CS);
    gpio_reset_pin(ADC2_SPI_CS);
    gpio_set_direction(ADC1_SPI_CS, GPIO_MODE_OUTPUT);
    gpio_set_direction(ADC2_SPI_CS, GPIO_MODE_OUTPUT);
    gpio_set_level(ADC1_SPI_CS, 1);
    gpio_set_level(ADC2_SPI_CS, 1);

    gpio_reset_pin(LED_STATUS_GREEN);
    gpio_reset_pin(LED_STATUS_RED);

    gpio_set_direction(LED_STATUS_GREEN, GPIO_MODE_OUTPUT);
    gpio_set_direction(LED_STATUS_RED, GPIO_MODE_OUTPUT);
    gpio_set_level(LED_STATUS_GREEN, 1);
    gpio_set_level(LED_STATUS_RED, 0);
}

esp_err_t cc_tcpip_stack_init(esp_eth_handle_t eth_handle)
{
    // Initialize TCP/IP network interface (should be called only once in application)
    ESP_ERROR_CHECK(esp_netif_init());
    // Create default event loop that running in background
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    esp_netif_config_t cfg = ESP_NETIF_DEFAULT_ETH();
    g_eth_netif = esp_netif_new(&cfg);
    g_eth_glue = esp_eth_new_netif_glue(eth_handle);
    ESP_ERROR_CHECK(esp_netif_attach(g_eth_netif, g_eth_glue));

    return ESP_OK;
}

void app_main(void)
{
    ESP_LOGI(TAG, "BEMS RTU starting...");

    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // Load persisted settings
    nvs_load_web_period();

    // Install the GPIO driver's ETS_GPIO_INTR_SOURCE ISR handler service, 
    //  which allows per-pin GPIO interrupt handlers.
    gpio_install_isr_service(0);

    init_leds_gpio();

    //init_i2c_pca9555();
    //xTaskCreate(task_relay_test, "relay_test", 4096, NULL, 4, NULL);
 
    //init_pulse_inputs();
    //init_rs485(RS485_UART_NUM_1, RS485_TX_PIN_1, RS485_RX_PIN_1, RS485_DE_RE_PIN_1);
    //init_rs485(RS485_UART_NUM_2, RS485_TX_PIN_2, RS485_RX_PIN_2, RS485_DE_RE_PIN_2);
    //xTaskCreate(task_pulse_data_collection, "pulse_collect", 4096, NULL, 5, NULL);

    init_spi2_host();

#if PP_ENABLE_ETHERNET
    esp_eth_handle_t eth_handle;

    ESP_ERROR_CHECK(w5500_init(&eth_handle));
    g_eth_handle = eth_handle;
    ESP_ERROR_CHECK(cc_tcpip_stack_init(eth_handle));

    // Register user defined event handers
    ESP_ERROR_CHECK(esp_event_handler_register(ETH_EVENT, ESP_EVENT_ANY_ID, &eth_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_ETH_GOT_IP, &got_ip_event_handler, NULL));

    ESP_ERROR_CHECK(esp_eth_start(eth_handle));
#else
    ESP_LOGI(TAG, "Ethernet disabled (PP_ENABLE_ETHERNET=0)");
#endif

#if PP_ENABLE_ADC
    init_ads131m08();
    xTaskCreate(task_ads131m08, "ads131m08", 4096, NULL, 6, NULL);
 #endif
 
    xTaskCreate(task_console, "console", 4096, NULL, 3, NULL);
    xTaskCreate(task_heartbeat, "heartbeat", 2048, NULL, 2, NULL);

    ESP_LOGI(TAG, "RTU ready......");
}
