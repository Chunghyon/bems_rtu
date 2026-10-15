#ifndef HH_LITTLEFS_H__
#define HH_LITTLEFS_H__
#include "esp_littlefs.h"
#include "esp_err.h"

esp_err_t ff_mount_littlefs(char * partition_label, char * mount_point);
esp_err_t ff_unmount_littlefs(char * partition_label);
esp_err_t ff_test_littlefs(void);
void ff_littlefs_init(void);

#endif //HH_LITTLEFS_H__