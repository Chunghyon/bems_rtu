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
 * 36-37   Q1 VAR (int32) reactive
 * 38-39   Q2 VAR
 * 40-41   Q3 VAR
 * 42-43   S1 VA (int32) apparent
 * 44-45   S2 VA
 * 46-47   S3 VA
 * 48      PF1 x1000
 * 49      PF2 x1000
 * 50      PF3 x1000
 * 60-61   kWh1 x1000 (uint32)
 * 62-63   kWh2 x1000
 * 64-65   kWh3 x1000
 * 70      Frequency x100 (Hz)
 * 80-81   pulse 1
 * 82-83   pulse 2
 * 84-85   pulse 3
 * 86-87   pulse 4
 * 100-115 ADC1 avg int32 x8 (2 regs each)
 * 120-135 ADC2 avg int32 x8
 * 200     master poll ok count
 * 201     master poll fail count
 * 202-211 last master FC03 payload (10 regs)
 * 220-229 second master payload (optional)
 */
#define MODBUS_REG_COUNT  256

typedef struct {
    float vrms[3];
    float irms[3];
    float kw[3];
    float kvar[3];
    float kva[3];
    float pf[3];
    float kwh[3];
    float freq_hz;
    uint32_t pulse[4];
    int32_t adc1[8];
    int32_t adc2[8];
} modbus_meas_t;

typedef void (*modbus_meas_fn)(modbus_meas_t *out);

void modbus_start(uart_port_t slave_uart, int slave_de_pin,
                  uart_port_t master_uart, int master_de_pin,
                  modbus_meas_fn meas_fn);
