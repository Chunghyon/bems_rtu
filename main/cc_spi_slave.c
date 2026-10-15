#ifdef SPI_SLAVE_ROLE
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

#include "driver/spi_slave.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "hh_spi.h"

#include "esp_vfs.h"
#include "hh_littlefs.h"
#include "ftp.h"

#include <stdint.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

static const char *TAG = "SPI_SLAVE";

extern void send_slm_packet_toPC(slm_opcode_t opcode, bool isLearn);

psram_data_t g_psram_face[MAX_FACE_FILES_COUNT] = {0};
SemaphoreHandle_t g_face_mutex = NULL; // 뮤텍스 정의
static int g_face_index = 0; // 현재 저장할 슬롯 인덱스

int g_current_max_face = FACE_COUNT_NORMAL;

extern bool LEARNING_MODE;
extern bool under_face_learning;
extern SemaphoreHandle_t g_voice_mutex;
extern psram_data_t g_psram_voice[MAX_VOICE_FILES_COUNT];
#ifdef PP_VOICE_FACE_PSRAM_RELEASE
extern void release_voice_memory(void);
#endif
#ifdef PP_ONLY_FOR_DEMO
extern void send_slm_packet_toDataServer(slm_opcode_t opcode);
#endif

static volatile spi_magic_t g_spi_magic_request = SPI_MAGIC_NONE; // 마스터로 보낼 요청 상태

// [내부 함수] g_psram_face 및 인덱스 초기화
void reset_face_storage(int new_max_count)
{
    if (g_face_mutex != NULL && xSemaphoreTake(g_face_mutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
        // 1. 모든 슬롯 메모리 해제
        for (int i = 0; i < MAX_FACE_FILES_COUNT; i++) {
            if (g_psram_face[i].data != NULL) {
                free(g_psram_face[i].data);
                g_psram_face[i].data = NULL;
            }
            g_psram_face[i].len = 0;
            g_psram_face[i].is_valid = false;
        }
        
        // 2. 인덱스 및 최대 개수 초기화
        g_face_index = 0;
        g_current_max_face = new_max_count;
        
        ESP_LOGI(TAG, "Face Storage Reset. Max Cont: %d", g_current_max_face);
        xSemaphoreGive(g_face_mutex);
    } else {
        ESP_LOGE(TAG, "Failed to take mutex for reset_face_storage");
    }
}

void request_spi_magic_to_master(spi_magic_t magic)
{
    switch(magic) {
        case SPI_MODE_LEARN:
        case SPI_MODE_NORMAL:
        case SPI_LEARN_START:
        case SPI_LEARN_STOP:
        case SPI_VOICE_RUNNING:
        case SPI_VOICE_STOPPED:
        {
            g_spi_magic_request = magic;
            ESP_LOGI(TAG, "valid magic request to master : %s", str_magic(magic));

            if (magic == SPI_MODE_LEARN || magic == SPI_LEARN_START) {
                // 학습 모드: FACE_COUNT_LEARNING개의 슬롯 사용
                if (LEARNING_MODE) { // 외부 변수 확인 (옵션)
                    reset_face_storage(FACE_COUNT_LEARNING);
                }
            } 
            else if (magic == SPI_MODE_NORMAL) {
                // 일반 모드: 10개 슬롯 사용
                reset_face_storage(FACE_COUNT_NORMAL);
            }
            break;
        }
        default:
        {
            g_spi_magic_request = SPI_MAGIC_NONE;
            ESP_LOGI(TAG, "invalid magic request to master : %s", str_magic(magic));
            break;
        }
    }
}

// --- 파일 저장 함수 ---
void save_chunk_to_file(FILE *f, uint8_t *buf, size_t len) {
    if (f == NULL) return;
    fwrite(buf, 1, len, f);
}

// --- SPI 콜백 ---
// 전송 완료 후 호출됨 (Handshake Low)
static void my_post_trans_cb(spi_slave_transaction_t *trans) {
    gpio_set_level((gpio_num_t)GPIO_HANDSHAKE, 0);
}

// 전송 준비 완료 후 호출됨 (Handshake High)
static void my_post_setup_cb(spi_slave_transaction_t *trans) {
    gpio_set_level((gpio_num_t)GPIO_HANDSHAKE, 1);
}

// --- 메인 태스크 ---
static void spi_slave_task(void *arg)
{
    esp_err_t ret;
    spi_slave_transaction_t t;
    
    // 수신 버퍼 (DMA 가능 메모리 - 내부 SRAM)
    face_header_t *recv_header = (face_header_t*)heap_caps_malloc(sizeof(face_header_t), MALLOC_CAP_DMA);
    uint8_t *chunk_buf = (uint8_t*)heap_caps_malloc(SPI_DMA_CHUNK_SIZE, MALLOC_CAP_DMA);

    face_header_t *send_header = (face_header_t*)heap_caps_malloc(sizeof(face_header_t), MALLOC_CAP_DMA);

    if (!recv_header || !chunk_buf || !send_header) {
        ESP_LOGE(TAG, "DMA Memory allocation failed");
        return;
    }

    ESP_LOGI(TAG, "SPI Slave Ready (DMA Mode)");

    while (1) {
        // ---------------------------------------------------------
        // 0. 송신 데이터(요청) 준비 (매직 설정)
        // ---------------------------------------------------------
        memset(send_header, 0, sizeof(face_header_t));
        send_header->magic = g_spi_magic_request; // 요청이 없으면 NONE(0) 전송
        send_header->total_len = 0;

        // ---------------------------------------------------------
        // 1. 헤더 송수신 대기 (Magic 교환)
        // ---------------------------------------------------------
        memset(&t, 0, sizeof(t));
        t.length = sizeof(face_header_t) * 8;
        t.rx_buffer = recv_header;
        t.tx_buffer = send_header;

        ret = spi_slave_transmit(SPI3_HOST, &t, portMAX_DELAY);
        if (ret != ESP_OK) continue;

        // ---------------------------------------------------------
        // 2. 결과 처리
        // ---------------------------------------------------------
        
        if (recv_header->magic == SPI_MAGIC_BOOTING) {
            spi_magic_t target_magic = LEARNING_MODE ? SPI_MODE_LEARN : SPI_MODE_NORMAL;
            
            // 이미 해당 요청을 보내고 있는 중이 아니라면 (중복 리셋 방지)
            if (g_spi_magic_request != target_magic) {
                ESP_LOGW(TAG, "Received SPI_MAGIC_BOOTING. Syncing Mode to %s", str_magic(target_magic));
                
                // 기존 진행 중인 요청 상태 클리어 (우선순위 0 처리를 위해)
                g_spi_magic_request = SPI_MAGIC_NONE; 
                
                // 초기화 및 요청 설정 (메모리 리셋 포함)
                request_spi_magic_to_master(target_magic);
            }
            continue; // 이번 트랜잭션은 Init 처리로 종료
        }
        else if (recv_header->magic == SPI_IMAGE_NORMAL) {
            ESP_LOGI(TAG, "Received %s from Master.", str_magic(recv_header->magic));
#ifdef PP_VOICE_FACE_PSRAM_RELEASE            
            release_voice_memory();
#endif
            // 일반 모드 개수(10개)로 리셋 및 메모리 정리
            reset_face_storage(FACE_COUNT_NORMAL);
            continue;
        }
        else if (recv_header->magic == SPI_IMAGE_LEARN) {
            ESP_LOGW(TAG, "Received %s from Master.", str_magic(recv_header->magic));
#ifdef PP_VOICE_FACE_PSRAM_RELEASE            
            release_voice_memory();
#endif
            // 학습 모드 개수(20개)로 리셋 및 메모리 정리
            reset_face_storage(FACE_COUNT_LEARNING);
            continue;
        }

        // [우선순위 1] 내가 요청 보낸 상태인 경우 (ACK 대기 중)
        if (g_spi_magic_request != SPI_MAGIC_NONE) {
            
            // 1-1. ACK 수신 확인 (요청값 + 1 규칙)
            if (recv_header->magic == g_spi_magic_request + 1) {
                ESP_LOGI(TAG, "ACK Received: %s. Request Cleared.", str_magic(recv_header->magic));
                g_spi_magic_request = SPI_MAGIC_NONE; // 요청 완료 -> 상태 해제
            } 
            // 1-2. 아직 ACK가 아님 -> 계속 요청 보냄 (Loop)
            else {
                // 노이즈(0)가 아닐 때만 로그 출력
                if (recv_header->magic != 0) {
                     ESP_LOGI(TAG, "Waiting for ACK... Req:%s, Recv:%s", 
                              str_magic(g_spi_magic_request), str_magic(recv_header->magic));
                }
            }
            // 요청 중에는 마스터가 보내는 이미지(FACE) 등을 모두 무시함 (정책)
            continue;
        }

        // [우선순위 2] 요청 상태가 아닐 때, 이미지 헤더(FACE) 수신 확인
        if (recv_header->magic == SPI_MAGIC_FACE) {
             // ---> (아래 3번 로직으로 진행)
        }
        // [우선순위 3] 그 외 잡음/ACK 패킷 무시
        else {
            if(recv_header->magic != SPI_MODE_LEARN_ACK && recv_header->magic != SPI_MODE_NORMAL_ACK
                && recv_header->magic != SPI_LEARN_START_ACK && recv_header->magic != SPI_LEARN_STOP_ACK 
                && recv_header->magic != 0) { 
                
                ESP_LOGW(TAG, "Invalid/Unexpected Magic: 0x%08lX (%s)", recv_header->magic, str_magic(recv_header->magic));
            }
            continue; 
        }

        // ---------------------------------------------------------
        // 3. (SPI_MAGIC_FACE 수신 시) 이미지 데이터 수신 로직 시작
        // ---------------------------------------------------------
        ESP_LOGI(TAG, "Header Received. w %u, h %u (%lu bytes)", 
            recv_header->width, recv_header->height, recv_header->total_len);
        
        // ---------------------------------------------------------
        // PSRAM 할당 준비
        // ---------------------------------------------------------
        uint32_t total_len = recv_header->total_len;
        uint8_t *psram_ptr = NULL;  // 현재 이미지를 위한 PSRAM 포인터

        // 임시 버퍼에 새 메모리 할당 (PSRAM)
        psram_ptr = (uint8_t*)heap_caps_malloc(total_len, MALLOC_CAP_SPIRAM);
        
        if (psram_ptr == NULL) {
            size_t free_spiram = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
            ESP_LOGI(TAG, "Attempting alloc %lu bytes. Free SPIRAM: %u bytes", total_len, free_spiram);
            // 메모리 부족 시, 데이터를 받긴 받아야 하므로(Drop) psram_ptr는 NULL 유지
        } else {
            ESP_LOGI(TAG, "Allocated PSRAM buffer at slot %d", g_face_index);
        }

        // ---------------------------------------------------------
        // 4. 이미지 데이터 수신 및 메모리 복사 (Chunk 단위)
        // ---------------------------------------------------------
        uint32_t received = 0;
        
        TickType_t start_tick = xTaskGetTickCount();
        const TickType_t timeout_ticks = pdMS_TO_TICKS(10000); // 10초 타임아웃

        while (received < total_len) {
            // 타임아웃 체크
            if ((xTaskGetTickCount() - start_tick) > timeout_ticks) {
                ESP_LOGE(TAG, "Timeout receiving image. %lu/%lu", received, total_len);
                break;
            }

            uint32_t remaining = total_len - received;
            uint32_t chunk_size = MIN(remaining, SPI_DMA_CHUNK_SIZE);

            memset(&t, 0, sizeof(t));
            t.length = chunk_size * 8;
            t.rx_buffer = chunk_buf; // DMA 버퍼로 받음
            t.tx_buffer = send_header;
            
            // 타임아웃 1초 (Master 지연 대비)
            ret = spi_slave_transmit(SPI3_HOST, &t, pdMS_TO_TICKS(1000)); 
            
            if (ret == ESP_OK) {
                // PSRAM에 복사 (할당 성공했을 때만)
                if (psram_ptr != NULL) {
                    memcpy(psram_ptr + received, chunk_buf, chunk_size);
                }
                received += chunk_size;
                // 데이터 수신 시 타임아웃 갱신 (선택)
                start_tick = xTaskGetTickCount(); 
            } else if (ret == ESP_ERR_TIMEOUT) {
                continue; 
            } else {
                ESP_LOGE(TAG, "Chunk receive failed");
                break;
            }
        }

        // ---------------------------------------------------------
        // 5. 마무리 및 등록 (뮤텍스 보호 추가)
        // ---------------------------------------------------------
        if (received == total_len && psram_ptr != NULL) {
            
            if (xSemaphoreTake(g_face_mutex, portMAX_DELAY) == pdTRUE) {
                
                // 기존 데이터 정리
                if (g_psram_face[g_face_index].data != NULL) {
                    free(g_psram_face[g_face_index].data);
                    g_psram_face[g_face_index].data = NULL; // 안전장치
                }

                // 새 데이터 등록
                g_psram_face[g_face_index].data = psram_ptr;
                g_psram_face[g_face_index].len = total_len;
                g_psram_face[g_face_index].is_valid = true;
                
                xSemaphoreGive(g_face_mutex); // 잠금 해제
            }
            
            ESP_LOGI(TAG, "Image stored in PSRAM slot %d", g_face_index);
            if (g_face_index == g_current_max_face - 1) {
                ESP_LOGI(TAG, "Face Buffer Full (%d), sending SLM_FACE_READY", g_current_max_face);
                send_slm_packet_toPC(SLM_FACE_READY, LEARNING_MODE);
                if(LEARNING_MODE) {
                    request_spi_magic_to_master(SPI_LEARN_STOP);
                }
#ifdef PP_ONLY_FOR_DEMO
                else {
                    send_slm_packet_toDataServer(SLM_FACE_READY);
                }
#endif
                under_face_learning = false;
            }

            g_face_index = (g_face_index + 1) % g_current_max_face;

        } else {
            // 수신 실패 또는 할당 실패: 메모리 해제
            if (psram_ptr != NULL) {
                free(psram_ptr);
            }
            ESP_LOGE(TAG, "Incomplete transfer or alloc fail. Dropped.");
        }
    }
}

// --- 초기화 함수 ---
void init_spi_slave(void)
{
    // [추가] 뮤텍스 생성
    g_face_mutex = xSemaphoreCreateMutex();

    // 1. SPI 버스 설정
    spi_bus_config_t buscfg = {
        .mosi_io_num = SPI_MOSI1,
        .miso_io_num = SPI_MISO1,
        .sclk_io_num = SPI_CLK1,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = SPI_DMA_CHUNK_SIZE + 1024,
    };

    // 2. 슬레이브 인터페이스 설정
    spi_slave_interface_config_t slvcfg = {
        .mode = 0,
        .spics_io_num = SPI_CS1,
        .queue_size = 3,
        .flags = 0,
        .post_setup_cb = my_post_setup_cb, // Handshake High
        .post_trans_cb = my_post_trans_cb  // Handshake Low
    };

    // 3. Handshake 핀 설정
    gpio_config_t io_conf = {
        .intr_type = GPIO_INTR_DISABLE,
        .mode = GPIO_MODE_OUTPUT,
        .pin_bit_mask = (1ULL << GPIO_HANDSHAKE),
    };
    gpio_config(&io_conf);
    gpio_set_level((gpio_num_t)GPIO_HANDSHAKE, 0); // 초기 상태 Low

    // 4. 초기화
    // SPI2_HOST 또는 SPI3_HOST 사용 (Master와 핀 연결 주의)
    ESP_ERROR_CHECK(spi_slave_initialize(SPI3_HOST, &buscfg, &slvcfg, SPI_DMA_CH_AUTO));

    // 5. 태스크 생성
    xTaskCreate(spi_slave_task, "spi_slave_task", 4096, NULL, 5, NULL);
}
#endif