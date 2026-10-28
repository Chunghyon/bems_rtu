#pragma once

#include "driver/uart.h"
#include "rtu_pins.h"

#ifdef __cplusplus
extern "C" {
#endif

void board_init_gpios(void);
void board_init_spi2(void);
void board_init_rs485(uart_port_t uart_num, int tx, int rx, int de_re);

#ifdef __cplusplus
}
#endif
