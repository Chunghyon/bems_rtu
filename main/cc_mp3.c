#if 0
/* Play MP3 file from Flash(spiffs system)

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_log.h"
#include "sdkconfig.h"

#include "audio_element.h"
#include "audio_pipeline.h"
#include "audio_event_iface.h"
#include "i2s_stream.h"
#include "spiffs_stream.h"
#include "mp3_decoder.h"

#include "esp_peripherals.h"
#include "periph_spiffs.h"
#include "board.h"

#include "hh_parameters.h"
#include "dirent.h"

static const char *TAG = "SPIFFS_MP3";

#define I2S_STD_PHILIPS_SLOT_MP3_ADF_CONFIG(bits_per_sample, mono_or_stereo) { \
    .data_bit_width = bits_per_sample,                                         \
    .slot_bit_width = I2S_SLOT_BIT_WIDTH_AUTO,                                 \
    .slot_mode = mono_or_stereo,                                               \
    .slot_mask = (mono_or_stereo == I2S_SLOT_MODE_MONO) ?                      \
                 I2S_STD_SLOT_LEFT : I2S_STD_SLOT_BOTH,                        \
    .ws_width = bits_per_sample,                                               \
    .ws_pol = false,                                                           \
    .bit_shift = true,                                                         \
    .left_align = true,                                                        \
    .big_endian = false,                                                       \
    .bit_order_lsb = false                                                     \
}

#define I2S_STREAM_CFG_MP3_WITH_TYPE_AND_CH(port, rate, bits, stream_type, channel)  { \
    .type = stream_type,                                                        \
    .std_cfg = {                                                                \
        .clk_cfg  = I2S_STD_CLK_DEFAULT_CONFIG(rate),                           \
        .slot_cfg = I2S_STD_PHILIPS_SLOT_MP3_ADF_CONFIG(bits, channel),         \
        .gpio_cfg = {                                                           \
			.mclk = I2S_MCLK,                                                   \
			.bclk = I2S_BCLK,                                                   \
			.ws   = I2S_LRCK,                                                   \
			.dout = I2S_DOUT,                                                   \
			.din  = I2S_DIN,                                                    \
            .invert_flags = {                                                   \
                .mclk_inv = false,                                              \
                .bclk_inv = false,                                              \
                .ws_inv = false,                                                \
            },                                                                  \
        },                                                                      \
    },                                                                          \
    .transmit_mode = I2S_COMM_MODE_STD,                                         \
    .chan_cfg = {                                                               \
        .id = port,                                                             \
        .role = I2S_ROLE_MASTER,                                                \
        .dma_desc_num = 3,                                                      \
        .dma_frame_num = 312,                                                   \
        .auto_clear = true,                                                     \
    },                                                                          \
    .use_alc = false,                                                           \
    .volume = 0,                                                                \
    .out_rb_size = I2S_STREAM_RINGBUFFER_SIZE,                                  \
    .task_stack = I2S_STREAM_TASK_STACK,                                        \
    .task_core = I2S_STREAM_TASK_CORE,                                          \
    .task_prio = I2S_STREAM_TASK_PRIO,                                          \
    .stack_in_ext = false,                                                      \
    .multi_out_num = 0,                                                         \
    .uninstall_drv = true,                                                      \
    .need_expand = false,                                                       \
    .expand_src_bits = I2S_DATA_BIT_WIDTH_16BIT,                                \
    .buffer_len = I2S_STREAM_BUF_SIZE,                                          \
}
#define I2S_STREAM_CFG_MP3_WITH_PARA(port, rate, bits, stream_type)  I2S_STREAM_CFG_MP3_WITH_TYPE_AND_CH(port, rate, bits, stream_type, I2S_SLOT_MODE_STEREO)
#define I2S_STREAM_CFG_MP3() I2S_STREAM_CFG_MP3_WITH_PARA(I2S_NUM_0, 16000, I2S_DATA_BIT_WIDTH_16BIT, AUDIO_STREAM_WRITER)

void pipeline_mp3_play(void)
{
    audio_pipeline_handle_t pipeline;
    audio_element_handle_t spiffs_stream_reader, i2s_stream_writer, mp3_decoder;

//    esp_log_level_set("*", ESP_LOG_WARN);
    esp_log_level_set(TAG, ESP_LOG_INFO);

    // Initialize peripherals management
    esp_periph_config_t periph_cfg = DEFAULT_ESP_PERIPH_SET_CONFIG();
    //periph_cfg.extern_stack = true; // (panic if extern stack)
    esp_periph_set_handle_t set = esp_periph_set_init(&periph_cfg);

    ESP_LOGI(TAG, "[ 1 ] Mount spiffs");
    periph_spiffs_cfg_t spiffs_cfg = {
        .root = "/spiffs",
        .partition_label = "mp3",
        .max_files = 2,
        .format_if_mount_failed = true
    };
    esp_periph_handle_t spiffs_handle = periph_spiffs_init(&spiffs_cfg);
    AUDIO_NULL_CHECK(TAG, spiffs_handle, return);

    ESP_LOGI(TAG, "Mounting SPIFFS filesystem");
    esp_periph_start(set, spiffs_handle);

    while (!periph_spiffs_is_mounted(spiffs_handle)) {
        vTaskDelay(100 / portTICK_PERIOD_MS);
    }
    ESP_LOGI(TAG, "SPIFFS filesystem mounted");

    DIR * chk = opendir("/spiffs");
    if (chk) {
        struct dirent *entry;
        int idx = 0;
        while ((entry = readdir(chk)) != NULL) {
            ESP_LOGI(TAG, "File%d: %s", idx++, entry->d_name);
        }
        closedir(chk);
    } else {
        ESP_LOGE(TAG, "Open directory error");
    }

    ESP_LOGI(TAG, "[ 2 ] Start codec chip");
    audio_board_handle_t board_handle = audio_board_init();
    audio_hal_ctrl_codec(board_handle->audio_hal, AUDIO_HAL_CODEC_MODE_DECODE, AUDIO_HAL_CTRL_START);

    ESP_LOGI(TAG, "[3.0] Create audio pipeline for playback");
    audio_pipeline_cfg_t pipeline_cfg = DEFAULT_AUDIO_PIPELINE_CONFIG();
    pipeline = audio_pipeline_init(&pipeline_cfg);
    AUDIO_NULL_CHECK(TAG, pipeline, return);

    ESP_LOGI(TAG, "[3.1] Create spiffs stream to read data from flash");
    spiffs_stream_cfg_t flash_cfg = SPIFFS_STREAM_CFG_DEFAULT();
    flash_cfg.type = AUDIO_STREAM_READER;
    spiffs_stream_reader = spiffs_stream_init(&flash_cfg);

    ESP_LOGI(TAG, "[3.2] Create i2s stream to write data to codec chip");
    i2s_stream_cfg_t i2s_cfg = I2S_STREAM_CFG_MP3();
    i2s_cfg.type = AUDIO_STREAM_WRITER;
    i2s_stream_writer = i2s_stream_init(&i2s_cfg);

    ESP_LOGI(TAG, "[3.3] Create mp3 decoder to decode mp3 file");
    mp3_decoder_cfg_t mp3_cfg = DEFAULT_MP3_DECODER_CONFIG();
    mp3_decoder = mp3_decoder_init(&mp3_cfg);

    ESP_LOGI(TAG, "[3.4] Register all elements to audio pipeline");
    audio_pipeline_register(pipeline, spiffs_stream_reader, "spiffs");
    audio_pipeline_register(pipeline, mp3_decoder, "mp3");
    audio_pipeline_register(pipeline, i2s_stream_writer, "i2s");

    ESP_LOGI(TAG, "[3.5] Link it together [flash]-->spiffs-->mp3_decoder-->i2s_stream-->[codec_chip]");
    const char *link_tag[3] = {"spiffs", "mp3", "i2s"};
    audio_pipeline_link(pipeline, &link_tag[0], 3);

    ESP_LOGI(TAG, "[3.6] Set up uri (file as spiffs, mp3 as mp3 decoder, and default output is i2s)");
    esp_err_t ret = audio_element_set_uri(spiffs_stream_reader, "/spiffs/hello.mp3");
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Error setting uri to spiffs");
        return;
    }

    ESP_LOGI(TAG, "[ 4 ] Set up event listener");
    audio_event_iface_cfg_t evt_cfg = AUDIO_EVENT_IFACE_DEFAULT_CFG();
    audio_event_iface_handle_t evt = audio_event_iface_init(&evt_cfg);

    ESP_LOGI(TAG, "[4.1] Listening event from all elements of pipeline");
    audio_pipeline_set_listener(pipeline, evt);

    ESP_LOGI(TAG, "[4.2] Listening event from peripherals");
    audio_event_iface_set_listener(esp_periph_set_get_event_iface(set), evt);

    ESP_LOGI(TAG, "[ 5 ] Start audio_pipeline");
    audio_pipeline_run(pipeline);

    ESP_LOGI(TAG, "[ 6 ] Listen for all pipeline events");
    while (1) {
        audio_event_iface_msg_t msg;
        esp_err_t ret = audio_event_iface_listen(evt, &msg, portMAX_DELAY);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "[ * ] Event interface error : %d", ret);
            continue;
        }

        if (msg.source_type == AUDIO_ELEMENT_TYPE_ELEMENT && msg.source == (void *) mp3_decoder
            && msg.cmd == AEL_MSG_CMD_REPORT_MUSIC_INFO) {
            audio_element_info_t music_info = {0};
            audio_element_getinfo(mp3_decoder, &music_info);

            ESP_LOGI(TAG, "[ * ] Receive music info from mp3 decoder, sample_rates=%d, bits=%d, ch=%d",
                     music_info.sample_rates, music_info.bits, music_info.channels);

            i2s_stream_set_clk(i2s_stream_writer, music_info.sample_rates, music_info.bits, music_info.channels);
            continue;
        }

        /* Stop when the last pipeline element (i2s_stream_writer in this case) receives stop event */
        if (msg.source_type == AUDIO_ELEMENT_TYPE_ELEMENT && msg.source == (void *) i2s_stream_writer
            && msg.cmd == AEL_MSG_CMD_REPORT_STATUS
            && (((int)msg.data == AEL_STATUS_STATE_STOPPED) || ((int)msg.data == AEL_STATUS_STATE_FINISHED))) {
            ESP_LOGI(TAG, "[ * ] Stop event received");
            break;
        }
    }

    ESP_LOGI(TAG, "[ 7 ] Stop audio_pipeline");
    audio_pipeline_stop(pipeline);
    audio_pipeline_wait_for_stop(pipeline);
    audio_pipeline_terminate(pipeline);

    audio_pipeline_unregister(pipeline, spiffs_stream_reader);
    audio_pipeline_unregister(pipeline, i2s_stream_writer);
    audio_pipeline_unregister(pipeline, mp3_decoder);

    /* Terminal the pipeline before removing the listener */
    audio_pipeline_remove_listener(pipeline);

    /* Stop all periph before removing the listener */
    esp_periph_set_stop_all(set);
    audio_event_iface_remove_listener(esp_periph_set_get_event_iface(set), evt);

    /* Make sure audio_pipeline_remove_listener & audio_event_iface_remove_listener are called before destroying event_iface */
    audio_event_iface_destroy(evt);

    /* Release all resources */
    audio_pipeline_deinit(pipeline);
    audio_element_deinit(spiffs_stream_reader);
    audio_element_deinit(i2s_stream_writer);
    audio_element_deinit(mp3_decoder);
    esp_periph_set_destroy(set);
}
#endif
