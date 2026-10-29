#include "project.h"
#include "board.h"
#include "modbus_rtu.h"

#include "driver/gpio.h"
#include "driver/uart.h"
#include "driver/spi_master.h"
#include "esp_log.h"

#define GPIO_OUTPUT_PIN_SEL  \
	(1ULL << ETH_SPI_CS) | (1ULL << ETH_RST_PIN) | (1ULL << ADC1_SPI_CS) | (1ULL << ADC2_SPI_CS) | \
    (1ULL << LED_STATUS_GREEN) | (1ULL << LED_STATUS_RED)

#define GPIO_INPUT_PIN_SEL  \
	(1ULL << ETH_INT_PIN) | (1ULL << ADC_DRDY_PIN)

static const char *TAG = "board";

void board_init_gpios(void)
{
	gpio_config_t io_conf;

	io_conf.pin_bit_mask 	= GPIO_OUTPUT_PIN_SEL;		//bit mask of the LED pins
	io_conf.mode 			= GPIO_MODE_OUTPUT;			//set as output mode
	io_conf.pull_down_en 	= false;					//disable pull-down mode
	io_conf.pull_up_en 		= false;					//disable pull-up mode
	io_conf.intr_type 		= GPIO_INTR_DISABLE;		//disable interrupt
	gpio_config(&io_conf);								//configure GPIO with the given settings

	io_conf.pin_bit_mask 	= GPIO_INPUT_PIN_SEL;		//bit mask of interrupt pins
	io_conf.mode 			= GPIO_MODE_INPUT;			//set as input mode
	io_conf.pull_down_en 	= false;					//disable pull-down mode
	io_conf.pull_up_en 		= false;					//disable pull-up mode
	io_conf.intr_type 		= GPIO_INTR_POSEDGE;		//interrupt of rising edge
	gpio_config(&io_conf);								//configure GPIO with the given settings

    gpio_set_level(ETH_SPI_CS, 1);
    gpio_set_level(ETH_RST_PIN, 1);
    gpio_set_level(ADC1_SPI_CS, 1);
    gpio_set_level(ADC2_SPI_CS, 1);
    gpio_set_level(LED_STATUS_GREEN, 1);
    gpio_set_level(LED_STATUS_RED, 0);
}

void board_init_spi2(void)
{
    spi_bus_config_t adc_buscfg = {
        .mosi_io_num = SPI2_MOSI,
        .miso_io_num = SPI2_MISO,
        .sclk_io_num = SPI2_CLK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
    };
    ESP_ERROR_CHECK(spi_bus_initialize(SPI2_HOST, &adc_buscfg, SPI_DMA_CH_AUTO));
    ESP_LOGI(TAG, "ADC SPI bus ready (SPI2_HOST, MOSI=%d MISO=%d CLK=%d)",
        SPI2_MOSI, SPI2_MISO, SPI2_CLK);
}

void board_init_rs485(uart_port_t uart_num, int tx, int rx, int de_re)
{
    uart_config_t uart_config = {
        .baud_rate = MODBUS_BAUD,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    uart_driver_install(uart_num, 1024, 1024, 0, NULL, 0);
    uart_param_config(uart_num, &uart_config);
    uart_set_pin(uart_num, tx, rx, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    uart_set_rx_timeout(uart_num, 10);

    if (de_re >= 0) {
        gpio_set_direction(de_re, GPIO_MODE_OUTPUT);
        gpio_set_level(de_re, 0);
    }
    ESP_LOGI(TAG, "RS-485 UART%d initialized (%d 8N1)", uart_num, MODBUS_BAUD);
}
