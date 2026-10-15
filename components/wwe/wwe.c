/*
 * ESPRESSIF MIT License
 *
 * Copyright (c) 2024 <ESPRESSIF SYSTEMS (SHANGHAI) CO., LTD>
 *
 * Permission is hereby granted for use on all ESPRESSIF SYSTEMS products, in which case,
 * it is free of charge, to any person obtaining a copy of this software and associated
 * documentation files (the "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the Software is furnished
 * to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all copies or
 * substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
 * FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
 * COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 * IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 */

#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include "esp_log.h"

#include "amrnb_encoder.h"
#include "amrwb_encoder.h"
#include "audio_element.h"
#include "audio_idf_version.h"
#include "audio_mem.h"
#include "audio_pipeline.h"
#include "audio_recorder.h"
#include "audio_thread.h"
#include "prompts_uri.h"
#include "board.h"
#include "esp_audio.h"
#include "filter_resample.h"
#include "i2s_stream.h"
#include "mp3_decoder.h"
#include "periph_adc_button.h"
#include "raw_stream.h"
#include "recorder_encoder.h"
#include "recorder_sr.h"
#include "tone_stream.h"
#include "es7210.h"

#include "model_path.h"
#include "wm8960.h"
#include "hh_parameters.h"
#include "hh_util.h"

#include "esp_vfs.h"
#include "esp_vfs_fat.h"

#include "esp_heap_caps.h"
#include "freertos/semphr.h"

#include "hh_SPI.h"
#include "ftp.h"

static char *TAG = "wwe";

#define NO_ENCODER  (0)
#define ENC_2_AMRNB (1)
#define ENC_2_AMRWB (2)

#define RECORDER_ENC_ENABLE (NO_ENCODER)
#define WAKENET_ENABLE      (1)		// 1/0, Wakenet 사용 함/안함
#define MULTINET_ENABLE     (false)		// 1/0, Multinet(명령어 인식) 사용 함/안함
#define SPEECH_CMDS_RESET   (false)

#define SPEECH_COMMANDS     ("da kai dian deng,kai dian deng;guan bi dian deng,guan dian deng;guan deng;")

#ifndef CODEC_ADC_SAMPLE_RATE
#define CODEC_ADC_SAMPLE_RATE    48000
#endif

#ifndef CODEC_ADC_BITS_PER_SAMPLE
#define CODEC_ADC_BITS_PER_SAMPLE  16
#endif

#ifndef RECORD_HARDWARE_AEC
#define RECORD_HARDWARE_AEC  (false)
#endif

#ifndef CODEC_ADC_I2S_PORT
#define CODEC_ADC_I2S_PORT  (0)
#endif


// [전역 변수]
psram_data_t g_psram_voice[MAX_VOICE_FILES_COUNT] = {0}; 
static int current_file_idx = 0;              // 현재 녹음 중인 파일 번호
static uint8_t *g_psram_base_ptr = NULL;      // 전체 메모리 베이스 포인터

SemaphoreHandle_t g_voice_mutex = NULL;

void ff_play_prompt(tone_type_t type);

static esp_audio_handle_t     player        = NULL;
static audio_rec_handle_t     recorder      = NULL;
static audio_element_handle_t raw_read      = NULL;
static QueueHandle_t          rec_q         = NULL;
static bool                   voice_reading = false;

static audio_pipeline_handle_t global_pipeline = NULL;
static audio_element_handle_t  global_raw_read = NULL;
static audio_element_handle_t global_i2s_writer = NULL;

static bool voice_under_recording = false;

extern bool LEARNING_MODE;
extern tone_type_t tone_type_to_play;
extern int g_current_max_face;

extern void reset_face_storage(int new_max_count);
extern void send_slm_packet_toPC(slm_opcode_t opcode, bool isLearn);
#ifdef PP_ONLY_FOR_DEMO
extern void send_slm_packet_toDataServer(slm_opcode_t opcode);
#endif
void voice_mem_init(void) {
    if (g_voice_mutex == NULL) {
        g_voice_mutex = xSemaphoreCreateMutex();
    }

    // [추가] 뮤텍스 획득 후 안전하게 할당
    if (xSemaphoreTake(g_voice_mutex, pdMS_TO_TICKS(500)) == pdTRUE) {
        
        // 이미 할당되어 있으면 스킵
        if (g_psram_base_ptr != NULL) {
             xSemaphoreGive(g_voice_mutex);
             return; 
        }

        uint32_t total_alloc_size = MAX_VOICE_FILES_COUNT * VOICE_FILE_SIZE_LIMIT;
        
        // 메모리 부족 시 heap_caps_get_free_size 등으로 체크 가능하지만 일단 시도
        g_psram_base_ptr = heap_caps_malloc(total_alloc_size, MALLOC_CAP_SPIRAM);
        
        if (g_psram_base_ptr == NULL) {
            ESP_LOGE(TAG, "Failed to allocate PSRAM for voice! (Out of Mem?)");
            xSemaphoreGive(g_voice_mutex);
            return;
        }
        
        ESP_LOGI(TAG, "Voice memory allocated/re-allocated: %lu bytes", total_alloc_size);
        
        // 포인터 분할 할당 및 초기화
        for (int i = 0; i < MAX_VOICE_FILES_COUNT; i++) {
            g_psram_voice[i].data = g_psram_base_ptr + (i * VOICE_FILE_SIZE_LIMIT);
            g_psram_voice[i].len = 0;
            g_psram_voice[i].is_valid = false;
        }
        xSemaphoreGive(g_voice_mutex);
    }
}

void voice_record_start(void)
{
	request_spi_magic_to_master(SPI_VOICE_RUNNING);

    if (LEARNING_MODE) {
		ESP_LOGI(TAG, "Starting Voice Learning Mode...");
    } else {
        ESP_LOGI(TAG, "Starting Voice Normal Mode...");
    }

#ifdef PP_VOICE_FACE_PSRAM_RELEASE
    reset_face_storage(g_current_max_face);
#endif
	vTaskDelay(pdMS_TO_TICKS(10));
    
    // 메모리 할당 시도 (실패 시 재시도)
    int retry = 3;
    while (retry--) {
        voice_mem_init();
        
        if (g_psram_base_ptr != NULL) {
            break; // 성공
        }
        
        ESP_LOGW(TAG, "Voice alloc failed. Retrying... (%d)", retry);
        vTaskDelay(pdMS_TO_TICKS(100)); // 힙 정리를 기다림
    }

    if (g_psram_base_ptr == NULL) {
        size_t free_psram = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
        ESP_LOGE(TAG, "Cannot start recording. Memory alloc failed. Free PSRAM: %u", free_psram);
        return; 
    }

    if(LEARNING_MODE) ff_play_prompt(TONE_TYPE_VSTART);

    // [전체 초기화]
    if (g_voice_mutex != NULL && xSemaphoreTake(g_voice_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        for (int i = 0; i < MAX_VOICE_FILES_COUNT; i++) {
            g_psram_voice[i].len = 0;
            g_psram_voice[i].is_valid = false;
        }
        current_file_idx = 0;
        voice_under_recording = true;
        xSemaphoreGive(g_voice_mutex);
    }
}

void voice_record_stop(void)
{
    voice_under_recording = false;

	send_slm_packet_toPC(SLM_VOICE_READY, LEARNING_MODE);
#ifdef PP_ONLY_FOR_DEMO
	if(!LEARNING_MODE) {
		send_slm_packet_toDataServer(SLM_VOICE_READY);
	}
#endif
    // 현재 녹음 중이던 파일도 유효 처리
    if (current_file_idx < MAX_VOICE_FILES_COUNT && g_psram_voice[current_file_idx].len > 0) {
        g_psram_voice[current_file_idx].is_valid = true;
    }

    ESP_LOGI(TAG, "Voice Learn Stopped. Total %d files recorded.", current_file_idx + (g_psram_voice[current_file_idx].len > 0 ? 1 : 0));

    if(LEARNING_MODE) tone_type_to_play = TONE_TYPE_STOP;
}

// 외부에서 호출할 수 있는 안전한 메모리 해제 함수
void release_voice_memory(void) {
    if (g_voice_mutex != NULL && xSemaphoreTake(g_voice_mutex, pdMS_TO_TICKS(500)) == pdTRUE) {
        
        ESP_LOGI(TAG, "Releasing Voice Memory Block...");

        // 1. 개별 포인터 무효화 (Race Condition 방지)
        for (int i = 0; i < MAX_VOICE_FILES_COUNT; i++) {
            g_psram_voice[i].data = NULL;
            g_psram_voice[i].len = 0;
            g_psram_voice[i].is_valid = false;
        }

        // 2. 실제 메모리 블록 해제 (베이스 포인터만 free)
        if (g_psram_base_ptr != NULL) {
            free(g_psram_base_ptr);
            g_psram_base_ptr = NULL; // 다시 NULL로 초기화
            ESP_LOGI(TAG, "Voice Base Memory Freed.");
        }
        
        // 3. 녹음 상태 강제 초기화
        voice_under_recording = false;
        current_file_idx = 0;

        xSemaphoreGive(g_voice_mutex);
    } else {
        ESP_LOGE(TAG, "Failed to take mutex for release_voice_memory");
    }
}

static int input_cb_for_afe(int16_t *buffer, int buf_sz, void *user_ctx, TickType_t ticks)
{
    // Raw Stream은 설정상 Stereo(2ch) 데이터를 리턴함
    int ret = raw_stream_read(raw_read, (char *)buffer, buf_sz);

    // [중요] 뮤텍스 보호 추가
    // Try to take mutex with minimal wait. If failed, skip this chunk to avoid glitch/block.
    if (ret > 0 && voice_under_recording) {
        
        if (g_voice_mutex != NULL && xSemaphoreTake(g_voice_mutex, 0) == pdTRUE) { // 0 ticks wait

            // 안전장치 다시 확인 (뮤텍스 잡은 후 상태가 변했을 수 있음)
            if (g_psram_base_ptr != NULL && current_file_idx < MAX_VOICE_FILES_COUNT) {
                
                // 해당 슬롯의 데이터 포인터가 유효한지 확인 (SPI Task가 날렸을 수도 있음)
                if (g_psram_voice[current_file_idx].data != NULL) {
                     psram_data_t *curr_file = &g_psram_voice[current_file_idx];

                    // Stereo(2ch) -> Mono(1ch) 변환하여 저장
                    int total_samples = ret / 2;
                    int stereo_frames = total_samples / 2; 
                    int bytes_to_write = stereo_frames * 2; 

                    // 파일 용량 초과 체크
                    if ((curr_file->len + bytes_to_write) > VOICE_FILE_SIZE_LIMIT) {
                         // ... (기존 용량 초과 로직) ...
                         // 주의: 여기서 current_file_idx가 변경되면, 루프 밖이나 다음 호출부터 반영됨
                         // 여기 로직은 그대로 유지하되, 포인터 접근만 주의
                         
                         curr_file->is_valid = true;
                         ESP_LOGI(TAG, "RAM File %d saved (%lu bytes).", current_file_idx + 1, curr_file->len);
                         
                         if (LEARNING_MODE) {
                            current_file_idx++;
                            if (current_file_idx >= MAX_VOICE_FILES_COUNT) {
                                voice_under_recording = false;
                                ESP_LOGW(TAG, "All 15 record slots full! Stopping...");
                                voice_record_stop();
                                xSemaphoreGive(g_voice_mutex); // [필수] 반환 후 리턴
                                return ret;
                            }
                            // 다음 파일 포인터 유효성 체크
                            if(g_psram_voice[current_file_idx].data == NULL) {
                                ESP_LOGE(TAG, "Next slot ptr is NULL! Stopping.");
                                voice_under_recording = false;
                                voice_record_stop();
                                xSemaphoreGive(g_voice_mutex); // [필수] 반환 후 리턴
                                return ret;
                            }
                        }
                        else {
                            voice_under_recording = false;
                            ESP_LOGI(TAG, "Single recording finished.");
                            voice_record_stop();
                            xSemaphoreGive(g_voice_mutex); // [필수] 반환 후 리턴
                            return ret;
                        }
                   }

                   // [데이터 저장]
                   // 여기서 다시 한 번 data 포인터가 NULL이 아닌지 확인 (위에서 했지만 안전하게)
                   if (curr_file->data != NULL) {
                        int16_t *src = (int16_t *)buffer;
                        int16_t *dst = (int16_t *)(curr_file->data + curr_file->len);

                        for (int i = 0; i < stereo_frames; i++) {
                            dst[i] = src[i * 2]; // Left Channel only
                        }
                        curr_file->len += bytes_to_write;
                   }
                }
            }
            // 작업 완료 후 반환
            xSemaphoreGive(g_voice_mutex);
        }
        else {
             // 뮤텍스를 못 잡음 -> 드물겠지만, 이 경우 현재 프레임은 버려짐 (녹음 깨짐 방지보다는 시스템 크래시 방지가 우선)
             // ESP_LOGW(TAG, "Skipped recording frame due to mutex contention");
        }
    }
    return ret;
}

static void voice_read_task(void *args)
{
    int msg = 0;

	rec_q = xQueueCreate(3, sizeof(int));

    while (true) {
        // [수정] 큐 대기 (Non-blocking으로 변경하여 학습 모드 체크)
        if (xQueueReceive(rec_q, &msg, portMAX_DELAY) == pdTRUE) {
            switch (msg) {
                case REC_START: {
                    ESP_LOGW(TAG, "voice read begin");
                    voice_reading = true;
					voice_record_start();
                    break;
                }
                case REC_STOP: {
                    ESP_LOGW(TAG, "voice read stopped");
                    voice_reading = false;
                    break;
                }
                case REC_CANCEL: {
                    ESP_LOGW(TAG, "voice read cancel");
                    voice_reading = false;
                    break;
                }
                default: break;
            }
        }
    }

    vTaskDelete(NULL);
}

static esp_err_t rec_engine_cb(audio_rec_evt_t *event, void *user_data)
{
	if(LEARNING_MODE == true) {
		return ESP_OK;
	}

	if (AUDIO_REC_WAKEUP_START == event->type) {
		recorder_sr_wakeup_result_t *wakeup_result = event->event_data;

		ESP_LOGI(TAG, "rec_engine_cb - REC_EVENT_WAKEUP_START");
		ESP_LOGI(TAG, "wakeup: vol %f, mod idx %d, word idx %d", wakeup_result->data_volume, wakeup_result->wakenet_model_index, wakeup_result->wake_word_index);
		esp_audio_sync_play(player, tone_uri[TONE_TYPE_HELLO], 0);
		if (voice_reading) {
			int msg = REC_CANCEL;
			if (xQueueSend(rec_q, &msg, 0) != pdPASS) {
				ESP_LOGE(TAG, "rec cancel send failed");
			}
		}
	} else if (AUDIO_REC_VAD_START == event->type) {
		ESP_LOGI(TAG, "rec_engine_cb - REC_EVENT_VAD_START");
		if (!voice_reading) {
			int msg = REC_START;
			if (xQueueSend(rec_q, &msg, 0) != pdPASS) {
				ESP_LOGE(TAG, "rec start send failed");
			}
		}
	} else if (AUDIO_REC_VAD_END == event->type) {
		ESP_LOGI(TAG, "rec_engine_cb - REC_EVENT_VAD_STOP");
		if (voice_reading) {
			int msg = REC_STOP;
			if (xQueueSend(rec_q, &msg, 0) != pdPASS) {
				ESP_LOGE(TAG, "rec stop send failed");
			}
		}
	} else if (AUDIO_REC_WAKEUP_END == event->type) {
		ESP_LOGI(TAG, "rec_engine_cb - REC_EVENT_WAKEUP_END");
		//AUDIO_MEM_SHOW(TAG);
	} else if (AUDIO_REC_COMMAND_DECT <= event->type) {
		recorder_sr_mn_result_t *mn_result = event->event_data;

		ESP_LOGI(TAG, "rec_engine_cb - AUDIO_REC_COMMAND_DECT");
		ESP_LOGW(TAG, "command %d, phrase_id %d, prob %f, str: %s"
			, event->type, mn_result->phrase_id, mn_result->prob, mn_result->str);
	} else {
		ESP_LOGE(TAG, "Unkown event");
	}
	return ESP_OK;
}

static esp_audio_handle_t setup_player()
{
	AUDIO_MEM_SHOW(TAG);

	esp_audio_cfg_t cfg = DEFAULT_ESP_AUDIO_CONFIG();
	audio_board_handle_t board_handle = audio_board_init();

	cfg.vol_handle = board_handle->audio_hal;
	cfg.vol_set = (audio_volume_set)audio_hal_set_volume;
	cfg.vol_get = (audio_volume_get)audio_hal_get_volume;
	cfg.resample_rate = CODEC_ADC_SAMPLE_RATE;
	cfg.prefer_type = ESP_AUDIO_PREFER_MEM;

	player = esp_audio_create(&cfg);
	audio_hal_ctrl_codec(board_handle->audio_hal, AUDIO_HAL_CODEC_MODE_BOTH, AUDIO_HAL_CTRL_START);

	// Create readers and add to esp_audio
	tone_stream_cfg_t tone_cfg = TONE_STREAM_CFG_DEFAULT();
	tone_cfg.type = AUDIO_STREAM_READER;
	tone_cfg.label = "prompts";
	esp_audio_input_stream_add(player, tone_stream_init(&tone_cfg));

	// Add decoders and encoders to esp_audio
	mp3_decoder_cfg_t mp3_dec_cfg = DEFAULT_MP3_DECODER_CONFIG();
	mp3_dec_cfg.task_core = 1;
	esp_audio_codec_lib_add(player, AUDIO_CODEC_TYPE_DECODER, mp3_decoder_init(&mp3_dec_cfg));

	// Create writers and add to esp_audio
	i2s_stream_cfg_t i2s_writer = I2S_STREAM_CFG_DEFAULT_WITH_PARA(I2S_NUM_0, CODEC_ADC_SAMPLE_RATE, CODEC_ADC_BITS_PER_SAMPLE, AUDIO_STREAM_WRITER);
	i2s_writer.need_expand = (CODEC_ADC_BITS_PER_SAMPLE != 16);

	global_i2s_writer = i2s_stream_init(&i2s_writer);
    esp_audio_output_stream_add(player, global_i2s_writer);

	// Set default volume
//  esp_audio_vol_set(player, 60);
	esp_audio_vol_set(player, 100);

	AUDIO_MEM_SHOW(TAG);

	ESP_LOGI(TAG, "esp_audio instance is:%p", player);
	return player;
}

static void start_recorder()
{
	char *audio_sr_input_fmt = AUDIO_ADC_INPUT_CH_FORMAT;
	audio_element_handle_t i2s_stream_reader;
	audio_pipeline_handle_t pipeline;
	audio_pipeline_cfg_t pipeline_cfg = DEFAULT_AUDIO_PIPELINE_CONFIG();

	pipeline = audio_pipeline_init(&pipeline_cfg);
	if (NULL == pipeline) {
		ESP_LOGE(TAG, "Error initializing audio pipeline");
		return;
	}

	global_pipeline = pipeline; // [추가] 전역 변수 저장

	i2s_stream_cfg_t i2s_cfg = I2S_STREAM_CFG_DEFAULT_WITH_PARA(CODEC_ADC_I2S_PORT, CODEC_ADC_SAMPLE_RATE, CODEC_ADC_BITS_PER_SAMPLE, AUDIO_STREAM_READER);
	i2s_stream_reader = i2s_stream_init(&i2s_cfg);
	if (NULL == i2s_stream_reader) {
		audio_pipeline_deinit(pipeline);
		ESP_LOGE(TAG, "Error initializing i2s stream");
		return;
	}

	audio_element_handle_t filter = NULL;
	rsp_filter_cfg_t rsp_cfg = DEFAULT_RESAMPLE_FILTER_CONFIG();
    rsp_cfg.src_rate = CODEC_ADC_SAMPLE_RATE;
    rsp_cfg.src_ch = 2;
    rsp_cfg.src_bits = 16; // <--- [중요] 여기가 32일 수도 있음. 일단 16으로 명시

    rsp_cfg.dest_rate = 16000;
    rsp_cfg.dest_ch = 2;
    rsp_cfg.dest_bits = 16; // <--- [필수] 우리는 16bit로 저장하고 재생함

	filter = rsp_filter_init(&rsp_cfg);

	raw_stream_cfg_t raw_cfg = RAW_STREAM_CFG_DEFAULT();
	raw_cfg.type = AUDIO_STREAM_READER;
	raw_read = raw_stream_init(&raw_cfg);
	if (NULL == raw_read) {
		if (filter) {
			audio_element_deinit(filter);
		}
		audio_element_deinit(i2s_stream_reader);
		audio_pipeline_deinit(pipeline);
		ESP_LOGE(TAG, "Error initializing raw stream");
		return;
	}
	global_raw_read = raw_read; // [추가] 전역 변수 저장

	esp_err_t ret;
	ret = audio_pipeline_register(pipeline, i2s_stream_reader, "i2s");
	if (ret != ESP_OK) {
		audio_element_deinit(raw_read);
		if (filter) {
			audio_element_deinit(filter);
		}
		audio_element_deinit(i2s_stream_reader);
		audio_pipeline_deinit(pipeline);
		ESP_LOGE(TAG, "Error registering i2s to pipeline");
		return;
	}
	ret = audio_pipeline_register(pipeline, raw_read, "raw");
	if (ret != ESP_OK) {
		audio_pipeline_unregister(pipeline, i2s_stream_reader);
		audio_element_deinit(raw_read);
		if (filter) {
			audio_element_deinit(filter);
		}
		audio_element_deinit(i2s_stream_reader);
		audio_pipeline_deinit(pipeline);
		ESP_LOGE(TAG, "Error registering raw to pipeline");
		return;
	}

	if (filter) {
		audio_pipeline_register(pipeline, filter, "filter");
		const char *link_tag[3] = {"i2s", "filter", "raw"};
		audio_pipeline_link(pipeline, &link_tag[0], 3);
	} else {
		const char *link_tag[2] = {"i2s", "raw"};
		audio_pipeline_link(pipeline, &link_tag[0], 2);
	}

	ret = audio_pipeline_run(pipeline);
	if (ret != ESP_OK) {
		if (filter) {
			audio_pipeline_unregister(pipeline, filter);
			audio_element_deinit(filter);
		}
		audio_pipeline_unregister(pipeline, raw_read);
		audio_element_deinit(raw_read);
		audio_pipeline_unregister(pipeline, i2s_stream_reader);
		audio_element_deinit(i2s_stream_reader);
		audio_pipeline_deinit(pipeline);
		ESP_LOGE(TAG, "Error running pipeline");
		return;
	}

	recorder_sr_cfg_t recorder_sr_cfg = DEFAULT_RECORDER_SR_CFG(audio_sr_input_fmt, "model", AFE_TYPE_SR, AFE_MODE_HIGH_PERF);

	//ESP_LOGI(TAG, "Recorder AFE config before modify:");
	//afe_config_print(recorder_sr_cfg.afe_cfg);

	recorder_sr_cfg.afe_cfg->memory_alloc_mode = AFE_MEMORY_ALLOC_MORE_PSRAM;
	recorder_sr_cfg.afe_cfg->wakenet_init = WAKENET_ENABLE;
#if WAKENET_ENABLE
	recorder_sr_cfg.afe_cfg->vad_mode = VAD_MODE_4;
	recorder_sr_cfg.afe_cfg->aec_init = RECORD_HARDWARE_AEC;
	recorder_sr_cfg.afe_cfg->agc_mode = AFE_MN_PEAK_NO_AGC;
#else
	recorder_sr_cfg.afe_cfg->vad_mode = VAD_MODE_3;
	recorder_sr_cfg.afe_cfg->se_init = true;
	recorder_sr_cfg.afe_cfg->aec_init = WAKENET_ENABLE ? RECORD_HARDWARE_AEC : true;
	recorder_sr_cfg.afe_cfg->agc_mode = WAKENET_ENABLE ? AFE_AGC_MODE_WAKENET : AFE_AGC_MODE_WEBRTC;
#endif

	//ESP_LOGI(TAG, "Recorder AFE config after modify:");
	//afe_config_print(recorder_sr_cfg.afe_cfg);

	recorder_sr_cfg.multinet_init = MULTINET_ENABLE;
	recorder_sr_cfg.mn_language = "";

#if RECORDER_ENC_ENABLE
	recorder_encoder_cfg_t recorder_encoder_cfg = { 0 };
#if RECORDER_ENC_ENABLE == ENC_2_AMRNB
	rsp_filter_cfg_t filter_cfg = DEFAULT_RESAMPLE_FILTER_CONFIG();
	filter_cfg.src_ch = 1;
	filter_cfg.src_rate = 16000;
	filter_cfg.dest_ch = 1;
	filter_cfg.dest_rate = 8000;
	filter_cfg.stack_in_ext = true;
	filter_cfg.max_indata_bytes = 1024;

	amrnb_encoder_cfg_t amrnb_cfg = DEFAULT_AMRNB_ENCODER_CONFIG();
	amrnb_cfg.contain_amrnb_header = true;
	amrnb_cfg.stack_in_ext = true;

	recorder_encoder_cfg.resample = rsp_filter_init(&filter_cfg);
	recorder_encoder_cfg.encoder = amrnb_encoder_init(&amrnb_cfg);
#elif RECORDER_ENC_ENABLE == ENC_2_AMRWB
	amrwb_encoder_cfg_t amrwb_cfg = DEFAULT_AMRWB_ENCODER_CONFIG();
	amrwb_cfg.contain_amrwb_header = true;
	amrwb_cfg.stack_in_ext = true;
	amrwb_cfg.out_rb_size = 4 * 1024;

	recorder_encoder_cfg.encoder = amrwb_encoder_init(&amrwb_cfg);
#endif
#endif

	audio_rec_cfg_t cfg = AUDIO_RECORDER_DEFAULT_CFG();
	cfg.read = (recorder_data_read_t)&input_cb_for_afe;
	cfg.sr_handle = recorder_sr_create(&recorder_sr_cfg, &cfg.sr_iface);
#if SPEECH_CMDS_RESET
	char err[200];
	recorder_sr_reset_speech_cmd(cfg.sr_handle, SPEECH_COMMANDS, err);
#endif
#if RECORDER_ENC_ENABLE
	cfg.encoder_handle = recorder_encoder_create(&recorder_encoder_cfg, &cfg.encoder_iface);
#endif
	cfg.event_cb = rec_engine_cb;
	cfg.vad_off = 1000;
	recorder = audio_recorder_create(&cfg);
	if (NULL == recorder) {
		if (filter) {
			audio_pipeline_unregister(pipeline, filter);
			audio_element_deinit(filter);
		}
		audio_pipeline_unregister(pipeline, raw_read);
		audio_element_deinit(raw_read);
		audio_pipeline_unregister(pipeline, i2s_stream_reader);
		audio_element_deinit(i2s_stream_reader);
		audio_pipeline_deinit(pipeline);
		ESP_LOGE(TAG, "Error initializing recorder");
	}
	else {
		ESP_LOGI(TAG, "Recorder has been created");
	}
}

void ff_play_prompt(tone_type_t type)
{
	if (player) {
		wm8960_setSpeakerVolume(127);
		esp_audio_sync_play(player, tone_uri[type], 0);
	}
}

void log_clear(void)
{
	esp_log_level_set("*", ESP_LOG_INFO);

	esp_log_level_set("AUDIO_THREAD", ESP_LOG_ERROR);
	esp_log_level_set("I2C_BUS", ESP_LOG_ERROR);
	esp_log_level_set("AUDIO_HAL", ESP_LOG_ERROR);
	esp_log_level_set("ESP_AUDIO_TASK", ESP_LOG_ERROR);
	esp_log_level_set("ESP_DECODER", ESP_LOG_ERROR);
	esp_log_level_set("I2S", ESP_LOG_ERROR);
	esp_log_level_set("AUDIO_FORGE", ESP_LOG_ERROR);
	esp_log_level_set("ESP_AUDIO_CTRL", ESP_LOG_ERROR);
	esp_log_level_set("AUDIO_PIPELINE", ESP_LOG_ERROR);
	esp_log_level_set("AUDIO_ELEMENT", ESP_LOG_ERROR);
	esp_log_level_set("TONE_PARTITION", ESP_LOG_ERROR);
	esp_log_level_set("TONE_STREAM", ESP_LOG_ERROR);
	esp_log_level_set("MP3_DECODER", ESP_LOG_ERROR);
	esp_log_level_set("I2S_STREAM", ESP_LOG_ERROR);
	esp_log_level_set("RSP_FILTER", ESP_LOG_ERROR);
	esp_log_level_set("AUDIO_EVT", ESP_LOG_ERROR);
}

void ff_wwe_init(void)
{
	//show_all_heap_info(TAG);
    //log_clear();

	audio_thread_create(NULL, NAME_AUDIO, voice_read_task, NULL, STACK_AUDIO, PRIORITY_AUDIO, true, CORE_AUDIO);

    voice_mem_init();
    setup_player();
    start_recorder();
}
