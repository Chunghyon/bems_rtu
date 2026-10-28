#pragma once

#include <stdint.h>
#include "driver/uart.h"

/**
 * BEMS RTU Modbus map (holding / input registers, FC03/FC04).
 * 32-bit values are big-endian across two registers (hi, lo).
 *
 *  0      status (bit0 = measurements valid)
 *  1      slave id
 *  2      map version (1)
 * 10-11   Vrms1 x100
 * 12-13   Vrms2 x100
 * 14-15   Vrms3 x100
 * 20-21   Irms1 x1000
 * 22-23   Irms2 x1000
 * 24-25   Irms3 x1000
 * 30-31   P1 watts (int32)
 * 32-33   P2 watts
 * 34-35   P3 watts
 * 40      PF1 x1000
 * 41      PF2 x1000
 * 42      PF3 x1000
 * 50-51   pulse 1
 * 52-53   pulse 2
 * 54-55   pulse 3
 * 56-57   pulse 4
 * 100-115 ADC1 avg int32 x8 (2 regs each)
 * 120-135 ADC2 avg int32 x8
 * 200     master poll ok count
 * 201     master poll fail count
 * 202-211 last master FC03 payload (10 regs)
 */
#define MODBUS_REG_COUNT  256

typedef struct {
    float vrms[3];
    float irms[3];
    float kw[3];
    float pf[3];
    uint32_t pulse[4];
    int32_t adc1[8];
    int32_t adc2[8];
} modbus_meas_t;

typedef void (*modbus_meas_fn)(modbus_meas_t *out);

void modbus_start(uart_port_t slave_uart, int slave_de_pin,
                  uart_port_t master_uart, int master_de_pin,
                  modbus_meas_fn meas_fn);
