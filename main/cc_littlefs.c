#include <stdio.h>
#include <string.h>
#include <sys/unistd.h>
#include <sys/stat.h>
#include "dirent.h"
#include "esp_system.h"
#include "esp_err.h"
#include "esp_log.h"

#include "hh_littlefs.h"
#include "hh_parameters.h"

#include "ftp.h"

static const char *TAG = "littlefs";
/*
static esp_vfs_littlefs_conf_t conf = {
	.base_path = PP_MOUNT_POINT,
	.partition_label = PP_PARTITION_LABEL,
	.format_if_mount_failed = true,
	.dont_mount = false,
};
*/
esp_err_t ff_mount_littlefs(char * partition_label, char * mount_point)
{
	esp_err_t ret = ESP_OK;
	esp_vfs_littlefs_conf_t conf = {
		.base_path = mount_point,
		.partition_label = partition_label,
		.format_if_mount_failed = true,
		.dont_mount = false,
	};

	if(esp_littlefs_mounted(conf.partition_label)) {
		ESP_LOGI(TAG, "partition %s is already mounted", conf.partition_label);
		return ret;
	}

	ret = esp_vfs_littlefs_register(&conf);
	if (ret != ESP_OK) {
		if(ret == ESP_ERR_NOT_FOUND) {
			ESP_LOGE(TAG, "Failed to find LittleFS partition %s", conf.partition_label);
		} else {
			ESP_LOGE(TAG, "Failed to initialize LittleFS (%s)", esp_err_to_name(ret));
		}
		return ret;
	}

	size_t total = 0, used = 0;
	ret = esp_littlefs_info(conf.partition_label, &total, &used);
	if (ret == ESP_OK) {
		ESP_LOGI(TAG, "Partition size: total: %d, used: %d", total, used);
	} else {
		ESP_LOGE(TAG, "Failed to get LittleFS partition information (%s)", esp_err_to_name(ret));
	}
	return ret;
}

esp_err_t ff_test_littlefs(void)
{
	ESP_LOGI(TAG, "Opening file");
	FILE *f = fopen(PP_MOUNT_POINT"/hello.txt", "w");
	if (f == NULL) {
		ESP_LOGE(TAG, "Failed to open file for writing");
		return ESP_FAIL;
	}
	fprintf(f, "Hello World!\n");
	fclose(f);
	ESP_LOGI(TAG, "File written");

	// Check if destination file exists before renaming
	struct stat st;
	if (stat(PP_MOUNT_POINT"/foo.txt", &st) == 0) {
		// Delete it if it exists
		unlink(PP_MOUNT_POINT"/foo.txt");
	}

	// Rename original file
	ESP_LOGI(TAG, "Renaming file");
	if (rename(PP_MOUNT_POINT"/hello.txt", PP_MOUNT_POINT"/foo.txt") != 0) {
		ESP_LOGE(TAG, "Rename failed");
		return ESP_FAIL;
	}

	// Open renamed file for reading
	ESP_LOGI(TAG, "Reading file");
	f = fopen(PP_MOUNT_POINT"/foo.txt", "r");
	if (f == NULL) {
		ESP_LOGE(TAG, "Failed to open file for reading");
		return ESP_FAIL;
	}

	char line[128] = {0};
	fgets(line, sizeof(line), f);
	fclose(f);
	// strip newline
	char* pos = strpbrk(line, "\r\n");
	if (pos) {
		*pos = '\0';
	}
	ESP_LOGI(TAG, "Read from file: '%s'", line);

	ESP_LOGI(TAG, "Reading from flashed filesystem example.txt");
	f = fopen(PP_MOUNT_POINT"/example.txt", "r");
	if (f == NULL) {
		ESP_LOGE(TAG, "Failed to open file for reading");
		return ESP_FAIL;
	}
	fgets(line, sizeof(line), f);
	fclose(f);
	// strip newline
	pos = strpbrk(line, "\r\n");
	if (pos) {
		*pos = '\0';
	}
	ESP_LOGI(TAG, "Read from file: '%s'", line);

	return ESP_OK;
}

esp_err_t ff_unmount_littlefs(char * partition_label)
{
	esp_err_t ret = ESP_OK;

	if(!esp_littlefs_mounted(partition_label)) {
		ESP_LOGW(TAG, "partition %s is not mounted", partition_label);
		return ret;
	}

	ret = esp_vfs_littlefs_unregister(partition_label);
	if (ret != ESP_OK) {
			ESP_LOGE(TAG, "Failed to unmount %s (%s)", partition_label, esp_err_to_name(ret));
	} else {
			ESP_LOGI(TAG, "%s unmounted", partition_label);
	}
	return ret;
}

void ff_littlefs_init(void)
{
	ESP_LOGI(TAG, "Initializing LittleFS");
	esp_err_t ret;

	ret = ff_mount_littlefs(PP_PARTITION_LABEL, PP_MOUNT_POINT);
	if(ret != ESP_OK) {
		ESP_LOGE(TAG, "LittleFS mount failed");
		return;
	}
#if 0
	ret = ff_test_littlefs();
	if(ret != ESP_OK) {
		ESP_LOGE(TAG, "LittleFS test failed");
		return;
	}

	ret = ff_unmount_littlefs(PP_PARTITION_LABEL);
	if(ret != ESP_OK) {
		ESP_LOGE(TAG, "LittleFS unmount failed");
		return;
	}
#endif
}
