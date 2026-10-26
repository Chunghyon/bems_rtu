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

#define PP_ENABLE_ETHERNET		1
#define PP_ENABLE_ADC			1

// ADC conversion mode: 1 = use DRDY interrupt, 0 = on-demand (timed read)
#define PP_ADC_USE_DRDY			0
// On-demand sample interval in milliseconds
// Range: 1 ~ 60000 (1 ms ~ 60 s). Must be >= ADC conversion period.
#define PP_ADC_SAMPLE_MS		250

// ADC hardware OSR (oversampling ratio) — sets conversion period
// Valid: 128, 256, 512, 1024, 2048, 4096, 8192, 16384
// With 8.192 MHz CLKIN: data rate = 8.192e6 / (2 * OSR)
//   128→32 kHz, 256→16 kHz, 512→8 kHz, 1024→4 kHz,
//   2048→2 kHz, 4096→1 kHz, 8192→500 Hz, 16384→250 Hz
#define PP_ADC_OSR				16384

// Ethernet SPI host selection
// 0 = share SPI2_HOST with ADC (original pins GPIO11/13/12)
// 1 = use dedicated SPI3_HOST (MOSI=GPIO35, MISO=GPIO36, CLK=GPIO37)
#define PP_ETH_USE_SPI3			1

// ========== ADC SPI (always SPI2_HOST) ==========
#define ADC_SPI_HOST			SPI2_HOST
#define ADC_SPI_MOSI			GPIO_NUM_11
#define ADC_SPI_MISO			GPIO_NUM_13
#define ADC_SPI_CLK				GPIO_NUM_12

// ========== Ethernet SPI ==========
#if PP_ETH_USE_SPI3
#define ETH_SPI_HOST			SPI3_HOST
#define ETH_SPI_MOSI			GPIO_NUM_2
#define ETH_SPI_MISO			GPIO_NUM_48
#define ETH_SPI_CLK				GPIO_NUM_1
#else
#define ETH_SPI_HOST			SPI2_HOST
#define ETH_SPI_MOSI			GPIO_NUM_11
#define ETH_SPI_MISO			GPIO_NUM_13
#define ETH_SPI_CLK				GPIO_NUM_12
#endif
#define ETH_SPI_CLK_SPEED_HZ	16000000  // 16 MHz for W5500, can be adjusted

// ========== Ethernet (W5500) ==========
#define ETH_SPI_CS              GPIO_NUM_21 // SCSn
#define ETH_RST_PIN             GPIO_NUM_45 // RSTn
#define ETH_INT_PIN             GPIO_NUM_41 // INTn

// ======= ADC Common(ADS131M08) ========
#define ADC_SYNC_PIN			GPIO_NUM_3
#define ADC_DRDY_PIN			GPIO_NUM_46
#define ADC_SPI_CLK_SPEED_HZ    8000000

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

// ========== I2C to control PCA9555PWR ==========
#define I2C_SDA_PIN             GPIO_NUM_8
#define I2C_SCL_PIN             GPIO_NUM_9
#define PCA9555_IO_INT_PIN    	GPIO_NUM_42

#endif // RTU_PINS_H_
