#ifndef HH_SPIFFS_H__
#define HH_SPIFFS_H__
#include "esp_spiffs.h"
#include "esp_err.h"

extern esp_err_t ff_mount_spiffs_audio(void);
extern void ff_unmount_spiffs_audio(void);
extern esp_err_t ff_mount_spiffs_video(void);
extern void ff_unmount_spiffs_video(void);
extern esp_err_t ff_mount_spiffs_mp3(void);
extern void ff_unmount_spiffs_mp3(void);

extern void ff_spiffs_init(void);

#endif //HH_SPIFFS_H__