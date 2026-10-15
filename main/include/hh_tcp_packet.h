#ifndef HH_PACKETDEF_H_
#define HH_PACKETDEF_H_

#include "HH_parameters.h"

#ifdef USE_PERIODIC_TCP_COMM
#define TCP_CONNECTION_TIMEOUT	21000	//21 secs
#define TCP_RUOK_PERIOD			10000
#endif

typedef enum __attribute((packed))
{
	REQ_NOTI_START	= 0x0010,	//from Server, no-payload
	RSP_NOTI_START	= 0x8010,	//to Server, ACK 0x00, NACK 0x#1, #= NACK reason
	REQ_NOTI_STOP	= 0x0020,	//from Server, no-payload
	RSP_NOTI_STOP	= 0x8020,	//to Server, ACK 0x00, NACK 0x#1, #= NACK reason

	RPT_NOTI_ONOFF	= 0x0000,	//to Server, onoff 4bit, netID 12bit, onoff 4bit, netID 12bit ==> 4bytes per channel
	CMD_SET_ONOFF	= 0x0040,	//from Server

	RSP_SET_ONOFF	= 0x8040,	//to Server, ACK/NACK

	CMD_SET_PARAM	= 0x0001,	//from Server
	RSP_SET_PARAM	= 0x8001,	//to Server
	CMD_GET_PARAM	= 0x0002,	//from Server
	RSP_GET_PARAM	= 0x8002,	//to Server
	RPT_NOTI_PARAM	= 0x0004,	//to Server
	RSP_NOTI_PARAM	= 0x8004,	//from Server

	CMD_QRY_CONN	= 0x0008,	//from Server, What lamp numbers are connected?
	RSP_QRY_CONN	= 0x8008,	//to Server
	RPT_CHG_CONN	= 0x0080,	//to Server, lamp conn status changed
	RSP_CHG_CONN	= 0x8080,	//from Server

} pkt_type_t;

typedef enum __attribute((packed))
{
	TCP_LIGHT_ONOFF			= 0x00,
	TCP_EN_SENSE			= 0x01,
	TCP_EN_RELAY,			//0x02
	TCP_ID_CORNER1,			//0x03
	TCP_ID_CORNER2,			//0x04
	TCP_ID_CORNER3,			//0x05
	TCP_CO_RANGE,			//0x06
	TCP_KEEPING_TIME,		//0x07
	TCP_RISING_TIME,		//0x08
	TCP_FALLING_TIME,		//0x09
	TCP_PWM_FREQUENCY,		//0x0A
	TCP_SENSITIVITY,		//0x0B
	TCP_BRIGHT_MAX,			//0x0C
	TCP_BRIGHT_MIN,			//0x0D
	TCP_BRIGHT_MAN,			//0x0E
	TCP_ID_SELF,			//0x0F

	TCP_SERVER_RESET		= 0xFE,
} tcp_param_id_t;

typedef struct __attribute((packed))
{
	uint32_t	pid;	// Packet ID, fixed as 0xffffffff
	uint32_t	tid;	// Transaction ID
	pkt_type_t	type;	// type ID
	uint8_t		len; 	// length of payload
} tcp_hdr_t;

typedef struct __attribute((packed))
{
	tcp_hdr_t	hdr;		//sizeof(hdr) = 9
	uint8_t		payload[2+1+(2*TCP_BRIGHT_MAN)];	//max_sizeof(payload) = 2(net_id)+1(param_cnt)+(13*2)
} tcp_pkt_t;

typedef struct __attribute((packed))
{
	tcp_param_id_t	param_id;
	uint8_t 		val;
} param_pair_t;

typedef struct __attribute((packed))
{
	tcp_param_id_t	param_id;
	uint16_t 		val;	//self_id value
} param_self_id_t;

typedef struct __attribute((packed))
{
	uint16_t	net_id;
	uint8_t		param_cnt;	//the number of param pair
	union
	{
		param_pair_t	param_pair[TCP_BRIGHT_MAN];
		param_self_id_t	param_self_id;
		tcp_param_id_t	param_id[TCP_ID_SELF];//used in CMD_GET_PARAM
	};
} payload_param_t;

typedef struct
{
	bool		is_set_command;
	uint8_t		channel;
	TickType_t	timestamp;
} q_delayd_config_t;

typedef struct __attribute((packed))
{
	TickType_t	timestamp;
	tcp_pkt_t	tcp_pkt;
} q_confirm_ack_t;

#define TCPtoSPI(x) \
		(x==TCP_EN_SENSE) 	  ? CONFIG_EN_SENSE	:		\
		(x==TCP_EN_RELAY) 	  ? CONFIG_EN_RELAY :		\
		(x==TCP_ID_CORNER1)   ? CONFIG_ID_CORNER1 : 	\
		(x==TCP_ID_CORNER2)   ? CONFIG_ID_CORNER2 : 	\
		(x==TCP_ID_CORNER3)   ? CONFIG_ID_CORNER3 : 	\
		(x==TCP_CO_RANGE) 	  ? CONFIG_CO_RANGE :		\
		(x==TCP_KEEPING_TIME) ? CONFIG_KEEPING_TIME :	\
		(x==TCP_RISING_TIME)  ? CONFIG_RISING_TIME :	\
		(x==TCP_FALLING_TIME) ? CONFIG_FALLING_TIME :	\
		(x==TCP_PWM_FREQUENCY)? CONFIG_PWM_FREQUENCY :	\
		(x==TCP_SENSITIVITY)  ? CONFIG_SENSITIVITY :	\
		(x==TCP_BRIGHT_MAX)   ? CONFIG_BRIGHT_MAX :		\
		(x==TCP_BRIGHT_MIN)   ? CONFIG_BRIGHT_MIN :		\
		(x==TCP_BRIGHT_MAN)   ? CONFIG_BRIGHT_MAN :		\
		(x==TCP_ID_SELF)   	  ? CONFIG_ID_SELF : 0xFF

#endif //HH_PACKETDEF_H_

