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
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_system.h"
#include "esp_log.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "nvs_flash.h"
#include "driver/gpio.h"
#include "driver/uart.h"
#include "driver/spi_master.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_eth.h"
#include "esp_eth_mac.h"
#include "esp_eth_phy.h"
#include "esp_http_server.h"

#include "rtu_pins.h"

static const char *TAG = "RTU_MAIN";

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

    gpio_install_isr_service(0);
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

// ========== ADC ==========
static adc_oneshot_unit_handle_t adc1_handle;

static void init_adc(void)
{
    adc_oneshot_unit_init_cfg_t init_config = {
        .unit_id = ADC_UNIT_1,
    };
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&init_config, &adc1_handle));

    adc_oneshot_chan_cfg_t config = {
        .bitwidth = ADC_BITWIDTH_DEFAULT,
        .atten = ADC_ATTEN_DB_12,
    };
    ESP_ERROR_CHECK(adc_oneshot_config_channel(adc1_handle, AI_ADC_CHANNEL_1, &config));
    ESP_ERROR_CHECK(adc_oneshot_config_channel(adc1_handle, AI_ADC_CHANNEL_2, &config));

    ESP_LOGI(TAG, "ADC initialized");
}

static int read_adc(adc_channel_t channel)
{
    int raw = 0;
    if (adc1_handle) {
        adc_oneshot_read(adc1_handle, channel, &raw);
    }
    return raw;
}

// ========== W5500 Ethernet ==========
static esp_eth_handle_t eth_handle = NULL;
static esp_netif_t *eth_netif = NULL;

static void eth_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data)
{
    if (event_base == ETH_EVENT) {
        if (event_id == ETHERNET_EVENT_CONNECTED) {
            ESP_LOGI(TAG, "Ethernet Link Up");
        } else if (event_id == ETHERNET_EVENT_DISCONNECTED) {
            ESP_LOGI(TAG, "Ethernet Link Down");
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_ETH_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        ESP_LOGI(TAG, "Got IP: " IPSTR, IP2STR(&event->ip_info.ip));
    }
}

static void init_w5500(void)
{
    // Reset pin
    gpio_set_direction(ETH_RST_PIN, GPIO_MODE_OUTPUT);
    gpio_set_level(ETH_RST_PIN, 0);
    vTaskDelay(pdMS_TO_TICKS(10));
    gpio_set_level(ETH_RST_PIN, 1);
    vTaskDelay(pdMS_TO_TICKS(50));

    // SPI bus
    spi_bus_config_t buscfg = {
        .miso_io_num = ETH_SPI_MISO,
        .mosi_io_num = ETH_SPI_MOSI,
        .sclk_io_num = ETH_SPI_CLK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
    };
    ESP_ERROR_CHECK(spi_bus_initialize(SPI2_HOST, &buscfg, SPI_DMA_CH_AUTO));

    spi_device_interface_config_t devcfg = {
        .command_bits = 16,
        .address_bits = 8,
        .mode = 0,
        .clock_speed_hz = 20 * 1000 * 1000,
        .spics_io_num = ETH_SPI_CS,
        .queue_size = 20,
    };

    eth_w5500_config_t w5500_config = ETH_W5500_DEFAULT_CONFIG(SPI2_HOST, &devcfg);
    w5500_config.int_gpio_num = ETH_INT_PIN;

    eth_mac_config_t mac_config = ETH_MAC_DEFAULT_CONFIG();
    eth_phy_config_t phy_config = ETH_PHY_DEFAULT_CONFIG();
    phy_config.phy_addr = 1;
    phy_config.reset_gpio_num = ETH_RST_PIN;

    esp_eth_mac_t *mac = esp_eth_mac_new_w5500(&w5500_config, &mac_config);
    esp_eth_phy_t *phy = esp_eth_phy_new_w5500(&phy_config);

    esp_eth_config_t eth_config = ETH_DEFAULT_CONFIG(mac, phy);
    ESP_ERROR_CHECK(esp_eth_driver_install(&eth_config, &eth_handle));

    esp_netif_config_t cfg = ESP_NETIF_DEFAULT_ETH();
    eth_netif = esp_netif_new(&cfg);
    ESP_ERROR_CHECK(esp_netif_attach(eth_netif, esp_eth_new_netif_glue(eth_handle)));

    ESP_ERROR_CHECK(esp_event_handler_register(ETH_EVENT, ESP_EVENT_ANY_ID, &eth_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_ETH_GOT_IP, &eth_event_handler, NULL));

    ESP_ERROR_CHECK(esp_eth_start(eth_handle));
    ESP_LOGI(TAG, "W5500 Ethernet initialized");
}

// ========== Simple Web Server ==========
static esp_err_t root_get_handler(httpd_req_t *req)
{
    char buf[512];
    snprintf(buf, sizeof(buf),
        "<!DOCTYPE html><html><head><meta charset='utf-8'><title>BEMS RTU</title>"
        "<style>body{font-family:sans-serif;margin:2em;} .box{border:1px solid #ccc;padding:1em;margin:1em 0;}</style></head>"
        "<body><h1>BEMS RTU Status</h1>"
        "<div class='box'><h2>Pulse Counters</h2>"
        "<p>CH1: %lu</p><p>CH2: %lu</p><p>CH3: %lu</p><p>CH4: %lu</p></div>"
        "<div class='box'><h2>Analog Inputs</h2>"
        "<p>AI1: %d</p><p>AI2: %d</p></div>"
        "<p><small>ESP32-S3 RTU | Design based on Korean energy metering RTU report</small></p>"
        "</body></html>",
        pulse_counts[0], pulse_counts[1], pulse_counts[2], pulse_counts[3],
        read_adc(AI_ADC_CHANNEL_1), read_adc(AI_ADC_CHANNEL_2));

    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, buf, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

static void start_webserver(void)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = 80;

    httpd_handle_t server = NULL;
    if (httpd_start(&server, &config) == ESP_OK) {
        httpd_uri_t uri_get = {
            .uri = "/",
            .method = HTTP_GET,
            .handler = root_get_handler,
            .user_ctx = NULL
        };
        httpd_register_uri_handler(server, &uri_get);
        ESP_LOGI(TAG, "Web server started on port 80");
    } else {
        ESP_LOGE(TAG, "Failed to start web server");
    }
}

// ========== Periodic task ==========
static void task_data_collection(void *arg)
{
    while (1) {
        ESP_LOGI(TAG, "Pulses: %lu %lu %lu %lu | AI: %d %d",
                 pulse_counts[0], pulse_counts[1], pulse_counts[2], pulse_counts[3],
                 read_adc(AI_ADC_CHANNEL_1), read_adc(AI_ADC_CHANNEL_2));
        vTaskDelay(pdMS_TO_TICKS(10000));
    }
}

// ========== LEDs ==========
static void init_leds(void)
{
    gpio_set_direction(LED_STATUS_GREEN, GPIO_MODE_OUTPUT);
    gpio_set_direction(LED_STATUS_RED, GPIO_MODE_OUTPUT);
    gpio_set_level(LED_STATUS_GREEN, 1);
    gpio_set_level(LED_STATUS_RED, 0);
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

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    init_leds();
    init_pulse_inputs();
    init_rs485(RS485_UART_NUM_1, RS485_TX_PIN_1, RS485_RX_PIN_1, RS485_DE_RE_PIN_1);
    init_rs485(RS485_UART_NUM_2, RS485_TX_PIN_2, RS485_RX_PIN_2, RS485_DE_RE_PIN_2);
    init_adc();
    init_w5500();
    start_webserver();

    xTaskCreate(task_data_collection, "data_collect", 4096, NULL, 5, NULL);

    ESP_LOGI(TAG, "RTU ready. Web server at http://<ip>/");
}
