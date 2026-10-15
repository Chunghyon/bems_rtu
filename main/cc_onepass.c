#include <string.h>
#include <sys/param.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"

#include "lwip/err.h"
#include "lwip/sockets.h"
#include "lwip/sys.h"
#include <lwip/netdb.h>

#include "hh_parameters.h"

#include "hh_onepass_common.h"
#include "hh_util.h"
#include "hh_pkt_util.h"
#include "hh_nvs.h"
#include "hh_spi.h"
#include "prompts_uri.h"

static const char *TAG = "onepass";

static void set_socket_timeout(int socket, uint32_t timeout_usec);
extern void open_gate_and_start_keeping_timer(void);
extern void rpt_gate_status(onepass_opcode_t opcode);
extern void set_new_param(gate_param_t * p_new_param);

extern void ff_blink_Rled(int duty, int period_ms, int count);
extern void ff_blink_Gled(int duty, int period_ms, int count);

extern uint32_t global_eth_timestamp;

extern bool is_ETH_connected;

extern gate_param_t g_gate_param;

struct sockaddr_in onepassServerAddr;
QueueHandle_t qhandle_to_OnepassServer;

bool onepass_tcp_client_is_running;
bool req_to_kill_tcp_client;

bool onepass_server_is_connected;

static uint16_t m_onepass_tid = 0;

uint32_t g_site_code = SITE_ID_NOT_ASSIGNED;

extern ip_thru_ble_t frcd_ip;

QueueHandle_t qhandle_skip_list;
uint32_t onepass_heartbeat_timestamp;

extern bool LEARNING_MODE;
extern int g_current_max_face;
extern void ff_play_prompt(tone_type_t type);
#ifdef PP_VOICE_FACE_PSRAM_RELEASE
extern void reset_face_storage(int new_max_count);
#endif
static volatile bool waiting_for_status_rpt_ack = false;

static void set_socket_timeout(int socket, uint32_t timeout_usec)
{
	struct timeval read_timeout;
	read_timeout.tv_sec = timeout_usec/1000000;
	read_timeout.tv_usec = timeout_usec%1000000;
	setsockopt(socket, SOL_SOCKET, SO_RCVTIMEO, &read_timeout, sizeof(read_timeout));
}

static char * str_slm_opcode(slm_opcode_t opcode)
{
	switch(opcode)
	{
	case SLM_FACE_READY :			return "$SLM_FACE_READY";
	case SLM_VOICE_READY :			return "$SLM_VOICE_READY";
	case SLM_FACE_LEARN_START :		return "$SLM_FACE_LEARN_START";
	case SLM_VOICE_LEARN_START :	return "$SLM_VOICE_LEARN_START";
	case SLM_RECOG_RESULT :			return "$SLM_RECOG_RESULT";
	case SLM_GATE_CONTROL :			return "$SLM_GATE_CONTROL";
	case SLM_FACE_LEARN_OK :		return "$SLM_FACE_LEARN_OK";
	case SLM_VOICE_LEARN_OK :		return "$SLM_VOICE_LEARN_OK";
	default : 						return NULL;
	}
}

static char * str_ent_req_type(terminal_type_t type)
{
	switch(type)
	{
	case TERMINAL_TYPE_SMART_PHONE :	return "&TERMINAL_TYPE_SMART_PHONE";
	case TERMINAL_TYPE_SMART_BAND :		return "&TERMINAL_TYPE_SMART_BAND";
	case TERMINAL_TYPE_NFC :			return "&TERMINAL_TYPE_NFC";
	case TERMINAL_TYPE_SMART_KEY :		return "&TERMINAL_TYPE_SMART_KEY";
	case TERMINAL_TYPE_CAR_LOCATOR :	return "&TERMINAL_TYPE_CAR_LOCATOR";
	default : 							return "\033[1;31m&WRONG_TERMINAL_TYPE\033[1;37m";
	}
}

static char * str_onepass_opcode(onepass_opcode_t opcode)
{
	switch(opcode)
	{
	case ONEPASS_PARKING_LOC_PUT_REQ :	return "*ONEPASS_PARKING_LOC_PUT_REQ";
	case ONEPASS_PARKING_LOC_PUT_ACK :	return "*ONEPASS_PARKING_LOC_PUT_ACK";
	case ONEPASS_PARKING_LOC_GET_REQ :	return "*ONEPASS_PARKING_LOC_GET_REQ";
	case ONEPASS_PARKING_LOC_GET_ACK :	return "*ONEPASS_PARKING_LOC_GET_ACK";
	case ONEPASS_PARKING_LOC_RMV_REQ :	return "*ONEPASS_PARKING_LOC_RMV_REQ";
	case ONEPASS_PARKING_LOC_RMV_ACK :	return "*ONEPASS_PARKING_LOC_RMV_ACK";
	case ONEPASS_GATE_ENT_REQ :			return "*ONEPASS_GATE_ENT_REQ";
	case ONEPASS_GATE_ENT_ACK :			return "*ONEPASS_GATE_ENT_ACK";
	case ONEPASS_GATE_STATUS_RPT :		return "*ONEPASS_GATE_STATUS_RPT";
	case ONEPASS_GATE_STAUS_RPT_RCVD :	return "*ONEPASS_GATE_STAUS_RPT_RCVD";
	case ONEPASS_GATE_STATUS_GET_REQ :	return "*ONEPASS_GATE_STATUS_GET_REQ";
	case ONEPASS_GATE_STATUS_GET_ACK :	return "*ONEPASS_GATE_STATUS_GET_ACK";
	case ONEPASS_GATE_PARAM_SET_REQ :	return "*ONEPASS_GATE_PARAM_SET_REQ";
	case ONEPASS_GATE_PARAM_SET_ACK :	return "*ONEPASS_GATE_PARAM_SET_ACK";
	case ONEPASS_GATE_PARAM_GET_REQ :	return "*ONEPASS_GATE_PARAM_GET_REQ";
	case ONEPASS_GATE_PARAM_GET_ACK :	return "*ONEPASS_GATE_PARAM_GET_ACK";
	case ONEPASS_RESI_POS_RPT :			return "*ONEPASS_RESI_POS_RPT";
	case ONEPASS_RESI_POS_RPT_RCVD :	return "*ONEPASS_RESI_POS_RPT_RCVD";
	case ONEPASS_NOTICE_MESSAGE_REQ :	return "*ONEPASS_NOTICE_MESSAGE_REQ";
	case ONEPASS_NOTICE_MESSAGE_ACK :	return "*ONEPASS_NOTICE_MESSAGE_ACK";
	case ONEPASS_SITE_CODE_REQ :		return "*ONEPASS_SITE_CODE_REQ";
	case ONEPASS_SITE_CODE_ACK :		return "*ONEPASS_SITE_CODE_ACK";
	case ONEPASS_TCP_HEARTBEAT :		return "*ONEPASS_TCP_HEARTBEAT";
	case ONEPASS_TCP_HEARTBEAT_ACK :	return "*ONEPASS_TCP_HEARTBEAT_ACK";

	case ONEPASS_INDICATE_SLM_LEARN :	return "*ONEPASS_INDICATE_SLM_LEARN";
	case ONEPASS_INDICATE_SLM :			return "*ONEPASS_INDICATE_SLM";
	case ONEPASS_INDICATE_DB_MANAGE :	return "*ONEPASS_INDICATE_DB_MANAGE";
	case ONEPASS_INDICATE_AIR_MON :		return "*ONEPASS_INDICATE_AIR_MON";
	
	default :							return "\033[1;31m*WRONG_ONEPASS\033[1;37m";
	}
}

void menu_set_site_code(void)
{
	uint32_t org_code = g_site_code;
	int code = 0;

	CC_LOGNI("\n************** Set Site Code ***************\n");

	while(1)
	{
		CC_LOGNI("?? Present Site Code : \033[1;32m%lu\033[1;37m ==> New ID : ", org_code);
		code = GetPositiveInteger();
		if(code < 0)
		{
			if(code) {
				CC_LOGNI("!! Cancelled. Return to upper menu...\n\n");
				return;
			}
			else {
				CC_LOGNI("!! Wrong input. Input again.\n\n");
				continue;
			}
		}

		if(g_site_code != code) {
			g_site_code = code;
			nvs_add(SEL_NVS0, NVS0_SITE_CODE);
		}

		CC_LOGNI("\n!! Site Code is set successfully : \033[1;32m%lu\033[1;37m --> \033[1;32m%lu\033[1;37m\n", org_code, g_site_code);

		return;
	}
}

static void prt_onepass_pkt(onepass_pkt_t * p_pkt, bool tx)
{
	switch(p_pkt->header.opcode)
	{
	case ONEPASS_INDICATE_SLM_LEARN :
	case ONEPASS_INDICATE_SLM :
	{
		slm_opcode_t slm_opcode = p_pkt->slm_payload.opcode;
		CC_LOGN("\n======\n\033[0;32mOnePass TCP Client %s\033[0;37m ", tx ? "TX" : "RX");
		CC_LOGN("%s, len %u\n", str_onepass_opcode(p_pkt->header.opcode), p_pkt->header.len);
		CC_LOGN("SLM# 0x%02X(%u), SLM Opcode %s\n", p_pkt->slm_payload.ch, p_pkt->slm_payload.ch, str_slm_opcode(slm_opcode));
		if(p_pkt->header.len) {
			uint8_t * p_pl = (uint8_t *)&p_pkt->slm_payload;
			CC_LOGN("Data :");
			for(int i=0; i<p_pkt->header.len; i++) {
				CC_LOGN(" %02X", p_pl[i]);
			}
			CC_LOGN("\n");
		}
		break;
	}
	break;

	case ONEPASS_INDICATE_DB_MANAGE :
	case ONEPASS_INDICATE_AIR_MON :
		CC_LOGN("%s, \033[1/31mignored\033[1;37m\n", str_onepass_opcode(p_pkt->header.opcode));
		break;

	case ONEPASS_GATE_ENT_REQ :
		CC_LOGN("\n======\n\033[0;33mOnePass TCP Client TX\033[0;37m ");
		CC_LOGN("%s", str_onepass_opcode(p_pkt->header.opcode));
		CC_LOGN(", TID 0x%04X",	p_pkt->header.tid);
		CC_LOGN(", LM# 0x%02X(%u)", p_pkt->payload.ch, p_pkt->payload.ch);
		CC_LOGN(", TYPE %s", str_ent_req_type(p_pkt->payload.data.gate_ent_req.type));
		if( p_pkt->payload.data.gate_ent_req.type == TERMINAL_TYPE_SMART_PHONE ) {
			if(0x8000&p_pkt->payload.data.gate_ent_req.phone_num.mid) CC_LOGN(", phone# %03u-%03u-%04u",
					p_pkt->payload.data.gate_ent_req.phone_num.top,
					p_pkt->payload.data.gate_ent_req.phone_num.mid,
					p_pkt->payload.data.gate_ent_req.phone_num.btm);
			else CC_LOGN(", phone# %03u-%04u-%04u",
					p_pkt->payload.data.gate_ent_req.phone_num.top,
					p_pkt->payload.data.gate_ent_req.phone_num.mid,
					p_pkt->payload.data.gate_ent_req.phone_num.btm);
			CC_LOGN(", KEY 0x%08lX\n", p_pkt->payload.data.gate_ent_req.key);
		}
		else {
			CC_LOGN(", ID 0x%08lX\n", p_pkt->payload.data.gate_ent_req.id);
		}
		break;

	case ONEPASS_GATE_ENT_ACK :
		CC_LOGN("\n======\n\033[0;32mOnePass TCP Client RX\033[0;37m ");
		CC_LOGN("%s", str_onepass_opcode(p_pkt->header.opcode));
		CC_LOGN(", LM# 0x%02X(%u)", p_pkt->payload.ch, p_pkt->payload.ch);
		CC_LOGN(", Result %s\n", p_pkt->payload.data.gate_ent_ack.result ? "FAIL" : "PASS");
		break;

	case ONEPASS_GATE_STATUS_RPT :
		CC_LOGN("\n======\n\033[0;33mOnePass TCP Client TX\033[0;37m ");
		CC_LOGN("%s", str_onepass_opcode(p_pkt->header.opcode));
		CC_LOGN(", LM# 0x%02X(%u)", p_pkt->payload.ch, p_pkt->payload.ch);
		CC_LOGN(", State %s\n", p_pkt->payload.data.gate_status_rpt.state ? "CLOSED" : "OPEN");
		break;

	case ONEPASS_GATE_STAUS_RPT_RCVD :
	case ONEPASS_GATE_PARAM_SET_ACK :
	case ONEPASS_GATE_STATUS_GET_REQ :
	case ONEPASS_GATE_PARAM_GET_REQ :
		CC_LOGN("\n======\n\033[0;32mOnePass TCP Client RX\033[0;37m ");
		CC_LOGN("%s", str_onepass_opcode(p_pkt->header.opcode));
		CC_LOGN(", LM# 0x%02X(%u)\n", p_pkt->payload.ch, p_pkt->payload.ch);
		break;

	case ONEPASS_GATE_STATUS_GET_ACK :
		CC_LOGN("\n======\n\033[0;33mOnePass TCP Client TX\033[0;37m ");
		CC_LOGN("%s", str_onepass_opcode(p_pkt->header.opcode));
		CC_LOGN(", LM# 0x%02X(%u)\n", p_pkt->payload.ch, p_pkt->payload.ch);
		CC_LOGN(", State %s\n", p_pkt->payload.data.gate_status_get_ack.state ? "CLOSED" : "OPEN");
		break;

	case ONEPASS_GATE_PARAM_SET_REQ :
		CC_LOGN("\n======\n\033[0;32mOnePass TCP Client RX\033[0;37m ");
		CC_LOGN("%s", str_onepass_opcode(p_pkt->header.opcode));
		CC_LOGN(", LM# 0x%02X(%u)", p_pkt->payload.ch, p_pkt->payload.ch);
		CC_LOGN(", RSSI %d %%", p_pkt->payload.data.gate_param_set_req.param.sensitivity);
		CC_LOGN(", OpenSec %d secs", p_pkt->payload.data.gate_param_set_req.param.open_duration);
		CC_LOGN(", Sensor %s\n", p_pkt->payload.data.gate_param_set_req.param.radar_enable ? "Enable" : "Disable");
		break;

	case ONEPASS_GATE_PARAM_GET_ACK :
		CC_LOGN("\n======\n\033[0;33mOnePass TCP Client TX\033[0;37m ");
		CC_LOGN("%s", str_onepass_opcode(p_pkt->header.opcode));
		CC_LOGN(", LM# 0x%02X(%u)", p_pkt->payload.ch, p_pkt->payload.ch);
		CC_LOGN(", RSSI %d %%", p_pkt->payload.data.gate_param_set_req.param.sensitivity);
		CC_LOGN(", OpenSec %d secs", p_pkt->payload.data.gate_param_set_req.param.open_duration);
		CC_LOGN(", Sensor %s\n", p_pkt->payload.data.gate_param_set_req.param.radar_enable ? "Enable" : "Disable");
		break;

	case ONEPASS_SITE_CODE_ACK :
		CC_LOGN("\n======\n\033[0;32mOnePass TCP Client RX\033[0;37m ");
		CC_LOGN("%s", str_onepass_opcode(p_pkt->header.opcode));
		CC_LOGN(", Site Code : 0x%08lX\n", p_pkt->site_code);
		break;

	case ONEPASS_TCP_HEARTBEAT :
	case ONEPASS_TCP_HEARTBEAT_ACK :
		break;

	default :
		CC_LOGN("OnePass TCP Client %s(0x%04x)", str_onepass_opcode(p_pkt->header.opcode), p_pkt->header.opcode);
		CC_LOGN("\n");
		break;
	}
}

static void exchange_header_BELE(onepass_header_t * p_header)
{
	reverse_32bit(&p_header->pid);
	reverse_16bit(&p_header->opcode);
	reverse_16bit(&p_header->tid);
}

static void conv_onepass_LEtoBE(onepass_pkt_t * p_pkt)
{
	onepass_opcode_t opcode;
	opcode = p_pkt->header.opcode;		// opcode before exchange

	exchange_header_BELE(&p_pkt->header);
	switch(opcode)
	{
	case ONEPASS_PARKING_LOC_PUT_REQ :
	case ONEPASS_PARKING_LOC_RMV_REQ :
		if(p_pkt->payload.data.park_loc_put_req.type != PARK_LOC_TYPE_CAR_NUMBER) {			// various id in uint32_t
			reverse_32bit(&p_pkt->payload.data.park_loc_put_req.id_u32);
		}
		break;

	case ONEPASS_PARKING_LOC_GET_REQ :
		break;

	case ONEPASS_GATE_ENT_REQ :
		if(p_pkt->payload.data.gate_ent_req.type == TERMINAL_TYPE_SMART_PHONE) {	// 0x02 SmartBand, 0x03 NFC, 0x04 SmartKey
			reverse_16bit(&p_pkt->payload.data.gate_ent_req.phone_num.mid);
			reverse_16bit(&p_pkt->payload.data.gate_ent_req.phone_num.btm);
			reverse_32bit(&p_pkt->payload.data.gate_ent_req.key);
		}
		else {														// 0x02 SmartBand, 0x03 NFC, 0x04 SmartKey
			reverse_32bit(&p_pkt->payload.data.gate_ent_req.id);
		}
		break;

	case ONEPASS_RESI_POS_RPT :
		if(p_pkt->payload.data.resi_pos_rpt.type == TERMINAL_TYPE_SMART_PHONE) {
			reverse_16bit(&p_pkt->payload.data.resi_pos_rpt.phone_num.mid);
			reverse_16bit(&p_pkt->payload.data.resi_pos_rpt.phone_num.btm);
		}
		else {
			reverse_32bit(&p_pkt->payload.data.resi_pos_rpt.id);	//0x02 SmartBand, ___, 0x04 SmartKey
		}
		break;

	default :
		break;
	}
}

static void conv_onepass_BEtoLE(onepass_pkt_t * p_pkt)
{
	exchange_header_BELE(&p_pkt->header);

	if(p_pkt->header.opcode == ONEPASS_SITE_CODE_ACK) {
		reverse_32bit(&p_pkt->site_code);
	}
}

extern void voice_record_start(void);
static uint8_t recent_learn_data[15] = {0, };

void send_slm_packet_toPC(slm_opcode_t opcode, bool isLearn)
{
	onepass_pkt_t onepass_pkt = { 0, };

	if(isLearn) {
		onepass_pkt.header.opcode	= ONEPASS_INDICATE_SLM_LEARN;
		onepass_pkt.header.len		= 0x11;	//
		onepass_pkt.slm_payload.ch	= 0x00; // fixed as 100 for Stand-alone LM
		memcpy(&onepass_pkt.slm_payload.data, recent_learn_data, 15);
	}
	else {
		onepass_pkt.header.opcode	= ONEPASS_INDICATE_SLM;
		onepass_pkt.header.len		= 3;	//
		onepass_pkt.slm_payload.ch	= PP_STANDALONE_CHANNEL_NUMBER; // fixed as 100 for Stand-alone LM
	}

	onepass_pkt.slm_payload.opcode = opcode;

	if(onepass_tcp_client_is_running) {
		xQueueSendToBack( qhandle_to_OnepassServer, &onepass_pkt, pdMS_TO_TICKS(10) );
	}
}

static void process_pkt_fr_onepass_server(onepass_pkt_t * p_pkt)
{
	onepass_pkt_t onepass_pkt = { 0, };

	switch(p_pkt->header.opcode)
	{
	case ONEPASS_TCP_HEARTBEAT_ACK :	// heartbeat_ack is NOT defined.
		break;

	case ONEPASS_SITE_CODE_ACK :
		if(g_site_code != p_pkt->site_code) {
			g_site_code = p_pkt->site_code;
			nvs_add(SEL_NVS0, NVS0_SITE_CODE);
		}
		break;

	case ONEPASS_GATE_ENT_ACK :
		if(p_pkt->payload.ch != PP_STANDALONE_CHANNEL_NUMBER) {
			CC_LOGN("!! Error, Chnnel Number MUST be 100.\n");
			break;
		}

		if(p_pkt->payload.data.gate_ent_ack.result == 0) {
#ifdef PP_REBOOT_WHEN_NO_ONEPASS_GATE_ENT_ACK_FOR_LONG_TIME
			gate_ent_ack_timestamp = xTaskGetTickCount();
#endif
			ff_blink_Gled(100, 150, 3);
			open_gate_and_start_keeping_timer();
		}
		else {
			ff_blink_Rled(100, 150, 3);
		}

		break;

	case ONEPASS_GATE_STAUS_RPT_RCVD :
		waiting_for_status_rpt_ack = false;
		break;

	case ONEPASS_GATE_STATUS_GET_REQ :
		rpt_gate_status(ONEPASS_GATE_STATUS_GET_ACK);
		break;

	case ONEPASS_GATE_PARAM_SET_REQ :
		if(p_pkt->payload.ch != PP_STANDALONE_CHANNEL_NUMBER) {
			CC_LOGN("!! Error, Chnnel Number MUST be 100.\n");
			break;
		}

		onepass_pkt.header.opcode	= ONEPASS_GATE_PARAM_SET_ACK;
		onepass_pkt.header.len		= sizeof(gate_param_set_ack_t)+1;
		onepass_pkt.payload.ch		= PP_STANDALONE_CHANNEL_NUMBER; // fixed as 100 for Stand-alone LM
		if(onepass_tcp_client_is_running) {
			xQueueSendToBack( qhandle_to_OnepassServer, &onepass_pkt, pdMS_TO_TICKS(10) );
		}

		gate_param_t new_param = {0, };
		new_param.sensitivity = p_pkt->payload.data.gate_param_set_req.param.sensitivity;
		new_param.open_duration = p_pkt->payload.data.gate_param_set_req.param.open_duration;
		new_param.radar_enable = p_pkt->payload.data.gate_param_set_req.param.radar_enable;
		set_new_param(&new_param);
		break;

	case ONEPASS_GATE_PARAM_GET_REQ :
		if(p_pkt->payload.ch != PP_STANDALONE_CHANNEL_NUMBER) {
			CC_LOGN("!! Error, Chnnel Number MUST be 100.\n");
			break;
		}

		onepass_pkt.header.opcode	= ONEPASS_GATE_PARAM_GET_ACK;
		onepass_pkt.header.len		= sizeof(gate_param_get_ack_t)+1;
		onepass_pkt.payload.ch		= PP_STANDALONE_CHANNEL_NUMBER; // fixed as 100 for Stand-alone LM
		memcpy(&onepass_pkt.payload.data.gate_param_get_ack.param, &g_gate_param, sizeof(gate_param_t));
		if(onepass_tcp_client_is_running) {
			xQueueSendToBack( qhandle_to_OnepassServer, &onepass_pkt, pdMS_TO_TICKS(10) );
		}
		break;

	case ONEPASS_INDICATE_SLM_LEARN :
	{
		slm_opcode_t opcode = p_pkt->slm_payload.opcode;

		if(LEARNING_MODE) {
			if(opcode == SLM_VOICE_LEARN_START) {
				memcpy(recent_learn_data, &p_pkt->slm_payload.data, 15);
                ESP_LOGI(TAG, "Voice Learn Start -> Freeing Face Memory");
				voice_record_start();
			}
			else if(opcode == SLM_FACE_LEARN_START) {
				memcpy(recent_learn_data, &p_pkt->slm_payload.data, 15);
				request_spi_magic_to_master(SPI_LEARN_START);
				ff_play_prompt(TONE_TYPE_FSTART);
			}
		}
		else {
			CC_LOGN("!! Received learn command while NOT in LEARNING_MODE. Ignored.\n");
		}
		break;
	}

	case ONEPASS_INDICATE_SLM :
	{
		slm_opcode_t opcode = p_pkt->slm_payload.opcode;
		if(!LEARNING_MODE) {
			if(opcode == SLM_GATE_CONTROL) {
				bool result = p_pkt->slm_payload.data[0];
				if(result)	{
					CC_LOGN("!! SLM_GATE_CONTROL : OPEN command received.\n");
					ff_blink_Gled(100, 150, 3);
					open_gate_and_start_keeping_timer();
				}
				else {
					CC_LOGN("!! SLM_GATE_CONTROL : Access Denied.\n");
					ff_blink_Rled(100, 150, 3);
				}
			}
			request_spi_magic_to_master(SPI_VOICE_STOPPED);
		}
		else {
			CC_LOGN("!! Received SLM command while in LEARNING_MODE(opcode %s). Ignored.\n", str_slm_opcode(opcode));
		}
		break;
	}

	default :
		CC_LOGN("NOT supported opcode : 0x%04X\n", p_pkt->header.opcode);
		break;
	}
}

static void task_onepass_tcp_client(void * arg)
{
	onepass_tcp_client_is_running = true;
	ESP_LOGI(TAG, "\033[1;36mOnePass TCP_Client Task has started.\033[1;37m");

#if CHECK_ONEPASS_RCV_TIMEOUT
	uint32_t timestamp_onepass_last_rx = 0;
#endif

	qhandle_skip_list	= xQueueCreate( 32, sizeof(skip_list_t) );

	while ( !req_to_kill_tcp_client && is_ETH_connected )
	{
		int sock = -1;
		while(1) {
			sock =  socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
			if (sock < 0) {
				if( req_to_kill_tcp_client || !is_ETH_connected ) break;
				ESP_LOGE(TAG, "TCP Client : Unable to create socket: errno %d.", errno);
			}
			else {
				int err = connect(sock, (struct sockaddr *)&onepassServerAddr, sizeof(onepassServerAddr));
				if (err != 0) {
					if( req_to_kill_tcp_client || !is_ETH_connected ) break;
					shutdown(sock, 0);
					close(sock);
				}
				else {
					set_socket_timeout(sock, 50*1000);	// 50 msec
					onepass_server_is_connected = true;
					uint32_t addr = onepassServerAddr.sin_addr.s_addr;
					char str[INET_ADDRSTRLEN] = {0, };
					inet_ntop(AF_INET, &addr, str, INET_ADDRSTRLEN);
					CC_LOGNI("\033[1;33m!! TCP Connected to ONEPASS Server (%s:%d)\n\033[1;37m",
								ETH_ONEPASS_MONITORING_SERVER_IP_STR, ETH_ONEPASS_TCP_SERVER_PORT);

#if CHECK_ONEPASS_RCV_TIMEOUT
					timestamp_onepass_last_rx = xTaskGetTickCount();
#endif
					onepass_heartbeat_timestamp = 0;
					break;
				}
			}
			vTaskDelay(pdMS_TO_TICKS(10));
		}

		while ( !req_to_kill_tcp_client && is_ETH_connected ) {
			if ( uxQueueMessagesWaiting(qhandle_to_OnepassServer) ) {
				skip_list_t tmp_pkt;
				onepass_pkt_t tx = { 0, };
				xQueueReceive( qhandle_to_OnepassServer, &tx, pdMS_TO_TICKS(1) );

				UBaseType_t skip_count = uxQueueMessagesWaiting(qhandle_skip_list);
				if( skip_count ) {
					bool overlapped = false;
					for(int i=0; i<skip_count; i++) {
						xQueueReceive( qhandle_skip_list, &tmp_pkt, pdMS_TO_TICKS(1) );
						if(!is_Time_Elapsed(tmp_pkt.timestamp, pdMS_TO_TICKS(3000))) {
							xQueueSendToBack( qhandle_skip_list, &tmp_pkt, pdMS_TO_TICKS(1) );	// re-enqueue if the pkt is NOT old
							if(!overlapped && !memcmp(&tmp_pkt.onepass_pkt, &tx, sizeof(onepass_pkt_t))) {
								overlapped = true;
							}
						}
						else {
							if(waiting_for_status_rpt_ack && (ONEPASS_GATE_STATUS_RPT == tmp_pkt.onepass_pkt.header.opcode)) {
								CC_LOGN("status report again because the server didn't respond.\n");
								rpt_gate_status(ONEPASS_GATE_STATUS_RPT);	//
							}
						}
					}
					if(overlapped) continue;	// Skip because the same packet is already sent within 3 secs.
				}

				tmp_pkt.timestamp = xTaskGetTickCount();
				memcpy(&tmp_pkt.onepass_pkt, &tx, sizeof(onepass_pkt_t));
				xQueueSendToBack( qhandle_skip_list, &tmp_pkt, pdMS_TO_TICKS(1) );	// add new packet into skip list

				if(ONEPASS_GATE_STATUS_RPT == tmp_pkt.onepass_pkt.header.opcode) {
					waiting_for_status_rpt_ack = true;
				}

				tx.header.pid = 0xFFFF55AA;
				if(tx.header.tid == 0) tx.header.tid = m_onepass_tid++;
				prt_onepass_pkt(&tx, 1);

				conv_onepass_LEtoBE(&tx);

				uint8_t len_tx = sizeof(onepass_header_t) + tx.header.len;
				int err = send(sock, &tx, len_tx, 0);
				if (err < 0) {
					ESP_LOGE(TAG, "Error occured during sending: errno %d.", errno);
					break;
				}
				else {
					global_eth_timestamp = xTaskGetTickCount();
				}
			}

			onepass_pkt_t rx = { 0, };
			int len_rx = recv(sock, &rx, sizeof(onepass_pkt_t), 0);
			// Error occured during receiving
			if ( len_rx < 0 ) {
#if CHECK_ONEPASS_RCV_TIMEOUT
				if( errno != EAGAIN || is_Time_Elapsed(timestamp_onepass_last_rx, pdMS_TO_TICKS(ONEPASS_HEARTBEAT_TIME_MS))) {
					if(errno != EAGAIN) CC_LOGE("recv failed: errno %d\n", errno);
					else {
						CC_LOGNI("No signal from ONEPASS Server for %d secs. Re-create a socket.\n", ONEPASS_HEARTBEAT_TIME_MS/1000);
					}
					break;
				}
#else
				if( errno != EAGAIN) {
					CC_LOGE("recv failed: errno %d\n", errno);
					break;
				}
#endif
			}
			// Data received
			else if( len_rx > 0) {

				conv_onepass_BEtoLE(&rx);
				if(rx.header.len>128) rx.header.len = 128;	// restrict max. length of notice_message is 128

				if(rx.header.opcode == ONEPASS_PARKING_LOC_PUT_ACK || rx.header.opcode == ONEPASS_PARKING_LOC_GET_ACK) { //exclude park_time
					rx.header.len = 22;	// ch, result, park_loc
				}

				global_eth_timestamp = xTaskGetTickCount();
#if CHECK_ONEPASS_RCV_TIMEOUT
				timestamp_onepass_last_rx = xTaskGetTickCount();
#endif

				prt_onepass_pkt(&rx, 0);
				process_pkt_fr_onepass_server(&rx);
			}
			else {
				ESP_LOGW(TAG, "!! Received length is zero.");
				break;
			}

			vTaskDelay( pdMS_TO_TICKS(10) );
		}

		if (sock != -1) {
			ESP_LOGI(TAG, "!! Shutting down socket and restarting...");
			shutdown(sock, 0);
			close(sock);
		}
	}

	vQueueDelete(qhandle_to_OnepassServer);
	vQueueDelete(qhandle_skip_list);

	onepass_server_is_connected = false;
	onepass_tcp_client_is_running = false;
	req_to_kill_tcp_client = false;
	vTaskDelete(NULL);
}

#ifdef PP_ONLY_FOR_DEMO
#define DATA_SERVER_IP      "10.100.100.37"
#define DATA_SERVER_PORT    25003

static bool data_server_client_running = false;
static bool req_kill_data_server_client = false;
static QueueHandle_t qhandle_to_DataServer;
bool skip_sending_data_server_packet = false;

void send_slm_packet_toDataServer(slm_opcode_t opcode)
{
	if(skip_sending_data_server_packet) {
		return;
	}
	CC_LOGN("\n%s: SLM Opcode %s\n", __func__, str_slm_opcode(opcode));
	onepass_pkt_t onepass_pkt = { 0, };
	
	onepass_pkt.header.opcode	= ONEPASS_INDICATE_SLM;
	onepass_pkt.header.len		= 3;	//
	onepass_pkt.slm_payload.ch	= PP_STANDALONE_CHANNEL_NUMBER; // fixed as 100 for Stand-alone LM

	onepass_pkt.slm_payload.opcode = opcode;

    if (data_server_client_running) {
        if (qhandle_to_DataServer != NULL) {
            xQueueSendToBack(qhandle_to_DataServer, &onepass_pkt, pdMS_TO_TICKS(10));
        }
    }
}

static void process_pkt_fr_data_server(onepass_pkt_t * p_pkt)
{
	if(LEARNING_MODE) {
		CC_LOGN("!! Ignore All commands while in LEARNING_MODE.\n");
		return;
	}

	slm_opcode_t opcode = p_pkt->slm_payload.opcode;
	if(p_pkt->header.opcode == ONEPASS_INDICATE_SLM) {
		uint8_t len = p_pkt->header.len;
		uint8_t ch = p_pkt->slm_payload.ch;
		char * p_str = str_slm_opcode(opcode);
		CC_LOGN("\n======\n\033[0;32mData Server Client RX\033[0;37m ");
		CC_LOGN(" ch 0x%02X(%u), SLM Opcode %s(0x%02X), len %u\n", ch, ch, p_str?p_str:"Wrong", opcode, len);
		if(len > 2) {
			CC_LOGN("Data : ");
			uint8_t * data = p_pkt->slm_payload.data;
			for(int i=2; i<len; i++) {
				CC_LOGN(" %02X", data[i]);
			}
			CC_LOGN("\n");
		}
		if(opcode == SLM_GATE_CONTROL) {
			bool result = p_pkt->slm_payload.data[0];
			if(result)	{
				CC_LOGN("!! SLM_GATE_CONTROL : OPEN_REQ Accepted.\n");
				ff_blink_Gled(100, 150, 3);
				open_gate_and_start_keeping_timer();
			}
			else {
				CC_LOGN("!! SLM_GATE_CONTROL : OPEN_REQ Denied.\n");
				ff_blink_Rled(100, 150, 3);
			}
		}
	}
	else {
		CC_LOGN("!! Received unexpected command(0x%04X), opcode(%s).\n", p_pkt->header.opcode, str_slm_opcode(opcode));
	}
}

// [추가] Data Server 통신 태스크
static void task_data_server_client(void * arg)
{
    data_server_client_running = true;
    ESP_LOGI(TAG, "Data Server Demo Client Task Started. Target: %s:%d", DATA_SERVER_IP, DATA_SERVER_PORT);

    // 큐 생성
    qhandle_to_DataServer = xQueueCreate(16, sizeof(onepass_pkt_t));
    
    struct sockaddr_in dest_addr;
    dest_addr.sin_addr.s_addr = inet_addr(DATA_SERVER_IP);
    dest_addr.sin_family = AF_INET;
    dest_addr.sin_port = htons(DATA_SERVER_PORT);

    while (!req_kill_data_server_client && is_ETH_connected) {
		if(skip_sending_data_server_packet) {
			vTaskDelay(pdMS_TO_TICKS(1000));
			continue;
		}
        int sock = socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
        if (sock < 0) {
            ESP_LOGE(TAG, "DataServer: Unable to create socket: errno %d", errno);
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }

        ESP_LOGI(TAG, "DataServer: Connecting to %s:%d...", DATA_SERVER_IP, DATA_SERVER_PORT);
        int err = connect(sock, (struct sockaddr *)&dest_addr, sizeof(dest_addr));
        if (err != 0) {
            ESP_LOGW(TAG, "DataServer: Socket unable to connect: errno %d", errno);
            close(sock);
            vTaskDelay(pdMS_TO_TICKS(2000)); // 재연결 대기
            continue;
        }

        ESP_LOGI(TAG, "\033[1;35mDataServer: Successfully connected\033[1;37m");

        // [중요] 소켓을 Non-blocking 모드로 설정 (수신 대기 중에도 송신 큐 처리를 위해)
        int flags = fcntl(sock, F_GETFL, 0);
        fcntl(sock, F_SETFL, flags | O_NONBLOCK);

        while (!req_kill_data_server_client && is_ETH_connected) {
            
            // 1. 데이터 수신 (Non-blocking)
			onepass_pkt_t rx = { 0, };
			int len_rx = recv(sock, &rx, sizeof(onepass_pkt_t), 0);

			if(rx.header.len>128) rx.header.len = 128;	// restrict max. length of notice_message is 128
			if ( len_rx < 0 ) {
				if( errno != EAGAIN) {
					CC_LOGE("recv failed: errno %d\n", errno);
					break;
				}
			}
			// Data received
			else if( len_rx > 0) {
				CC_LOGN("Raw Data from DataServer(BE) \n");
				for(int i=0; i<len_rx; i++) {
					CC_LOGN(" %02X", ((uint8_t*)&rx)[i]);
					if(i && i%16 == 0) CC_LOGN("\n");
				}
				CC_LOGN("\n");

				conv_onepass_BEtoLE(&rx);
				process_pkt_fr_data_server(&rx);
			}
			else {
				ESP_LOGW(TAG, "!! Received length is zero.");
				break;
			}

			vTaskDelay( pdMS_TO_TICKS(10) );

            // 2. 데이터 송신 (Queue Check)
            if (qhandle_to_DataServer != NULL) {
                onepass_pkt_t tx_pkt;
                // 큐에 보낼 데이터가 있는지 확인 (즉시 리턴)
                if (xQueueReceive(qhandle_to_DataServer, &tx_pkt, 0) == pdTRUE) {
					tx_pkt.header.pid = 0xFFFF55AA;
					tx_pkt.header.tid = m_onepass_tid++;
									
                    // 엔디안 변환 (필요 시)
                    conv_onepass_LEtoBE(&tx_pkt); 

                    // 전체 패킷 크기 계산 (헤더 + 페이로드)
                    // 여기선 간단히 전체 구조체 사이즈 또는 프로토콜 정의에 따라 전송
                    // 실제 onepass 프로토콜은 헤더 + 가변길이 바디
                    int total_len = sizeof(onepass_header_t) + tx_pkt.header.len; 

                    int sent = send(sock, &tx_pkt, total_len, 0);
                    if (sent < 0) {
                        ESP_LOGE(TAG, "DataServer: send failed errno %d", errno);
                        break; // 송신 실패 시 재연결 시도
                    } else {
                        ESP_LOGI(TAG, "DataServer TX: Sent Packet (Op:0x%04X, Len:%d)", 
                                 tx_pkt.header.opcode, sent);
                    }
                }
            }

            // CPU 점유를 낮추기 위한 약간의 딜레이
            vTaskDelay(pdMS_TO_TICKS(10));
        }

        if (sock != -1) {
            shutdown(sock, 0);
            close(sock);
        }
        
        if (!req_kill_data_server_client) {
             ESP_LOGI(TAG, "DataServer: Reconnecting in 3 seconds...");
             vTaskDelay(pdMS_TO_TICKS(3000));
        }
    }

    // 종료 처리: 큐 삭제 및 플래그 정리
    if (qhandle_to_DataServer != NULL) {
        vQueueDelete(qhandle_to_DataServer);
        qhandle_to_DataServer = NULL;
    }

    data_server_client_running = false;
    req_kill_data_server_client = false;
    vTaskDelete(NULL);
}

void start_data_server_interface_only_for_demo(void)
{
    ESP_LOGI(TAG, "Starting Data Server Interface (Demo Mode)...");

    // 이미 실행 중이면 재시작을 위해 종료
    if (data_server_client_running) {
        req_kill_data_server_client = true;
        while (data_server_client_running) {
            vTaskDelay(pdMS_TO_TICKS(10));
        }
        req_kill_data_server_client = false;
    }

    // 태스크 생성
    BaseType_t res = xTaskCreatePinnedToCore(task_data_server_client, 
                                             "task_data_cli", 
                                             4096, 
                                             NULL, 
                                             5,     // Priority (적절히 조정)
                                             NULL, 
                                             1);    // Core 1 사용

    if (res != pdPASS) {
        ESP_LOGE(TAG, "Failed to create Data Server Client task");
    } else {
        ESP_LOGI(TAG, "Data Server Client task created");
    }
}
#endif

void start_onepass_tcp_client(void)
{
	ESP_LOGI(TAG, "Creating OnePass Client Task");

	if(frcd_ip.rmt_ip[0]) {
		uint32_t * p_ip = (uint32_t*)&frcd_ip.rmt_ip[0];
		onepassServerAddr.sin_addr.s_addr = *p_ip;
	}
	else {
		onepassServerAddr.sin_addr.s_addr = inet_addr(ETH_ONEPASS_MONITORING_SERVER_IP_STR);
	}

	onepassServerAddr.sin_family = AF_INET;
	onepassServerAddr.sin_port = htons(ETH_ONEPASS_TCP_SERVER_PORT);

	if(onepass_tcp_client_is_running) {
		req_to_kill_tcp_client = true;
		ESP_LOGI(TAG, "Killing OnePass Client task...");
		while(onepass_tcp_client_is_running) vTaskDelay(pdMS_TO_TICKS(10));
	}

	qhandle_to_OnepassServer = xQueueCreate( 8, sizeof(onepass_pkt_t) );
	if(!qhandle_to_OnepassServer) ESP_LOGE(TAG, "Queue(qhandle_to_OnepassServer) Create failed...");

	//show_all_heap_info(TAG);

	BaseType_t res;

	res = xTaskCreatePinnedToCore(task_onepass_tcp_client, NAME_TCPC, STACK_TCPC, NULL, PRIORITY_TCPC, NULL, CORE_TCPC);

	if(res != pdPASS) {
		ESP_LOGE(TAG, "Task(task_onepass_tcp_client) Create failed. res %d", res);
	}
	else {
		ESP_LOGI(TAG, "OnePass Client Task is created successfully.");
		show_all_heap_info(TAG);
	}
}
