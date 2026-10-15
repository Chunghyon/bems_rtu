#if 0
#include "math.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"

#include "hh_i2s.h"
#include "hh_parameters.h"

#define TAG "tone"

extern QueueHandle_t queue_to_codec;

void play_tone(float freq, int duration_ms)
{
    int samples_total = PP_SAMPLE_RATE * duration_ms / 1000;
    int samples_sent = 0;
    float phase = 0.0f;
    float phase_inc = 2.0f * M_PI * freq / PP_SAMPLE_RATE;

    while (samples_sent < samples_total) {
		int16_t pcm_buf[PP_SAMPLES_PER_FRAME] = {0};
        for (int i = 0; i < PP_SAMPLES_PER_FRAME; i++) {
			if(samples_sent + i >= samples_total) {
				break;
			}
            pcm_buf[i] = (int16_t)(32767 * sinf(phase));
            phase += phase_inc;
            if (phase > 2.0f * M_PI) phase -= 2.0f * M_PI;
        }
#if (QUEUE_DEPTH_TO_CODEC > 1)
		if (xQueueSend(queue_to_codec, pcm_buf, pdMS_TO_TICKS(PP_FRAME_TIMEOUT_MS)) != pdTRUE) {
#else
		if (xQueueOverwrite(queue_to_codec, pcm_buf) != pdTRUE) {
#endif
			ESP_LOGW(TAG, "Failed to send to queue_to_codec");
		}
		else {
			samples_sent += PP_SAMPLES_PER_FRAME;
		}
    }
}
#endif