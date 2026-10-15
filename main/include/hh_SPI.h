#ifndef HH_SPI_H__
#define HH_SPI_H__

#include "stdint.h"
#include "freertos/FreeRTOS.h"
#include "HH_parameters.h"

#define SPI_BUFFER_LENGTH	(32)

#define SENSITIVITY_MAX			(700)		//the bigger, the more sensitive
#define SENSITIVITY_MIN			(400)
#define DEFAULT_STEP_COUNT		(30)		// step count for PWM-Rising/Falling

#define SELF_ID_NOT_ASSIGNED	(0xFFFF)
#define CORNER_ID_NOT_ASSIGNED	(0xFF)

#define TO_ALL_SERVERS			(0xFF)
#define TO_ALL_SENSORS			(0xFF)
#define NO_CHANNEL				(0xFF)

#define spp_sprintf(s,...)         sprintf((char*)(s), ##__VA_ARGS__)
#define SPP_DATA_MAX_LEN           (512)
#define SPP_CMD_MAX_LEN            (20)
#define SPP_STATUS_MAX_LEN         (20)
#define SPP_DATA_BUFF_MAX_LEN      (2*1024)

enum{
    SPP_IDX_SVC,

    SPP_IDX_SPP_DATA_RECV_CHAR,
    SPP_IDX_SPP_DATA_RECV_VAL,

    SPP_IDX_SPP_DATA_NOTIFY_CHAR,
    SPP_IDX_SPP_DATA_NTY_VAL,
    SPP_IDX_SPP_DATA_NTF_CFG,
#if false
    SPP_IDX_SPP_COMMAND_CHAR,
    SPP_IDX_SPP_COMMAND_VAL,

    SPP_IDX_SPP_STATUS_CHAR,
    SPP_IDX_SPP_STATUS_VAL,
    SPP_IDX_SPP_STATUS_CFG,
#endif
    SPP_IDX_NB,
};

typedef enum __attribute((packed))
{
	/** to/from EMC Caller **/
	GET_CALLER_ID	= 0x00,
	SET_CALLER_ID,
	RPT_CALLER_ID,
	ACK_CALLER_ID,

	/** to/from Stand-alone LobbyMaster **/
	GET_SALM_ID		= 0x10,
	SET_SALM_ID,
	RPT_SALM_ID,
	ACK_SALM_ID,

	/** to/from GW **/
	SET_GW_ID		= 0x20,
	RST_GW_ID,
	SET_GW_ID_ACK	= 0x40,
	RST_GW_ID_ACK,

	/* PP_IP_BY_PROVISION_20220713 */
	GET_ETH_IP		= 0x80,
	SET_ETH_IP,
	RPT_ETH_IP,
	ACK_ETH_IP,
} ble_cmd_t;

typedef struct
{
    uint16_t provisioned_devices;
    uint16_t configured_devices;
    uint16_t last_device_address;
    uint16_t next_device_address;
} network_stats_data_t;

typedef struct __attribute((packed))
{
	uint8_t id_corner1;			//Corner ID1
	uint8_t id_corner2;			//Corner ID2
	uint8_t id_corner3;			//Corner ID3
	uint16_t id_self;			//light ID
} cowork_id_t;


typedef struct __attribute((packed))
{
    uint8_t onoff		  : 1;	//light on/off status
	uint8_t detect 		  :	1;	//Sensor detect status

	uint8_t en_sense	  : 1;	//sensor on/off, 1/0
	uint8_t en_relay	  : 1;	//relaying on/off, 1/0
	uint8_t co_range	  : 4;	//co-working light count, 0~7						//Byte#1

	uint8_t keeping_time  : 4;	//on-duration in secs, 3+(value*3) secs
	uint8_t rising_time	  : 2;	//ramping-up time in secs, (value*0.5) secs
	uint8_t falling_time  : 2;	//ramping down time in secs, (value*0.5) secs		//Byte#2

	uint8_t pwm_frequency;		//PWM frequency, 1~50kHz							//Byte#3
	uint8_t bright_max;			//ramping-up-to value, 0~100						//Byte#4
	uint8_t bright_min;			//ramping-dn-to value, 0~100						//Byte#5
	uint8_t bright_man;			//manual-on brightness, 0~100
	uint8_t sensitivity;		//sensor sensitivity, 0~100							//Byte#6

	cowork_id_t	id;																		//Byte#7~

} cc_server_config_t;

typedef enum __attribute((packed))
{
	//length == 1
	LIGHT_ONOFF,
	SENSOR_DETECT,

	CONFIG_EN_SENSE,
	CONFIG_EN_RELAY,
	CONFIG_CO_RANGE,		//Byte#1

	CONFIG_KEEPING_TIME,
	CONFIG_RISING_TIME,
	CONFIG_FALLING_TIME,	//Byte#2

	CONFIG_PWM_FREQUENCY,	//Byte#3
    CONFIG_BRIGHT_MAX,		//Byte#4
	CONFIG_BRIGHT_MIN,		//Byte#5
	CONFIG_BRIGHT_MAN,		//Byte#6
	CONFIG_SENSITIVITY,		//Byte#7
    CONFIG_ID_CORNER1,
    CONFIG_ID_CORNER2,
    CONFIG_ID_CORNER3,
    CONFIG_ID_SELF,
#ifdef USE_CONFIG_ALL
	CONFIG_ALL,
#endif
	CONFIG_RESET  = 0xfe,
	CONFIG_INVALID = 0xff
} cc_type_t;

typedef cc_server_config_t cc_data_t;

typedef struct __attribute((packed))
{
	cc_type_t		type;
	cc_data_t		data;
} type_data_t;

typedef enum __attribute((packed))
{
	SPI_OPCODE_EMPTY			= 0x00,

//======= ESP to NRF
	SPI_OPCODE_GET_ONOFF,
	SPI_OPCODE_GET_CONFIG,
	SPI_OPCODE_SET_ONOFF,
	SPI_OPCODE_SET_CONFIG,

	SPI_OPCODE_GET_GW,
	SPI_OPCODE_SET_GW,

    SPI_OPCODE_REBOOT_GW,
    SPI_OPCODE_RESET_NODE,

//======= NRF to ESP
	SPI_OPCODE_RPT_ONOFF,
	SPI_OPCODE_RPT_CONFIG,

	SPI_OPCODE_REMOCON_COMMAND,

	SPI_OPCODE_RPT_NRF_READY,
	SPI_OPCODE_RPT_NO_RESPONSE,

	SPI_OPCODE_LAMP_MAX,

//======= Onepass, NRF to ESP
	SPI_OPCODE_PARK_LOC_PUT_REQ,
	SPI_OPCODE_PARK_LOC_GET_REQ,
	SPI_OPCODE_PARK_LOC_RMV_REQ,
	SPI_OPCODE_GATE_ENT_REQ,
	SPI_OPCODE_GATE_STATUS_RPT,
	SPI_OPCODE_GATE_STATUS_GET_ACK,
	SPI_OPCODE_GATE_PARAM_SET_ACK,
	SPI_OPCODE_GATE_PARAM_GET_ACK,

	SPI_OPCODE_ADV_RSSI_TRANSFER,
	SPI_OPCODE_SMARTPHONE_EMC,
	SPI_OPCODE_SITE_CODE_REQ,

//======= Onepass, ESP to NRF
	SPI_OPCODE_PARK_LOC_PUT_ACK,
	SPI_OPCODE_PARK_LOC_GET_ACK,
	SPI_OPCODE_PARK_LOC_RMV_ACK,
	SPI_OPCODE_GATE_ENT_ACK,
	SPI_OPCODE_GATE_STATUS_RPT_RCVD,
	SPI_OPCODE_GATE_STATUS_GET_REQ,
	SPI_OPCODE_GATE_PARAM_SET_REQ,
	SPI_OPCODE_GATE_PARAM_GET_REQ,

	SPI_OPCODE_SITE_CODE_ACK,

	SPI_OPCODE_NOTICE_MESSAGE_0,	// message 1/4
	SPI_OPCODE_NOTICE_MESSAGE_1,	// message 2/4
	SPI_OPCODE_NOTICE_MESSAGE_2,	// message 3/4
	SPI_OPCODE_NOTICE_MESSAGE_3,	// message 4/4

	SPI_OPCODE_ONEPASS_MAX,
//======= End of Opcode

#if PP_IP_BY_PROVISION_20220713
	SPI_OPCODE_GET_ETH_IP,
	SPI_OPCODE_SET_ETH_IP,
	SPI_OPCODE_RPT_ETH_IP,
	SPI_OPCODE_ACK_ETH_IP,
#endif

#if PP_MESH_TRAFFIC_CONTROL_220923
	SPI_OPCODE_EMC_TALKING_BEGIN,
	SPI_OPCODE_EMC_TALKING_END,
#endif

	SPI_OPCODE_NO_OPCODE		= 0xff
} spi_opcode_t;

typedef struct __attribute((packed))
{
	uint8_t		ch;
    type_data_t	typedata;
} ch_type_data_t;

typedef enum __attribute((packed))
{
	GW_PARAM_ID,
} gw_param_t;

#if PP_IP_BY_PROVISION_20220713
typedef struct __attribute((packed))
{
	uint8_t loc_ip[5];
	uint8_t loc_gw[4];
	uint8_t rmt_ip[4];
} ip_thru_ble_t;

#endif

#define SPI_DATA_LEN_MAX	(256)
#define SPI_TOTAL_LEN_MAX	(SPI_DATA_LEN_MAX + 1) //opcode(1) + data(256)

typedef struct __attribute((packed))
{
	spi_opcode_t	opcode;
	union __attribute((packed))
	{
		onepass_payload_t	onepass;
		ip_thru_ble_t		ip;
		uint8_t				data[SPI_DATA_LEN_MAX];
	};
} spi_format_t;

typedef struct __attribute((packed))
{
	ble_cmd_t	cmd;
	union __attribute((packed))
	{
		uint8_t			id[3];
		ip_thru_ble_t	ip;
	};
} ble_format_t;

typedef struct
{
	uint32_t timestamp_no_rsp;
	uint32_t timestamp_waiting;
	uint32_t timestamp_waiting_onoff;
	uint32_t timestamp_last;
	bool responded_at_least_once;
	bool status_rechecking;
	bool write_param_needed;
	bool read_param_needed;
	cc_type_t read_type;
	bool toggle;
	bool onoff_recheck;
	bool sense_recheck;
	bool first_en_sense_reported;
} ch_recheck_info_t;

#define SPItoTCP(x) \
		(x==CONFIG_EN_SENSE)	  ? TCP_EN_SENSE :		\
		(x==CONFIG_EN_RELAY)	  ? TCP_EN_RELAY :		\
		(x==CONFIG_ID_CORNER1)    ? TCP_ID_CORNER1 : 	\
		(x==CONFIG_ID_CORNER2)    ? TCP_ID_CORNER2 : 	\
		(x==CONFIG_ID_CORNER3)    ? TCP_ID_CORNER3 : 	\
		(x==CONFIG_CO_RANGE) 	  ? TCP_CO_RANGE :		\
		(x==CONFIG_KEEPING_TIME)  ? TCP_KEEPING_TIME :	\
		(x==CONFIG_RISING_TIME)   ? TCP_RISING_TIME :	\
		(x==CONFIG_FALLING_TIME)  ? TCP_FALLING_TIME :	\
		(x==CONFIG_PWM_FREQUENCY) ? TCP_PWM_FREQUENCY :	\
		(x==CONFIG_SENSITIVITY)   ? TCP_SENSITIVITY :	\
		(x==CONFIG_BRIGHT_MAX)    ? TCP_BRIGHT_MAX :	\
		(x==CONFIG_BRIGHT_MIN)    ? TCP_BRIGHT_MIN :	\
		(x==CONFIG_BRIGHT_MAN)    ? TCP_BRIGHT_MAN :	\
		(x==CONFIG_ID_SELF)   	  ? TCP_ID_SELF : 0xFF


extern void task_spi(void * arg);

// --- SPI Protocol Definitions ---
#define SPI_DMA_CHUNK_SIZE  4096
//#define SPI_DMA_CHUNK_SIZE  (12 * 1024)

// Status Flags (Slave -> Master)
#define STATUS_IDLE         0xEE
#define STATUS_HAS_DATA     0xAA
#define STATUS_BUSY         0xBB

// Commands (Master -> Slave)
#define CMD_CHECK_STATUS    0x01
#define CMD_RESET_SYSTEM    0x02
#define CMD_SET_THRESHOLD   0x03

typedef enum {
	SPI_MAGIC_NONE 		= 0,
	SPI_MAGIC_BOOTING	= 0x42454749,			// 'BOOTING'

	SPI_MODE_LEARN 		= 0x4C454152,			// 'LEAR'
	SPI_MODE_LEARN_ACK 	= SPI_MODE_LEARN+1,		// 'LACK'
	SPI_MODE_NORMAL 	= 0x4E4F524D,			// 'NORM'
	SPI_MODE_NORMAL_ACK = SPI_MODE_NORMAL+1,	// 'NACK'

	SPI_LEARN_START		= 0x53544152,			// 'STAR', Learning Start Command
	SPI_LEARN_START_ACK	= SPI_LEARN_START+1,	// 'SASK', Learning Start Acknowledgement
	SPI_LEARN_STOP		= 0x53544F50,			// 'STOP', Learning Stop Command
	SPI_LEARN_STOP_ACK	= SPI_LEARN_STOP+1,		// 'STAK', Learning Stop Acknowledgement

	SPI_VOICE_RUNNING	= 0x56524E47,			// 'VRNG', Voice Recognition Running
	SPI_VOICE_RUNNING_ACK= SPI_VOICE_RUNNING+1,	// 'VRAG', Voice Recognition Running Acknowledged
	SPI_VOICE_STOPPED	= 0x56535450,			// 'VSTP', Voice Recognition Stopped
	SPI_VOICE_STOPPED_ACK= SPI_VOICE_STOPPED+1,	// 'VSKP', Voice Recognition Stopped Acknowledged

	SPI_IMAGE_NORMAL 	= 0x4E524D4C,			// 'NRML', Normal Image Packet Start Point
	SPI_IMAGE_LEARN		= 0x4C524E4C,			// 'LRNL', Learning Image Packet Start Point

	SPI_MAGIC_FACE 		= 0xFACECAFE,
	SPI_MAGIC_MAX 		= 0xFFFFFFFF
} spi_magic_t;

#define str_magic(x) \
		(x==SPI_MAGIC_NONE)      ? "NONE" : \
		(x==SPI_MAGIC_BOOTING)   ? "BOOTING" : \
		(x==SPI_MODE_LEARN)     ? "LEARN" : \
		(x==SPI_MODE_LEARN_ACK) ? "LEARN_ACK" : \
		(x==SPI_MODE_NORMAL)    ? "NORMAL" : \
		(x==SPI_MODE_NORMAL_ACK) ? "NORMAL_ACK" : \
		(x==SPI_LEARN_START)     ? "START" : \
		(x==SPI_LEARN_START_ACK) ? "START_ACK" : \
		(x==SPI_LEARN_STOP)      ? "STOP" : \
		(x==SPI_LEARN_STOP_ACK)  ? "STOP_ACK" : \
		(x==SPI_VOICE_RUNNING)     ? "VOICE_RUNNING" : \
		(x==SPI_VOICE_RUNNING_ACK) ? "VOICE_RUNNING_ACK" : \
		(x==SPI_VOICE_STOPPED)      ? "VOICE_STOPPED" : \
		(x==SPI_VOICE_STOPPED_ACK)  ? "VOICE_STOPPED_ACK" : \
		(x==SPI_IMAGE_NORMAL)    ? "PACKET_NORMAL" : \
		(x==SPI_IMAGE_LEARN)     ? "PACKET_LEARN" : \
		(x==SPI_MAGIC_FACE)      ? "FACE" : "UNKNOWN"

// Image Header Structure
typedef struct __attribute__((packed)) {
   	spi_magic_t magic;     	// SPI_MAGIC_FACE or Command/Rsp, 4B
    uint32_t total_len;     // 데이터 길이, 4B
    uint16_t width;			// 이미지 너비, 2B
    uint16_t height;		// 이미지 높이, 2B
    float    score;			// 인식 점수, 4B
    uint8_t  reserved[16];  // 16B, total 32바이트 맞춤
} face_header_t;

void request_spi_magic_to_master(spi_magic_t magic);

#endif
