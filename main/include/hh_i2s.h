#ifndef HH_I2S_H__
#define HH_I2S_H__

#include "driver/i2s_std.h"

#define PP_USE_I2S_MCLK_OUTPUT
#define SYSCLK_FROM_PLLx

#ifdef SYSCLK_FROM_PLL
#define PP_MCLK_FREQ			24000000//24MHz
#else
#define PP_MCLK_FREQ			12288000//12.288MHz
#endif
#define PP_I2S_NUM				I2S_NUM_0
#define	PP_SAMPLE_RATE			16000
#define PP_SAMPLES_PER_FRAME	256			//256 samples in 16kHz ==> 16ms
#define PP_FRAME_LATENCY_MS		((int)(1000*PP_SAMPLES_PER_FRAME/PP_SAMPLE_RATE))	//in msec
#define PP_FRAME_TIMEOUT_MS		((int)(PP_FRAME_LATENCY_MS*1.1))
#define I2S_DMA_BUFFER_COUNT	2

#define PP_USE_SEMAPHORE_FOR_I2S_TXx
#define PP_USE_SEMAPHORE_FOR_I2S_RXx

#define QUEUE_DEPTH_TO_CODEC	4	// if 1, queue overwrite is expected
#define QUEUE_DEPTH_FR_CODEC	4	// if 1, queue overwrite is expected

typedef enum
{
	I2S_EVENT_ID_TX_DONE,
	I2S_EVENT_ID_RX_DONE,
	I2S_EVENT_ID_TX_OVF,
	I2S_EVENT_ID_RX_OVF,
} i2s_event_id_t;

typedef struct {
	i2s_event_id_t id;
	i2s_event_data_t * p_data;
} i2s_event_new_t;

extern void record_audio_to_flash(char * file, size_t total_samples);
extern void play_audio_from_flash(char * file);
extern void ff_i2s_install(void);
extern void ff_i2s_init(void);

#endif //HH_I2S_H__
