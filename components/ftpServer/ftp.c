/*
 * This file is based on 'ftp' from Pycom Limited.
 *
 * Author: LoBo, loboris@gmail.com
 * Copyright (c) 2017, LoBo
 */

#include <stdio.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <ctype.h>
#include <stdio.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/event_groups.h"

#include "dirent.h"
#include "esp_system.h"

#include "esp_flash.h"
#include "nvs_flash.h"
#include "esp_log.h"

#include "lwip/sockets.h"
#include "lwip/dns.h"
#include "lwip/netdb.h"
#include "lwip/sockets.h" // send() 함수용

#include "ftp.h"
#include "hh_parameters.h"

#ifndef WAVE_HEADER_SIZE
#define WAVE_HEADER_SIZE	44
#endif

const char *TAG = "ftp";

extern bool LEARNING_MODE;

extern psram_data_t g_psram_face[MAX_FACE_FILES_COUNT];
extern psram_data_t g_psram_voice[MAX_VOICE_FILES_COUNT];

static const char* VOICE_LEARN_PREFIX = "test_record_";
static const char* VOICE_WORK_PREFIX  = "record.wav";
static const char* FACE_FILE_PREFIX   = "Face";

extern SemaphoreHandle_t g_voice_mutex;    // wwe.c 에 정의 필요
extern SemaphoreHandle_t g_face_mutex;

extern int g_current_max_face;

extern int FTP_TASK_FINISH_BIT;
extern EventGroupHandle_t xEventTask;

int ftp_buff_size = CONFIG_MICROPY_FTPSERVER_BUFFER_SIZE;

static uint8_t ftp_stop = 0;
char ftp_user[FTP_USER_PASS_LEN_MAX + 1];
char ftp_pass[FTP_USER_PASS_LEN_MAX + 1];

static bool print_wait = true;

/******************************************************************************
 DECLARE PRIVATE DATA
 ******************************************************************************/
static ftp_data_t ftp_data = {0};
static char *ftp_path = NULL;
static char *ftp_scratch_buffer = NULL;;
static char *ftp_cmd_buffer = NULL;
static uint8_t ftp_nlist = 0;
static const ftp_cmd_t ftp_cmd_table[] = {
    { FTP_CMD_FEAT }, { FTP_CMD_SYST }, { FTP_CMD_CDUP }, { FTP_CMD_CWD  },
    { FTP_CMD_PWD  }, { FTP_CMD_XPWD }, { FTP_CMD_SIZE }, { FTP_CMD_MDTM },
    { FTP_CMD_TYPE }, { FTP_CMD_USER }, { FTP_CMD_PASS }, { FTP_CMD_PASV },
    { FTP_CMD_LIST }, { FTP_CMD_RETR }, { FTP_CMD_STOR }, { FTP_CMD_DELE },
    { FTP_CMD_RMD  }, { FTP_CMD_MKD  }, { FTP_CMD_RNFR }, { FTP_CMD_RNTO },
    { FTP_CMD_NOOP }, { FTP_CMD_QUIT }, { FTP_CMD_APPE }, { FTP_CMD_NLST }, 
	{ FTP_CMD_AUTH }
};
// ==== PRIVATE FUNCTIONS ===================================================

uint64_t mp_hal_ticks_ms() {
	uint64_t time_ms = xTaskGetTickCount() * portTICK_PERIOD_MS;
	return time_ms;
}

//--------------------------------
static void stoupper (char *str) {
	while (str && *str != '\0') {
		*str = (char)toupper((int)(*str));
		str++;
	}
}

// ==== File functions =========================================
static void ftp_send_reply (ftp_reply_code_t status, char *message);

static void ftp_send_virtual_voice_file(int file_idx) 
{
    // 인덱스 범위 체크
    if (file_idx < 0 || file_idx >= MAX_VOICE_FILES_COUNT) {
        ESP_LOGE(TAG, "Invalid voice file index: %d", file_idx);
        ftp_send_reply(FTP_CODE_FILE_UNAVAILABLE, NULL);
        return;
    }

    uint8_t *raw_data = NULL;
    uint32_t raw_len = 0;
    bool mutex_taken = false;

    // 1. 뮤텍스 획득 (데이터 전송 완료 시까지 유지)
    if (g_voice_mutex != NULL && xSemaphoreTake(g_voice_mutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
        mutex_taken = true;
        psram_data_t *rf = &g_psram_voice[file_idx];
        if (rf->is_valid && rf->data != NULL && rf->len > 0) {
            raw_data = rf->data;
            raw_len = rf->len;
        } else {
            // 데이터 없음 -> 반환
            xSemaphoreGive(g_voice_mutex);
            mutex_taken = false;
        }
    }

    if (!mutex_taken || raw_data == NULL || raw_len == 0) {
        ESP_LOGE(TAG, "Voice file %d empty or invalid (Mutex timeout)", file_idx + 1);
        ftp_send_reply(FTP_CODE_FILE_UNAVAILABLE, NULL);
        if (mutex_taken) xSemaphoreGive(g_voice_mutex);
        return;
    }

    // 2. WAV 헤더 생성
    int channels = 1;	// 1 for mono
    int sample_rate = 16000;
    int bits_per_sample = 16;
    int byte_rate = sample_rate * channels * (bits_per_sample / 8);
    int block_align = channels * (bits_per_sample / 8);

    uint8_t wav_header[WAVE_HEADER_SIZE] = {
        'R', 'I', 'F', 'F', 0, 0, 0, 0,  // 0-7: RIFF chunk
        'W', 'A', 'V', 'E', 'f', 'm', 't', ' ', // 8-15: WAVE fmt
        16, 0, 0, 0, 1, 0,               // 16-21: size(16), PCM(1)
        (uint8_t)channels, 0,            // 22-23: Channels
        0, 0, 0, 0,                      // 24-27: Sample Rate
        0, 0, 0, 0,                      // 28-31: Byte Rate
        (uint8_t)block_align, 0,         // 32-33: Block Align
        (uint8_t)bits_per_sample, 0,     // 34-35: BitsPerSample
        'd', 'a', 't', 'a', 0, 0, 0, 0   // 36-43: data chunk
    };

    uint32_t file_size = raw_len + 36;
    memcpy(&wav_header[4], &file_size, 4);      // RIFF size
    memcpy(&wav_header[24], &sample_rate, 4);   // Sample Rate
    memcpy(&wav_header[28], &byte_rate, 4);     // Byte Rate
    memcpy(&wav_header[40], &raw_len, 4);       // Data size

    ESP_LOGI(TAG, "Sending voice file %d (Raw: %"PRIu32" bytes) + WAV Header - Locked", file_idx+1, raw_len);
    ftp_send_reply(FTP_CODE_ABOUT_TO_OPEN_DATA, NULL);

    // 3. 헤더 전송 (44 bytes)
    send(ftp_data.d_sd, wav_header, WAVE_HEADER_SIZE, 0);

    // 4. 데이터 전송 loop (Face 파일 전송 구조 적용)
    uint32_t sent = 0;
    uint32_t chunk_size = 1460; // TCP MTU 고려
    int result;
    uint32_t timeout = FTP_TIMEOUT_DEFAULT_MS;
    
    while (sent < raw_len) {
        uint32_t remaining = raw_len - sent;
        uint32_t sending = (remaining > chunk_size) ? chunk_size : remaining;
        
        result = send(ftp_data.d_sd, raw_data + sent, sending, 0);
        
        if (result > 0) {
            sent += result;
            ftp_data.total += result;
            timeout = FTP_TIMEOUT_DEFAULT_MS; // 성공 시 타임아웃 리셋
            
            // Watchdog 방지를 위한 지연
            if ((sent % (chunk_size * 20)) == 0) vTaskDelay(1);
        }
        else {
            // 4-1. 상대방 연결 종료
            if (result == 0) {
                ESP_LOGE(TAG, "Voice send: Connection closed by peer");
                ftp_send_reply(FTP_CODE_TRANSFER_ABORTED, NULL);
                xSemaphoreGive(g_voice_mutex); // 전송 중단 시 뮤텍스 반환
                return;
            }
            
            // 4-2. Non-blocking 에러 (EAGAIN/EWOULDBLOCK) 확인
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                vTaskDelay(1); 
                timeout -= portTICK_PERIOD_MS;
                if (timeout <= 0) {
                     ESP_LOGE(TAG, "Voice send: Timeout");
                     ftp_send_reply(FTP_CODE_TRANSFER_ABORTED, NULL);
                     xSemaphoreGive(g_voice_mutex);
                     return;
                }
            } else {
                // 4-3. 그 외 에러
                ESP_LOGE(TAG, "Voice send fail, errno=%d", errno);
                ftp_send_reply(FTP_CODE_TRANSFER_ABORTED, NULL);
                xSemaphoreGive(g_voice_mutex);
                return;
            }
        }
    }
    
    // 5. 전송 완료 후 뮤텍스 반환
    xSemaphoreGive(g_voice_mutex);
    
    ESP_LOGI(TAG, "Voice file sent successfully");
    ftp_send_reply(FTP_CODE_CLOSING_DATA_CONN, NULL);
}

// 가상 파일 전송 함수
static void ftp_send_virtual_face_file(int index) {
    uint8_t *data_ptr = NULL;
    uint32_t data_len = 0;
    bool mutex_taken = false; // 뮤텍스 획득 여부 플래그
    
    // 1. 뮤텍스 획득 (대기 시간 1초)
    if (g_face_mutex != NULL && xSemaphoreTake(g_face_mutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
        mutex_taken = true;

        if (g_psram_face[index].is_valid && g_psram_face[index].data != NULL) {
            data_ptr = g_psram_face[index].data;
            data_len = g_psram_face[index].len;
        } else {
            // 데이터가 없으면 바로 반환
            xSemaphoreGive(g_face_mutex);
            mutex_taken = false;
        }
    }

    if (!mutex_taken || data_ptr == NULL || data_len == 0) {
        ESP_LOGE(TAG, "Virtual file %d not found or invalid (Mutex timeout or empty)", index);
        ftp_send_reply(FTP_CODE_FILE_UNAVAILABLE, NULL);
        if (mutex_taken) xSemaphoreGive(g_face_mutex); // 안전장치
        return;
    }

    ESP_LOGI(TAG, "Sending virtual file %d (Size: %lu bytes) - SPI Locked", index, data_len);
    
    // 150 File status okay; about to open data connection.
    ftp_send_reply(FTP_CODE_ABOUT_TO_OPEN_DATA, NULL);

    uint32_t sent = 0;
    uint32_t chunk_size = 1460;
    int result;
    uint32_t timeout = FTP_TIMEOUT_DEFAULT_MS;

    // 2. 전송 루프 (뮤텍스를 잡은 상태로 진행)
    while (sent < data_len) {
        uint32_t remaining = data_len - sent;
        uint32_t sending = (remaining > chunk_size) ? chunk_size : remaining;

        result = send(ftp_data.d_sd, data_ptr + sent, sending, 0);
        
        if (result > 0) {
            sent += result;
            ftp_data.total += result;
            timeout = FTP_TIMEOUT_DEFAULT_MS;
            // Watchdog trigger 방지 및 네트워크 부하 조절을 위해 주기적 Yield
            if ((sent % (chunk_size * 20)) == 0) vTaskDelay(1);
        } else {
            // 전송 실패 또는 대기

            // 1. 상대방이 연결을 끊은 경우 (result == 0)
            if (result == 0) {
                ESP_LOGE(TAG, "Virtual file send: Connection closed by peer");
                ftp_send_reply(FTP_CODE_TRANSFER_ABORTED, NULL);
                xSemaphoreGive(g_face_mutex);
                return;
            }

            // 2. 에러 발생 (result < 0) -> errno 확인
            // LWIP에서는 EAGAIN과 EWOULDBLOCK을 혼용할 수 있으므로 둘 다 체크
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                vTaskDelay(1); // 1 tick 대기
                timeout -= portTICK_PERIOD_MS;
                if (timeout <= 0) {
                    ESP_LOGE(TAG, "Virtual file send: Timeout");
                    ftp_send_reply(FTP_CODE_TRANSFER_ABORTED, NULL);
                    xSemaphoreGive(g_face_mutex);
                    return;
                }
            } else {
                // 그 외의 치명적 에러 (EPIPE, ENOTCONN 등)
                ESP_LOGE(TAG, "Virtual file send failed. errno=%d", errno);
                ftp_send_reply(FTP_CODE_TRANSFER_ABORTED, NULL);
                xSemaphoreGive(g_face_mutex);
                return;
            }
        }
    }
    
    // 3. 전송 완료 후 뮤텍스 반환 -> 이제 SPI가 다시 데이터를 쓸 수 있음
    xSemaphoreGive(g_face_mutex);
    
    ESP_LOGI(TAG, "Virtual file sent successfully - SPI Unlocked");
    ftp_send_reply(FTP_CODE_CLOSING_DATA_CONN, NULL);
}

//--------------------------------------------------------------
static bool ftp_open_file (const char *path, const char *mode) {
	ESP_LOGI(TAG, "ftp_open_file: path=[%s]", path);
	char fullname[128];
	strcpy(fullname, PP_MOUNT_POINT);
	strcat(fullname, path);
	ESP_LOGI(TAG, "ftp_open_file: fullname=[%s]", fullname);
	//ftp_data.fp = fopen(path, mode);
	ftp_data.fp = fopen(fullname, mode);
	if (ftp_data.fp == NULL) {
		ESP_LOGE(TAG, "ftp_open_file: open fail [%s]", fullname);
		return false;
	}
	ftp_data.e_open = E_FTP_FILE_OPEN;
	return true;
}

//--------------------------------------
static void ftp_close_files_dir (void) {
	char * str_open = conv_open2str(ftp_data.e_open);
	if(str_open) {
		ESP_LOGI(TAG, "%s, e_open %s", __func__, str_open);
	}
	else {
		ESP_LOGI(TAG, "%s, e_open %d", __func__, ftp_data.e_open);
	}

	if (ftp_data.e_open == E_FTP_FILE_OPEN) {
        if (ftp_data.fp) { // [추가] NULL 체크
            fclose(ftp_data.fp);
        }
		ftp_data.fp = NULL;
	}
	else if (ftp_data.e_open == E_FTP_DIR_OPEN) {
        if (ftp_data.dp) { // [추가] NULL 체크
            closedir(ftp_data.dp);
        }
 		ftp_data.dp = NULL;
	}
	ftp_data.e_open = E_FTP_NOTHING_OPEN;
}

//------------------------------------------------
static void ftp_close_filesystem (void) {
	ESP_LOGI(TAG, "%s", __func__);
	ftp_close_files_dir();
	if (ftp_data.fp) {
		fclose(ftp_data.fp);
		ftp_data.fp = NULL;
	}
	if (ftp_data.dp) {
		closedir(ftp_data.dp);
		ftp_data.dp = NULL;
	}
}

//---------------------------------------------------------------------------------------------
static ftp_result_t ftp_read_file (char *filebuf, uint32_t desiredsize, uint32_t *actualsize) {
	ESP_LOGI(TAG, "%s desiredsize=%"PRIu32"", __func__, desiredsize);
	ftp_result_t result = E_FTP_RESULT_CONTINUE;
	*actualsize = fread(filebuf, 1, desiredsize, ftp_data.fp);
	if (*actualsize == 0) {
		if (feof(ftp_data.fp))
			result = E_FTP_RESULT_OK;
		else
			result = E_FTP_RESULT_FAILED;
		ftp_close_files_dir();
	} else if (*actualsize < desiredsize) {
		ftp_close_files_dir();
		result = E_FTP_RESULT_OK;
	}
	return result;
}

//-----------------------------------------------------------------
static ftp_result_t ftp_write_file (char *filebuf, uint32_t size) {
	ESP_LOGI(TAG, "%s size=%"PRIu32"", __func__, size);
	ftp_result_t result = E_FTP_RESULT_FAILED;
	uint32_t actualsize = fwrite(filebuf, 1, size, ftp_data.fp);
	if (actualsize == size) {
		result = E_FTP_RESULT_OK;
	} else {
		ftp_close_files_dir();
	}
	return result;
}

//---------------------------------------------------------------
static ftp_result_t ftp_open_dir_for_listing (const char *path) {
    ESP_LOGI(TAG, "%s", __func__);
    if (ftp_data.dp) {
        closedir(ftp_data.dp);
        ftp_data.dp = NULL;
    }
    ESP_LOGI(TAG, "%s path=[%s] mount_point=[%s]", __func__, path, PP_MOUNT_POINT);
    char fullname[128];
    strcpy(fullname, PP_MOUNT_POINT);
    strcat(fullname, path);
    ESP_LOGI(TAG, "full path name %s", fullname);
    
    ftp_data.dp = opendir(fullname);  // Open the directory
    if (ftp_data.dp == NULL) {

		char temp_path[128];
        strncpy(temp_path, path, sizeof(temp_path)-1);
        temp_path[sizeof(temp_path)-1] = '\0';
        size_t len = strlen(temp_path);
        if (len > 1 && temp_path[len-1] == '/') {
            temp_path[len-1] = '\0';
        }

		if (strcmp(path, "/iLRiTek") == 0 || 
            strcmp(path, "/iLRiTek/dataset") == 0 ||
            strcmp(path, PP_VIRTUAL_FOLDER_FACE) == 0 || 
            strcmp(path, PP_VIRTUAL_FOLDER_VOICE) == 0) {

			// 중요: 가상 디렉토리이므로 실제 파일시스템을 열지 않음.
            ftp_data.e_open = E_FTP_NOTHING_OPEN; 
            ftp_data.listroot = false;
            return E_FTP_RESULT_CONTINUE;
        }

        ESP_LOGE(TAG, "%s: open fail [%s]", __func__, fullname);
        return E_FTP_RESULT_FAILED;
    }

    ftp_data.e_open = E_FTP_DIR_OPEN;
    ftp_data.listroot = false;
    return E_FTP_RESULT_CONTINUE;
}

static void ftp_get_eplf_item (uint32_t *p_next, struct dirent *de, 
                              const char* v_name, uint32_t v_size) 
{
    char *type = "-";
    char *name = NULL;
    uint32_t fsize = 0;
    time_t mtime = 946684800; // Default time

    if (de != NULL) {
        // [파일 시스템 항목]
        type = (de->d_type & DT_DIR) ? "d" : "-";
        name = de->d_name;
        
        struct stat buf;
        char fullname[128];
        strcpy(fullname, PP_MOUNT_POINT);
        strcat(fullname, ftp_path);
        if (fullname[strlen(fullname)-1] != '/') strcat(fullname, "/");
        strcat(fullname, de->d_name);
        if (stat(fullname, &buf) == 0) {
             fsize = buf.st_size;
             mtime = buf.st_mtime;
        }
    } else {
        // [가상 항목]
        name = (char*)v_name;
        fsize = v_size;
        time(&mtime);
        if (name && strlen(name) > 0 && name[strlen(name)-1] == '/') {
            type = "d";
        }
    }

    // 날짜 문자열 생성
    char str_time[64];
    struct tm *tm_info;
    time_t now;
    if (time(&now) < 0) now = 946684800;
    tm_info = localtime(&mtime);

    if ((mtime + FTP_UNIX_SECONDS_180_DAYS) < now) strftime(str_time, 127, "%b %d %Y", tm_info);
    else strftime(str_time, 63, "%b %d %H:%M", tm_info);

    // 1. 현재 버퍼 상태 확인
    uint32_t current_idx = *p_next;
    char *dest = (char *)ftp_data.dBuffer + current_idx;
    uint32_t remaining = ftp_buff_size - current_idx;

    // 2. 임시 작성 (길이 확인용)
    int written = 0;
    if (ftp_nlist) {
        written = snprintf(dest, remaining, "%s\r\n", name);
    } else {
        written = snprintf(dest, remaining, "%srw-rw-rw-   1 root  root %9"PRIu32" %s %s\r\n", type, fsize, str_time, name);
    }

    // 3. 버퍼 부족 시 재할당 (Truncated 되거나 에러인 경우)
    if (written < 0 || (uint32_t)written >= remaining) {
        ESP_LOGW(TAG, "Buffer too small (size=%d), reallocating...", ftp_buff_size);
        
        // 필요한 크기 계산 (넉넉하게 512바이트 단위로 증가)
        uint32_t needed = current_idx + (written < 0 ? 128 : written) + 64; 
        uint32_t new_size = ((needed / 512) + 1) * 512;
        if (new_size < ftp_buff_size + 512) new_size = ftp_buff_size + 512;

        uint8_t *new_buf = realloc(ftp_data.dBuffer, new_size);
        if (new_buf) {
            ftp_data.dBuffer = new_buf;
            ftp_buff_size = new_size;
            
            // 포인터 갱신 및 재작성
            dest = (char *)ftp_data.dBuffer + current_idx;
            remaining = ftp_buff_size - current_idx;
            
            if (ftp_nlist) {
                written = snprintf(dest, remaining, "%s\r\n", name);
            } else {
                written = snprintf(dest, remaining, "%srw-rw-rw-   1 root  root %9"PRIu32" %s %s\r\n", type, fsize, str_time, name);
            }
        } else {
            ESP_LOGE(TAG, "Buffer reallocation ERROR");
            return; // Skip item
        }
    }

    // 4. 인덱스 업데이트
    if (written > 0) {
        *p_next += written;
    }
}

//--------------------------------------------------------------------------------------
static ftp_result_t ftp_list_dir(char *list, uint32_t maxlistsize, uint32_t *listsize) {
    // 주의: 인자로 넘어온 list, maxlistsize 대신 전역 ftp_data.dBuffer, ftp_buff_size를 사용함
    // (realloc으로 인해 포인터가 바뀔 수 있기 때문)
    
    ESP_LOGI(TAG, "%s", __func__);
    uint32_t next = 0;
    uint listcount = 0;
    ftp_result_t result = E_FTP_RESULT_CONTINUE;
    struct dirent *de;

    if (ftp_data.dp) {
        // 실제 파일 시스템 리스팅 (한 번에 8개씩 끊어서 처리 - 기존 로직 유지)
        while (listcount < 8) {
            de = readdir(ftp_data.dp);
            if (de == NULL) {
                result = E_FTP_RESULT_OK;
                break;
            }
            if (de->d_name[0] == '.' && de->d_name[1] == 0) continue;
            if (de->d_name[0] == '.' && de->d_name[1] == '.' && de->d_name[2] == 0) continue;

            // [수정] 포인터 변경 방식 적용
            ftp_get_eplf_item(&next, de, NULL, 0); 
            listcount++;
            
            // 너무 많이 버퍼가 커지는 것을 방지하기 위해 루프 체크가 필요할 수도 있으나,
            // realloc 방식이므로 메모리가 허용하는 한 계속 담아도 됨.
        }
    } else {
        result = E_FTP_RESULT_OK;
    }
    
    // [가상 항목 추가 로직]
    if (result == E_FTP_RESULT_OK) {
        
        if (strcmp(ftp_path, "/") == 0) {
            ftp_get_eplf_item(&next, NULL, "iLRiTek/", 0);
        }
        else if (strcmp(ftp_path, "/iLRiTek") == 0 || strcmp(ftp_path, "/iLRiTek/") == 0) {
            ftp_get_eplf_item(&next, NULL, "dataset/", 0);
        }
        else if (strcmp(ftp_path, "/iLRiTek/dataset") == 0 || strcmp(ftp_path, "/iLRiTek/dataset/") == 0) {
            ftp_get_eplf_item(&next, NULL, "Face/", 0);
            ftp_get_eplf_item(&next, NULL, "Voice/", 0);
        }
        else if (strcmp(ftp_path, PP_VIRTUAL_FOLDER_FACE) == 0 || 
				 (strncmp(ftp_path, PP_VIRTUAL_FOLDER_FACE, strlen(PP_VIRTUAL_FOLDER_FACE)) == 0 && 
				  ftp_path[strlen(PP_VIRTUAL_FOLDER_FACE)] == '/')) {
            if (g_face_mutex != NULL && xSemaphoreTake(g_face_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
                for (int i = 0; i < g_current_max_face; i++) {
                    if (g_psram_face[i].is_valid) {
                         char vname[32];
                         // 파일명 생성: Face_1.bmp, Face_2.bmp ...
                         snprintf(vname, sizeof(vname), "%s%d.bmp", FACE_FILE_PREFIX, i + 1);
                         ftp_get_eplf_item(&next, NULL, vname, g_psram_face[i].len);
                    }
                }
                xSemaphoreGive(g_face_mutex);
            }
        }
        else if (strcmp(ftp_path, PP_VIRTUAL_FOLDER_VOICE) == 0 || 
				 (strncmp(ftp_path, PP_VIRTUAL_FOLDER_VOICE, strlen(PP_VIRTUAL_FOLDER_VOICE)) == 0 && 
				  ftp_path[strlen(PP_VIRTUAL_FOLDER_VOICE)] == '/')) {
            if (g_voice_mutex != NULL && xSemaphoreTake(g_voice_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
                // [수정] LEARNING_MODE에 따른 리스팅 분기
                if (LEARNING_MODE) {
                    // 학습 모드: test_record_1.wav ~ test_record_15.wav
                    for (int i = 0; i < MAX_VOICE_FILES_COUNT; i++) {
                        if (g_psram_voice[i].is_valid) {
                            char vname[32];
                            snprintf(vname, sizeof(vname), "%s%d.wav", VOICE_LEARN_PREFIX, i + 1);
                            ftp_get_eplf_item(&next, NULL, vname, g_psram_voice[i].len + WAVE_HEADER_SIZE);
                        }
                    }
                } else {
                    // 일반 모드: record.wav 단일 파일 (보통 인덱스 0번 사용)
                    if (g_psram_voice[0].is_valid) {
                        ftp_get_eplf_item(&next, NULL, VOICE_WORK_PREFIX, g_psram_voice[0].len + WAVE_HEADER_SIZE);
                    }
                }
                xSemaphoreGive(g_voice_mutex);
            }
        }
    }

    if (result == E_FTP_RESULT_OK) {
        if (ftp_data.dp != NULL) {
             ftp_close_files_dir();
        } else {
             ftp_data.e_open = E_FTP_NOTHING_OPEN;
        }
    }
    *listsize = next;
    return result;
}

// ==== Socket functions ==============================================================

//------------------------------------
static void ftp_close_cmd_data(void) {
	ESP_LOGI(TAG, "%s", __func__);
	closesocket(ftp_data.c_sd);
	closesocket(ftp_data.d_sd);
	ftp_data.c_sd  = -1;
	ftp_data.d_sd  = -1;
	ftp_close_filesystem ();
}

//----------------------------
static void _ftp_reset(void) {
	// close all connections and start all over again
	ESP_LOGW(TAG, "%s", __func__);
	closesocket(ftp_data.lc_sd);
	closesocket(ftp_data.ld_sd);
	ftp_data.lc_sd = -1;
	ftp_data.ld_sd = -1;
	ftp_close_cmd_data();

	ftp_data.e_open = E_FTP_NOTHING_OPEN;
	ftp_data.state = E_FTP_STE_START;
	ftp_data.substate = E_FTP_STE_SUB_DISCONNECTED;
}

static void ftp_send_reply (ftp_reply_code_t status, char *message) {
	if (!message) {
		message = "";
	}
	snprintf((char *)ftp_cmd_buffer, 4, "%"PRIu32, (uint32_t)status);
	strcat ((char *)ftp_cmd_buffer, " ");
	strcat ((char *)ftp_cmd_buffer, message);
	strcat ((char *)ftp_cmd_buffer, "\r\n");

	int32_t timeout = FTP_TIMEOUT_DEFAULT_MS;
	ftp_result_t result;
	size_t size = strlen((char *)ftp_cmd_buffer);

	ESP_LOGI(TAG, "Send reply: [%.*s]", size-2, ftp_cmd_buffer);
	vTaskDelay(1);

	while (1) {
		result = send(ftp_data.c_sd, ftp_cmd_buffer, size, 0);
		if (result == size) {
			if (status == FTP_CODE_SERVICE_CLOSING) {
				ESP_LOGI(TAG, "Send reply: Closing connection.");
				closesocket(ftp_data.d_sd);
				ftp_data.d_sd = -1;
				closesocket(ftp_data.ld_sd);
				ftp_data.ld_sd = -1;
				closesocket(ftp_data.c_sd);
				ftp_data.substate = E_FTP_STE_SUB_DISCONNECTED;
				ftp_close_filesystem();
			}
			else if (status == FTP_CODE_TRANSFER_ABORTED || 
				status == FTP_CODE_ACTION_LOCAL_ERROR || 
				status == FTP_CODE_FILE_UNAVAILABLE) {
				ESP_LOGI(TAG, "Send reply: Closing data connection.");
				closesocket(ftp_data.d_sd);
				ftp_data.d_sd = -1;
				ftp_close_filesystem();
			}

			vTaskDelay(1);
			break;
		}
		else {
			vTaskDelay(1);
			if ((timeout <= 0) || (errno != EAGAIN)) {
				// error
				_ftp_reset();
				ESP_LOGW(TAG, "Error sending command reply.");
				break;
			}
		}
		timeout -= portTICK_PERIOD_MS;
	}
}

//-------------------------------------------------------------------------------------
static bool ftp_create_listening_socket (int32_t *sd, uint32_t port, uint8_t backlog) {
	struct sockaddr_in sServerAddress;
	int32_t _sd;
	int32_t result;

	// open a socket for ftp data listen
	*sd = socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
	_sd = *sd;

	if (_sd > 0) {
		// enable non-blocking mode
		uint32_t option = fcntl(_sd, F_GETFL, 0);
		option |= O_NONBLOCK;
		fcntl(_sd, F_SETFL, option);

		// enable address reusing
		option = 1;
		result = setsockopt(_sd, SOL_SOCKET, SO_REUSEADDR, &option, sizeof(option));

		// bind the socket to a port number
		sServerAddress.sin_family = AF_INET;
		sServerAddress.sin_addr.s_addr = INADDR_ANY;
		sServerAddress.sin_len = sizeof(sServerAddress);
		sServerAddress.sin_port = htons(port);

		result |= bind(_sd, (const struct sockaddr *)&sServerAddress, sizeof(sServerAddress));

		// start listening
		result |= listen (_sd, backlog);

		if (!result) {
			print_wait = true;
			return true;
		}
		closesocket(*sd);
	}
	return false;
}

//--------------------------------------------------------------------------------------------
static ftp_result_t ftp_wait_for_connection (int32_t l_sd, int32_t *n_sd, uint32_t *ip_addr) {
	if(print_wait) {
		ESP_LOGI(TAG, "%s...", __func__);
		print_wait = false;
	}
	struct sockaddr_in	sClientAddress;
	socklen_t  in_addrSize;

	// accepts a connection from a TCP client, if there is any, otherwise returns EAGAIN
	*n_sd = accept(l_sd, (struct sockaddr *)&sClientAddress, (socklen_t *)&in_addrSize);
	int32_t _sd = *n_sd;
	if (_sd < 0) {
		if (errno == EAGAIN) {
			return E_FTP_RESULT_CONTINUE;
		}
		// error
		_ftp_reset();
		return E_FTP_RESULT_FAILED;
	}

	if (ip_addr) {
		// check on which network interface the client was connected and save the IP address
		struct sockaddr_in clientAddr, serverAddr;
		in_addrSize = sizeof(struct sockaddr_in);
		getpeername(_sd, (struct sockaddr *)&clientAddr, (socklen_t *)&in_addrSize);
		getsockname(_sd, (struct sockaddr *)&serverAddr, (socklen_t *)&in_addrSize);
		ESP_LOGI(TAG, "Client IP: 0x%08"PRIx32, clientAddr.sin_addr.s_addr);
		ESP_LOGI(TAG, "Server IP: 0x%08"PRIx32, serverAddr.sin_addr.s_addr);
		*ip_addr = serverAddr.sin_addr.s_addr;
	}

	// enable non-blocking mode if not data channel connection
	uint32_t option = fcntl(_sd, F_GETFL, 0);
	if (l_sd != ftp_data.ld_sd) option |= O_NONBLOCK;
	fcntl(_sd, F_SETFL, option);

	// client connected, so go on
	return E_FTP_RESULT_OK;
}

//------------------------------------------
static void ftp_send_list(uint32_t datasize)
{
	int32_t timeout = FTP_TIMEOUT_DEFAULT_MS;
	ftp_result_t result;

	ESP_LOGI(TAG, "%s, datasize %"PRIu32, __func__, datasize);
	vTaskDelay(1);

	while (1) {
		result = send(ftp_data.d_sd, ftp_data.dBuffer, datasize, 0);
		if (result == datasize) {
			vTaskDelay(1);
			ESP_LOGI(TAG, "Send OK");
			break;
		}
		else {
			vTaskDelay(1);
			if ((timeout <= 0) || (errno != EAGAIN)) {
				// error
				_ftp_reset();
				ESP_LOGW(TAG, "Error sending list data.");
				break;
			}
		}
		timeout -= portTICK_PERIOD_MS;
	}
}

//-----------------------------------------------
static void ftp_send_file_data(uint32_t datasize)
{
	ftp_result_t result;
	uint32_t timeout = FTP_TIMEOUT_DEFAULT_MS;

	ESP_LOGI(TAG, "%s, datasize %"PRIu32, __func__, datasize);
	vTaskDelay(1);

	while (1) {
		result = send(ftp_data.d_sd, ftp_data.dBuffer, datasize, 0);
		if (result == datasize) {
			vTaskDelay(1);
			ESP_LOGI(TAG, "Send OK");
			break;
		}
		else {
			vTaskDelay(1);
			if ((timeout <= 0) || (errno != EAGAIN)) {
				// error
				_ftp_reset();
				ESP_LOGW(TAG, "Error sending file data.");
				break;
			}
		}
		timeout -= portTICK_PERIOD_MS;
	}
}

//------------------------------------------------------------------------------------------------
static ftp_result_t ftp_recv_non_blocking (int32_t sd, void *buff, int32_t Maxlen, int32_t *rxLen)
{
	if (sd < 0) return E_FTP_RESULT_FAILED;

	*rxLen = recv(sd, buff, Maxlen, 0);
	if (*rxLen > 0) return E_FTP_RESULT_OK;
	else if (errno != EAGAIN) return E_FTP_RESULT_FAILED;

	return E_FTP_RESULT_CONTINUE;
}

// ==== Directory functions =======================

//-----------------------------------
#if 0
static void ftp_fix_path(char *pwd) {
	ESP_LOGI(TAG, "ftp_fix_path: pwd=[%s]", pwd);
	char ph_path[128];
	uint len = strlen(pwd);

	if (len == 0) {
		strcpy (pwd, "/");
	}
	else if ((len > 1) && (pwd[len-1] == '/')) pwd[len-1] = '\0';

	// Convert to physical path
	if (strstr(pwd, VFS_NATIVE_INTERNAL_MP) == pwd) {
		ESP_LOGI(TAG, "ftp_fix_path: VFS_NATIVE_INTERNAL_MP=[%s]", VFS_NATIVE_INTERNAL_MP);
		sprintf(ph_path, "%s%s", VFS_NATIVE_MOUNT_POINT, pwd+strlen(VFS_NATIVE_INTERNAL_MP));
		if (strcmp(ph_path, VFS_NATIVE_MOUNT_POINT) == 0) strcat(ph_path, "/");
		ESP_LOGI(TAG, "ftp_fix_path: ph_path=[%s]", ph_path);
		strcpy(pwd, ph_path);
	}
	else if (strstr(pwd, VFS_NATIVE_EXTERNAL_MP) == pwd) {
		ESP_LOGI(TAG, "ftp_fix_path: VFS_NATIVE_EXTERNAL_MP=[%s]", VFS_NATIVE_EXTERNAL_MP);
		sprintf(ph_path, "%s%s", VFS_NATIVE_SDCARD_MOUNT_POINT, pwd+strlen(VFS_NATIVE_EXTERNAL_MP));
		if (strcmp(ph_path, VFS_NATIVE_SDCARD_MOUNT_POINT) == 0) strcat(ph_path, "/");
		ESP_LOGI(TAG, "ftp_fix_path: ph_path=[%s]", ph_path);
		strcpy(pwd, ph_path);
	}
}
#endif


/*
 * Add directory or file name to the current path
 * Initially, pwd is set to "/" (file system root)
 * There are two possible entries in root:
 *	 "flash"	which can be fatfs or spiffs
 *	 "sd"		sd card
 * flash and sd entries have to be translated to their VFS names
 * trailing '/' is required for flash&sd root entries (translated)
 */
//-------------------------------------------------
static void ftp_open_child (char *pwd, char *dir) {
	ESP_LOGI(TAG, "%s, [%s] + [%s]", __func__, pwd, dir);
	if (strlen(dir) > 0) {
		if (dir[0] == '/') {
			// ** absolute path
			strcpy(pwd, dir);
		}
		else {
			// ** relative path
			// add trailing '/' if needed
			if ((strlen(pwd) > 1) && (pwd[strlen(pwd)-1] != '/') && (dir[0] != '/')) strcat(pwd, "/");
			// append directory/file name
			strcat(pwd, dir);
		}
#if 0
		ftp_fix_path(pwd);
#endif
	}
	ESP_LOGI(TAG, "New pwd: %s", pwd);
}

// Return to parent directory
//---------------------------------------
static void ftp_close_child (char *pwd) {
	ESP_LOGI(TAG, "%s, %s", __func__, pwd);
	uint len = strlen(pwd);
	if (pwd[len-1] == '/') {
		pwd[len-1] = '\0';
		len--;
		if ((len == 0) || (strcmp(pwd, VFS_NATIVE_MOUNT_POINT) == 0) || (strcmp(pwd, VFS_NATIVE_SDCARD_MOUNT_POINT) == 0)) {
			strcpy(pwd, "/");
		}
	}
	else {
		while (len) {
			if (pwd[len-1] == '/') {
				pwd[len-1] = '\0';
				len--;
				break;
			}
			len--;
		}

		if (len == 0) {
			strcpy (pwd, "/");
		}
		else if ((strcmp(pwd, VFS_NATIVE_MOUNT_POINT) == 0) || (strcmp(pwd, VFS_NATIVE_SDCARD_MOUNT_POINT) == 0)) {
			strcat(pwd, "/");
		}
	}
	ESP_LOGI(TAG, "New pwd: %s", pwd);
}

// Remove file name from path
//-----------------------------------------------------------
static void remove_fname_from_path (char *pwd, char *fname) {
    if (strlen(fname) == 0) return;
    
    // fname이 pwd의 끝부분과 일치하는지 확인
    size_t cwd_len = strlen(pwd);
    size_t fname_len = strlen(fname);
    
    if (cwd_len >= fname_len) {
        // pwd가 ".../fname" 형태인지 확인
        if (strcmp(pwd + cwd_len - fname_len, fname) == 0) {
            // 끝부분 잘라냄
            pwd[cwd_len - fname_len] = '\0';
            
            // 그 앞의 슬래시 처리 (루트가 아니면 제거)
            size_t new_len = strlen(pwd);
            if (new_len > 1 && pwd[new_len-1] == '/') {
                pwd[new_len-1] = '\0';
            }
        }
    }
    // 만약 fname이 경로에 포함되어 있지 않다면? 
    // 로직 오류지만, 안전을 위해 Last Slash 기준으로 자름
    else {
        char *p = strrchr(pwd, '/');
        if (p) {
            if (p == pwd) *(p+1) = '\0'; // 루트
            else *p = '\0';
        }
    }
}

// ==== Param functions =================================================

//------------------------------------------------------------------------------------------
static void ftp_pop_param(char **str, char *param, bool stop_on_space, bool stop_on_newline)
{
	char lastc = '\0';
	while (**str != '\0') {
		if (stop_on_space && (**str == ' ')) break;
		if ((**str == '\r') || (**str == '\n')) {
			if (!stop_on_newline) {
				(*str)++;
				continue;
			}
			else break;
		}
		if ((**str == '/') && (lastc == '/')) {
			(*str)++;
			continue;
		}
		lastc = **str;
		*param++ = **str;
		(*str)++;
	}
	*param = '\0';
}

//--------------------------------------------------
static ftp_cmd_index_t ftp_pop_command(char **str) {
	char _cmd[FTP_CMD_SIZE_MAX];
	ftp_pop_param (str, _cmd, true, true);
	stoupper (_cmd);
	for (ftp_cmd_index_t i = 0; i < E_FTP_NUM_FTP_CMDS; i++) {
		if (!strcmp (_cmd, ftp_cmd_table[i].cmd)) {
			// move one step further to skip the space
			(*str)++;
			return i;
		}
	}
	return E_FTP_CMD_NOT_SUPPORTED;
}

// Get file name from parameter and append to ftp_path
//-------------------------------------------------------
static void ftp_get_param_and_open_child(char **bufptr) {
	ftp_pop_param(bufptr, ftp_scratch_buffer, false, false);
	ftp_open_child(ftp_path, ftp_scratch_buffer);
	ftp_data.closechild = true;
}

// ==== Ftp command processing =====

//----------------------------------
static void ftp_process_cmd (void) {
	int32_t len;
	char *bufptr = (char *)ftp_cmd_buffer;
	ftp_result_t result;
	struct stat buf;
//	int res;

	memset(bufptr, 0, FTP_MAX_PARAM_SIZE + FTP_CMD_SIZE_MAX);
	ftp_data.closechild = false;

	ftp_state_t old_state = ftp_data.state;

	// use the reply buffer to receive new commands
	result = ftp_recv_non_blocking(ftp_data.c_sd, ftp_cmd_buffer, FTP_MAX_PARAM_SIZE + FTP_CMD_SIZE_MAX, &len);
	if (result == E_FTP_RESULT_OK) {
		ftp_cmd_buffer[len] = '\0';
		// bufptr is moved as commands are being popped
		ftp_cmd_index_t cmd = ftp_pop_command(&bufptr);
		if (!ftp_data.loggin.passvalid &&
				((cmd != E_FTP_CMD_USER) && (cmd != E_FTP_CMD_PASS) && (cmd != E_FTP_CMD_QUIT) 
					&& (cmd != E_FTP_CMD_FEAT) && (cmd != E_FTP_CMD_AUTH))) {
			ftp_send_reply(FTP_CODE_NEED_ACCOUNT, NULL);
			return;
		}
		if ((cmd >= 0) && (cmd < E_FTP_NUM_FTP_CMDS)) {
			ESP_LOGI(TAG, "CMD: %s", ftp_cmd_table[cmd].cmd);
		}
		else {
			ESP_LOGW(TAG, "CMD: %d, no controller", cmd);
			return;
		}

		char fullname[128];
		char fullname2[128];
		strcpy(fullname, PP_MOUNT_POINT);
		strcpy(fullname2, PP_MOUNT_POINT);

		switch (cmd) {
		case E_FTP_CMD_FEAT:
		{
			ftp_send_reply(FTP_CODE_CMD_NOT_IMPL, "no-features");
			break;
		}

		case E_FTP_CMD_AUTH:
		{
			ftp_send_reply(FTP_CODE_CMD_NOT_IMPL_PARAM, "not-supported");
			break;
		}

		case E_FTP_CMD_SYST:
		{
			ftp_send_reply(FTP_CODE_SYSTEM_TYPE, "UNIX Type: L8");
			break;
		}

		case E_FTP_CMD_CDUP:
		{
			ftp_close_child(ftp_path);
			ftp_send_reply(FTP_CODE_FILE_ACTION_OK, NULL);
			break;
		}

		case E_FTP_CMD_CWD:
		{
            ftp_pop_param (&bufptr, ftp_scratch_buffer, false, false);

            if (strlen(ftp_scratch_buffer) > 0) {
                if ((ftp_scratch_buffer[0] == '.') && (ftp_scratch_buffer[1] == '\0')) {
                    ftp_data.dp = NULL;
                    ftp_send_reply(FTP_CODE_FILE_ACTION_OK, NULL);
                    break;
                }
                if ((ftp_scratch_buffer[0] == '.') && (ftp_scratch_buffer[1] == '.') && (ftp_scratch_buffer[2] == '\0')) {
                    ftp_close_child (ftp_path);
                    ftp_send_reply(FTP_CODE_FILE_ACTION_OK, NULL);
                    break;
                }
                else ftp_open_child (ftp_path, ftp_scratch_buffer);
            }

            // 루트 체크
            if ((ftp_path[0] == '/') && (ftp_path[1] == '\0')) {
                ftp_data.dp = NULL;
                ftp_send_reply(FTP_CODE_FILE_ACTION_OK, NULL);
            }
            else {
                strcat(fullname, ftp_path);
                ESP_LOGI(TAG, "E_FTP_CMD_CWD fullname=[%s]", fullname);
                ftp_data.dp = opendir(fullname);
                if (ftp_data.dp != NULL) {
                    closedir(ftp_data.dp);
                    ftp_data.dp = NULL;
                    ftp_send_reply(FTP_CODE_FILE_ACTION_OK, NULL);
                }
                
                // [수정] 가상 경로 진입 허용 (CWD)
                else {
                    // 가상 경로 확인을 위해 잠시 끝 슬래시 제거 (Normalization)
                    size_t p_len = strlen(ftp_path);
                    char last_char = 0;
                    if (p_len > 1 && ftp_path[p_len-1] == '/') {
                        last_char = '/';
                        ftp_path[p_len-1] = '\0'; // 잠시 자름
                    }

                    if (strcmp(ftp_path, "/iLRiTek") == 0 || 
                        strcmp(ftp_path, "/iLRiTek/dataset") == 0 || 
                        strcmp(ftp_path, PP_VIRTUAL_FOLDER_FACE) == 0 || 
                        strcmp(ftp_path, PP_VIRTUAL_FOLDER_VOICE) == 0) 
                    {
                        // 가상 경로 성공 -> 슬래시는 사용자 취향껏 복구하거나 유지
                        // FTP 표준이나 클라이언트에 따라 슬래시 없는 게 나을 수도 있음. 
                        // 여기서는 일단 성공 처리하고 복구하지 않음 (깔끔한 경로)
                        ESP_LOGI(TAG, "Virtual path CWD OK: %s", ftp_path);
                        ftp_send_reply(FTP_CODE_FILE_ACTION_OK, NULL);
                    }
                    else {
                        // 실패시 복구
                        if (last_char) ftp_path[strlen(ftp_path)] = last_char;

                        ftp_close_child (ftp_path); // 실패했으므로 경로 원복
                        ftp_send_reply(FTP_CODE_FILE_UNAVAILABLE, NULL);
                    }
                }
            }
            break;
		}

		case E_FTP_CMD_PWD:
		case E_FTP_CMD_XPWD:
		{
			char lpath[128];
			strcpy(lpath, ftp_path);

			ftp_send_reply(FTP_CODE_PATHNAME_CREATED, lpath);
			break;
		}

		case E_FTP_CMD_SIZE:
		{
			ftp_get_param_and_open_child (&bufptr);
			strcat(fullname, ftp_path);
			ESP_LOGI(TAG, "E_FTP_CMD_SIZE fullname=[%s]", fullname);

			int res = stat(fullname, &buf);
			if (res == 0) {
				// send the file size
				snprintf((char *)ftp_data.dBuffer, ftp_buff_size, "%"PRIu32, (uint32_t)buf.st_size);
				ftp_send_reply(FTP_CODE_FILE_STATUS, (char *)ftp_data.dBuffer);
			}
            else {
                uint32_t virtual_size = 0;
                bool found = false;

                // Face 폴더 검사
                if (strstr(ftp_path, PP_VIRTUAL_FOLDER_FACE)) {
                     // 파일명만 추출해서 "Face_" 로 시작하고 숫자가 맞으면 g_psram_face[i].len 반환
                     char *fname = strrchr(ftp_path, '/');
                     if(fname) fname++; else fname = ftp_path;
                     
                     if (strncmp(fname, FACE_FILE_PREFIX, strlen(FACE_FILE_PREFIX)) == 0) {
                         int idx = atoi(fname + strlen(FACE_FILE_PREFIX)) - 1;
                         if (idx >= 0 && idx < g_current_max_face && g_psram_face[idx].is_valid) {
                             virtual_size = g_psram_face[idx].len;
                             found = true;
                         }
                     }
                }
                // Voice 폴더 검사
                else if (strstr(ftp_path, PP_VIRTUAL_FOLDER_VOICE)) {
                    char *fname = strrchr(ftp_path, '/');
                    if (fname) fname++; else fname = ftp_path;

                    int v_idx = -1;

                    // [수정] Case 1: record.wav (Normal Mode) 추가
                    if (strcmp(fname, VOICE_WORK_PREFIX) == 0) {
                        v_idx = 0; // 보통 0번 슬롯 사용
                    }
                    // Case 2: test_record_X.wav (Learning Mode)
                    else if (strncmp(fname, VOICE_LEARN_PREFIX, strlen(VOICE_LEARN_PREFIX)) == 0) {
                        int num = atoi(fname + strlen(VOICE_LEARN_PREFIX));
                        if (num >= 1 && num <= MAX_VOICE_FILES_COUNT) {
                            v_idx = num - 1;
                        }
                    }

                    // 유효성 검사 및 크기 계산 (WAV Header WAVE_HEADER_SIZE(==44) bytes 추가)
                    if (v_idx >= 0 && v_idx < MAX_VOICE_FILES_COUNT) {
						if (g_voice_mutex != NULL && xSemaphoreTake(g_voice_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
							if (g_psram_voice[v_idx].is_valid && g_psram_voice[v_idx].data != NULL) {
								 virtual_size = g_psram_voice[v_idx].len + WAVE_HEADER_SIZE;
								 found = true;
							}
							xSemaphoreGive(g_voice_mutex);
						}
                    }
                }

                if (found) {
                    snprintf((char *)ftp_data.dBuffer, ftp_buff_size, "%"PRIu32, virtual_size);
        			ftp_send_reply(FTP_CODE_FILE_STATUS, (char *)ftp_data.dBuffer);
                } else {
                    ftp_send_reply(FTP_CODE_FILE_UNAVAILABLE, NULL);
                }
            }
			break;
		}

		case E_FTP_CMD_MDTM:
		{
			ftp_get_param_and_open_child (&bufptr);
			strcat(fullname, ftp_path);
			ESP_LOGI(TAG, "E_FTP_CMD_MDTM fullname=[%s]", fullname);
			//res = stat(ftp_path, &buf);
			int res = stat(fullname, &buf);
			if (res == 0) {
				// send the file modification time
				//snprintf((char *)ftp_data.dBuffer, ftp_buff_size, "%u", (uint32_t)buf.st_mtime);
				//snprintf((char *)ftp_data.dBuffer, ftp_buff_size, "20210212010203");
				//time_t time = buf.st_mtime + (CONFIG_LOCAL_TIMEZONE*60*60);
				time_t time = buf.st_mtime;
				struct tm *ptm = localtime(&time);
				//char buf[128];
				//strftime(buf, sizeof(buf), "%Y%m%d%H%M%S", ptm);
				strftime((char *)ftp_data.dBuffer, ftp_buff_size, "%Y%m%d%H%M%S", ptm);
				ESP_LOGI(TAG, "E_FTP_CMD_MDTM ftp_data.dBuffer=[%s]", ftp_data.dBuffer);
				ftp_send_reply(FTP_CODE_FILE_STATUS, (char *)ftp_data.dBuffer);
			}
            else if (strstr(fullname, PP_VIRTUAL_FOLDER_FACE) || strstr(fullname, PP_VIRTUAL_FOLDER_VOICE)) {
                 // 가상 파일은 항상 현재 시간 또는 특정 시간으로 응답
                 time_t now;
                 time(&now);
                 struct tm *ptm = localtime(&now);
                 strftime((char *)ftp_data.dBuffer, ftp_buff_size, "%Y%m%d%H%M%S", ptm);
                 ftp_send_reply(FTP_CODE_FILE_STATUS, (char *)ftp_data.dBuffer);
			}
			else {
				ftp_send_reply(FTP_CODE_FILE_UNAVAILABLE, NULL);
			}
			break;
		}

		case E_FTP_CMD_TYPE:
		{
			ftp_send_reply(FTP_CODE_COMMAND_OK, NULL);
			break;
		}

		case E_FTP_CMD_USER:
		{
			ftp_pop_param (&bufptr, ftp_scratch_buffer, true, true);
			if (!memcmp(ftp_scratch_buffer, ftp_user, MAX(strlen(ftp_scratch_buffer), strlen(ftp_user)))) {
				ftp_data.loggin.uservalid = true && (strlen(ftp_user) == strlen(ftp_scratch_buffer));
			}
			ftp_send_reply(FTP_CODE_USER_OK_NEED_PASS, NULL);
			break;
		}

		case E_FTP_CMD_PASS:
		{
			ftp_pop_param (&bufptr, ftp_scratch_buffer, true, true);
			if (!memcmp(ftp_scratch_buffer, ftp_pass, MAX(strlen(ftp_scratch_buffer), strlen(ftp_pass))) &&
					ftp_data.loggin.uservalid) {
				ftp_data.loggin.passvalid = true && (strlen(ftp_pass) == strlen(ftp_scratch_buffer));
				if (ftp_data.loggin.passvalid) {
					ftp_send_reply(FTP_CODE_USER_LOGGED_IN, NULL);
					ESP_LOGI(TAG, "Connected.");
					break;
				}
			}
			ftp_send_reply(FTP_CODE_NOT_LOGGED_IN, NULL);
			break;
		}

		case E_FTP_CMD_PASV:
		{
			// some servers (e.g. google chrome) send PASV several times very quickly
			closesocket(ftp_data.d_sd);

			ftp_data.d_sd = -1;
			ftp_data.substate = E_FTP_STE_SUB_DISCONNECTED;
			bool socketcreated = true;
			if (ftp_data.ld_sd < 0) {
				socketcreated = ftp_create_listening_socket(&ftp_data.ld_sd, FTP_PASIVE_DATA_PORT, FTP_DATA_CLIENTS_MAX - 1);
			}

			if (socketcreated) {
				uint8_t *pip = (uint8_t *)&ftp_data.ip_addr;
				ftp_data.dtimeout = 0;
				snprintf((char *)ftp_data.dBuffer, ftp_buff_size, "(%u,%u,%u,%u,%u,%u)",
							pip[0], pip[1], pip[2], pip[3], (FTP_PASIVE_DATA_PORT >> 8), (FTP_PASIVE_DATA_PORT & 0xFF));
				ftp_data.substate = E_FTP_STE_SUB_LISTEN_FOR_DATA;
				ESP_LOGI(TAG, "Data socket created");
				ftp_send_reply(FTP_CODE_ENTERING_PASSIVE, (char *)ftp_data.dBuffer);
			}
			else {
				ESP_LOGW(TAG, "Error creating data socket");
				ftp_send_reply(FTP_CODE_CANT_OPEN_DATA, NULL);
			}

			break;
		}

		case E_FTP_CMD_LIST:
		case E_FTP_CMD_NLST:
		{
            ftp_pop_param(&bufptr, ftp_scratch_buffer, false, false);
            
            if (strlen(ftp_scratch_buffer) > 0) {
                if (ftp_scratch_buffer[0] == '-') {
                    // 옵션 플래그(예: -a, -l)이므로 경로 변경 안함
                    ESP_LOGI(TAG, "LIST/NLST option ignored: %s", ftp_scratch_buffer);
                    ftp_data.closechild = false; 
                } else {
                    // 유효한 경로 인자임
                    ftp_open_child(ftp_path, ftp_scratch_buffer);
                    ftp_data.closechild = true;
                }
            } else {
                ftp_data.closechild = false;
            }
			ftp_nlist = (cmd == E_FTP_CMD_LIST) ? 0 : 1;

			ftp_result_t result = ftp_open_dir_for_listing(ftp_path);
			if (result == E_FTP_RESULT_CONTINUE) {
				ESP_LOGI(TAG, "E_FTP_CMD_LIST/NLST continue");
				ftp_data.state = E_FTP_STE_CONTINUE_LISTING;
				ESP_LOGI(TAG, "State Change, %s -> %s", conv_state2str(old_state), conv_state2str(ftp_data.state));
				ftp_send_reply(FTP_CODE_ABOUT_TO_OPEN_DATA, NULL);
			}
			else {
				ESP_LOGW(TAG, "E_FTP_CMD_LIST/NLST result %d", result);
				ftp_send_reply(FTP_CODE_FILE_UNAVAILABLE, NULL);
			}
			break;
		}

        case E_FTP_CMD_RETR:
        {
            ftp_data.total = 0;
            ftp_data.time = 0;
            ftp_get_param_and_open_child(&bufptr);

            if (ftp_data.d_sd < 0) {
                ESP_LOGE(TAG, "Data connection not established (d_sd=%d)", ftp_data.d_sd);
                ftp_send_reply(FTP_CODE_CANT_OPEN_DATA, NULL);
                break;
            }

            int v_index = -1;
            int v_type = 0; // 0: 일반 파일, 1: Face, 2: Voice

            // =========================================================
            // [중요] 가상 폴더인지 먼저 확인 (strstr 사용)
            // =========================================================
            char *pFace = strstr(ftp_path, PP_VIRTUAL_FOLDER_FACE);
            char *pVoice = strstr(ftp_path, PP_VIRTUAL_FOLDER_VOICE);

            if (pFace) {
                // Face 가상 폴더 경로 포함됨 -> 무조건 Face 가상 파일 처리 시도
                // 파일명 찾기 (마지막 슬래시 뒤)
                char *fname = strrchr(ftp_path, '/');
                if (fname) fname++; else fname = ftp_path;
                
                if (strncmp(fname, FACE_FILE_PREFIX, strlen(FACE_FILE_PREFIX)) == 0) {
                    // "Face_" 로 시작함
                    const char *num_part = fname + strlen(FACE_FILE_PREFIX);
                     // 숫자 파싱 (atoi)
                    int file_num = atoi(num_part); 
                    if (file_num >= 1 && file_num <= g_current_max_face) {
                         v_index = file_num - 1;
                         v_type = 1; // Face
                    } else {
                        // Face 폴더인데 유효하지 않은 인덱스
                        ESP_LOGW(TAG, "Invalid Face Index: %d (Max: %d)", file_num, g_current_max_face);
                        v_type = -1; // 처리 중단 신호
                    }
                } else {
                    // Face 폴더인데 Face_X.bmp 형식이 아님
                    v_type = -1; 
                }
            }
            else if (pVoice) {
                // Voice 가상 폴더
                char *fname = strrchr(ftp_path, '/');
                if (fname) fname++; else fname = ftp_path;

                if (strncmp(fname, VOICE_LEARN_PREFIX, strlen(VOICE_LEARN_PREFIX)) == 0) {
                     int num = atoi(fname + strlen(VOICE_LEARN_PREFIX));
                     if (num >= 1 && num <= MAX_VOICE_FILES_COUNT) {
                         v_index = num - 1;
                         v_type = 2; // Voice
                     }
                }
                else if (strcmp(fname, VOICE_WORK_PREFIX) == 0) {
                    v_index = 0;
                    v_type = 2; // Voice
                }
                else {
                    v_type = -1;
                }
            }


            // =========================================================
            // 전송 분기 처리
            // =========================================================
            if (v_type == 1 && v_index >= 0) {
                // [Face 가상 파일 전송]
                ftp_send_virtual_face_file(v_index);
                ftp_data.state = E_FTP_STE_END_TRANSFER;
            } 
            else if (v_type == 2 && v_index >= 0) {
                // [Voice 가상 파일 전송]
                ftp_send_virtual_voice_file(v_index); 
                ftp_data.state = E_FTP_STE_END_TRANSFER;
            } 
            else if (v_type == -1) {
                // [가상 폴더인데 파일 못 찾음 -> 에러 리턴]
                // 가상 폴더의 경우 실제 파일시스템을 뒤지면 안 됨 (littlefs 에러 로그 방지)
                ESP_LOGW(TAG, "Virtual file not found: %s", ftp_path);
                ftp_data.state = E_FTP_STE_END_TRANSFER;
                ftp_send_reply(FTP_CODE_FILE_UNAVAILABLE, NULL);
            }
            else if ((strlen(ftp_path) > 0) && (ftp_path[strlen(ftp_path)-1] != '/')) {
                // [일반 파일 시스템 파일]
                if (ftp_open_file(ftp_path, "rb")) {
                    ftp_data.state = E_FTP_STE_CONTINUE_FILE_TX;
                    vTaskDelay(20 / portTICK_PERIOD_MS);
                    ftp_send_reply(FTP_CODE_ABOUT_TO_OPEN_DATA, NULL);
                }
                else {
                    ftp_data.state = E_FTP_STE_END_TRANSFER;
                    ftp_send_reply(FTP_CODE_FILE_UNAVAILABLE, NULL);
                }
            }
            else {
                ftp_data.state = E_FTP_STE_END_TRANSFER;
                ftp_send_reply(FTP_CODE_FILE_UNAVAILABLE, NULL);
            }
            ESP_LOGI(TAG, "State Change, %s -> %s", conv_state2str(old_state), conv_state2str(ftp_data.state));
            break;
        }

		case E_FTP_CMD_APPE:
		{
			ftp_data.total = 0;
			ftp_data.time = 0;
			ftp_get_param_and_open_child(&bufptr);
			if ((strlen(ftp_path) > 0) && (ftp_path[strlen(ftp_path)-1] != '/')) {
				if (ftp_open_file(ftp_path, "ab")) {
					ftp_data.state = E_FTP_STE_CONTINUE_FILE_RX;
					vTaskDelay(20 / portTICK_PERIOD_MS);
					ftp_send_reply(FTP_CODE_ABOUT_TO_OPEN_DATA, NULL);
				}
				else {
					ftp_data.state = E_FTP_STE_END_TRANSFER;
					ftp_send_reply(FTP_CODE_FILE_UNAVAILABLE, NULL);
				}
			}
			else {
				ftp_data.state = E_FTP_STE_END_TRANSFER;
				ftp_send_reply(FTP_CODE_FILE_UNAVAILABLE, NULL);
			}
			ESP_LOGI(TAG, "State Change, %s -> %s", conv_state2str(old_state), conv_state2str(ftp_data.state));
			break;
		}

		case E_FTP_CMD_STOR:
		{
			ftp_data.total = 0;
			ftp_data.time = 0;
			ftp_get_param_and_open_child(&bufptr);
			if ((strlen(ftp_path) > 0) && (ftp_path[strlen(ftp_path)-1] != '/')) {
				ESP_LOGI(TAG, "E_FTP_CMD_STOR ftp_path=[%s]", ftp_path);
				bool res = ftp_open_file(ftp_path, "wb");
				if (res) {
					ESP_LOGI(TAG, "E_FTP_CMD_STOR ftp_open_file OK");
					ftp_data.state = E_FTP_STE_CONTINUE_FILE_RX;
					vTaskDelay(20 / portTICK_PERIOD_MS);
					ftp_send_reply(FTP_CODE_ABOUT_TO_OPEN_DATA, NULL);
				}
				else {
					ESP_LOGI(TAG, "E_FTP_CMD_STOR ftp_open_file FAILED");
					ftp_data.state = E_FTP_STE_END_TRANSFER;
					ftp_send_reply(FTP_CODE_FILE_UNAVAILABLE, NULL);
				}
			}
			else {
				ESP_LOGW(TAG, "E_FTP_CMD_STOR INVALID PATH");
				ftp_data.state = E_FTP_STE_END_TRANSFER;
				ftp_send_reply(FTP_CODE_FILE_UNAVAILABLE, NULL);
			}
			ESP_LOGI(TAG, "State Change, %s -> %s", conv_state2str(old_state), conv_state2str(ftp_data.state));
			break;
		}

		case E_FTP_CMD_DELE:
		{
			ftp_get_param_and_open_child(&bufptr);
			if ((strlen(ftp_path) > 0) && (ftp_path[strlen(ftp_path)-1] != '/')) {
				ESP_LOGI(TAG, "E_FTP_CMD_DELE ftp_path=[%s]", ftp_path);

				int v_index = -1;
				int v_type = 0; // 0: None, 1: Face, 2: Voice

                // -------------------------------------------------------------
                // 1. Face 파일 확인 (Face_X.bmp 동적 파싱)
                // -------------------------------------------------------------
                char *fname_face = NULL;
                size_t face_folder_len = strlen(PP_VIRTUAL_FOLDER_FACE);
                
                // 경로 파싱 (RETR과 동일한 로직)
                if (strncmp(ftp_path, PP_VIRTUAL_FOLDER_FACE, face_folder_len) == 0 && 
                    ftp_path[face_folder_len] == '/') {
                    fname_face = ftp_path + face_folder_len + 1;
                }
                else if (ftp_path[0] == '/') {
                    fname_face = ftp_path + 1;
                }
                // 상대 경로는 ftp_get_param_and_open_child()에 의해 이미 절대 경로로 변환됨

                if (fname_face) {
                     if (strncmp(fname_face, FACE_FILE_PREFIX, strlen(FACE_FILE_PREFIX)) == 0) {
                         const char *num_part = fname_face + strlen(FACE_FILE_PREFIX);
                         int file_num = atoi(num_part);
                         // g_current_max_face 범위 내인지 확인
                         if (file_num >= 1 && file_num <= g_current_max_face) {
                             v_index = file_num - 1;
                             v_type = 1; // Face
                         }
                     }
                }

				// -------------------------------------------------------------
				// 2. Voice 파일 확인 (record.wav, test_record_X.wav)
				// -------------------------------------------------------------
				if (v_type == 0) {
					size_t vfolder_len = strlen(PP_VIRTUAL_FOLDER_VOICE);
					const char *fname_voice = NULL;

					if (strncmp(ftp_path, PP_VIRTUAL_FOLDER_VOICE, vfolder_len) == 0 && 
						ftp_path[vfolder_len] == '/') {
						fname_voice = ftp_path + vfolder_len + 1;
					}
					else if (ftp_path[0] == '/') {
						fname_voice = ftp_path + 1;
					}
					else {
						fname_voice = ftp_path;
					}

					if (fname_voice) {
						if (strcmp(fname_voice, VOICE_WORK_PREFIX) == 0) {
							v_index = 0;
							v_type = 2; // Voice
						}
						else if (strncmp(fname_voice, VOICE_LEARN_PREFIX, strlen(VOICE_LEARN_PREFIX)) == 0) {
							const char *num_part = fname_voice + strlen(VOICE_LEARN_PREFIX);
							int file_num = atoi(num_part);
							if (file_num >= 1 && file_num <= MAX_VOICE_FILES_COUNT) {
								v_index = file_num - 1;
								v_type = 2; // Voice
							}
						}
					}
				}

				// -------------------------------------------------------------
				// 3. 삭제(데이터 초기화) 실행
				// -------------------------------------------------------------
				if (v_type == 1 && v_index >= 0) {
					// [Face 삭제]
					if (g_face_mutex != NULL && xSemaphoreTake(g_face_mutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
						g_psram_face[v_index].len = 0;
						g_psram_face[v_index].is_valid = false;
						// 필요 시 데이터 0 초기화: if(g_psram_face[v_index].data) memset(...);
						xSemaphoreGive(g_face_mutex);

						ESP_LOGI(TAG, "Virtual Face File %d deleted (memory cleared)", v_index);
						vTaskDelay(20 / portTICK_PERIOD_MS);
						ftp_send_reply(FTP_CODE_FILE_ACTION_OK, NULL);
					} else {
						ftp_send_reply(FTP_CODE_FILE_UNAVAILABLE, NULL);
					}
				}
				else if (v_type == 2 && v_index >= 0) {
					// [Voice 삭제]
					if (g_voice_mutex != NULL && xSemaphoreTake(g_voice_mutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
						g_psram_voice[v_index].len = 0;
						g_psram_voice[v_index].is_valid = false;
						xSemaphoreGive(g_voice_mutex);

						ESP_LOGI(TAG, "Virtual Voice File %d deleted (memory cleared)", v_index);
						vTaskDelay(20 / portTICK_PERIOD_MS);
						ftp_send_reply(FTP_CODE_FILE_ACTION_OK, NULL);
					} else {
						ftp_send_reply(FTP_CODE_FILE_UNAVAILABLE, NULL);
					}
				}
				else {
					// [실제 파일 시스템 파일 삭제]
					strcat(fullname, ftp_path);
					ESP_LOGI(TAG, "E_FTP_CMD_DELE fullname=[%s]", fullname);

					if (unlink(fullname) == 0) {
						vTaskDelay(20 / portTICK_PERIOD_MS);
						ftp_send_reply(FTP_CODE_FILE_ACTION_OK, NULL);
					}
					else {
						ftp_send_reply(FTP_CODE_FILE_UNAVAILABLE, NULL);
					}
				}
			}
			else {
				ftp_send_reply(FTP_CODE_FILE_ACTION_OK, NULL);
			}
			
			break;
		}
		case E_FTP_CMD_RMD:
		{
			ftp_get_param_and_open_child(&bufptr);
			if ((strlen(ftp_path) > 0) && (ftp_path[strlen(ftp_path)-1] != '/')) {
				ESP_LOGI(TAG, "E_FTP_CMD_RMD ftp_path=[%s]", ftp_path);

				strcat(fullname, ftp_path);
				ESP_LOGI(TAG, "E_FTP_CMD_MKD fullname=[%s]", fullname);

				//if (rmdir(ftp_path) == 0) {
				if (rmdir(fullname) == 0) {
					vTaskDelay(20 / portTICK_PERIOD_MS);
					ftp_send_reply(FTP_CODE_FILE_ACTION_OK, NULL);
				}
				else ftp_send_reply(FTP_CODE_FILE_UNAVAILABLE, NULL);
			}
			else ftp_send_reply(FTP_CODE_FILE_ACTION_OK, NULL);
			break;
		}

		case E_FTP_CMD_MKD:
		{
			ftp_get_param_and_open_child(&bufptr);
			if ((strlen(ftp_path) > 0) && (ftp_path[strlen(ftp_path)-1] != '/')) {
				ESP_LOGI(TAG, "E_FTP_CMD_MKD ftp_path=[%s]", ftp_path);

				strcat(fullname, ftp_path);
				ESP_LOGI(TAG, "E_FTP_CMD_MKD fullname=[%s]", fullname);

				//if (mkdir(ftp_path, 0755) == 0) {
				if (mkdir(fullname, 0755) == 0) {
					vTaskDelay(20 / portTICK_PERIOD_MS);
					ftp_send_reply(FTP_CODE_FILE_ACTION_OK, NULL);
				}
				else ftp_send_reply(FTP_CODE_FILE_UNAVAILABLE, NULL);
			}
			else ftp_send_reply(FTP_CODE_FILE_ACTION_OK, NULL);
			break;
		}

		case E_FTP_CMD_RNFR:
		{
			ftp_get_param_and_open_child(&bufptr);
			ESP_LOGI(TAG, "E_FTP_CMD_RNFR ftp_path=[%s]", ftp_path);

			strcat(fullname, ftp_path);
			ESP_LOGI(TAG, "E_FTP_CMD_MKD fullname=[%s]", fullname);

			int res = stat(fullname, &buf);
			if (res == 0) {
				ftp_send_reply(FTP_CODE_FILE_ACTION_PENDING, NULL);
				// save the path of the file to rename
				strcpy((char *)ftp_data.dBuffer, ftp_path);
			} else {
				ftp_send_reply(FTP_CODE_FILE_UNAVAILABLE, NULL);
			}
			break;
		}

		case E_FTP_CMD_RNTO:
		{
			ftp_get_param_and_open_child(&bufptr);
			// the path of the file to rename was saved in the data buffer
			ESP_LOGI(TAG, "E_FTP_CMD_RNTO ftp_path=[%s], ftp_data.dBuffer=[%s]", ftp_path, (char *)ftp_data.dBuffer);
			strcat(fullname, (char *)ftp_data.dBuffer);
			ESP_LOGI(TAG, "E_FTP_CMD_RNTO fullname=[%s]", fullname);
			strcat(fullname2, ftp_path);
			ESP_LOGI(TAG, "E_FTP_CMD_RNTO fullname2=[%s]", fullname2);

			//if (rename((char *)ftp_data.dBuffer, ftp_path) == 0) {
			if (rename(fullname, fullname2) == 0) {
				ftp_send_reply(FTP_CODE_FILE_ACTION_OK, NULL);
			} else {
				ftp_send_reply(FTP_CODE_FILE_UNAVAILABLE, NULL);
			}
			break;
		}

		case E_FTP_CMD_NOOP:
			ftp_send_reply(FTP_CODE_COMMAND_OK, NULL);
			break;

		case E_FTP_CMD_QUIT:
			ftp_send_reply(FTP_CODE_SERVICE_CLOSING, NULL);
			break;

		default:
			ESP_LOGW(TAG, "No controller for this command, %d", cmd);
			ftp_send_reply(FTP_CODE_CMD_NOT_IMPL, NULL);
			break;
		}

		if (ftp_data.closechild) {
			remove_fname_from_path(ftp_path, ftp_scratch_buffer);
		}
	}
	else if (result == E_FTP_RESULT_CONTINUE) {
		if (ftp_data.ctimeout > FTP_SERVER_TIMEOUT_MS) {
			ESP_LOGW(TAG, "FTP Server timeout");
			ftp_send_reply(FTP_CODE_SERVICE_CLOSING, NULL);
		}
	}
	else {
		ESP_LOGI(TAG, "ftp_recv_non_blocking failed");
		ftp_close_cmd_data();
	}
}

// ==== PUBLIC FUNCTIONS ===================================================================

//---------------------
void ftp_deinit(void) {
	if (ftp_path) free(ftp_path);
	if (ftp_cmd_buffer) free(ftp_cmd_buffer);
	if (ftp_data.dBuffer) free(ftp_data.dBuffer);
	if (ftp_scratch_buffer) free(ftp_scratch_buffer);
	ftp_path = NULL;
	ftp_cmd_buffer = NULL;
	ftp_data.dBuffer = NULL;
	ftp_scratch_buffer = NULL;
}

//-------------------
bool ftp_init(void) {
	ftp_stop = 0;
	// Allocate memory for the data buffer, and the file system structures (from the RTOS heap)
	ftp_deinit();

	memset(&ftp_data, 0, sizeof(ftp_data_t));
	ftp_data.dBuffer = malloc(ftp_buff_size+1);
	if (ftp_data.dBuffer == NULL) return false;

	ftp_path = malloc(FTP_MAX_PARAM_SIZE);
	if (ftp_path == NULL) {
		free(ftp_data.dBuffer);
		return false;
	}

	ftp_scratch_buffer = malloc(FTP_MAX_PARAM_SIZE);
	if (ftp_scratch_buffer == NULL) {
		free(ftp_path);
		free(ftp_data.dBuffer);
		return false;
	}

	ftp_cmd_buffer = malloc(FTP_MAX_PARAM_SIZE + FTP_CMD_SIZE_MAX);
	if (ftp_cmd_buffer == NULL) {
		free(ftp_scratch_buffer);
		free(ftp_path);
		free(ftp_data.dBuffer);
		return false;
	}

	//SOCKETFIFO_Init((void *)ftp_fifoelements, FTP_SOCKETFIFO_ELEMENTS_MAX);

	ftp_data.c_sd  = -1;
	ftp_data.d_sd  = -1;
	ftp_data.lc_sd = -1;
	ftp_data.ld_sd = -1;
	ftp_data.e_open = E_FTP_NOTHING_OPEN;
	ftp_data.state = E_FTP_STE_DISABLED;
	ftp_data.substate = E_FTP_STE_SUB_DISCONNECTED;

	//if (ftp_mutex == NULL) ftp_mutex = xSemaphoreCreateMutex();
	return true;
}

//============================
int ftp_run (uint32_t elapsed)
{
	if (ftp_stop) return -2;

	ftp_data.dtimeout += elapsed;
	ftp_data.ctimeout += elapsed;
	ftp_data.time += elapsed;

	ftp_state_t old_state = ftp_data.state;

	switch (ftp_data.state) {
		case E_FTP_STE_DISABLED:
			ESP_LOGI(TAG, "[E_FTP_STE_DISABLED]");
			if (ftp_data.enabled) {
				ftp_data.state = E_FTP_STE_START;
				ESP_LOGI(TAG, "State Change, %s -> %s", conv_state2str(old_state), conv_state2str(ftp_data.state));
			}
			break;

		case E_FTP_STE_START:
			ESP_LOGI(TAG, "[E_FTP_STE_START]");
			if (ftp_create_listening_socket(&ftp_data.lc_sd, FTP_CMD_PORT, FTP_CMD_CLIENTS_MAX - 1)) {
				ftp_data.state = E_FTP_STE_READY;
				ESP_LOGI(TAG, "State Change, %s -> %s", conv_state2str(old_state), conv_state2str(ftp_data.state));
			}
			break;

		case E_FTP_STE_READY:
			if (ftp_data.c_sd < 0 && ftp_data.substate == E_FTP_STE_SUB_DISCONNECTED) {
				if (E_FTP_RESULT_OK == ftp_wait_for_connection(ftp_data.lc_sd, &ftp_data.c_sd, &ftp_data.ip_addr)) {
					ftp_data.txRetries = 0;
					ftp_data.logginRetries = 0;
					ftp_data.ctimeout = 0;
					ftp_data.loggin.uservalid = false;
					ftp_data.loggin.passvalid = false;
					strcpy (ftp_path, "/");
					ESP_LOGI(TAG, "FTP Connected.");
					ftp_send_reply(FTP_CODE_SERVICE_READY, "ESP32 FTP Server");
					break;
				}
			}
			if (ftp_data.c_sd > 0 && ftp_data.substate != E_FTP_STE_SUB_LISTEN_FOR_DATA) {
				ftp_process_cmd();
				if (ftp_data.state != E_FTP_STE_READY) {
					break;
				}
			}
			break;

		case E_FTP_STE_END_TRANSFER:
			ESP_LOGI(TAG, "[E_FTP_STE_END_TRANSFER]");
			if (ftp_data.d_sd >= 0) {
				closesocket(ftp_data.d_sd);
				ftp_data.d_sd = -1;
			}
			break;

		case E_FTP_STE_CONTINUE_LISTING:
			ESP_LOGI(TAG, "[E_FTP_STE_CONTINUE_LISTING]");
			uint32_t listsize = 0;
			ftp_result_t list_res = ftp_list_dir((char *)ftp_data.dBuffer, ftp_buff_size, &listsize);
			if (listsize > 0) ftp_send_list(listsize);
			if (list_res == E_FTP_RESULT_OK) {
				ftp_send_reply(FTP_CODE_CLOSING_DATA_CONN, NULL);
				ftp_data.state = E_FTP_STE_END_TRANSFER;
				ESP_LOGI(TAG, "State Change, %s -> %s", conv_state2str(old_state), conv_state2str(ftp_data.state));
			}
			ftp_data.ctimeout = 0;
			break;

		case E_FTP_STE_CONTINUE_FILE_TX:
			ESP_LOGI(TAG, "[E_FTP_STE_CONTINUE_FILE_TX]");
			// read and send the next block from the file
			{
				uint32_t readsize;
				ftp_result_t result;
				ftp_data.ctimeout = 0;
				result = ftp_read_file ((char *)ftp_data.dBuffer, ftp_buff_size, &readsize);
				if (result == E_FTP_RESULT_FAILED) {
					ftp_send_reply(FTP_CODE_ACTION_LOCAL_ERROR, NULL);
					ftp_data.state = E_FTP_STE_END_TRANSFER;
					ESP_LOGI(TAG, "State Change, %s -> %s", conv_state2str(old_state), conv_state2str(ftp_data.state));
				}
				else {
					if (readsize > 0) {
						ftp_send_file_data(readsize);
						ftp_data.total += readsize;
						ESP_LOGI(TAG, "Sent %"PRIu32", total: %"PRIu32, readsize, ftp_data.total);
					}
					if (result == E_FTP_RESULT_OK) {
						ftp_send_reply(FTP_CODE_CLOSING_DATA_CONN, NULL);
						ftp_data.state = E_FTP_STE_END_TRANSFER;
						ESP_LOGI(TAG, "State Change, %s -> %s", conv_state2str(old_state), conv_state2str(ftp_data.state));
					}
				}
			}
			break;

		case E_FTP_STE_CONTINUE_FILE_RX:
			ESP_LOGI(TAG, "[E_FTP_STE_CONTINUE_FILE_RX]");
			int32_t len;
			ftp_result_t result = E_FTP_RESULT_OK;

			ESP_LOGI(TAG, "ftp_buff_size=%d", ftp_buff_size);
			result = ftp_recv_non_blocking(ftp_data.d_sd, ftp_data.dBuffer, ftp_buff_size, &len);
			if (result == E_FTP_RESULT_OK) {
				// block of data received
				ftp_data.dtimeout = 0;
				ftp_data.ctimeout = 0;
				// save received data to file
				if (E_FTP_RESULT_OK != ftp_write_file ((char *)ftp_data.dBuffer, len)) {
					ftp_send_reply(FTP_CODE_ACTION_LOCAL_ERROR, NULL);
					ftp_data.state = E_FTP_STE_END_TRANSFER;
					ESP_LOGW(TAG, "Error writing to file");
					ESP_LOGI(TAG, "State Change, %s -> %s", conv_state2str(old_state), conv_state2str(ftp_data.state));
				}
				else {
					ftp_data.total += len;
					ESP_LOGI(TAG, "Received %"PRIu32", total: %"PRIu32, len, ftp_data.total);
				}
			}
			else if (result == E_FTP_RESULT_CONTINUE) {
				// nothing received
				if (ftp_data.dtimeout > FTP_DATA_TIMEOUT_MS) {
					ftp_close_files_dir();
					ftp_send_reply(FTP_CODE_TRANSFER_ABORTED, NULL);
					ftp_data.state = E_FTP_STE_END_TRANSFER;
					ESP_LOGW(TAG, "Receiving to file timeout");
					ESP_LOGI(TAG, "State Change, %s -> %s", conv_state2str(old_state), conv_state2str(ftp_data.state));
				}
			}
			else {
				ESP_LOGW(TAG, "ftp_recv_non_blocking failed");
				ftp_close_files_dir();
				ftp_send_reply(FTP_CODE_CLOSING_DATA_CONN, NULL);
				ftp_data.state = E_FTP_STE_END_TRANSFER;
				ESP_LOGI(TAG, "State Change, %s -> %s", conv_state2str(old_state), conv_state2str(ftp_data.state));
				break;
			}
			break;

		default:
			break;
	}

	ftp_substate_t old_sub = ftp_data.substate;
	switch (ftp_data.substate) {
	case E_FTP_STE_SUB_DISCONNECTED:
		ESP_LOGD(TAG, "E_FTP_STE_SUB_DISCONNECTED");
		break;

	case E_FTP_STE_SUB_LISTEN_FOR_DATA:
		if (E_FTP_RESULT_OK == ftp_wait_for_connection(ftp_data.ld_sd, &ftp_data.d_sd, NULL)) {
			ftp_data.dtimeout = 0;
			ftp_data.substate = E_FTP_STE_SUB_DATA_CONNECTED;
			ESP_LOGI(TAG, "SubState Change %s -> %s", conv_substate2str(old_sub), conv_substate2str(ftp_data.substate));
			print_wait = true;
		}
		else if (ftp_data.dtimeout > FTP_DATA_TIMEOUT_MS) {
			ESP_LOGW(TAG, "data_conn_wait_timeout (%"PRIi32" ms)", ftp_data.dtimeout);
			ftp_data.dtimeout = 0;
			closesocket(ftp_data.ld_sd);
			ftp_data.ld_sd = -1;
			ftp_data.substate = E_FTP_STE_SUB_DISCONNECTED;
			ESP_LOGI(TAG, "SubState Change %s -> %s", conv_substate2str(old_sub), conv_substate2str(ftp_data.substate));
			print_wait = true;
		}
		break;

	case E_FTP_STE_SUB_DATA_CONNECTED:
		if (ftp_data.state == E_FTP_STE_READY && (ftp_data.dtimeout > FTP_DATA_TIMEOUT_MS)) {
			ESP_LOGW(TAG, "data_channel_timeout (%"PRIi32" ms)", ftp_data.dtimeout);
			// close the listening and the data socket
			closesocket(ftp_data.ld_sd);
			closesocket(ftp_data.d_sd);
			ftp_data.ld_sd = -1;
			ftp_data.d_sd = -1;
			ftp_close_filesystem ();
			ftp_data.substate = E_FTP_STE_SUB_DISCONNECTED;
			ESP_LOGI(TAG, "SubState Change %s -> %s", conv_substate2str(old_sub), conv_substate2str(ftp_data.substate));
		}
		break;
	default:
		break;
	}

	// check the state of the data sockets
	if (ftp_data.d_sd < 0 && (ftp_data.state > E_FTP_STE_READY)) {
		ftp_data.substate = E_FTP_STE_SUB_DISCONNECTED;
		ftp_data.state = E_FTP_STE_READY;
		ESP_LOGI(TAG, "SubState Change %s -> %s", conv_substate2str(old_sub), conv_substate2str(ftp_data.substate));
		ESP_LOGI(TAG, "State Change %s -> %s", conv_state2str(old_state), conv_state2str(ftp_data.state));
		ESP_LOGI(TAG, "Data socket disconnected");
	}

	//xSemaphoreGive(ftp_mutex);
	return 0;
}

//----------------------
bool ftp_enable (void) {
	bool res = false;
	if (ftp_data.state == E_FTP_STE_DISABLED) {
		ftp_data.enabled = true;
		res = true;
	}
	return res;
}

//-------------------------
bool ftp_isenabled (void) {
	bool res = (ftp_data.enabled == true);
	return res;
}

//-----------------------
bool ftp_disable (void) {
	bool res = false;
	if (ftp_data.state == E_FTP_STE_READY) {
		_ftp_reset();
		ftp_data.enabled = false;
		ftp_data.state = E_FTP_STE_DISABLED;
		res = true;
	}
	return res;
}

//---------------------
bool ftp_reset (void) {
	_ftp_reset();
	return true;
}

// Return current ftp server state
//------------------
int ftp_getstate() {
	int fstate = ftp_data.state | (ftp_data.substate << 8);
	if ((ftp_data.state == E_FTP_STE_READY) && (ftp_data.c_sd > 0)) fstate = E_FTP_STE_CONNECTED;
	return fstate;
}

//-------------------------
bool ftp_terminate (void) {
	bool res = false;
	if (ftp_data.state == E_FTP_STE_READY) {
		ftp_stop = 1;
		_ftp_reset();
		res = true;
	}
	return res;
}

//-------------------------
bool ftp_stop_requested() {
	bool res = (ftp_stop == 1);
	return res;
}

#if 0
//-------------------------------
int32_t ftp_get_maxstack (void) {
	int32_t maxstack = ftp_stack_size - uxTaskGetStackHighWaterMark(NULL);
	return maxstack;
}
#endif

//-------------------------------
void ftp_task (void *pvParameters)
{
	ESP_LOGI(TAG, "ftp_task start");
	//esp_log_level_set(TAG, ESP_LOG_WARN); 
	strcpy(ftp_user, FTP_USER_DEFAULT);
	strcpy(ftp_pass, FTP_PASS_DEFAULT);
	ESP_LOGI(TAG, "ftp_user:[%s] ftp_pass:[%s]", ftp_user, ftp_pass);

	uint64_t elapsed, time_ms = mp_hal_ticks_ms();
	// Initialize ftp, create rx buffer and mutex
	if (!ftp_init()) {
		ESP_LOGE(TAG, "Init Error");
		xEventGroupSetBits(xEventTask, FTP_TASK_FINISH_BIT);
		vTaskDelete(NULL);
	}
	ESP_LOGI(TAG, "ftp_init ok");

	// We have network connection, enable ftp
	if(ftp_enable()) {
		ESP_LOGI(TAG, "ftp_enable ok");
	} else {
		ESP_LOGE(TAG, "ftp_enable Error");
		ftp_deinit();
		xEventGroupSetBits(xEventTask, FTP_TASK_FINISH_BIT);
		vTaskDelete(NULL);
	}

	time_ms = mp_hal_ticks_ms();

	while (1) {
		// Calculate time between two ftp_run() calls
		elapsed = mp_hal_ticks_ms() - time_ms;
		time_ms = mp_hal_ticks_ms();

		int res = ftp_run(elapsed);
		if (res < 0) {
			if (res == -1) {
				ESP_LOGE(TAG, "\nRun Error");
			}
			// -2 is returned if Ftp stop was requested by user
			break;
		}

		vTaskDelay(pdMS_TO_TICKS(10));

	} // end while

	ESP_LOGW(TAG, "\nTask terminated!");
	xEventGroupSetBits(xEventTask, FTP_TASK_FINISH_BIT);
	vTaskDelete(NULL);
}
