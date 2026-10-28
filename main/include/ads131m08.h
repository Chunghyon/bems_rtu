#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "rtu_pins.h"

#ifdef __cplusplus
extern "C" {
#endif

#define ADS131M08_NUM_CH  8

// ADC1 channels
typedef enum {
    ADC1_AC_CURRENT1 = 0,
    ADC1_AC_CURRENT2,
    ADC1_AC_CURRENT3,
    ADC1_AC_VOLTAGE1,
    ADC1_AC_VOLTAGE2,
    ADC1_AC_VOLTAGE3,
    ADC1_COMMON1,
    ADC1_COMMON2,
    ADC1_CH_COUNT
} adc1_channel_t;

// ADC2 channels
typedef enum {
    ADC2_COMMON3 = 0,
    ADC2_COMMON4,
    ADC2_COMMON5,
    ADC2_COMMON6,
    ADC2_RESERVED1,
    ADC2_RESERVED2,
    ADC2_RESERVED3,
    ADC2_RESERVED4,
    ADC2_CH_COUNT
} adc2_channel_t;

extern bool adc_log_enabled;

void ads131m08_init(void);
void ads131m08_start_tasks(void);

// Manual calibration of measurement channels (call when inputs are at zero)
void ads131m08_calibrate_measure(uint16_t samples);

// Getters for web / modbus
void ads131m08_get_avg(int32_t adc1[ADS131M08_NUM_CH], int32_t adc2[ADS131M08_NUM_CH]);
void ads131m08_get_power(float vrms[3], float irms[3], float kw[3], float pf[3], uint32_t *sps);
void ads131m08_get_power_ext(float kvar[3], float kva[3], float kwh[3], float *freq);

const char *ads131m08_ch_name_adc1(int ch);
const char *ads131m08_ch_name_adc2(int ch);

#ifdef __cplusplus
}
#endif
