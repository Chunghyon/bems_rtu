#ifndef _WM8960_H_
#define _WM8960_H_


#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_err.h"
#include "esp_log.h"
#include "driver/i2c.h"

#define delay(x) vTaskDelay(pdMS_TO_TICKS(x))

#define CODEC_ADDR      0x1a
#define I2C_BUS_NO      0
#define ACK_CHECK_EN    0
#define ACK_CHECK_DIS   0
#define ACK_VAL         0
#define NACK_VAL        1

// I2C address (7-bit format for Wire library)
#define WM8960_ADDR 0x1A 

// WM8960 register addresses
#define WM8960_REG_LEFT_INPUT_VOLUME 0x00
#define WM8960_REG_RIGHT_INPUT_VOLUME 0x01
#define WM8960_REG_LOUT1_VOLUME 0x02
#define WM8960_REG_ROUT1_VOLUME 0x03
#define WM8960_REG_CLOCKING_1 0x04
#define WM8960_REG_ADC_DAC_CTRL_1 0x05
#define WM8960_REG_ADC_DAC_CTRL_2 0x06
#define WM8960_REG_AUDIO_INTERFACE_1 0x07
#define WM8960_REG_CLOCKING_2 0x08
#define WM8960_REG_AUDIO_INTERFACE_2 0x09
#define WM8960_REG_LEFT_DAC_VOLUME 0x0A
#define WM8960_REG_RIGHT_DAC_VOLUME 0x0B
#define WM8960_REG_RESET 0x0F
#define WM8960_REG_3D_CONTROL 0x10
#define WM8960_REG_ALC1 0x11
#define WM8960_REG_ALC2 0x12
#define WM8960_REG_ALC3 0x13
#define WM8960_REG_NOISE_GATE 0x14
#define WM8960_REG_LEFT_ADC_VOLUME 0x15
#define WM8960_REG_RIGHT_ADC_VOLUME 0x16
#define WM8960_REG_ADDITIONAL_CONTROL_1 0x17
#define WM8960_REG_ADDITIONAL_CONTROL_2 0x18
#define WM8960_REG_PWR_MGMT_1 0x19
#define WM8960_REG_PWR_MGMT_2 0x1A
#define WM8960_REG_ADDITIONAL_CONTROL_3 0x1B
#define WM8960_REG_ANTI_POP_1 0x1C
#define WM8960_REG_ANTI_POP_2 0x1D
#define WM8960_REG_ADCL_SIGNAL_PATH 0x20
#define WM8960_REG_ADCR_SIGNAL_PATH 0x21
#define WM8960_REG_LEFT_OUT_MIX_1 0x22
#define WM8960_REG_RIGHT_OUT_MIX_2 0x25
#define WM8960_REG_MONO_OUT_MIX_1 0x26
#define WM8960_REG_MONO_OUT_MIX_2 0x27
#define WM8960_REG_LOUT2_VOLUME 0x28
#define WM8960_REG_ROUT2_VOLUME 0x29
#define WM8960_REG_MONO_OUT_VOLUME 0x2A
#define WM8960_REG_INPUT_BOOST_MIXER_1 0x2B
#define WM8960_REG_INPUT_BOOST_MIXER_2 0x2C
#define WM8960_REG_BYPASS_1 0x2D
#define WM8960_REG_BYPASS_2 0x2E
#define WM8960_REG_PWR_MGMT_3 0x2F
#define WM8960_REG_ADDITIONAL_CONTROL_4 0x30
#define WM8960_REG_CLASS_D_CONTROL_1 0x31
#define WM8960_REG_CLASS_D_CONTROL_3 0x33
#define WM8960_REG_PLL_N 0x34
#define WM8960_REG_PLL_K_1 0x35
#define WM8960_REG_PLL_K_2 0x36
#define WM8960_REG_PLL_K_3 0x37

// PGA input selections
#define WM8960_PGAL_LINPUT2 0
#define WM8960_PGAL_LINPUT3 1
#define WM8960_PGAL_VMID 2
#define WM8960_PGAR_RINPUT2 0
#define WM8960_PGAR_RINPUT3 1
#define WM8960_PGAR_VMID 2

// Mic (aka PGA) BOOST gain options
#define WM8960_MIC_BOOST_GAIN_0DB 0
#define WM8960_MIC_BOOST_GAIN_13DB 1
#define WM8960_MIC_BOOST_GAIN_20DB 2
#define WM8960_MIC_BOOST_GAIN_29DB 3

#define WM8960_FORMAT_RIGHT_JUSTIFIED   0
#define WM8960_FORMAT_LEFT_JUSTIFIED    1
#define WM8960_FORMAT_I2S               2
#define WM8960_FORMAT_DSP               3

// Boost Mixer gain options
// These are used to control the gain (aka volume) at the following settings:
// LIN2BOOST
// LIN3BOOST
// RIN2BOOST
// RIN3BOOST
#define WM8960_BOOST_MIXER_GAIN_MUTE 0
#define WM8960_BOOST_MIXER_GAIN_NEG_12DB 1
#define WM8960_BOOST_MIXER_GAIN_NEG_9DB 2
#define WM8960_BOOST_MIXER_GAIN_NEG_6DB 3
#define WM8960_BOOST_MIXER_GAIN_NEG_3DB 4
#define WM8960_BOOST_MIXER_GAIN_0DB 5
#define WM8960_BOOST_MIXER_GAIN_3DB 6
#define WM8960_BOOST_MIXER_GAIN_6DB 7

// Output Mixer gain options
// These are used to control the gain (aka volume) at the following settings:
// LI2LOVOL
// LB2LOVOL
// RI2LOVOL
// RB2LOVOL
// These are useful as analog bypass signal path options.
#define WM8960_OUTPUT_MIXER_GAIN_0DB 0
#define WM8960_OUTPUT_MIXER_GAIN_NEG_3DB 1
#define WM8960_OUTPUT_MIXER_GAIN_NEG_6DB 2
#define WM8960_OUTPUT_MIXER_GAIN_NEG_9DB 3
#define WM8960_OUTPUT_MIXER_GAIN_NEG_12DB 4
#define WM8960_OUTPUT_MIXER_GAIN_NEG_15DB 5
#define WM8960_OUTPUT_MIXER_GAIN_NEG_18DB 6
#define WM8960_OUTPUT_MIXER_GAIN_NEG_21DB 7

// Mic Bias voltage options
#define WM8960_MIC_BIAS_VOLTAGE_0_9_AVDD 0
#define WM8960_MIC_BIAS_VOLTAGE_0_65_AVDD 1

// SYSCLK divide
#define WM8960_SYSCLK_DIV_BY_1 0
#define WM8960_SYSCLK_DIV_BY_2 2
#define WM8960_SYSCLK_DIV_BY_3 3

#define WM8960_CLKSEL_MCLK 0
#define WM8960_CLKSEL_PLL 1

#define WM8960_PLL_MODE_INTEGER 0
#define WM8960_PLL_MODE_FRACTIONAL 1
#define WM8960_PLLPRESCALE_DIV_1 0
#define WM8960_PLLPRESCALE_DIV_2 1

#define WM8960_ADAC_DIV_256 0
#define WM8960_ADAC_DIV_384 1
#define WM8960_ADAC_DIV_512 2
#define WM8960_ADAC_DIV_768 3
#define WM8960_ADAC_DIV_1024 4
#define WM8960_ADAC_DIV_1152 5
#define WM8960_ADAC_DIV_1536 6

// Class d clock divide
#define WM8960_DCLKDIV_2    1
#define WM8960_DCLKDIV_3    2
#define WM8960_DCLKDIV_4    3
#define WM8960_DCLKDIV_6    4
#define WM8960_DCLKDIV_8    5
#define WM8960_DCLKDIV_12   6
#define WM8960_DCLKDIV_16   7

// Word length settings (aka bits per sample)
// Audio Data Word Length
#define WM8960_WL_16BIT 0
#define WM8960_WL_20BIT 1
#define WM8960_WL_24BIT 2
#define WM8960_WL_32BIT 3

// Additional Digital Audio Interface controls
// LRP (aka left-right-polarity)
// Right, left and I2S modes – LRCLK polarity 
// 0 = normal LRCLK polarity 
// 1 = inverted LRCLK polarity 
#define WM8960_LR_POLARITY_NORMAL 0
#define WM8960_LR_POLARITY_INVERT 1

// ALRSWAP (aka ADC left/right swap)
// Left/Right ADC channel swap
// 1 = Swap left and right ADC data in audio interface
// 0 = Output left and right data as normal 
#define WM8960_ALRSWAP_NORMAL 0
#define WM8960_ALRSWAP_SWAP 1

// Gain mins, maxes, offsets and step-sizes for all the amps within the codec.
#define WM8960_PGA_GAIN_MIN -17.25
#define WM8960_PGA_GAIN_MAX 30.00
#define WM8960_PGA_GAIN_OFFSET 17.25
#define WM8960_PGA_GAIN_STEPSIZE 0.75

#define WM8960_HP_GAIN_MIN -73.00
#define WM8960_HP_GAIN_MAX 6.00
#define WM8960_HP_GAIN_OFFSET 121.00
#define WM8960_HP_GAIN_STEPSIZE 1.00

#define WM8960_SPEAKER_GAIN_MIN -73.00
#define WM8960_SPEAKER_GAIN_MAX 6.00
#define WM8960_SPEAKER_GAIN_OFFSET 121.00
#define WM8960_SPEAKER_GAIN_STEPSIZE 1.00

#define WM8960_ADC_GAIN_MIN -97.00
#define WM8960_ADC_GAIN_MAX 30.00
#define WM8960_ADC_GAIN_OFFSET 97.50
#define WM8960_ADC_GAIN_STEPSIZE 0.50

#define WM8960_DAC_GAIN_MIN -127.00
#define WM8960_DAC_GAIN_MAX 0.00
#define WM8960_DAC_GAIN_OFFSET 127.50
#define WM8960_DAC_GAIN_STEPSIZE 0.50

// Automatic Level Control Modes
#define WM8960_ALC_MODE_OFF 0
#define WM8960_ALC_MODE_RIGHT_ONLY 1
#define WM8960_ALC_MODE_LEFT_ONLY 2
#define WM8960_ALC_MODE_STEREO 3

// Automatic Level Control Target Level dB
#define WM8960_ALC_TARGET_LEVEL_NEG_22_5DB 0
#define WM8960_ALC_TARGET_LEVEL_NEG_21DB 1
#define WM8960_ALC_TARGET_LEVEL_NEG_19_5DB 2
#define WM8960_ALC_TARGET_LEVEL_NEG_18DB 3
#define WM8960_ALC_TARGET_LEVEL_NEG_16_5DB 4
#define WM8960_ALC_TARGET_LEVEL_NEG_15DB 5
#define WM8960_ALC_TARGET_LEVEL_NEG_13_5DB 6
#define WM8960_ALC_TARGET_LEVEL_NEG_12DB 7
#define WM8960_ALC_TARGET_LEVEL_NEG_10_5DB 8
#define WM8960_ALC_TARGET_LEVEL_NEG_9DB 9
#define WM8960_ALC_TARGET_LEVEL_NEG_7_5DB 10
#define WM8960_ALC_TARGET_LEVEL_NEG_6DB 11
#define WM8960_ALC_TARGET_LEVEL_NEG_4_5DB 12
#define WM8960_ALC_TARGET_LEVEL_NEG_3DB 13
#define WM8960_ALC_TARGET_LEVEL_NEG_1_5DB 14

// Automatic Level Control Max Gain Level dB
#define WM8960_ALC_MAX_GAIN_LEVEL_NEG_12DB 0
#define WM8960_ALC_MAX_GAIN_LEVEL_NEG_6DB 1
#define WM8960_ALC_MAX_GAIN_LEVEL_0DB 2
#define WM8960_ALC_MAX_GAIN_LEVEL_6DB 3
#define WM8960_ALC_MAX_GAIN_LEVEL_12DB 4
#define WM8960_ALC_MAX_GAIN_LEVEL_18DB 5
#define WM8960_ALC_MAX_GAIN_LEVEL_24DB 6
#define WM8960_ALC_MAX_GAIN_LEVEL_30DB 7

// Automatic Level Control Min Gain Level dB
#define WM8960_ALC_MIN_GAIN_LEVEL_NEG_17_25DB 0
#define WM8960_ALC_MIN_GAIN_LEVEL_NEG_11_25DB 1
#define WM8960_ALC_MIN_GAIN_LEVEL_NEG_5_25DB 2
#define WM8960_ALC_MIN_GAIN_LEVEL_0_75DB 3
#define WM8960_ALC_MIN_GAIN_LEVEL_6_75DB 4
#define WM8960_ALC_MIN_GAIN_LEVEL_12_75DB 5
#define WM8960_ALC_MIN_GAIN_LEVEL_18_75DB 6
#define WM8960_ALC_MIN_GAIN_LEVEL_24_75DB 7

// Automatic Level Control Hold Time (MS and SEC)
#define WM8960_ALC_HOLD_TIME_0MS 0
#define WM8960_ALC_HOLD_TIME_3MS 1
#define WM8960_ALC_HOLD_TIME_5MS 2
#define WM8960_ALC_HOLD_TIME_11MS 3
#define WM8960_ALC_HOLD_TIME_21MS 4
#define WM8960_ALC_HOLD_TIME_43MS 5
#define WM8960_ALC_HOLD_TIME_85MS 6
#define WM8960_ALC_HOLD_TIME_170MS 7
#define WM8960_ALC_HOLD_TIME_341MS 8
#define WM8960_ALC_HOLD_TIME_682MS 9
#define WM8960_ALC_HOLD_TIME_1365MS 10
#define WM8960_ALC_HOLD_TIME_3SEC 11
#define WM8960_ALC_HOLD_TIME_5SEC 12
#define WM8960_ALC_HOLD_TIME_10SEC 13
#define WM8960_ALC_HOLD_TIME_23SEC 14
#define WM8960_ALC_HOLD_TIME_44SEC 15

// Automatic Level Control Decay Time (MS and SEC)
#define WM8960_ALC_DECAY_TIME_24MS 0
#define WM8960_ALC_DECAY_TIME_48MS 1
#define WM8960_ALC_DECAY_TIME_96MS 2
#define WM8960_ALC_DECAY_TIME_192MS 3
#define WM8960_ALC_DECAY_TIME_384MS 4
#define WM8960_ALC_DECAY_TIME_768MS 5
#define WM8960_ALC_DECAY_TIME_1536MS 6
#define WM8960_ALC_DECAY_TIME_3SEC 7
#define WM8960_ALC_DECAY_TIME_6SEC 8
#define WM8960_ALC_DECAY_TIME_12SEC 9
#define WM8960_ALC_DECAY_TIME_24SEC 10

// Automatic Level Control Attack Time (MS and SEC)
#define WM8960_ALC_ATTACK_TIME_6MS 0
#define WM8960_ALC_ATTACK_TIME_12MS 1
#define WM8960_ALC_ATTACK_TIME_24MS 2
#define WM8960_ALC_ATTACK_TIME_482MS 3
#define WM8960_ALC_ATTACK_TIME_964MS 4
#define WM8960_ALC_ATTACK_TIME_1928MS 5
#define WM8960_ALC_ATTACK_TIME_3846MS 6
#define WM8960_ALC_ATTACK_TIME_768MS 7
#define WM8960_ALC_ATTACK_TIME_1536MS 8
#define WM8960_ALC_ATTACK_TIME_3SEC 9
#define WM8960_ALC_ATTACK_TIME_6SEC 10

// Speaker Boost Gains (DC and AC)
#define WM8960_SPEAKER_BOOST_GAIN_0DB 0
#define WM8960_SPEAKER_BOOST_GAIN_2_1DB 1
#define WM8960_SPEAKER_BOOST_GAIN_2_9DB 2
#define WM8960_SPEAKER_BOOST_GAIN_3_6DB 3
#define WM8960_SPEAKER_BOOST_GAIN_4_5DB 4
#define WM8960_SPEAKER_BOOST_GAIN_5_1DB 5

// VMIDSEL settings
#define WM8960_VMIDSEL_DISABLED 0
#define WM8960_VMIDSEL_2X50KOHM 1
#define WM8960_VMIDSEL_2X250KOHM 2
#define WM8960_VMIDSEL_2X5KOHM 3

#define WM8960_DAC_STEREO   0
#define WM8960_DAC_MONO     1

// ADC L/R Selection
#define WM8960_ADC_LR_NORMAL        0
#define WM8960_ADC_LR_ONLY_LEFT     1
#define WM8960_ADC_LR_ONLY_RIGHT    2
#define WM8960_ADC_LR_SWAP          3

// VREF to Analogue Output Resistance
// (Disabled Outputs)
// 0 = 500 VMID to output
// 1 = 20k VMID to output 
#define WM8960_VROI_500 0
#define WM8960_VROI_20K 1

// Analogue Bias Optimisation
// 00 = Reserved
// 01 = Increased bias current optimized for
// AVDD=2.7V
// 1X = Lowest bias current, optimized for
// AVDD=3.3V 

#define WM8960_VSEL_INCREASED_BIAS_CURRENT 1
#define WM8960_VSEL_LOWEST_BIAS_CURRENT 3

// JACK DETECT INPUT
#define WM8960_JACKDETECT_GPIO1 0
#define WM8960_JACKDETECT_LINPUT3 1
#define WM8960_JACKDETECT_RINPUT3 2

bool wm8960_enableVREF(void); // Necessary for all other functions
bool wm8960_disableVREF(void); // Use for turning this off to save power

bool wm8960_reset(void); // Resets all registers to their default state

/////////////////////////////////////////////////////////
///////////////////////////////////////////////////////// PGA 
/////////////////////////////////////////////////////////

bool wm8960_enableAINL(void);
bool wm8960_disableAINL(void);
bool wm8960_enableAINR(void);
bool wm8960_disableAINR(void);

bool wm8960_enableLMIC(void);
bool wm8960_disableLMIC(void);
bool wm8960_enableRMIC(void);
bool wm8960_disableRMIC(void);

bool wm8960_enableLMICBOOST(void);
bool wm8960_disableLMICBOOST(void);
bool wm8960_enableRMICBOOST(void);
bool wm8960_disableRMICBOOST(void);

// PGA input signal select
// Each PGA (left and right) has a switch on its non-inverting input.
// On PGA_LEFT:
// You can select between VMID, LINPUT2 or LINPUT3
// Note, the inverting input of PGA_LEFT is perminantly connected to 
// LINPUT1
// On PGA_RIGHT:
// You can select between VMIN, RINPUT2 or RINPUT3
// Note, the inverting input of PGA_RIGHT is perminantly connected to 
// RINPUT1

// 3 options: WM8960_PGAL_LINPUT2, WM8960_PGAL_LINPUT3, WM8960_PGAL_VMID
bool wm8960_pgaLeftNonInvSignalSelect(uint8_t signal); 

// 3 options: WM8960_PGAR_RINPUT2, WM8960_PGAR_RINPUT3, WM8960_PGAR_VMID
bool wm8960_pgaRightNonInvSignalSelect(uint8_t signal); 

// Connections from each INPUT1 to the inverting input of its PGA
	
// Connect LINPUT1 to inverting input of Left Input PGA
bool wm8960_connectLMN1(void); 
	
// Disconnect LINPUT1 from inverting input of Left Input PGA
bool wm8960_disconnectLMN1(void); 	

// Connect RINPUT1 to inverting input of Right Input PGA
bool wm8960_connectRMN1(void); 	

// Disconnect RINPUT1 from inverting input of Right Input PGA
bool wm8960_disconnectRMN1(void); 

// Connection from output of PGAs to downstream "boost mixers"

// Connect Left Input PGA to Left Input Boost mixer
bool wm8960_connectLMIC2B(void); 

// Disconnect Left Input PGA to Left Input Boost mixer
bool wm8960_disconnectLMIC2B(void);	

// Connect Right Input PGA to Right Input Boost mixer
bool wm8960_connectRMIC2B(void); 	

// Disconnect Right Input PGA to Right Input Boost mixer	
bool wm8960_disconnectRMIC2B(void);	

// 0-63, (0 = -17.25dB) <<-- 0.75dB steps -->> (63 = +30dB)
bool wm8960_setLINVOL(uint8_t volume); 
bool wm8960_setLINVOLDB(float dB);

// 0-63, (0 = -17.25dB) <<-- 0.75dB steps -->> (63 = +30dB)
bool wm8960_setRINVOL(uint8_t volume); 
bool wm8960_setRINVOLDB(float dB);

// Zero Cross prevents zipper sounds on volume changes
bool wm8960_enablePgaZeroCross(void); // Sets both left and right PGAs
bool wm8960_disablePgaZeroCross(void); // Sets both left and right PGAs

bool wm8960_enableLINMUTE(void);
bool wm8960_disableLINMUTE(void);
bool wm8960_enableRINMUTE(void);
bool wm8960_disableRINMUTE(void);

// Causes left and right input PGA volumes to be updated 
// (LINVOL and RINVOL)
bool wm8960_pgaLeftIPVUSet(void); 

// Causes left and right input PGA volumes to be updated 
// (LINVOL and RINVOL)
bool wm8960_pgaRightIPVUSet(void); 

// Boosts

// WM8960_MIC_BOOST_GAIN_0DB or _13DB, _20DB, _29DB
bool wm8960_setLMICBOOST(uint8_t boost_gain); 

// WM8960_MIC_BOOST_GAIN_0DB or _13DB, _20DB, _29DB
bool wm8960_setRMICBOOST(uint8_t boost_gain); 

// WM8960_BOOST_MIXER_GAIN_MUTE, WM8960_BOOST_MIXER_GAIN_NEG_12DB, ...
bool wm8960_setLIN3BOOST(uint8_t boost_gain); 

// WM8960_BOOST_MIXER_GAIN_MUTE, WM8960_BOOST_MIXER_GAIN_NEG_12DB, ...
bool wm8960_setLIN2BOOST(uint8_t boost_gain); 

// WM8960_BOOST_MIXER_GAIN_MUTE, WM8960_BOOST_MIXER_GAIN_NEG_12DB, ...
bool wm8960_setRIN3BOOST(uint8_t boost_gain); 

// WM8960_BOOST_MIXER_GAIN_MUTE, WM8960_BOOST_MIXER_GAIN_NEG_12DB, ...
bool wm8960_setRIN2BOOST(uint8_t boost_gain); 

// Mic Bias control
bool wm8960_enableMicBias(void);
bool wm8960_disableMicBias(void);

// WM8960_MIC_BIAS_VOLTAGE_0_9_AVDD (0.9*AVDD) 
// or WM8960_MIC_BIAS_VOLTAGE_0_65_AVDD (0.65*AVDD)
bool wm8960_setMicBiasVoltage(bool voltage); 

/////////////////////////////////////////////////////////
///////////////////////////////////////////////////////// ADC
/////////////////////////////////////////////////////////

bool wm8960_enableAdcLeft(void);
bool wm8960_disableAdcLeft(void);
bool wm8960_enableAdcRight(void);
bool wm8960_disableAdcRight(void);

// ADC digital volume
// Note, also needs to handle control of the ADCVU bits (volume update).
// Valid inputs are 0-255
// 0 = mute
// 1 = -97dB
// ... 0.5dB steps up to
// 195 = 0dB
// 255 = +30dB
bool wm8960_setAdcLeftDigitalVolume(uint8_t volume); 
bool wm8960_setAdcRightDigitalVolume(uint8_t volume);
bool wm8960_setAdcLeftDigitalVolumeDB(float dB); 
bool wm8960_setAdcRightDigitalVolumeDB(float dB);

// Causes left and right input ADC volumes to be updated
bool wm8960_adcLeftADCVUSet(void); 

// Causes left and right input ADC volumes to be updated
bool wm8960_adcRightADCVUSet(void); 

/////////////////////////////////////////////////////////
///////////////////////////////////////////////////////// ALC
/////////////////////////////////////////////////////////

// Automatic Level Control
// Note that when the ALC function is enabled, the settings of
// Registers 0 and 1 (LINVOL, IPVU, LIZC, LINMUTE, RINVOL, RIZC and 
// RINMUTE) are ignored.

// Also sets alc sample rate to match global sample rate.
bool wm8960_enableAlc(uint8_t mode); 
bool wm8960_disableAlc(void);

// Valid inputs are 0-15
// 0 = -22.5dB FS ... 1.5dB steps ... 15 = -1.5dB FS
bool wm8960_setAlcTarget(uint8_t target); 

// Valid inputs are 0-10, 0 = 24ms, 1 = 48ms ... 10 = 24.58seconds
bool wm8960_setAlcDecay(uint8_t decay); 

// Valid inputs are 0-10, 0 = 6ms, 1 = 12ms, 2 = 24ms ... 
// 10 = 6.14seconds
bool wm8960_setAlcAttack(uint8_t attack); 

// Valid inputs are 0-7, 0 = -12dB, ... 7 = +30dB
bool wm8960_setAlcMaxGain(uint8_t maxGain);

// Valid inputs are 0-7, 0 = -17.25dB, ... 7 = +24.75dB 
bool wm8960_setAlcMinGain(uint8_t attack); 

// Valid inputs are 0-15, 0 = 0ms, ... 15 = 43.691s
bool wm8960_setAlcHold(uint8_t attack); 

// Peak Limiter
bool wm8960_enablePeakLimiter(void);
bool wm8960_disablePeakLimiter(void);

// Noise Gate
bool wm8960_enableNoiseGate(void);
bool wm8960_disableNoiseGate(void);

// 0-31, 0 = -76.5dBfs, 31 = -30dBfs
bool wm8960_setNoiseGateThreshold(uint8_t threshold); 

/////////////////////////////////////////////////////////
///////////////////////////////////////////////////////// DAC
/////////////////////////////////////////////////////////

// Enable/disble each channel
bool wm8960_enableDacLeft(void);
bool wm8960_disableDacLeft(void);
bool wm8960_enableDacRight(void);
bool wm8960_disableDacRight(void);

// DAC digital volume
// Note, also needs to handle control of the DACVU bits (volume update).
// Valid inputs are 0-255
// 0 = mute
// 1 = -127dB
// ... 0.5dB steps up to
// 255 = 0dB
bool wm8960_setDacLeftDigitalVolume(uint8_t volume); 
bool wm8960_setDacRightDigitalVolume(uint8_t volume);	
bool wm8960_setDacLeftDigitalVolumeDB(float dB); 
bool wm8960_setDacRightDigitalVolumeDB(float dB);	

// Causes left and right input DAC volumes to be updated
bool wm8960_dacLeftDACVUSet(void); 

// Causes left and right input DAC volumes to be updated
bool wm8960_dacRightDACVUSet(void); 

// DAC mute
bool wm8960_enableDacMute(void);
bool wm8960_disableDacMute(void);

// DE-Emphasis

// 3D Stereo Enhancement
// 3D enable/disable
bool wm8960_enable3d(void);
bool wm8960_disable3d(void);
bool wm8960_set3dDepth(uint8_t depth); // 0 = 0%, 15 = 100%

// 3D upper/lower cut-off frequencies.

// DAC output -6dB attentuation enable/disable
bool wm8960_enableDac6dbAttenuation(void);
bool wm8960_disableDac6dbAttentuation(void);

//////////////////////////////////////////////////////
////////////////////////////////////////////////////// OUTPUT mixers
//////////////////////////////////////////////////////

// What's connected to what? Oh so many options...
// LOMIX	Left Output Mixer
// ROMIX	Right Output Mixer
// OUT3MIXMono Output Mixer

// Enable/disable left and right output mixers
bool wm8960_enableLOMIX(void);
bool wm8960_disableLOMIX(void);
bool wm8960_enableROMIX(void);
bool wm8960_disableROMIX(void);
bool wm8960_enableOUT3MIX(void);
bool wm8960_disableOUT3MIX(void);

// Enable/disable audio path connections/vols to/from output mixers
// See datasheet page 35 for a nice image of all the connections.

bool wm8960_enableLI2LO(void);
bool wm8960_disableLI2LO(void);

// 0-7, 0 = 0dB, ... 3dB steps ... 7 = -21dB
bool wm8960_setLI2LOVOL(uint8_t volume); 

bool wm8960_enableLB2LO(void);
bool wm8960_disableLB2LO(void);

// 0-7, 0 = 0dB, ... 3dB steps ... 7 = -21dB
bool wm8960_setLB2LOVOL(uint8_t volume); 

bool wm8960_enableLD2LO(void);
bool wm8960_disableLD2LO(void);

bool wm8960_enableRI2RO(void);
bool wm8960_disableRI2RO(void);

// 0-7, 0 = 0dB, ... 3dB steps ... 7 = -21dB
bool wm8960_setRI2ROVOL(uint8_t volume); 

bool wm8960_enableRB2RO(void);
bool wm8960_disableRB2RO(void);

// 0-7, 0 = 0dB, ... 3dB steps ... 7 = -21dB
bool wm8960_setRB2ROVOL(uint8_t volume); 

bool wm8960_enableRD2RO(void);
bool wm8960_disableRD2RO(void);

// Mono Output mixer. 
// Note, for capless HPs, we'll want this to output a buffered VMID.
// To do this, we need to disable both of these connections.
bool wm8960_enableLI2MO(void);
bool wm8960_disableLI2MO(void);
bool wm8960_enableRI2MO(void);
bool wm8960_disableRI2MO(void);

// This will disable both connections, thus enable VMID on OUT3. Note, 
// to enable VMID, you also need to enable OUT3 in the 
// WM8960_REG_PWR_MGMT_2 [1]
//bool wm8960_enableOUT3asVMID(void); 

// Enables VMID in the WM8960_REG_PWR_MGMT_1 register, and set's it to 
// playback/record settings of 2*50Kohm.
bool wm8960_enableVMID(void); 
bool wm8960_disableVMID(void);
bool wm8960_setVMID(uint8_t setting);

/////////////////////////////////////////////////////////
///////////////////////////////////////////////////////// Headphones
/////////////////////////////////////////////////////////

// Enable and disable headphones (mute)
bool wm8960_enableHeadphones(void);
bool wm8960_disableHeadphones(void);
bool wm8960_enableRightHeadphone(void);
bool wm8960_disableRightHeadphone(void);
bool wm8960_enableLeftHeadphone(void);
bool wm8960_disableLeftHeadphone(void);

bool wm8960_enableHeadphoneStandby(void);
bool wm8960_disableHeadphoneStandby(void);

// Set headphone volume
// Although you can control each headphone output independently, here 
// we are going to assume you want both left and right to do the same 
// thing.

// Valid inputs are 47-127. 0-47 = mute, 48 = -73dB ... 1dB steps ...
// 127 = +6dB
bool wm8960_setHeadphoneVolume(uint8_t volume); 
// Updates both left and right channels
// Handles the OUT1VU (volume update) bit control, so that it happens at
// the same time on both channels. Note, we must also make sure that the
// outputs are enabled in the WM8960_REG_PWR_MGMT_2 [6:5]

// Zero Cross prevents zipper sounds on volume changes
// Sets both left and right Headphone outputs
bool wm8960_enableHeadphoneZeroCross(void); 
bool wm8960_disableHeadphoneZeroCross(void);

// Set headphone volume dB
// Sets the volume of the headphone output buffer amp to a speicified
// dB value passed in as a float argument.
// Valid dB settings are -74.0 up to +6.0
// User input will be rounded to nearest whole integer
// -74 (or lower) = MUTE
// -73 = -73dB (MIN)
// ... 1dB steps ...
// 0 = 0dB
// ... 1dB steps ...
// 6 = +6dB  (MAX)
bool wm8960_setHeadphoneVolumeDB(float dB);

bool wm8960_enableHeadphoneJackDetect(void);
bool wm8960_disableHeadphoneJackDetect(void);
bool wm8960_setHeadphoneJackDetectInput(uint8_t setting);

/////////////////////////////////////////////////////////
///////////////////////////////////////////////////////// Speakers
/////////////////////////////////////////////////////////

// Enable and disable speakers (mute)
bool wm8960_enableSpeakers(void);
bool wm8960_disableSpeakers(void);
bool wm8960_enableRightSpeaker(void);
bool wm8960_disableRightSpeaker(void);
bool wm8960_enableLeftSpeaker(void);
bool wm8960_disableLeftSpeaker(void);

// Set Speaker output volume
// Although you can control each Speaker output independently, here we 
// are going to assume you want both left and right to do the same thing.
// Valid inputs are 47-127. 0-47 = mute, 48 = -73dB ... 1dB steps ... 
// 127 = +6dB

bool wm8960_setSpeakerVolume(uint8_t volume); 
uint8_t wm8960_getSpeakerVolume(void);

// Updates both left and right channels
// Handles the SPKVU (volume update) bit control, so that it happens at 
// the same time on both channels. Note, we must also make sure that the 
// outputs are enabled in the WM8960_REG_PWR_MGMT_2 [4:3]
// And the class D control reg WM8960_REG_CLASS_D_CONTROL_1 [7:6]

bool wm8960_setSpeakerVolumeDB(float dB);

// Zero Cross prevents zipper sounds on volume changes
// Sets both left and right Speaker outputs
bool wm8960_enableSpeakerZeroCross(void); 
bool wm8960_disableSpeakerZeroCross(void);	

// DC and AC gain - allows signal to be higher than the DACs swing
// (use only if your SPKVDD is high enough to handle a larger signal)
// Valid inputs are 0-5
// 0 = +0dB (1.0x boost) ... up to ... 5 = +5.1dB (1.8x boost)
bool wm8960_setSpeakerDcGain(uint8_t gain);
bool wm8960_setSpeakerAcGain(uint8_t gain);

//////////////////////////////////////
////////////////////////////////////// Digital audio interface control
//////////////////////////////////////

// Defaults to I2S, peripheral-mode, 24-bit word length

// Loopback
// When enabled, the output data from the ADC audio interface is fed 
// directly into the DAC data input.
bool wm8960_enableLoopBack(void);
bool wm8960_disableLoopBack(void);

///////////////////////////////////////////////////////
/////////////////////////////////////////////////////// Clock controls
///////////////////////////////////////////////////////

// Getting the Frequency of SampleRate as we wish
// Our MCLK (an external clock on the SFE breakout board) is 24.0MHz.
// According to table 40 (DS pg 58), we want SYSCLK to be 11.2896 for a 
// SR of 44.1KHz. To get that Desired Output (SYSCLK), we need the 
// following settings on the PLL stuff:
// As found on table 45 (ds pg 61).
// PRESCALE DIVIDE (PLLPRESCALE): 2
// POSTSCALE DVIDE (SYSCLKDIV[1:0]): 2
// FIXED POST-DIVIDE: 4
// R: 7.5264 
// N: 7h
// K: 86C226h

// Example at bottom of table 46, shows that we should be in fractional 
// mode for a 44.1KHz.

// In terms of registers, this is what we want for 44.1KHz
// PLLEN=1	(PLL enable)
// PLLPRESCALE=1	(divide by 2) *This get's us from MCLK (24MHz) down 
// to 12MHZ for F2
// PLLN=7h	(PLL N value) *this is "int R"
// PLLK=86C226h(PLL K value) *this is int ( 2^24 * (R- intR)) 
// SDM=1	(Fractional mode)
// CLKSEL=1	(PLL select) 
// MS=0(Peripheral mode)
// WL=00	(16 bits)
// SYSCLKDIV=2(Divide by 2)
// ADCDIV=000(Divide by 1) = 44.1kHz
// DACDIV=000(Divide by 1) = 44.1kHz
// BCLKDIV=0100(Divide by 4) = 64fs
// DCLKDIV=111(Divide by 16) = 705.6kHz

// And now for the functions that will set these registers...
bool wm8960_enablePLL(void);
bool wm8960_disablePLL(void);

// Valid options are WM8960_PLLPRESCALE_DIV_1, WM8960_PLLPRESCALE_DIV_2
bool wm8960_setPLLPRESCALE(bool div); 

bool wm8960_setPLLN(uint8_t n);

// Send each nibble of 24-bit value for value K
bool wm8960_setPLLK(uint8_t one, uint8_t two, uint8_t three); 

bool wm8960_setSMD(bool mode); // 0=integer, 1=fractional
bool wm8960_setCLKSEL(bool sel); // 0=MCLK, 1=PLL_output

// (0=divide by 1), (2=div by 2) *1 and 3 are "reserved"
bool wm8960_setSYSCLKDIV(uint8_t div); 

// 000 = SYSCLK / (1.0*256). See ds pg 57 for other options
bool wm8960_setADCDIV(uint8_t div); 

// 000 = SYSCLK / (1.0*256). See ds pg 57 for other options
bool wm8960_setDACDIV(uint8_t div); 

// 0100 (4) = sufficiently high for 24bit, div by 4 allows for max word 
// length of 32bit
bool wm8960_setBCLKDIV(uint8_t div); 

// Class D amp, 111= SYSCLK/16, so 11.2896MHz/16 = 705.6KHz
bool wm8960_setDCLKDIV(uint8_t div); 

// Set LR clock to be the same for ADC & DAC (needed for loopback mode)
bool wm8960_setALRCGPIO(void); 

bool wm8960_enableMasterMode(void);
bool wm8960_enablePeripheralMode(void);

bool wm8960_setWL(uint8_t word_length);

bool wm8960_setLRP(bool polarity);

bool wm8960_setALRSWAP(bool swap);

bool wm8960_setVROI(bool setting);

bool wm8960_setVSEL(uint8_t setting);

bool wm8960_dac_stereomono(bool stereo);	//0 stereo, 1 mono

bool wm8960_adc_datasel(uint8_t sel);   //0 l2l r2r, 1 l2l l2r, 2 r2l r2r, 3 r2l l2r

bool wm8960_setFORMAT(uint8_t format);

// **The WM8960 does not support reading registers!!!
uint8_t wm8960_convertDBtoSetting(float dB, float offset, float stepSize, float minDB, float maxDB);

esp_err_t wm8960_init(void);
esp_err_t wm8960_deinit(void);
esp_err_t wm8960_adc_onoff(bool on);
esp_err_t wm8960_dac_onoff(bool on);
esp_err_t wm8960_both_onoff(bool on);
esp_err_t wm8960_speaker_onoff(bool on);
bool wm8960_showRegister(void);
#endif
