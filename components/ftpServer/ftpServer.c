/*
	FTP Server example.

	This example code is in the Public Domain (or CC0 licensed, at your option.)

	Unless required by applicable law or agreed to in writing, this
	software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
	CONDITIONS OF ANY KIND, either express or implied.
*/

#include <stdio.h>
#include <inttypes.h>
#include <string.h>
#include <sys/unistd.h>
#include <sys/stat.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_mac.h" // for MACSTR
#include "esp_event.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_vfs.h"
#include "esp_netif.h"

#include "hh_littlefs.h"
#include "ftp.h"

#if (ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 0, 0))
#define esp_vfs_fat_spiflash_mount esp_vfs_fat_spiflash_mount_rw_wl
#define esp_vfs_fat_spiflash_unmount esp_vfs_fat_spiflash_unmount_rw_wl
#define sntp_setoperatingmode esp_sntp_setoperatingmode
#define sntp_setservername esp_sntp_setservername
#define sntp_init esp_sntp_init
#endif

static const char *TAG = "ftpServer";

EventGroupHandle_t xEventTask;
int FTP_TASK_FINISH_BIT = BIT2;

void ftp_server_start(void)
{
	// Create FTP server task
	xEventTask = xEventGroupCreate();
	xTaskCreate(ftp_task, "FTP", 1024*6, NULL, 2, NULL);
#if 0
	xEventGroupWaitBits( xEventTask,
		FTP_TASK_FINISH_BIT, /* The bits within the event group to wait for. */
		pdTRUE, /* BIT_0 should be cleared before returning. */
		pdFALSE, /* Don't wait for both bits, either bit will do. */
		portMAX_DELAY);/* Wait forever. */
#endif
}

void ftp_server_stop(void)
{

}
