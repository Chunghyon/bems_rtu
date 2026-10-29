#pragma once

#include "driver/gpio.h"
#include "driver/uart.h"
#include "project.h"

#define SPI2_MOSI				GPIO_NUM_11
#define SPI2_MISO				GPIO_NUM_13
#define SPI2_CLK				GPIO_NUM_12

#define SPI3_MOSI				GPIO_NUM_2
#define SPI3_MISO				GPIO_NUM_48
#define SPI3_CLK				GPIO_NUM_1

#define ETH_SPI_CS              GPIO_NUM_21 // SCSn
#define ETH_RST_PIN             GPIO_NUM_45 // RSTn
#define ETH_INT_PIN             GPIO_NUM_41 // INTn

#define ADC_SYNC_PIN			GPIO_NUM_3
#define ADC_DRDY_PIN			GPIO_NUM_47	// was 46 (strapping); 47 is not a strap pin

#define ADC1_SPI_CS				GPIO_NUM_10
#define ADC2_SPI_CS				GPIO_NUM_38

// ========== Pulse Inputs (DI) ==========
// Use GPIO interrupt / PCNT, not ADC
#define PULSE_INPUT_1           GPIO_NUM_7
#define PULSE_INPUT_2           GPIO_NUM_15
#define PULSE_INPUT_3           GPIO_NUM_16
#define PULSE_INPUT_4           GPIO_NUM_20

// ========== I2C to control PCA9555PWR ==========
#define I2C_SDA_PIN             GPIO_NUM_8
#define I2C_SCL_PIN             GPIO_NUM_9
#define PCA9555_IO_INT_PIN    	GPIO_NUM_42

// ========== Status LEDs ==========
#define LED_STATUS_GREEN        GPIO_NUM_39
#define LED_STATUS_RED          GPIO_NUM_40

// ========== UART for RS-485 / Modbus ==========
#define RS485_TX_PIN_1          GPIO_NUM_17
#define RS485_RX_PIN_1          GPIO_NUM_18
#define RS485_DE_RE_PIN_1       GPIO_NUM_19

#define RS485_TX_PIN_2          GPIO_NUM_4
#define RS485_RX_PIN_2          GPIO_NUM_5
#define RS485_DE_RE_PIN_2       GPIO_NUM_6

#ifdef __cplusplus
extern "C" {
#endif

void board_init_gpios(void);
void board_init_spi2(void);
void board_init_rs485(uart_port_t uart_num, int tx, int rx, int de_re);

#ifdef __cplusplus
}
#endif
