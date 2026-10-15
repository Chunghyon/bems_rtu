#if 0
#include <stdio.h>
#include "stdint.h"
#include "string.h"
#include <math.h>

#include "esp_system.h"
#include "esp_mac.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include "esp_timer.h"

#include "driver/gpio.h"
#include "driver/i2s_std.h"
#include "driver/ledc.h"

#include "wm8960.h"
#include "hh_parameters.h"
#include "hh_i2s.h"
#include "hh_spiffs.h"

#define TAG "i2s"
static int i2s_log_level = ESP_LOG_INFO;

static bool cb_on_recv(i2s_chan_handle_t handle, i2s_event_data_t *event, void *user_ctx);
static bool cb_on_recv_ovf(i2s_chan_handle_t handle, i2s_event_data_t *event, void *user_ctx);
//static bool cb_on_sent(i2s_chan_handle_t handle, i2s_event_data_t *event, void *user_ctx);
static const i2s_event_callbacks_t i2s_event_cb = 
{
	.on_recv = cb_on_recv,
	.on_recv_q_ovf = cb_on_recv_ovf,
//	.on_sent = cb_on_sent,
	.on_sent = NULL,
};

QueueHandle_t queue_to_codec;
QueueHandle_t queue_fr_codec;

bool loopback_to_codec;

static void i2s_read_task(void *args);
static void i2s_write_task(void *args);

static i2s_chan_handle_t chan_handler_tx;        // I2S tx channel handler
static i2s_chan_handle_t chan_handler_rx;        // I2S rx channel handler
static QueueHandle_t i2s0_tx_queue;
static QueueHandle_t i2s0_rx_queue;
static TaskHandle_t task_handle_i2s_read;
static TaskHandle_t task_handle_i2s_write;

//static void send_voice_to_codec(void);
static void receive_voice_from_codec(void);

#ifdef PP_USE_SEMAPHORE_FOR_I2S_TX
static SemaphoreHandle_t send_sem;
#endif
#ifdef PP_USE_SEMAPHORE_FOR_I2S_RX
static SemaphoreHandle_t recv_sem;
#endif

static void i2s_read_task(void *args)
{
	while (1) {
		i2s_event_new_t evt;
		if(xQueueReceive(i2s0_rx_queue, &evt, portMAX_DELAY))
		{
			switch(evt.id)
			{
			case I2S_EVENT_ID_RX_DONE :
			{
#if 0
				static int64_t last_time = 0;
				int64_t now = esp_timer_get_time(); // 마이크로초(us) 단위
				if (last_time != 0) {
					ESP_LOGI(TAG, "I2S RX_DONE interval: %lld us", now - last_time);
				}
				last_time = now;
#endif
				receive_voice_from_codec();
				break;
			}
			case I2S_EVENT_ID_RX_OVF :
				ESP_LOGW(TAG, "RX buffer overflow");
				break;
			default :
				ESP_LOGD(TAG, "evt : %d", evt.id);
				break;
			}
		}
	}
	vTaskDelete(NULL);
}

static void i2s_write_task(void *args)
{
	while (1) {
		esp_err_t err;
		int16_t pcm_in[PP_SAMPLES_PER_FRAME] = {0,};
		size_t bytes_written;
	
#ifdef PP_USE_SEMAPHORE_FOR_I2S_TX
		xSemaphoreTake(send_sem, portMAX_DELAY);
#endif
	
		BaseType_t res = xQueueReceive(queue_to_codec, pcm_in, portMAX_DELAY);
		if (res == pdTRUE) {
			err = i2s_channel_write(chan_handler_tx, pcm_in, sizeof(pcm_in), &bytes_written, pdMS_TO_TICKS(PP_FRAME_TIMEOUT_MS));
			if (err != ESP_OK) ESP_LOGW(TAG, "I2S write error: %d", err);
			else {
				if (bytes_written != PP_SAMPLES_PER_FRAME*2) {
					ESP_LOGW(TAG, "I2S write failed: %d bytes written", bytes_written);
				}
				else {
					CC_LOGN("W");
				}
			}
		}
#ifdef PP_USE_SEMAPHORE_FOR_I2S_TX
		xSemaphoreGive(send_sem);
#endif
#if 0
		i2s_event_new_t evt;
		if(xQueueReceive(i2s0_tx_queue, &evt, portMAX_DELAY))
		{
			switch(evt.id)
			{
			case I2S_EVENT_ID_TX_DONE :
				send_voice_to_codec();
				break;
			case I2S_EVENT_ID_TX_OVF :
				ESP_LOGW(TAG, "TX buffer overflow");
				break;
			default :
				ESP_LOGD(TAG, "evt : %d", evt.id);
				break;
			}
			vTaskDelay(pdMS_TO_TICKS(10));
		}
#endif
	}
	vTaskDelete(NULL);
}

static bool cb_on_recv(i2s_chan_handle_t handle, i2s_event_data_t *event, void *user_ctx)
{
	if(user_ctx != chan_handler_rx) {
		ESP_LOGD(TAG, "user_ctx != chan_handler_rx");
		return true;
	}

	i2s_event_new_t evt = { .id =I2S_EVENT_ID_RX_DONE, .p_data = event, };
	portBASE_TYPE xTaskWokenByPost = pdFALSE;
	xQueueSendFromISR(i2s0_rx_queue, &evt, &xTaskWokenByPost);
	if(xTaskWokenByPost) portYIELD_FROM_ISR();
	return true;
}

static bool cb_on_recv_ovf(i2s_chan_handle_t handle, i2s_event_data_t *event, void *user_ctx)
{
	if(user_ctx != chan_handler_rx) {
		ESP_LOGD(TAG, "user_ctx != chan_handler_rx");
		return true;
	}

	i2s_event_new_t evt = { .id =I2S_EVENT_ID_RX_OVF, .p_data = event, };
	portBASE_TYPE xTaskWokenByPost = pdFALSE;
	xQueueSendFromISR(i2s0_rx_queue, &evt, &xTaskWokenByPost);
	if(xTaskWokenByPost) portYIELD_FROM_ISR();
	return true;
}

static void receive_voice_from_codec(void)
{
	size_t read_len;
	int16_t pcm_in[PP_SAMPLES_PER_FRAME] = {0,};
	//int16_t pcm_out[PP_SAMPLES_PER_FRAME] = {0,};

#ifdef PP_USE_SEMAPHORE_FOR_I2S_RX
	xSemaphoreTake(recv_sem, portMAX_DELAY);
#endif

	esp_err_t ret = ESP_FAIL;
#if 0
	for (int retry = 0; retry < 2; retry++) {
		ret = i2s_channel_read(chan_handler_rx, pcm_in, sizeof(pcm_in), &read_len, pdMS_TO_TICKS(PP_FRAME_LATENCY_MS));
		if (ret == ESP_OK) {
			break;
		}
	}

	if (ret != ESP_OK) {
		ESP_LOGW(TAG, "I2S read failed after retries: %d", ret);
	}
#else
//	ret = i2s_channel_read(chan_handler_rx, pcm_in, sizeof(pcm_in), &read_len, pdMS_TO_TICKS(PP_FRAME_LATENCY_MS));
	ret = i2s_channel_read(chan_handler_rx, pcm_in, sizeof(pcm_in), &read_len, 0);
	if (ret != ESP_OK) {
			ESP_LOGW(TAG, "I2S read failed: %s", esp_err_to_name(ret));
	}
#endif
	else if (read_len == sizeof(pcm_in)) {
		if (loopback_to_codec) {
#if (QUEUE_DEPTH_TO_CODEC > 1)
			if (xQueueSend(queue_to_codec, pcm_in, PP_FRAME_LATENCY_MS) != pdTRUE) {
#else
			if (xQueueOverwrite(queue_to_codec, pcm_in) != pdTRUE) {
#endif
				//ESP_LOGW(TAG, "Failed to send to queue_to_codec");
			}
			else {
				CC_LOGN("R");
			}
		}
		else {
#if (QUEUE_DEPTH_FR_CODEC > 1)
			if (xQueueSend(queue_fr_codec, pcm_in, PP_FRAME_LATENCY_MS) != pdTRUE) {
#else
			if (xQueueOverwrite(queue_fr_codec, pcm_in) != pdTRUE) {
#endif
				//ESP_LOGW(TAG, "Failed to send to queue_fr_codec");
			}
		}
	}
	else {
		ESP_LOGW(TAG, "I2S read incomplete: %d bytes", read_len);
	}
#ifdef PP_USE_SEMAPHORE_FOR_I2S_RX
	xSemaphoreGive(recv_sem);
#endif
}

#ifndef PP_USE_I2S_MCLK_OUTPUT
static ledc_timer_config_t ledc_timer = {
	.speed_mode 		= LEDC_LOW_SPEED_MODE,
	.timer_num  		= LEDC_TIMER_0,
	.duty_resolution	= 2,				//2-bits resolution
	.freq_hz			= PP_MCLK_FREQ		//12.288MHz or 24.0MHz
};

static ledc_channel_config_t ledc_channel = {
	.channel	= LEDC_CHANNEL_0,
	.gpio_num   = I2S_MCLK,					//GPIO_NUM_1
	.speed_mode = LEDC_LOW_SPEED_MODE,
	.timer_sel  = LEDC_TIMER_0,
	.duty		= 2							//50% duty
};
#endif

void change_i2s_log_level(void)
{
	i2s_log_level = (i2s_log_level == ESP_LOG_DEBUG) ? ESP_LOG_INFO : ESP_LOG_DEBUG;
	esp_log_level_set(TAG, i2s_log_level);
}

void ff_i2s_install(void)
{
	static bool driver_is_installed = false;
	if(driver_is_installed) return;

	i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(PP_I2S_NUM, I2S_ROLE_MASTER);
	chan_cfg.dma_desc_num = I2S_DMA_BUFFER_COUNT;
	chan_cfg.dma_frame_num = PP_SAMPLES_PER_FRAME;
	chan_cfg.auto_clear = true;
	ESP_ERROR_CHECK(i2s_new_channel(&chan_cfg, &chan_handler_tx, &chan_handler_rx));

	i2s_std_config_t std_cfg = {
		.clk_cfg  = I2S_STD_CLK_DEFAULT_CONFIG(PP_SAMPLE_RATE),
		.slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO),
		.gpio_cfg = {
#ifdef PP_USE_I2S_MCLK_OUTPUT
			.mclk = I2S_MCLK,
#else
			.mclk = I2S_GPIO_UNUSED,
#endif
			.bclk = I2S_BCLK,
			.ws   = I2S_LRCK,
			.dout = I2S_DOUT,
			.din  = I2S_DIN,
			.invert_flags = {
				.mclk_inv = false,
				.bclk_inv = false,
				.ws_inv   = false,
			},
		},
	};

	i2s_std_config_t std_cfg_rx = std_cfg;
	std_cfg_rx.slot_cfg.slot_mask = I2S_STD_SLOT_RIGHT; // mono

	i2s_std_config_t std_cfg_tx = std_cfg;
	std_cfg_tx.slot_cfg.slot_mask = I2S_STD_SLOT_BOTH; 

	/* Initialize the channels */
	ESP_ERROR_CHECK(i2s_channel_init_std_mode(chan_handler_rx, &std_cfg_rx));
	ESP_ERROR_CHECK(i2s_channel_init_std_mode(chan_handler_tx, &std_cfg_tx));

	ESP_ERROR_CHECK(i2s_channel_register_event_callback(chan_handler_rx, &i2s_event_cb, chan_handler_rx));
	//ESP_ERROR_CHECK(i2s_channel_register_event_callback(chan_handler_tx, &i2s_event_cb, chan_handler_tx));

	ESP_ERROR_CHECK(i2s_channel_enable(chan_handler_rx));
	ESP_ERROR_CHECK(i2s_channel_enable(chan_handler_tx));

	driver_is_installed = true;
}

void ff_i2s_init(void)
{
	esp_log_level_set(TAG, i2s_log_level);

#ifndef PP_USE_I2S_MCLK_OUTPUT
	ledc_timer_config(&ledc_timer);
	ledc_channel_config(&ledc_channel);
#endif

	queue_to_codec = xQueueCreate( QUEUE_DEPTH_TO_CODEC, PP_SAMPLES_PER_FRAME*2 );
	queue_fr_codec	= xQueueCreate( QUEUE_DEPTH_FR_CODEC, PP_SAMPLES_PER_FRAME*2 );

	i2s0_tx_queue = xQueueCreate(8, sizeof(i2s_event_new_t));
	i2s0_rx_queue = xQueueCreate(8, sizeof(i2s_event_new_t));

	ff_i2s_install();

#ifdef PP_USE_SEMAPHORE_FOR_I2S_TX
	send_sem = xSemaphoreCreateBinary();
	xSemaphoreGive(send_sem);
#endif
#ifdef PP_USE_SEMAPHORE_FOR_I2S_RX
	recv_sem = xSemaphoreCreateBinary();
	xSemaphoreGive(recv_sem);
#endif

	xTaskCreate(i2s_read_task, "i2s__rd", 8192, NULL, 6, &task_handle_i2s_read);
	xTaskCreate(i2s_write_task, "i2s__wr", 8192, NULL, 5, &task_handle_i2s_write);
}

// 1. 녹음(저장)
void record_audio_to_flash(char * file, size_t total_samples)
{
	esp_err_t err;
	err = ff_mount_spiffs_audio();
	if (err != ESP_OK) {
		ESP_LOGE(TAG, "Failed to mount SPIFFS");
		return;
	}

	FILE *fp = fopen(file, "wb");
	size_t recorded = 0;
	int16_t pcm_in[PP_SAMPLES_PER_FRAME];
	while (recorded < total_samples) {
		size_t read_len = 0;
		i2s_channel_read(chan_handler_rx, pcm_in, sizeof(pcm_in), &read_len, portMAX_DELAY);
		fwrite(pcm_in, 1, read_len, fp);
		recorded += read_len / sizeof(int16_t);
	}
	fclose(fp);

	ff_unmount_spiffs_audio();
}

// 2. 재생
void play_audio_from_flash(char * file)
{
	esp_err_t err;
	err = ff_mount_spiffs_audio();
	if (err != ESP_OK) {
		ESP_LOGE(TAG, "Failed to mount SPIFFS");
		return;
	}

	FILE *fp = fopen(file, "rb");
	int16_t pcm_out[PP_SAMPLES_PER_FRAME];
	size_t read_bytes;
	while ((read_bytes = fread(pcm_out, sizeof(int16_t), PP_SAMPLES_PER_FRAME, fp)) > 0) {
		size_t bytes_written = 0;
		i2s_channel_write(chan_handler_tx, pcm_out, read_bytes * sizeof(int16_t), &bytes_written, portMAX_DELAY);
	}
	fclose(fp);
	ff_unmount_spiffs_audio();
}

// [추가] 외부에서 PCM 데이터 재생을 요청하는 함수
void cc_i2s_play_pcm_data(const void *data, size_t size)
{
    // data: 재생할 Raw PCM 데이터
    // size: 데이터 길이 (bytes)
    // 주의: Blocking 방식으로 동작하며, 완료될 때까지 기다립니다.
    
    if (chan_handler_tx == NULL) {
        ESP_LOGE(TAG, "I2S TX channel is not initialized");
        return;
    }

    size_t written = 0;
    size_t total_written = 0;
    const uint8_t *ptr = (const uint8_t *)data;

    // 한 번에 너무 많이 쓰면 워치독이 걸릴 수 있으므로 청크 단위로 분할 전송
    const size_t CHUNK_SIZE = 1024; 

    while (total_written < size) {
        size_t to_write = size - total_written;
        if (to_write > CHUNK_SIZE) to_write = CHUNK_SIZE;

        esp_err_t ret = i2s_channel_write(chan_handler_tx, ptr + total_written, to_write, &written, portMAX_DELAY);
        
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to write I2S: %s", esp_err_to_name(ret));
            break;
        }
        total_written += written;
    }
}
#endif
