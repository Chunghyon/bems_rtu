/*
 * This file is part of the MicroPython ESP32 project, https://github.com/loboris/MicroPython_ESP32_psRAM_LoBo
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2018 LoBo (https://github.com/loboris)
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */
/*
 * This file is based on 'ftp' from Pycom Limited.
 *
 * Author: LoBo, loboris@gmail.com
 * Copyright (c) 2017, LoBo
 */

#ifndef FTP_H_
#define FTP_H_

#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

#define PP_PARTITION_LABEL 	"storage"
#define PP_MOUNT_POINT 		"/littlefs"

#define FTP_USER_PASS_LEN_MAX	32

#define CONFIG_MICROPY_FTPSERVER_BUFFER_SIZE 	1024
#define CONFIG_MICROPY_FTPSERVER_TIMEOUT 		300
#define MICROPY_ALLOC_PATH_MAX 					512

#define FTP_SERVER_TIMEOUT_MS      (CONFIG_MICROPY_FTPSERVER_TIMEOUT*1000)

/******************************************************************************
 DEFINE PRIVATE CONSTANTS
 ******************************************************************************/
#define FTP_CMD_PORT                        21
#define FTP_ACTIVE_DATA_PORT                20
#define FTP_PASIVE_DATA_PORT                2024

#define FTP_CMD_SIZE_MAX                    6
#define FTP_CMD_CLIENTS_MAX                 1
#define FTP_DATA_CLIENTS_MAX                1
#define FTP_MAX_PARAM_SIZE                  (MICROPY_ALLOC_PATH_MAX + 1)
#define FTP_UNIX_SECONDS_180_DAYS           15552000
#define FTP_DATA_TIMEOUT_MS                 10000   // 10 seconds
#define FTP_SOCKETFIFO_ELEMENTS_MAX         4

#define FTP_TIMEOUT_DEFAULT_MS              200

#define FTP_CMD_FEAT   "FEAT"   // 서버의 지원 기능(Features) 조회
#define FTP_CMD_SYST   "SYST"   // 서버 시스템 타입 조회
#define FTP_CMD_CDUP   "CDUP"   // 상위(부모) 디렉터리로 이동
#define FTP_CMD_CWD    "CWD"    // 작업 디렉터리 변경
#define FTP_CMD_PWD    "PWD"    // 현재 작업 디렉터리 경로 반환
#define FTP_CMD_XPWD   "XPWD"   // PWD와 동일, 일부 클라이언트 호환용
#define FTP_CMD_SIZE   "SIZE"   // 파일 크기(바이트) 조회
#define FTP_CMD_MDTM   "MDTM"   // 파일의 최종 수정 시간 조회
#define FTP_CMD_TYPE   "TYPE"   // 데이터 전송 타입 지정(ASCII/Binary)
#define FTP_CMD_USER   "USER"   // 사용자 이름 입력
#define FTP_CMD_PASS   "PASS"   // 비밀번호 입력
#define FTP_CMD_PASV   "PASV"   // 패시브 모드로 데이터 연결 요청
#define FTP_CMD_LIST   "LIST"   // 디렉터리/파일 목록 상세 정보 반환
#define FTP_CMD_RETR   "RETR"   // 파일 다운로드(서버→클라이언트)
#define FTP_CMD_STOR   "STOR"   // 파일 업로드(클라이언트→서버, 새로 저장)
#define FTP_CMD_DELE   "DELE"   // 파일 삭제
#define FTP_CMD_RMD    "RMD"    // 디렉터리 삭제
#define FTP_CMD_MKD    "MKD"    // 새 디렉터리 생성
#define FTP_CMD_RNFR   "RNFR"   // 파일/디렉터리 이름 변경(변경 전 이름)
#define FTP_CMD_RNTO   "RNTO"   // 파일/디렉터리 이름 변경(변경 후 이름)
#define FTP_CMD_NOOP   "NOOP"   // 아무 동작 없음(연결 유지용)
#define FTP_CMD_QUIT   "QUIT"   // 세션 종료 및 연결 해제
#define FTP_CMD_APPE   "APPE"   // 파일에 데이터 추가(append)
#define FTP_CMD_NLST   "NLST"   // 디렉터리/파일 이름만 간단히 반환
#define FTP_CMD_AUTH   "AUTH"   // 인증 방식 지정(FTPS 등)

typedef enum {
	E_FTP_CMD_NOT_SUPPORTED = -1,
	E_FTP_CMD_FEAT = 0,
	E_FTP_CMD_SYST,
	E_FTP_CMD_CDUP,
	E_FTP_CMD_CWD,
	E_FTP_CMD_PWD,
	E_FTP_CMD_XPWD,
	E_FTP_CMD_SIZE,
	E_FTP_CMD_MDTM,
	E_FTP_CMD_TYPE,
	E_FTP_CMD_USER,
	E_FTP_CMD_PASS,
	E_FTP_CMD_PASV,
	E_FTP_CMD_LIST,
	E_FTP_CMD_RETR,
	E_FTP_CMD_STOR,
	E_FTP_CMD_DELE,
	E_FTP_CMD_RMD,
	E_FTP_CMD_MKD,
	E_FTP_CMD_RNFR,
	E_FTP_CMD_RNTO,
	E_FTP_CMD_NOOP,
	E_FTP_CMD_QUIT,
	E_FTP_CMD_APPE,
	E_FTP_CMD_NLST,
	E_FTP_CMD_AUTH,
	E_FTP_NUM_FTP_CMDS
} ftp_cmd_index_t;

typedef enum {
	/**** Used ****/
	FTP_CODE_ABOUT_TO_OPEN_DATA  = 150,

	FTP_CODE_COMMAND_OK          = 200,
	FTP_CODE_FILE_STATUS         = 213,
	FTP_CODE_SYSTEM_TYPE         = 215,
	FTP_CODE_SERVICE_READY       = 220,
	FTP_CODE_SERVICE_CLOSING     = 221,
	FTP_CODE_CLOSING_DATA_CONN   = 226,
	FTP_CODE_ENTERING_PASSIVE    = 227,
	FTP_CODE_USER_LOGGED_IN      = 230,
	FTP_CODE_FILE_ACTION_OK      = 250,
	FTP_CODE_PATHNAME_CREATED    = 257,

	FTP_CODE_USER_OK_NEED_PASS   = 331,
	FTP_CODE_NEED_ACCOUNT        = 332,
	FTP_CODE_FILE_ACTION_PENDING = 350,

	FTP_CODE_CANT_OPEN_DATA      = 425,
	FTP_CODE_TRANSFER_ABORTED 	 = 426,
	FTP_CODE_ACTION_LOCAL_ERROR  = 451,

	FTP_CODE_CMD_NOT_IMPL        = 502,
	FTP_CODE_CMD_NOT_IMPL_PARAM  = 504,
	FTP_CODE_NOT_LOGGED_IN       = 530,
	FTP_CODE_FILE_UNAVAILABLE    = 550,

	/**** Not Used ****/
	FTP_CODE_HELP_MESSAGE        = 214,
	FTP_CODE_DATA_CONN_OPEN_NO_XFER = 225,
	FTP_CODE_SERVICE_NOT_AVAIL   = 421,
	FTP_CODE_FILE_ACTION_NOT_TAKEN = 450,
	FTP_CODE_ACTION_NOT_TAKEN_NO_SPACE = 452,
	FTP_CODE_SYNTAX_ERROR        = 500,
	FTP_CODE_SYNTAX_ERROR_PARAM  = 501,
	FTP_CODE_BAD_CMD_SEQUENCE    = 503,
	FTP_CODE_NEED_ACCOUNT_STORE  = 532,
	FTP_CODE_PAGE_TYPE_UNKNOWN   = 551,
	FTP_CODE_EXCEEDED_STORAGE    = 552,
	FTP_CODE_FILENAME_NOT_ALLOWED= 553
} ftp_reply_code_t;

typedef enum {
	E_FTP_STE_DISABLED = 0,
	E_FTP_STE_START,
	E_FTP_STE_READY,
	E_FTP_STE_END_TRANSFER,
	E_FTP_STE_CONTINUE_LISTING,
	E_FTP_STE_CONTINUE_FILE_TX,
	E_FTP_STE_CONTINUE_FILE_RX,
	E_FTP_STE_CONNECTED
} ftp_state_t;

#define conv_state2str(x) ( \
	(x) == E_FTP_STE_DISABLED ? "DISABLED" : \
	(x) == E_FTP_STE_START ? "START" : \
	(x) == E_FTP_STE_READY ? "READY" : \
	(x) == E_FTP_STE_END_TRANSFER ? "END_TRANSFER" : \
	(x) == E_FTP_STE_CONTINUE_LISTING ? "CONTINUE_LISTING" : \
	(x) == E_FTP_STE_CONTINUE_FILE_TX ? "CONTINUE_FILE_TX" : \
	(x) == E_FTP_STE_CONTINUE_FILE_RX ? "CONTINUE_FILE_RX" : \
	(x) == E_FTP_STE_CONNECTED ? "CONNECTED" : NULL)

typedef enum {
	E_FTP_STE_SUB_DISCONNECTED = 0,
	E_FTP_STE_SUB_LISTEN_FOR_DATA,
	E_FTP_STE_SUB_DATA_CONNECTED
} ftp_substate_t;
#define conv_substate2str(x) ( \
	(x) == E_FTP_STE_SUB_DISCONNECTED ? "DISCONNECTED" : \
	(x) == E_FTP_STE_SUB_LISTEN_FOR_DATA ? "LISTEN_FOR_DATA" : \
	(x) == E_FTP_STE_SUB_DATA_CONNECTED ? "DATA_CONNECTED" : NULL)


/******************************************************************************
 DEFINE PRIVATE TYPES
 ******************************************************************************/
typedef enum {
	E_FTP_RESULT_OK = 0,
	E_FTP_RESULT_CONTINUE,
	E_FTP_RESULT_FAILED
} ftp_result_t;

typedef struct {
	bool            uservalid : 1;
	bool            passvalid : 1;
} ftp_loggin_t;

typedef enum {
	E_FTP_NOTHING_OPEN = 0,
	E_FTP_FILE_OPEN,
	E_FTP_DIR_OPEN
} ftp_e_open_t;

#define conv_open2str(x) ( \
	(x) == E_FTP_NOTHING_OPEN ? "NOTHING_OPEN" : \
	(x) == E_FTP_FILE_OPEN ? "FILE_OPEN" : \
	(x) == E_FTP_DIR_OPEN ? "DIR_OPEN" : NULL)

typedef enum {
	E_FTP_CLOSE_NONE = 0,
	E_FTP_CLOSE_DATA,
	E_FTP_CLOSE_CMD_AND_DATA,
} ftp_e_closesocket_t;

typedef struct {
    uint8_t   *dBuffer;        // 데이터 송수신에 사용하는 버퍼 포인터
    uint32_t   ctimeout;       // 명령 채널 타임아웃(밀리초)
    union {
        DIR  *dp;              // 디렉터리 핸들(디렉터리 열기용, LIST/NLST 등)
        FILE *fp;              // 파일 핸들(파일 전송용, RETR/STOR 등)
    };
    int32_t    lc_sd;          // 리스닝 커맨드 소켓 디스크립터(명령 채널)
    int32_t    ld_sd;          // 리스닝 데이터 소켓 디스크립터(패시브 모드 데이터 채널)
    int32_t    c_sd;           // 커맨드 소켓 디스크립터(클라이언트와 연결된 명령 채널)
    int32_t    d_sd;           // 데이터 소켓 디스크립터(클라이언트와 연결된 데이터 채널)
    int32_t    dtimeout;       // 데이터 채널 타임아웃(밀리초)
    uint32_t   ip_addr;        // 클라이언트 IP 주소
    uint8_t    state;          // FTP 서버 상태(메인 상태머신)
    uint8_t    substate;       // FTP 서버 서브 상태(서브 상태머신)
    uint8_t    txRetries;      // 데이터 전송 재시도 횟수
    uint8_t    logginRetries;  // 로그인 재시도 횟수
    ftp_loggin_t loggin;       // 로그인 상태(사용자/비밀번호 유효성)
    uint8_t    e_open;         // 열려 있는 파일/디렉터리 타입(없음/파일/디렉터리)
    bool       closechild;     // 명령 처리 후 하위 리소스(파일/디렉터리) 닫기 여부
    bool       enabled;        // FTP 서비스 활성화 여부
    bool       listroot;       // 루트 디렉터리 목록 표시 여부
    uint32_t   total;          // 전송한 데이터 총량(바이트)
    uint32_t   time;           // 최근 작업 시간(타임스탬프 등)
} ftp_data_t;

typedef struct {
	char * cmd;
} ftp_cmd_t;


// #define MAX_ACTIVE_INTERFACES   3
//tcpip_adapter_if_t tcpip_if[MAX_ACTIVE_INTERFACES] = {TCPIP_ADAPTER_IF_MAX};

#define VFS_NATIVE_MOUNT_POINT          "/_#!#_spiffs"
#define VFS_NATIVE_SDCARD_MOUNT_POINT   "/_#!#_sdcard"
#define VFS_NATIVE_INTERNAL_PART_LABEL  "internalfs"
#define VFS_NATIVE_INTERNAL_MP          "/flash"
#define VFS_NATIVE_EXTERNAL_MP          "/sd"
#define VFS_NATIVE_TYPE_SPIFLASH        0
#define VFS_NATIVE_TYPE_SDCARD          1

#ifndef MIN
#define MIN(x, y) ((x) < (y) ? (x) : (y))
#endif
#ifndef MAX
#define MAX(x, y) ((x) > (y) ? (x) : (y))
#endif

#define FTP_USER_DEFAULT 	"pi"
#define FTP_PASS_DEFAULT 	"ilritech"

#define PP_VIRTUAL_FOLDER_FACE   "/iLRiTek/dataset/Face"
#define PP_VIRTUAL_FOLDER_VOICE  "/iLRiTek/dataset/Voice"

#define MAX_FACE_FILES_COUNT 	20	// for face learning storage
#define FACE_COUNT_LEARNING  	20
#define FACE_COUNT_NORMAL 		2

#define MAX_VOICE_FILES_COUNT   15	// for voice learning storage
#define RECORD_DURATION_SEC 	5
// 16000Hz * 16bit(2byte) * 1ch = 32000 bytes/sec
#define VOICE_FILE_SIZE_LIMIT	(16000 * 2 * RECORD_DURATION_SEC) 

// [추가] 디지털 볼륨 증폭 배수 (10배 ~ 20배 정도 조절 가능)
#define PLAYBACK_DIGITAL_GAIN  	1

enum _rec_msg_id {
	REC_START = 1,
	REC_STOP,
	REC_CANCEL,
};

typedef struct {
    uint8_t *data;      // 데이터 시작 포인터
    uint32_t len;       // 현재 저장된 길이
    bool is_valid;      // 유효한 파일인지 여부
} psram_data_t;

bool ftp_init (void);
void ftp_deinit (void);
int ftp_run (uint32_t elapsed);
bool ftp_enable (void);
bool ftp_isenabled (void);
bool ftp_disable (void);
bool ftp_reset (void);
int ftp_getstate();
bool ftp_terminate (void);
bool ftp_stop_requested();
int32_t ftp_get_maxstack (void);

void ftp_task (void *pvParameters);

#endif /* FTP_H_ */
