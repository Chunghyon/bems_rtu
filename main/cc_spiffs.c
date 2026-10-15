#include <stdio.h>
#include <string.h>
#include <sys/unistd.h>
#include <sys/stat.h>
#include "esp_system.h"
#include "esp_err.h"
#include "esp_log.h"

#include "hh_spiffs.h"
#include "hh_parameters.h"

static const char *TAG = "spiffs";

static esp_vfs_spiffs_conf_t conf_audio = {
	.base_path = "/audio",
	.partition_label = "audio",
	.max_files = 2,
	.format_if_mount_failed = true
};

static esp_vfs_spiffs_conf_t conf_video = {
	.base_path = "/video",
	.partition_label = "video",
	.max_files = 2,
	.format_if_mount_failed = true
};

static esp_vfs_spiffs_conf_t conf_mp3 = {
	.base_path = "/mp3",
	.partition_label = "mp3",
	.max_files = 2,
	.format_if_mount_failed = true
};

esp_err_t ff_mount_spiffs_audio(void)
{
	esp_err_t ret = ESP_OK;

	if(esp_spiffs_mounted(conf_audio.partition_label)) {
		ESP_LOGW(TAG, "SPIFFS_audio partition already mounted");
		return ESP_OK;
	}

	ret = esp_vfs_spiffs_register(&conf_audio);
	if (ret != ESP_OK) {
			if (ret == ESP_FAIL) {
					ESP_LOGE(TAG, "Failed to mount or format filesystem_audio");
			} else if (ret == ESP_ERR_NOT_FOUND) {
					ESP_LOGE(TAG, "Failed to find SPIFFS partition_audio");
			} else {
					ESP_LOGE(TAG, "Failed to initialize SPIFFS_audio (%s)", esp_err_to_name(ret));
			}
	}
	return ret;
}

void ff_unmount_spiffs_audio(void)
{
	esp_err_t ret = ESP_OK;

	if(!esp_spiffs_mounted(conf_audio.partition_label)) {
		ESP_LOGW(TAG, "SPIFFS_audio partition not mounted");
		return;
	}

	ret = esp_vfs_spiffs_unregister(conf_audio.partition_label);
	if (ret != ESP_OK) {
			ESP_LOGE(TAG, "Failed to unmount SPIFFS_audio (%s)", esp_err_to_name(ret));
	} else {
			ESP_LOGI(TAG, "SPIFFS_audio unmounted");
	}
}

esp_err_t ff_mount_spiffs_video(void)
{
	esp_err_t ret = ESP_OK;

	if(esp_spiffs_mounted(conf_video.partition_label)) {
		ESP_LOGW(TAG, "SPIFFS_video partition already mounted");
		return ESP_OK;
	}

	ret = esp_vfs_spiffs_register(&conf_video);
	if (ret != ESP_OK) {
			if (ret == ESP_FAIL) {
					ESP_LOGE(TAG, "Failed to mount or format filesystem_video");
			} else if (ret == ESP_ERR_NOT_FOUND) {
					ESP_LOGE(TAG, "Failed to find SPIFFS partition_video");
			} else {
					ESP_LOGE(TAG, "Failed to initialize SPIFFS_video (%s)", esp_err_to_name(ret));
			}
	}
	return ret;
}

void ff_unmount_spiffs_video(void)
{
	esp_err_t ret = ESP_OK;

	if(!esp_spiffs_mounted(conf_video.partition_label)) {
		ESP_LOGW(TAG, "SPIFFS_video partition not mounted");
		return;
	}

	ret = esp_vfs_spiffs_unregister(conf_video.partition_label);
	if (ret != ESP_OK) {
			ESP_LOGE(TAG, "Failed to unmount SPIFFS_video (%s)", esp_err_to_name(ret));
	} else {
			ESP_LOGI(TAG, "SPIFFS_video unmounted");
	}
}

esp_err_t ff_mount_spiffs_mp3(void)
{
	esp_err_t ret = ESP_OK;

	if(esp_spiffs_mounted(conf_mp3.partition_label)) {
		ESP_LOGW(TAG, "SPIFFS_mp3 partition already mounted");
		return ESP_OK;
	}

	ret = esp_vfs_spiffs_register(&conf_mp3);
	if (ret != ESP_OK) {
			if (ret == ESP_FAIL) {
					ESP_LOGE(TAG, "Failed to mount or format filesystem_mp3");
			} else if (ret == ESP_ERR_NOT_FOUND) {
					ESP_LOGE(TAG, "Failed to find SPIFFS partition_mp3");
			} else {
					ESP_LOGE(TAG, "Failed to initialize SPIFFS_mp3 (%s)", esp_err_to_name(ret));
			}
	}
	return ret;
}

void ff_unmount_spiffs_mp3(void)
{
	esp_err_t ret = ESP_OK;

	if(!esp_spiffs_mounted(conf_mp3.partition_label)) {
		ESP_LOGW(TAG, "SPIFFS_mp3 partition not mounted");
		return;
	}

	ret = esp_vfs_spiffs_unregister(conf_mp3.partition_label);
	if (ret != ESP_OK) {
			ESP_LOGE(TAG, "Failed to unmount SPIFFS_mp3 (%s)", esp_err_to_name(ret));
	} else {
			ESP_LOGI(TAG, "SPIFFS_mp3 unmounted");
	}
}

void ff_spiffs_init(void)
{
	ESP_LOGI(TAG, "Initializing SPIFFS");
	esp_err_t ret;

	ret = ff_mount_spiffs_audio();
	if(ret != ESP_OK) {
		return;
	}
  
	ret = ff_mount_spiffs_video();
	if(ret != ESP_OK) {
		ff_unmount_spiffs_audio();
		return;
	}

    ret = ff_mount_spiffs_mp3();
	if(ret != ESP_OK) {
		ff_unmount_spiffs_audio();
		ff_unmount_spiffs_video();
		return;
	}
#if 0 // too long time, watchdog timeout occurs
    ESP_LOGI(TAG, "Performing SPIFFS_check_audio().");
    ret = esp_spiffs_check(conf_audio.partition_label);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "SPIFFS_check()_audio failed (%s)", esp_err_to_name(ret));
        return;
    } else {
        ESP_LOGI(TAG, "SPIFFS_check()_audio successful");
    }

    ESP_LOGI(TAG, "Performing SPIFFS_check_video().");
    ret = esp_spiffs_check(conf_video.partition_label);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "SPIFFS_check()_video failed (%s)", esp_err_to_name(ret));
        return;
    } else {
        ESP_LOGI(TAG, "SPIFFS_check()_video successful");
    }
#endif
    size_t total = 0, used = 0;
    ret = esp_spiffs_info(conf_audio.partition_label, &total, &used);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get SPIFFS_audio partition information (%s). Formatting...", esp_err_to_name(ret));
        esp_spiffs_format(conf_audio.partition_label);
        esp_restart();
    } else {
        ESP_LOGI(TAG, "Partition_audio size: total: %d, used: %d", total, used);
		if(used) {
			ESP_LOGW(TAG, "SPIFFS_audio partition is not empty. Formatting...");
			esp_spiffs_format(conf_audio.partition_label);
		}
    }
#if 0
    if (used > total) {
        ESP_LOGW(TAG, "Number of used bytes cannot be larger than total. Performing SPIFFS_check()_audio.");
        ret = esp_spiffs_check(conf_audio.partition_label);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "SPIFFS_check()_audio failed (%s)", esp_err_to_name(ret));
			esp_spiffs_format(conf_audio.partition_label);
            esp_restart();
        } else {
            ESP_LOGI(TAG, "SPIFFS_check()_audio successful");
        }
    }
#endif
	ret = esp_spiffs_info(conf_video.partition_label, &total, &used);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get SPIFFS_video partition information (%s). Formatting...", esp_err_to_name(ret));
        esp_spiffs_format(conf_video.partition_label);
        esp_restart();
    } else {
        ESP_LOGI(TAG, "Partition_video size: total: %d, used: %d", total, used);
		if(used) {
			ESP_LOGW(TAG, "SPIFFS_video partition is not empty. Formatting...");
			esp_spiffs_format(conf_video.partition_label);
		}
    }
#if 0
    if (used > total) {
        ESP_LOGW(TAG, "Number of used bytes cannot be larger than total. Performing SPIFFS_check()_video.");
        ret = esp_spiffs_check(conf_video.partition_label);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "SPIFFS_check()_video failed (%s)", esp_err_to_name(ret));
			esp_spiffs_format(conf_audio.partition_label);
            esp_restart();
        } else {
            ESP_LOGI(TAG, "SPIFFS_check()_video successful");
        }
    }
#endif

    ret = esp_spiffs_info(conf_mp3.partition_label, &total, &used);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get SPIFFS_mp3 partition information (%s). Formatting...", esp_err_to_name(ret));
        esp_spiffs_format(conf_mp3.partition_label);
        esp_restart();
    } else {
        ESP_LOGI(TAG, "Partition_mp3 size: total: %d, used: %d", total, used);
    }

#if 0
    // Use POSIX and C standard library functions to work with files.
    // First create a file.
    ESP_LOGI(TAG, "Opening file");
    FILE* f = fopen("/audio/hello.txt", "w");
    if (f == NULL) {
        ESP_LOGE(TAG, "Failed to open file for writing");
        return;
    }
    fprintf(f, "Hello World!\n");
    fclose(f);
    ESP_LOGI(TAG, "File written");

    // Check if destination file exists before renaming
    struct stat st;
    if (stat("/audio/foo.txt", &st) == 0) {
        // Delete it if it exists
        unlink("/audio/foo.txt");
    }

    // Rename original file
    ESP_LOGI(TAG, "Renaming file");
    if (rename("/audio/hello.txt", "/audio/foo.txt") != 0) {
        ESP_LOGE(TAG, "Rename failed");
        return;
    }

    // Open renamed file for reading
    ESP_LOGI(TAG, "Reading file");
    f = fopen("/audio/foo.txt", "r");
    if (f == NULL) {
        ESP_LOGE(TAG, "Failed to open file for reading");
        return;
    }

    char line[64];
    fgets(line, sizeof(line), f);
    fclose(f);
    // strip newline
    char* pos = strchr(line, '\n');
    if (pos) {
        *pos = '\0';
    }
    ESP_LOGI(TAG, "Read from file: '%s'", line);
#endif
    // All done, unmount partition and disable SPIFFS
    ff_unmount_spiffs_audio();
	ff_unmount_spiffs_video();
	ff_unmount_spiffs_mp3();
}
