#ifndef RTU_PINS_H_
#define RTU_PINS_H_

/**
 * RTU Pin Definitions for ESP32-S3
 * 
 * Based on schematic: RTU_AI_PLATFORM_ESP32_261003.DSN
 * (OrCAD Capture. Net-to-pin mapping is approximate; verify against schematic.)
 * 
 * Design reference: 한국형_전력_수도_온수_태양광_RTU_ADC_설계_리포트_개정.docx
 * Architecture: RS-485/Modbus priority + Pulse + Analog
 * 
 * Updated: 2026-10-15 KST
 */

#include "driver/gpio.h"
#include "driver/uart.h"

// Common SPI pins observed in schematic context
#define ETH_SPI_HOST			SPI2_HOST
#define ETH_SPI_MOSI            GPIO_NUM_11
#define ETH_SPI_MISO            GPIO_NUM_13
#define ETH_SPI_CLK             GPIO_NUM_12

// ========== Ethernet (W5500) ==========
#define ETH_SPI_CS              GPIO_NUM_21 // SCSn
#define ETH_RST_PIN             GPIO_NUM_45 // RSTn
#define ETH_INT_PIN             GPIO_NUM_41 // INTn

// ======= ADC Common(ADS131M08) ========
#define ADC_SYNC_PIN			GPIO_NUM_3
#define ADC_DRDY_PIN			GPIO_NUM_46

// ========== ADC1(ADS131M08) ==========
#define ADC1_SPI_CS				GPIO_NUM_10
// ========== ADC2(ADS131M08) ==========
#define ADC2_SPI_CS				GPIO_NUM_38

// ========== UART for RS-485 / Modbus ==========
#define RS485_UART_NUM_1        UART_NUM_1
#define RS485_TX_PIN_1          GPIO_NUM_17
#define RS485_RX_PIN_1          GPIO_NUM_18
#define RS485_DE_RE_PIN_1       GPIO_NUM_19

#define RS485_UART_NUM_2        UART_NUM_2
#define RS485_TX_PIN_2          GPIO_NUM_4
#define RS485_RX_PIN_2          GPIO_NUM_5
#define RS485_DE_RE_PIN_2       GPIO_NUM_6

// ========== Pulse Inputs (DI) ==========
// Use GPIO interrupt / PCNT, not ADC
#define PULSE_INPUT_1           GPIO_NUM_7
#define PULSE_INPUT_2           GPIO_NUM_15
#define PULSE_INPUT_3           GPIO_NUM_16
#define PULSE_INPUT_4           GPIO_NUM_20

// ========== Status LEDs ==========
#define LED_STATUS_GREEN        GPIO_NUM_39
#define LED_STATUS_RED          GPIO_NUM_40

#endif // RTU_PINS_H_
