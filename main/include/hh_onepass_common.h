#ifndef HH_ONEPASS_ESP_COMMON_H__
#define HH_ONEPASS_ESP_COMMON_H__

#include <stdint.h>

#pragma GCC diagnostic ignored "-Waddress-of-packed-member"

#define PP_WIFI_USE_CH6n11_ONLY_20221005
#define PP_FW_AUTO_UPDATE

#define OTA_DATA_BUF_SIZE 					CONFIG_TCP_MSS		// for Web-browser to GW_ETH and GW_ETH to GW_WIFI

#define ETH_ONEPASS_TCP_SERVER_PORT			(6001)
#define ETH_VOIP_UDP_SERVER_PORT 			(7001)
#define ETH_OTA_TCP_SERVER_PORT				(8001)
#define ETH_LAMP_TCP_SERVER_PORT			(9001)
#define WIFI_AP_UDP_PORT					(6002)
#define WIFI_AP_TCP_PORT					(6003)
#define ETH_AIR_TCP_SERVER_PORT				(6004)

/*** LAMP Monitoring System PC IP (TCP Client) ***/
#define ETH_LAMP_MONITORING_CLIENT_IP_STR		"10.100.100.37"
#define ETH_LAMP_MONITORING_CLIENT_IP_U32		(0x2564640A)

/*** ONEPASS Monitoring System PC IP (TCP Server) ***/
#define ETH_ONEPASS_MONITORING_SERVER_IP_STR	ETH_LAMP_MONITORING_CLIENT_IP_STR
#define ETH_ONEPASS_MONITORING_SERVER_IP_U32	ETH_LAMP_MONITORING_CLIENT_IP_U32

/*** AIR Monitoring System PC IP (TCP Server) ***/
#define ETH_AIR_MONITORING_SERVER_IP_STR	ETH_LAMP_MONITORING_CLIENT_IP_STR
#define ETH_AIR_MONITORING_SERVER_IP_U32	ETH_LAMP_MONITORING_CLIENT_IP_U32

/*** EMC VOIP Server IP (UDP Server) ***/
#define ETH_EMC_VOIP_SERVER_IP_STR				"10.100.100.35"
#define ETH_EMC_VOIP_SERVER_IP_U32				(0x2364640A)

/*** EMC VOIP-RC IP (UDP Server) ***/
#define ETH_EMC_VOIP_RC1_IP_STR					"10.100.100.40"
#define ETH_EMC_VOIP_RC2_IP_STR					"10.100.100.41"
#define ETH_EMC_VOIP_RC3_IP_STR					"10.100.100.42"
#define ETH_EMC_VOIP_RC4_IP_STR					"10.100.100.43"
#define ETH_EMC_VOIP_RC5_IP_STR					"10.100.100.44"
#define ETH_EMC_VOIP_RC6_IP_STR					"10.100.100.45"
#define ETH_EMC_VOIP_RC7_IP_STR					"10.100.100.46"
#define ETH_EMC_VOIP_RC8_IP_STR					"10.100.100.47"
#define ETH_EMC_VOIP_RC1_IP_U32					(0x2864640A)
#define ETH_EMC_VOIP_RC2_IP_U32					(0x2964640A)
#define ETH_EMC_VOIP_RC3_IP_U32					(0x2A64640A)
#define ETH_EMC_VOIP_RC4_IP_U32					(0x2B64640A)
#define ETH_EMC_VOIP_RC5_IP_U32					(0x2C64640A)
#define ETH_EMC_VOIP_RC6_IP_U32					(0x2D64640A)
#define ETH_EMC_VOIP_RC7_IP_U32					(0x2E64640A)
#define ETH_EMC_VOIP_RC8_IP_U32					(0x2F64640A)

/*** LAMP/ONEPASS Inside Gateway Base IP (TCP Server) ***/
#define ETH_GATEWAY_IN_BASE_IP_STR				"10.100.100.100"
#define ETH_GATEWAY_IN_BASE_IP_U32				(0x6464640A)

/*** LAMP/ONEPASS Outside Gateway Base IP (TCP Server) ***/
#define ETH_GATEWAY_OUT_BASE_IP_STR				"10.100.101.100"
#define ETH_GATEWAY_OUT_BASE_IP_U32				(0x6465640A)

/*** Subnet mask and Gateway IP ***/
#define ETH_DEFAULT_NETMASK_STR					"255.255.0.0"
#define ETH_DEFAULT_NETMASK_U32					(0x0000FFFF)
#define ETH_DEFAULT_GATEWAY_STR					"10.100.100.1"
#define ETH_DEFAULT_GATEWAY_U32					(0x0164640A)

#define ETH_DUMMY_IP_STR						"10.100.100.254"
#define ETH_DUMMY_IP_U32						(0xFE64640A)

#define WIFI_DEFAULT_AP_IP_STR					"172.30.0.1"
#define WIFI_DEFAULT_AP_IP_U32					(0x01001EAC)
#define WIFI_DEFAULT_NETMASK_STR				"255.255.255.0"
#define WIFI_DEFAULT_NETMASK_U32				(0x00FFFFFF)

#define PP_CALLER_STATIC_IP 	1
#define WIFI_UDP_CHANNEL_COUNT	4
#define WIFI_IDIP_TABLE_SIZE	(WIFI_UDP_CHANNEL_COUNT*2)
#define VOIP_IDIP_TABLE_SIZE	16

#define PP_GATEWAY_COUNT					(100)				// Max. 112 = (0x80-0x10)/2*2 = 0x70
#define PP_GW_MESH_START_ADDRESS			(0x1000)			// GW_NODE_ADDRESS : 0x1000, 0x1080, 0x1100, 0x1180...
#define PP_ADJ_MESH_ADDR_GAP				(0x0080)			// Address gap between adjacent gateways

/* AP Channel
 * For 20 MHz: 1, 6 or 11 in USA and 1, 5, 9 or 13 in most parts of the world
 * For 40 MHz: 3 in USA and 3 or 11 in most parts of the world
 */
//#define DEFAULT_AP_CHANNEL	1
//#define DEFAULT_AP_CHANNEL	5
//#define DEFAULT_AP_CHANNEL	9
#define DEFAULT_AP_CHANNEL		13

#define MCLK_IS_4MHZ
#define PP_USE_G711
#define TX_REPEAT_COUNT			(1)
#define TOGGLE_LED_PERIOD		(4)

//#define WIFI_TXPOWER			20			// @brief     Set maximum WiFi transmiting power, 5dBm
#define WIFI_TXPOWER			78			// @brief     Set maximum WiFi transmiting power, 19.5dBm
//#define WIFI_TXPOWER			-4			// @brief     Set maximum WiFi transmiting power, -1dBm

#define SITE_ID_NOT_ASSIGNED	0xFFFFFFFF

#define PP_PERIORITY_HIGH			6
#define PP_PERIORITY_MID			5
#define PP_PERIORITY_LOW			4

#ifdef PP_FW_AUTO_UPDATE

#define AUTOUP_PKT_SIZE_MAX				(OTA_DATA_BUF_SIZE)
#if PP_ANOTHER_PORT_FOR_AUTOUP
#define AUTO_UPDATE_SERVER_PORT			(ETH_OTA_TCP_SERVER_PORT+100)
#else
#define AUTO_UPDATE_SERVER_PORT			(ETH_OTA_TCP_SERVER_PORT)
#endif

#define AUTO_UPDATE_TIMEOUT_MS			(5000)
typedef enum __attribute((packed))
{
	AUTOUP_TCP_OPCODE_NONE					= 0x00,

	AUTOUP_TCP_OPCODE_ETH_VERSION_REQ		= 0x01,
	AUTOUP_TCP_OPCODE_WIFI_VERSION_REQ,
	AUTOUP_TCP_OPCODE_CALLER_VERSION_REQ,
	AUTOUP_TCP_OPCODE_ETH_BEGIN_REQ,
	AUTOUP_TCP_OPCODE_WIFI_BEGIN_REQ,
	AUTOUP_TCP_OPCODE_CALLER_BEGIN_REQ,

	AUTOUP_TCP_OPCODE_DATA_REQ,

	AUTOUP_TCP_OPCODE_ETH_END_REQ,
	AUTOUP_TCP_OPCODE_WIFI_END_REQ,
	AUTOUP_TCP_OPCODE_CALLER_END_REQ,

	AUTOUP_TCP_OPCODE_CALLER_VERSION_SET_REQ,

	AUTOUP_TCP_OPCODE_TERMINATE_REQ,

	AUTOUP_TCP_OPCODE_ETH_VERSION_ACK		= (AUTOUP_TCP_OPCODE_ETH_VERSION_REQ + 0x80),
	AUTOUP_TCP_OPCODE_WIFI_VERSION_ACK,
	AUTOUP_TCP_OPCODE_CALLER_VERSION_ACK,
	AUTOUP_TCP_OPCODE_ETH_BEGIN_ACK,
	AUTOUP_TCP_OPCODE_WIFI_BEGIN_ACK,
	AUTOUP_TCP_OPCODE_CALLER_BEGIN_ACK,

	AUTOUP_TCP_OPCODE_DATA_ACK,

	AUTOUP_TCP_OPCODE_ETH_END_ACK,
	AUTOUP_TCP_OPCODE_WIFI_END_ACK,
	AUTOUP_TCP_OPCODE_CALLER_END_ACK,

	AUTOUP_TCP_OPCODE_CALLER_VERSION_SET_ACK,

	AUTOUP_TCP_OPCODE_TERMINATE_ACK,

	AUTOUP_TCP_OPCODE_ERROR					= 0xFF

} autoup_tcp_opcode_t;

typedef struct __attribute((packed))
{
	uint8_t		dummy;
	autoup_tcp_opcode_t opcode;
	union __attribute((packed)) {
		uint32_t	param;
		uint32_t	version;
		uint32_t	offset;
		uint32_t	seq;
	};
	uint16_t	length;	//length of data
} autoup_header_t;

typedef struct __attribute((packed))
{
	autoup_header_t	header;
	uint8_t			data[AUTOUP_PKT_SIZE_MAX];
} autoup_stream_t;

typedef enum
{
	OTA_TARGET_NONE,
	OTA_TARGET_ETH,
	OTA_TARGET_WIFI,
	OTA_TARGET_CALLER,
	OTA_TARGET_ALL,
} cc_ota_target_t;

typedef enum __attribute((packed))
{
	AUTOUP_RESULT_NONE,
	AUTOUP_RESULT_PING_FAIL,
	AUTOUP_RESULT_COMM_ERROR,
	AUTOUP_RESULT_SAME_VERSION,
	AUTOUP_RESULT_OK,
} autoup_result_t;

typedef enum
{
	AUTOUP_STATUS_NONE,
	AUTOUP_STATUS_VERSION_HEADER,
	AUTOUP_STATUS_VERSION_VERIFY,
	AUTOUP_STATUS_BEGIN_HEADER,
	AUTOUP_STATUS_BEGIN_SCRIPT,
	AUTOUP_STATUS_DATA_HEADER,
	AUTOUP_STATUS_DATA_FIRST,
	AUTOUP_STATUS_DATA_REST,
	AUTOUP_STATUS_TERMINATE,
	AUTOUP_STATUS_SET_CALLER_VERSION,
	AUTOUP_STATUS_MAX,
} autoup_status_t;;

#endif

typedef enum __attribute((packed))
{
	EMC_OPCODE_EMPTY				= 0x00,

	EMC_OPCODE_RPT_ID				= 0x01,	// by call
	EMC_OPCODE_RSP_ID				= 0x81,	// by voip
	EMC_OPCODE_REQ_TALK_START		= 0x02,	// by client
	EMC_OPCODE_ACK_TALK_START		= 0x82,	// by voip
	EMC_OPCODE_REQ_TALK_STOP		= 0x03,	// by call or voip
	EMC_OPCODE_ACK_TALK_STOP		= 0x83,	// by voip or call
	EMC_OPCODE_SET_MIC_VOLUME		= 0x04,	// by voip, len 1
	EMC_OPCODE_ACK_MIC_VOLUME		= 0x84,	// by call
	EMC_OPCODE_SET_SPK_VOLUME		= 0x05,	// by voip, len 1
	EMC_OPCODE_ACK_SPK_VOLUME		= 0x85,	// by call
	EMC_OPCODE_GET_MIC_VOLUME		= 0x06,	// by voip
	EMC_OPCODE_RSP_MIC_VOLUME		= 0x86,	// by call, len 1
	EMC_OPCODE_GET_SPK_VOLUME		= 0x07,	// by voip
	EMC_OPCODE_RSP_SPK_VOLUME		= 0x87,	// by call, len 1
	EMC_OPCODE_SET_LOC_MIC_VOLUME	= 0x08,	//
	EMC_OPCODE_ACK_LOC_MIC_VOLUME	= 0x88,	//
	EMC_OPCODE_SET_LOC_SPK_VOLUME	= 0x09,	//
	EMC_OPCODE_ACK_LOC_SPK_VOLUME	= 0x89,	//
	EMC_OPCODE_GET_LOC_MIC_VOLUME	= 0x0A,	//
	EMC_OPCODE_RSP_LOC_MIC_VOLUME	= 0x8A,	//
	EMC_OPCODE_GET_LOC_SPK_VOLUME	= 0x0B,	//
	EMC_OPCODE_RSP_LOC_SPK_VOLUME	= 0x8B,	//

	EMC_OPCODE_CMD_TEST_MODE		= 0x10,	// by voip
	EMC_OPCODE_RSP_TEST_MODE		= 0x90,	// by call
	EMC_OPCODE_RCVOK_TEST_MODE		= 0x91,	// VOIP to CALLER

	EMC_OPCODE_RPT_NFC_UID			= 0x20,	// by call
	EMC_OPCODE_ACK_NFC_UID			= 0xA0,
	EMC_OPCODE_REQ_SKSP_EMC			= 0x21,	// used for SP and SK's emergency call
	EMC_OPCODE_ACK_SKSP_EMC			= 0xA1,	// used for SP and SK's emergency call

#ifdef LH_AIR_MONITOR
    // from PC to AIR
	SPIS_AIR_FROM_PC				= 0x40,
    // from AIR to PC
	SPIS_AIR_TO_PC					= 0xc0,
#endif

#ifdef PP_WIFI_USE_CH6n11_ONLY_20221005
	WIFI_AP_CHANNEL_GET				= 0xCA,
	WIFI_AP_CHANNEL_GET_ACK,
	WIFI_AP_CHANNEL_SET,
	WIFI_AP_CHANNEL_SET_ACK,
#endif
	OTA_OPCODE_MIN					= 0xD0,

	OTA_OPCODE_WIFI_START_REQ,
	OTA_OPCODE_WIFI_START_ACK,
	OTA_OPCODE_WIFI_END_REQ,
	OTA_OPCODE_WIFI_END_ACK,

	OTA_OPCODE_WIFI_VER_GET,
	OTA_OPCODE_WIFI_VER_GET_ACK,

	OTA_OPCODE_CALLER_START_REQ,
	OTA_OPCODE_CALLER_START_ACK,
	OTA_OPCODE_CALLER_END_REQ,
	OTA_OPCODE_CALLER_END_ACK,

	OTA_OPCODE_CALLER_VER_GET,		// request CALLER version which is saved on WIFI's flash
	OTA_OPCODE_CALLER_VER_GET_ACK,	// inform current CALLER version

	OTA_OPCODE_CALLER_VER_SET,		// request to WIFI to save CALLER version on WIFI's nvs
	OTA_OPCODE_CALLER_VER_SET_ACK,	// inform current CALLER version

	OTA_OPCODE_WIFI_REBOOT_REQ,
	OTA_OPCODE_WIFI_REBOOT_ACK,

	OTA_OPCODE_DATA_SEND,
	OTA_OPCODE_DATA_RCVD,

	OTA_CALLER_WAKE_UP,				// indicate caller has just wake-up
	OTA_OPCODE_CONTINUE_HEARTBEAT	= 0xE4,

#ifdef PP_FW_AUTO_UPDATE	/*** for autoup, 20210326 ***/
	OTA_OPCODE_WIFI_SIZE_REQ		= 0xE5,
	OTA_OPCODE_WIFI_SIZE_ACK		= 0xE6,
	OTA_OPCODE_CALLER_SIZE_REQ		= 0xE7,
	OTA_OPCODE_CALLER_SIZE_ACK		= 0xE8,
	OTA_OPCODE_WIFI_DATA_REQ		= 0xE9,		// shall include offset for each transfer.
	OTA_OPCODE_WIFI_DATA_ACK		= 0xEA,		// max.OTA_DATA_BUF_SIZE bytes @offset per one transfer
	OTA_OPCODE_CALLER_DATA_REQ		= 0xEB,		// shall include offset for each transfer.
	OTA_OPCODE_CALLER_DATA_ACK		= 0xEC,		// max.OTA_DATA_BUF_SIZE bytes @offset per one transfer
	OTA_OPCODE_TRANSFER_START_REQ	= 0xED,
	OTA_OPCODE_TRANSFER_START_ACK	= 0xEE,
#endif //PP_FW_AUTO_UPDATE

	OTA_OPCODE_MAX					= 0xEF,

	EMC_OPCODE_VOICE_DATA			= 0xF0,

	EMC_OPCODE_GWID_REQ				= 0xF1,
	EMC_OPCODE_GWID_INFORM			= 0xF2,
	EMC_OPCODE_PC_ALIVE				= 0xF3,

	EMC_OPCODE_RCV_OK				= 0xF4,

	EMC_OPCODE_RC_ALIVE				= 0xF5,
	EMC_OPCODE_RC_ALIVE_RSP			= 0xF6,

	EMC_OPCODE_I2C_INTR_STATUS		= 0xF7,
	EMC_OPCODE_I2C_PRESSED_BUTTON	= 0xF8,
	EMC_OPCODE_I2C_FRONT_LED_CTRL	= 0xF9,	// bit2 LED Blinking ON/OFF, bit1 START LED ON/OFF, bit0 STOP LED ON/OFF
	EMC_OPCODE_I2C_FRONT_SIREN_CTRL	= 0xFA,	// bit1 SIREN ON/OFF, bit0 Source_SEL ON/OFF
	EMC_OPCODE_I2C_FRONT_TALK_CTRL	= 0xFB,	// bit0 TALK ON/OFF

	EMC_OPCODE_I2C_FRONT_RESET		= 0xFD,	// Reset Nuvoton

	EMC_OPCODE_DUMMY				= 0xFE,
	EMC_OPCODE_MAX					= 0xFF,

} emc_opcode_t;

typedef enum __attribute((packed))
{
	ONEPASS_OPCODE_NONE				= 0x0000,

	/* GW Request, Server Response */
	ONEPASS_PARKING_LOC_PUT_REQ		= 0x1001,
	ONEPASS_PARKING_LOC_PUT_ACK		= 0x9001,

	ONEPASS_PARKING_LOC_GET_REQ		= 0x1002,
	ONEPASS_PARKING_LOC_GET_ACK		= 0x9002,

	ONEPASS_PARKING_LOC_RMV_REQ		= 0x1003,
	ONEPASS_PARKING_LOC_RMV_ACK		= 0x9003,

	ONEPASS_GATE_ENT_REQ			= 0x1011,
	ONEPASS_GATE_ENT_ACK			= 0x9011,

	ONEPASS_GATE_STATUS_RPT			= 0x1012,
	ONEPASS_GATE_STAUS_RPT_RCVD		= 0x9012,

	/* Server Request, GW Response */
	ONEPASS_GATE_STATUS_GET_REQ		= 0x9013,
	ONEPASS_GATE_STATUS_GET_ACK		= 0x1013,

	ONEPASS_GATE_PARAM_SET_REQ		= 0x9014,
	ONEPASS_GATE_PARAM_SET_ACK		= 0x1014,

	ONEPASS_GATE_PARAM_GET_REQ		= 0x9015,
	ONEPASS_GATE_PARAM_GET_ACK		= 0x1015,

	/* Residents Location */
	ONEPASS_RESI_POS_RPT			= 0x1021,
	ONEPASS_RESI_POS_RPT_RCVD		= 0x9021,

	ONEPASS_TCP_HEARTBEAT			= 0x10FD,
	ONEPASS_TCP_HEARTBEAT_ACK		= 0x90FD,

	ONEPASS_SITE_CODE_REQ			= 0x10FE,
	ONEPASS_SITE_CODE_ACK			= 0x90FE,

	ONEPASS_NOTICE_MESSAGE_REQ		= 0x90FF,
	ONEPASS_NOTICE_MESSAGE_ACK		= 0x10FF,	// NOT used because PC won't wait for ACK.

	ONEPASS_INDICATE_SLM_LEARN		= 0xFF44,
	ONEPASS_INDICATE_SLM			= 0xFF55,
	ONEPASS_INDICATE_DB_MANAGE		= 0xFF77,
	ONEPASS_INDICATE_AIR_MON		= 0xFFAA,

	ONEPASS_OPCODE_WRONG			= 0xFFFF,

} onepass_opcode_t;

typedef enum __attribute((packed))
{
	PARK_LOC_TYPE_CAR_NUMBER = 0,
	PARK_LOC_TYPE_CAR_LOCATOR,
	PARK_LOC_TYPE_CALLER_NFC,
	PARK_LOC_TYPE_SMART_KEY,
	PARK_LOC_TYPE_MAX,
} park_loc_type_t;

typedef enum __attribute((packed))
{
	TERMINAL_TYPE_SMART_PHONE = 1,
	TERMINAL_TYPE_SMART_BAND,
	TERMINAL_TYPE_NFC,
	TERMINAL_TYPE_SMART_KEY,
	TERMINAL_TYPE_CAR_LOCATOR,
	TERMINAL_TYPE_MAX,
} terminal_type_t;

typedef struct __attribute((packed))
{
	uint8_t 		dummy;		// dummy byte because the first bit of the first byte receive by ETH_GW is fixed as zero. don't know why
	emc_opcode_t	opcode;
	uint8_t			id[3];
} emc_header_t;

typedef struct __attribute((packed))
{
	emc_header_t	header;
	union __attribute((packed))
	{
		uint32_t	seq;
		uint32_t	size;
		uint32_t	version;
		uint32_t	offset;
	};
} ota_header_t;

typedef struct __attribute((packed))
{
	emc_header_t	header;
	union __attribute((packed))
	{
		uint32_t	seq;
		uint32_t	size;
		uint32_t	version;
		uint32_t	offset;
#ifdef PP_WIFI_USE_CH6n11_ONLY_20221005
		uint32_t	ap_channel;
#endif
	};
	uint16_t		length;		//length of payload excluding header and length
	uint8_t			data[OTA_DATA_BUF_SIZE];
} ota_data_t;

typedef struct
{
	uint32_t		retry_count;
	uint32_t		timestamp;
	uint8_t			id[3];
	emc_opcode_t	pending_opcode;
	uint32_t		pending_seq;
	uint32_t		length;			//length this time
	uint32_t		total_len;
} caller_ota_status_t;

typedef enum __attribute((packed))
{
	FRONT_BUTTON_NONE,
	FRONT_BUTTON_COMM,
	FRONT_BUTTON_END,
	FRONT_BUTTON_PREV,
	FRONT_BUTTON_NEXT,
	FRONT_BUTTON_MAX,
} front_button_t;

typedef struct __attribute((packed))
{
	emc_opcode_t	opcode;
	union __attribute((packed))
	{
		uint32_t		u32_data;
		uint8_t			u8_data;
		front_button_t	button;
		uint8_t			call_id[3];
	};
} i2c_pkt_t;

typedef struct
{
	uint32_t	rc_id;
	uint32_t	timestamp;
} rc_alive_info_t;

typedef struct
{
	TickType_t		timestamp;
	uint8_t			id[3];
	emc_opcode_t	opcode;
	uint32_t		u32_data;
	uint8_t			rc_num;
} repeat_cmd_t;

typedef struct
{
	uint8_t			id[3];	//id[0] CALL#, id[1] GW#, id[2] Region#
	uint32_t		ipaddr;	//uint32_t
	uint16_t		ipport;
} idip_pair_t;

typedef struct __attribute((packed))
{
	uint8_t		top;
	uint16_t	mid;
	uint16_t	btm;
} phone_num_t;

typedef struct __attribute((packed))	// Smartphone beacon packet
{
	uint16_t	dummy0;			// fixed 0x1B, 0xFF
	uint16_t	mfg_id;			// 0xAA, 0x01
	uint16_t	dummy1;			// fixed 0xBE, 0xAC
	uint32_t	site_code;		// little-endian
	phone_num_t	phone_num;
	uint32_t	key;
	uint8_t		dev_name[5];	// "pHoNe"
	uint32_t	dummy3;			// reserved
} smartphone_beacon_t;

typedef struct __attribute((packed))	// i-phone beacon packet
{
	uint16_t	mfg_id;			// 0x004C
	uint16_t	dummy1;			// 0x1502
	uint32_t	site_code;		// little-endian
	phone_num_t	phone_num;
	uint32_t	key;
	uint8_t		dev_name[5];	// "pHoNe"
} ibeacon_t;

typedef struct __attribute((packed))
{
	uint32_t			pid;		// 4B
	uint16_t			tid;		// 2B
	onepass_opcode_t	opcode;		// uint16_t, 2B
	uint8_t				len;		// 1B
} onepass_header_t;

typedef struct __attribute((packed))
{
	park_loc_type_t		type;		// 0x00 Car number, 0x01 ID value of Car Locator, 0x02 CALL's ID and NFC UID
	union __attribute((packed))
	{
		uint8_t		car_num[20];
		struct __attribute((packed))
		{
			union __attribute((packed))
			{
				uint32_t	cl_id;
				uint32_t	sk_id;
				uint32_t	id_u32;
				uint32_t	nfc_id;
			};
			int8_t rssi;
		};
	};
} park_loc_put_req_t;

typedef struct __attribute((packed))
{
	uint8_t		result;
	uint8_t		park_loc[20];
//	uint8_t		park_time[14+1];	// park_time[14] = '\0'
} park_loc_put_ack_t;

#if PP_SK_USE_OLED_DISPLAY_20231104
typedef struct __attribute((packed))
{
	park_loc_type_t		type;		// 0x00 Car number, 0x01 ID value of Car Locator, 0x02 CALL's ID and NFC UID
	union __attribute((packed))
	{
		uint8_t		car_num[20];
		struct __attribute((packed))
		{
			union __attribute((packed))
			{
				uint32_t	cl_id;
				uint32_t	sk_id;
				uint32_t	id_u32;
				uint32_t	nfc_id;
			};
			int8_t rssi;
		};
	};
} park_loc_get_req_t;
#else
typedef struct __attribute((packed))
{
	uint8_t		car_num[20];		// UTF-8
} park_loc_get_req_t;
#endif

typedef park_loc_put_ack_t park_loc_get_ack_t;

typedef park_loc_put_req_t park_loc_rmv_req_t;

typedef struct __attribute((packed))
{
	uint8_t		result;
} park_loc_rmv_ack_t;

typedef struct __attribute((packed))
{
	terminal_type_t	type;		// 0x01 SmartPhone, 0x02 SmartBand, 0x03 NFC, 0x04 SmartKey
	union __attribute((packed))
	{
		phone_num_t	phone_num;	// uint8_t top_phone#, uint16_t mid_phone#, uint16_t btm_phone#
		uint32_t	id;			// id of SamrtBand/NFC/SmartKey
	};
	uint32_t	key;			// key value
} gate_ent_req_t;

typedef struct __attribute((packed))
{
	uint8_t		result;
} gate_ent_ack_t;

typedef struct __attribute((packed))
{
	uint8_t		state;		// 0x00 open, 0x01 closed
} gate_status_rpt_t;

typedef uint8_t gate_status_rpt_rcvd_t;	//dummy
typedef uint8_t	gate_status_get_req_t;	//dummy

typedef struct __attribute((packed))
{
	uint8_t		state;			// 0x00 open, 0x01 closed
} gate_status_get_ack_t;

typedef struct __attribute((packed))
{
	uint8_t		sensitivity;	// sensitivity (0~100)
	uint8_t		open_duration;	// open duration in secs
	uint8_t		radar_enable;	// sensor enable
} gate_param_t;

typedef struct __attribute((packed))
{
	gate_param_t	param;
} gate_param_set_req_t;

typedef uint8_t gate_param_set_ack_t;
typedef uint8_t gate_param_get_req_t;

typedef struct __attribute((packed))
{
	gate_param_t	param;
} gate_param_get_ack_t;

typedef struct __attribute((packed))
{
	terminal_type_t	type;	// 0x01 SmartPhone, 0x02 SmartBand, ___, 0x04 SmartKey
	union __attribute((packed))
	{
		phone_num_t	phone_num;	// uint8_t top_phone#, uint16_t mid_phone#, uint16_t btm_phone#
		uint32_t	id;			// id of SamrtBand/NFC/SmartKey
	};
	int8_t			rssi;
} resi_pos_rpt_t;

typedef uint8_t	resi_pos_rpt_rcvd_t;

typedef enum __attribute((packed))
{
	FUNC_SP_NORMAL		= 0,	// SmartPhone normal adv. data for ENT_REQ and RESI_POS_REPORT
	FUNC_SP_PARKING,
	FUNC_SP_EMC,

	FUNC_SB_NORMAL		= 0,	// SmartBand normal adv. data for ENT_REQ and RESI_POS_REPORT

	FUNC_SK_NORMAL		= 0,	// SmartKey normal adv. data for ENT_REQ and RESI_POS_REPORT
	FUNC_SK_PARKING,			// SmartKey adv. data for PARKING_ADD
	FUNC_SK_EMC,				// SmartKey adv. data for PARKING_REMOVE

	FUNC_CL_NORMAL		= 0,
	FUNC_CL_PARK_ADD,			// CarLocator adv. data for PARKING_ADD
	FUNC_CL_PARK_RMV,			// CarLocator adv. data for PARKING_RMV
} req_func_t;

typedef struct __attribute((packed))
{
	terminal_type_t	type;		// 0x01 SmartPhone, 0x02 SmartBand, 0x04 SmartKey, 0x05 CarLocator
	req_func_t		req_func;
	union __attribute((packed))
	{
		phone_num_t	phone_num;	// uint8_t top_phone#, uint16_t mid_phone#, uint16_t btm_phone#
		uint32_t	id;			// id of SamrtBand/NFC/SmartKey
	};
	int8_t			rssi;
} trans_adv_rssi_t;

typedef struct __attribute((packed))
{
	uint32_t key;
} sp_emc_call_req_t;

typedef struct __attribute((packed))
{
	bool result;
} sp_emc_call_ack_t;

typedef union __attribute((packed))
{
	park_loc_put_req_t		park_loc_put_req;
	park_loc_put_ack_t		park_loc_put_ack;
	park_loc_get_req_t		park_loc_get_req;
	park_loc_get_ack_t		park_loc_get_ack;
	park_loc_rmv_req_t		park_loc_rmv_req;
	park_loc_rmv_ack_t		park_loc_rmv_ack;
	gate_ent_req_t			gate_ent_req;
	gate_ent_ack_t			gate_ent_ack;
	gate_status_rpt_t		gate_status_rpt;
	gate_status_rpt_rcvd_t	gate_status_rpt_rcvd;
	gate_status_get_req_t	gate_status_get_req;
	gate_status_get_ack_t	gate_status_get_ack;
	gate_param_set_req_t	gate_param_set_req;
	gate_param_set_ack_t	gate_param_set_ack;
	gate_param_get_req_t	gate_param_get_req;
	gate_param_get_ack_t	gate_param_get_ack;
	resi_pos_rpt_t			resi_pos_rpt;
	resi_pos_rpt_rcvd_t 	resi_pos_rpt_rcvd;
	trans_adv_rssi_t		trans_adv_rssi;
	sp_emc_call_req_t		sp_emc_call_req;
	sp_emc_call_ack_t		sp_emc_call_ack;
} onepass_union_t;

typedef struct __attribute((packed))
{
	uint8_t				ch;	// Lamp# or LobbyMaster#
	onepass_union_t		data;
} onepass_payload_t;

typedef enum __attribute((packed))
{
	SLM_FACE_READY			= 0x51,
	SLM_VOICE_READY			= 0x52,

	SLM_FACE_LEARN_START	= 0xD1,
	SLM_VOICE_LEARN_START	= 0xD2,

	SLM_RECOG_RESULT		= 0xE1,
	SLM_GATE_CONTROL		= 0xE2,

	SLM_FACE_LEARN_OK		= 0xF1,
	SLM_VOICE_LEARN_OK		= 0xF2,

} slm_opcode_t;

typedef struct __attribute((packed))
{
	uint8_t			ch;		// SLM#
	slm_opcode_t	opcode;
	uint8_t			data[15];
} slm_payload_t;

typedef struct __attribute((packed))
{
	onepass_header_t	header;
	union __attribute((packed))
	{
		onepass_payload_t	payload;
		slm_payload_t		slm_payload;
		uint8_t notice_message[130];
		uint32_t site_code;
	};
} onepass_pkt_t;

typedef enum
{
	CALL_STATE_IDLE,
	CALL_STATE_WAITING_ACK_ID,
	CALL_STATE_ID_ACKED,
	CALL_STATE_WAITING_ACK_TALK_START,
	CALL_STATE_TALK_START_ACKED,
	CALL_STATE_TALK_PROCESSING,
	CALL_STATE_WAITING_ACK_TALK_STOP,
	CALL_STATE_TALK_STOP_ACKED,
	CALL_STATE_TALK_TERMINATED,
	CALL_STATE_MAX,
} call_state_t;

typedef struct
{
	uint32_t		timestamp;
	onepass_pkt_t	onepass_pkt;
} skip_list_t;

typedef struct
{
	uint16_t tid;
	uint32_t timestamp_tx;
	uint32_t timestamp_rx;
	uint32_t elapsed_ms;
} chk_elapsed_time_t;

#define OTA_SPI_QUEUE_COUNT	2

#endif //HH_ONEPASS_ESP_COMMON_H__
