#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "project.h"

#define ADS131M08_NUM_CH        8

#define ADC_SPI_HOST			SPI2_HOST
#define ADC_SPI_MOSI			SPI2_MOSI
#define ADC_SPI_MISO			SPI2_MISO
#define ADC_SPI_CLK				SPI2_CLK

// ======= ADC Common(ADS131M08) ========
#define ADC_SPI_CLK_SPEED_HZ    16000000
/* 	8 kSPS → SPI 4 MHz 이상
	16 kSPS → SPI 8 MHz 이상
	32 kSPS → SPI 16 MHz 이상 */

// ADC conversion mode: 1 = use DRDY interrupt, 0 = on-demand (timed read)
#define PP_ADC_USE_DRDY			1
// On-demand sample interval in milliseconds (used only when PP_ADC_USE_DRDY=0)
// Range: 1 ~ 60000 (1 ms ~ 60 s). Must be >= ADC conversion period.
#define PP_ADC_SAMPLE_MS		250
// ADC hardware OSR (oversampling ratio) — sets conversion period
// Valid: 128, 256, 512, 1024, 2048, 4096, 8192, 16384
// With 8.192 MHz CLKIN: data rate = 8.192e6 / (2 * OSR)
//   128→32 kHz, 256→16 kHz, 512→8 kHz, 1024→4 kHz,
//   2048→2 kHz, 4096→1 kHz, 8192→500 Hz, 16384→250 Hz
#define PP_ADC_OSR				1024

// 3-phase power calculation
#define PP_POWER_WINDOW_MS		100     // RMS / power window
#define PP_BEMS_PERIOD_SEC		5       // publish interval (1~5 s)
// Scale: ADC LSB (≈1.2 V / 2^23) × PT or CT ratio. Tune to the front-end.
#define PP_ADC_LSB_V			(1.2f / 8388608.0f)
#define PP_PT_RATIO				1.0f    // primary volts / ADC-pin volt  (tune per ZMPT / onboard)

// Burden resistors
#define BURDEN_ADC1_CH0_2_OHM       10.0f    // ADC1 ch0~2 fixed pull-down
#define BURDEN_COMMON_CUR_OHM       49.9f    // common channels when CUR_SEL=0 (49.9ohm)

// CT ratio: primary amps / ADC-pin volt. Tune PP_CT_RATIO_10OHM to your CT + 10ohm.
// 49.9ohm version is derived (inverse to burden). Adjust independently if needed.
#define PP_CT_RATIO_10OHM           1.0f
#define PP_CT_RATIO_49R9            (PP_CT_RATIO_10OHM * BURDEN_ADC1_CH0_2_OHM / BURDEN_COMMON_CUR_OHM)

#define PP_V_PER_COUNT			(PP_ADC_LSB_V * PP_PT_RATIO)
#define PP_I_PER_COUNT_10OHM    (PP_ADC_LSB_V * PP_CT_RATIO_10OHM)
#define PP_I_PER_COUNT_49R9     (PP_ADC_LSB_V * PP_CT_RATIO_49R9)

// Legacy (kept for compatibility; prefer the burden-specific ones)
#define PP_CT_RATIO				PP_CT_RATIO_10OHM
#define PP_I_PER_COUNT			PP_I_PER_COUNT_10OHM

#ifdef __cplusplus
extern "C" {
#endif

// ADC1 channels
// ch0~2: current inputs after 10ohm pull-down
// ch3~5: voltage inputs (on-board divider or external ZMPT101B)
// ch6~7: common1~2 (configurable via PCA9555 10V_SEL / CUR_SEL)
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
// ch0~3: common3~6 (configurable via PCA9555 DI_SEL + 10V_SEL / CUR_SEL)
// ch4~7: reserved, not connected
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

// Phase mode for power calculation
typedef enum {
    POWER_MODE_SINGLE = 1,   // use only phase 0 (V1/I1)
    POWER_MODE_THREE  = 3    // use all 3 phases
} power_phase_mode_t;

// Current source selection for power calculation
// Channel encoding: 0~7 = ADC1 ch0~7, 8~15 = ADC2 ch0~7
// When using common channels for current, configure PCA9555 CUR_SEL=0 (49.9ohm).
typedef enum {
    CURRENT_SRC_ADC1_CH0_2 = 0,   // use ADC1 ch0,1,2 (10 ohm burden)
    CURRENT_SRC_CUSTOM     = 1    // use user-specified channels (burden auto-selected)
} current_source_mode_t;

extern bool adc_log_enabled;
extern bool phase_log_enabled;

void ads131m08_init(void);
void ads131m08_start_tasks(void);

// Manual calibration of measurement channels (call when inputs are at zero)
void ads131m08_calibrate_measure(uint16_t samples);

// Phase mode control (default: POWER_MODE_THREE)
void ads131m08_set_phase_mode(power_phase_mode_t mode);
power_phase_mode_t ads131m08_get_phase_mode(void);

// Current source control for power calc
// mode: CURRENT_SRC_ADC1_CH0_2 or CURRENT_SRC_CUSTOM
// For CUSTOM, provide channel codes: 0~7=ADC1, 8~15=ADC2. Use -1 for unused.
// Burden is auto-selected: ADC1 ch0-2 → 10Ω, otherwise → 49.9Ω (common).
void ads131m08_set_current_source(current_source_mode_t mode, int ch_phase0, int ch_phase1, int ch_phase2);
current_source_mode_t ads131m08_get_current_source(void);
void ads131m08_get_current_channels(int *ch0, int *ch1, int *ch2);

// Voltage source channels for power calc (same encoding: 0~7=ADC1, 8~15=ADC2)
void ads131m08_set_voltage_channels(int ch_phase0, int ch_phase1, int ch_phase2);
void ads131m08_get_voltage_channels(int *ch0, int *ch1, int *ch2);

// NVS persistence for current source + common channel modes
void ads131m08_nvs_load(void);
void ads131m08_nvs_save(void);

// Getters for web / modbus
// In SINGLE mode, only index [0] is valid; [1] and [2] are zeroed.
void ads131m08_get_avg(int32_t adc1[ADS131M08_NUM_CH], int32_t adc2[ADS131M08_NUM_CH]);
void ads131m08_get_power(float vrms[3], float irms[3], float kw[3], float pf[3], uint32_t *sps);
void ads131m08_get_power_ext(float kvar[3], float kva[3], float kwh[3], float *freq);

const char *ads131m08_ch_name_adc1(int ch);
const char *ads131m08_ch_name_adc2(int ch);

#ifdef __cplusplus
}
#endif
