/*
 * ESPRESSIF MIT License
 *
 * Copyright (c) 2020 <ESPRESSIF SYSTEMS (SHANGHAI) CO., LTD>
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

#include <string.h>
#include "esp_log.h"
#include "driver/i2c.h"
#include "driver/gpio.h"
#include "board.h"

#include "new_codec.h"
#include "wm8960.h"

static const char *TAG = "new_codec";

static bool codec_init_flag;

audio_hal_func_t AUDIO_NEW_CODEC_DEFAULT_HANDLE = {
    .audio_codec_initialize = new_codec_init,
    .audio_codec_deinitialize = new_codec_deinit,
    .audio_codec_ctrl = new_codec_ctrl_state,
    .audio_codec_config_iface = new_codec_config_i2s,
    .audio_codec_set_mute = new_codec_set_voice_mute,
    .audio_codec_set_volume = new_codec_set_voice_volume,
    .audio_codec_get_volume = new_codec_get_voice_volume,
};

bool new_codec_initialized()
{
    return codec_init_flag;
}

esp_err_t new_codec_init(audio_hal_codec_config_t *cfg)
{
    ESP_LOGI(TAG, "[%s]", __func__);
    ESP_UNUSED(cfg);

    esp_err_t ret;

    ret = wm8960_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "new_codec init failed");
    }
    else {
        ESP_LOGI(TAG, "new_codec init success");
        codec_init_flag = true;
    }
    return ret;
}

esp_err_t new_codec_deinit(void)
{
    esp_err_t ret;
    ret = wm8960_deinit();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "new_codec deinit failed");
        return ret;
    }
    ESP_LOGI(TAG, "new_codec deinit success");
    codec_init_flag = false;
    return ESP_OK;
}

esp_err_t new_codec_ctrl_state(audio_hal_codec_mode_t mode, audio_hal_ctrl_t ctrl_state)
{
    esp_err_t ret = ESP_OK;

    switch (mode) {
        case AUDIO_HAL_CODEC_MODE_ENCODE:
            wm8960_adc_onoff((bool)ctrl_state);
            break;
        case AUDIO_HAL_CODEC_MODE_DECODE:
            wm8960_dac_onoff((bool)ctrl_state);
            break;
        case AUDIO_HAL_CODEC_MODE_BOTH:
            wm8960_both_onoff((bool)ctrl_state);
            break;
        default:
            ESP_LOGW(TAG, "Codec mode not support, default is decode mode");
            break;
    }
    return ret;
}

esp_err_t new_codec_config_i2s(audio_hal_codec_mode_t mode, audio_hal_codec_i2s_iface_t *iface)
{
    ESP_UNUSED(mode);
    ESP_UNUSED(iface);

    return ESP_OK;
}

esp_err_t new_codec_set_voice_mute(bool mute)
{
    mute ? wm8960_enableDacMute() : wm8960_disableDacMute();
    return ESP_OK;
}

esp_err_t new_codec_set_voice_volume(int volume)
{
    ESP_LOGI(TAG, "%s, %d", __func__, volume);
    if (volume < 0 || volume > 100) {
        ESP_LOGW(TAG, "Volume value should be between 0 and 127");
        return ESP_FAIL;
    }

    uint32_t speaker_volume = (127-47) * volume / 100 + 47; // map 0-100 to 47-127

    if(speaker_volume < 47) {
        speaker_volume = 0; // mute
    }
    else if(speaker_volume > 127) {
        speaker_volume = 127; // max volume
    }
    wm8960_setSpeakerVolume((uint8_t)speaker_volume);
    return ESP_OK;
}

esp_err_t new_codec_get_voice_volume(int *volume)
{
    uint8_t speaker_volume = wm8960_getSpeakerVolume();
    if(speaker_volume < 47) {
        *volume = 0; // mute
    }
    else if(speaker_volume > 127) {
        *volume = 100; // max volume
    }
    else {
        *volume = (speaker_volume - 47) * 100 / (127 - 47); // map 47-127 to 0-100
    }
    return ESP_OK;
}
