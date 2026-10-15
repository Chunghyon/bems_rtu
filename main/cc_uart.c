#include "stdlib.h"
#include "string.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"

#include "esp_wifi.h"
#include "esp_eth.h"

#include "driver/uart.h"
#include "hh_parameters.h"
#include "hh_util.h"

#define SERIAL_TXD  SPI_CS
#define SERIAL_RXD  SPI_MISO
#define SERIAL_RTS  SPI_CLK
#define SERIAL_CTS  SPI_MOSI

static const int RX_BUF_SIZE = 1024;

void ff_init_uart0(void)
{
    const uart_config_t uart_config0 = {
    		.baud_rate = 115200,
			.data_bits = UART_DATA_8_BITS,
			.parity = UART_PARITY_DISABLE,
			.stop_bits = UART_STOP_BITS_1,
			.flow_ctrl = UART_HW_FLOWCTRL_DISABLE
    };

    uart_param_config(UART_NUM_0, &uart_config0);
    uart_set_pin(UART_NUM_0, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    uart_driver_install(UART_NUM_0, RX_BUF_SIZE * 2, 0, 0, NULL, 0);// We won't use a buffer for sending data.
}


