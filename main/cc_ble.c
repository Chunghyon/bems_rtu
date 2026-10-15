/*
   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/


#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_bt.h"
#include "driver/uart.h"
#include "string.h"

#include "esp_gap_ble_api.h"
#include "esp_gatts_api.h"
#include "esp_bt_defs.h"
#include "esp_bt_main.h"

#include "hh_parameters.h"
#include "hh_nvs.h"
#include "hh_spi.h"
#include "hh_ledc.h"
#include "hh_util.h"

static const char *TAG = "cc_ble";

static char * str_gatts_evt(esp_gatts_cb_event_t evt)
{
	switch(evt)
	{
	case ESP_GATTS_REG_EVT : 				 return "ESP_GATTS_REG_EVT";
	case ESP_GATTS_READ_EVT : 				 return "ESP_GATTS_READ_EVT";
	case ESP_GATTS_WRITE_EVT : 				 return "ESP_GATTS_WRITE_EVT";
	case ESP_GATTS_EXEC_WRITE_EVT : 		 return "ESP_GATTS_EXEC_WRITE_EVT";
	case ESP_GATTS_MTU_EVT : 				 return "ESP_GATTS_MTU_EVT";
	case ESP_GATTS_CONF_EVT : 				 return "ESP_GATTS_CONF_EVT";
	case ESP_GATTS_UNREG_EVT : 				 return "ESP_GATTS_UNREG_EVT";
	case ESP_GATTS_CREATE_EVT : 			 return "ESP_GATTS_CREATE_EVT";
	case ESP_GATTS_ADD_INCL_SRVC_EVT : 		 return "ESP_GATTS_ADD_INCL_SRVC_EVT";
	case ESP_GATTS_ADD_CHAR_EVT : 			 return "ESP_GATTS_ADD_CHAR_EVT";
	case ESP_GATTS_ADD_CHAR_DESCR_EVT : 	 return "ESP_GATTS_ADD_CHAR_DESCR_EVT";
	case ESP_GATTS_DELETE_EVT : 			 return "ESP_GATTS_DELETE_EVT";
	case ESP_GATTS_START_EVT : 				 return "ESP_GATTS_START_EVT";
	case ESP_GATTS_STOP_EVT : 				 return "ESP_GATTS_STOP_EVT";
	case ESP_GATTS_CONNECT_EVT : 			 return "ESP_GATTS_CONNECT_EVT";
	case ESP_GATTS_DISCONNECT_EVT : 		 return "ESP_GATTS_DISCONNECT_EVT";
	case ESP_GATTS_OPEN_EVT : 				 return "ESP_GATTS_OPEN_EVT";
	case ESP_GATTS_CANCEL_OPEN_EVT : 		 return "ESP_GATTS_CANCEL_OPEN_EVT";
	case ESP_GATTS_CLOSE_EVT : 				 return "ESP_GATTS_CLOSE_EVT";
	case ESP_GATTS_LISTEN_EVT : 			 return "ESP_GATTS_LISTEN_EVT";
	case ESP_GATTS_CONGEST_EVT : 			 return "ESP_GATTS_CONGEST_EVT";
	case ESP_GATTS_RESPONSE_EVT : 			 return "ESP_GATTS_RESPONSE_EVT";
	case ESP_GATTS_CREAT_ATTR_TAB_EVT : 	 return "ESP_GATTS_CREAT_ATTR_TAB_EVT";
	case ESP_GATTS_SET_ATTR_VAL_EVT : 		 return "ESP_GATTS_SET_ATTR_VAL_EVT";
	case ESP_GATTS_SEND_SERVICE_CHANGE_EVT : return	"ESP_GATTS_SEND_SERVICE_CHANGE_EVT";
	default	:								 return "NOT_REGISTERED_EVT";
	}
}

extern ip_thru_ble_t frcd_ip;

extern void ff_blink_Rled(int duty, int period_ms, int count);
extern void ff_blink_Gled(int duty, int period_ms, int count);

extern bool cc_Rblink_timer_running;
extern bool cc_Gblink_timer_running;

#define GATTS_TABLE_TAG  "GATTS_SPP_DEMO"

#define SPP_PROFILE_NUM			1
#define SPP_PROFILE_APP_IDX		0
#define ESP_SPP_APP_ID			0x56

#define SAMPLE_DEVICE_NAME		"LOBBYM_SET_ID"
#define SPP_SVC_INST_ID			0

QueueHandle_t qhandle_ios_skip_list;

static const uint8_t spp_adv_data[22] = {
	0x02,0x01,0x06,
	0x03,0x03,0xF0,0xAB,
	0x0E,0x09,'L', 'O', 'B', 'B', 'Y', 'M', '_', 'S', 'E', 'T', '_', 'I', 'D'
};

extern uint32_t g_site_code;
extern QueueHandle_t qhandle_to_OnepassServer;
extern bool onepass_tcp_client_is_running;
extern uint8_t g_lobbymaster_id;
extern gate_param_t g_gate_param;
extern volatile bool front_movement_detected;

static uint16_t spp_mtu_size = 23;
static uint16_t spp_conn_id = 0xffff;
static esp_gatt_if_t spp_gatts_if = 0xff;
QueueHandle_t spp_uart_queue = NULL;

static bool enable_data_ntf = false;
static bool is_connected = false;
static esp_bd_addr_t spp_remote_bda = {0x0,};

static uint16_t spp_handle_table[SPP_IDX_NB];

static esp_ble_adv_params_t spp_adv_params = {
//	.adv_int_min		= 0x20,
//	.adv_int_max		= 0x40,
	.adv_int_min		= 0x0800,	//1.28 second
	.adv_int_max		= 0x0800,	//1.28 second
	.adv_type		   	= ADV_TYPE_IND,
	.own_addr_type	  	= BLE_ADDR_TYPE_PUBLIC,
	.channel_map		= ADV_CHNL_ALL,
	.adv_filter_policy  = ADV_FILTER_ALLOW_SCAN_ANY_CON_ANY,
};

struct gatts_profile_inst {
	esp_gatts_cb_t gatts_cb;
	uint16_t gatts_if;
	uint16_t app_id;
	uint16_t conn_id;
	uint16_t service_handle;
	esp_gatt_srvc_id_t service_id;
	uint16_t char_handle;
	esp_bt_uuid_t char_uuid;
	esp_gatt_perm_t perm;
	esp_gatt_char_prop_t property;
	uint16_t descr_handle;
	esp_bt_uuid_t descr_uuid;
};

typedef struct spp_receive_data_node{
	int32_t len;
	uint8_t * node_buff;
	struct spp_receive_data_node * next_node;
}spp_receive_data_node_t;

static spp_receive_data_node_t * temp_spp_recv_data_node_p1 = NULL;
static spp_receive_data_node_t * temp_spp_recv_data_node_p2 = NULL;

typedef struct spp_receive_data_buff{
	int32_t node_num;
	int32_t buff_size;
	spp_receive_data_node_t * first_node;
}spp_receive_data_buff_t;

static spp_receive_data_buff_t SppRecvDataBuff = {
	.node_num   = 0,
	.buff_size  = 0,
	.first_node = NULL
};

static void gatts_profile_event_handler(esp_gatts_cb_event_t event, esp_gatt_if_t gatts_if, esp_ble_gatts_cb_param_t *param);

/* One gatt-based profile one app_id and one gatts_if, this array will store the gatts_if returned by ESP_GATTS_REG_EVT */
static struct gatts_profile_inst spp_profile_tab[SPP_PROFILE_NUM] = {
	[SPP_PROFILE_APP_IDX] = {
		.gatts_cb = gatts_profile_event_handler,
		.gatts_if = ESP_GATT_IF_NONE,	   /* Not get the gatt_if, so initial is ESP_GATT_IF_NONE */
	},
};

/*
 *  SPP PROFILE ATTRIBUTES
 ****************************************************************************************
 */
#if ORG
/// SPP Service
static const uint16_t spp_service_uuid = 0xABF0;
/// Characteristic UUID
#define ESP_GATT_UUID_SPP_DATA_RECEIVE		0xABF1
#define ESP_GATT_UUID_SPP_DATA_NOTIFY		0xABF2
#define ESP_GATT_UUID_SPP_COMMAND_RECEIVE   0xABF3
#define ESP_GATT_UUID_SPP_COMMAND_NOTIFY	0xABF4
///SPP Service - data receive characteristic, read&write without response
static const uint16_t spp_data_receive_uuid = ESP_GATT_UUID_SPP_DATA_RECEIVE;
static const uint8_t  spp_data_receive_val[20] = {0x00};

///SPP Service - data notify characteristic, notify&read
static const uint16_t spp_data_notify_uuid = ESP_GATT_UUID_SPP_DATA_NOTIFY;
static const uint8_t  spp_data_notify_val[20] = {0x00};
static const uint8_t  spp_data_notify_ccc[2] = {0x00, 0x00};
#else
static const uint8_t spp_service_uuid[16]		= {0xAB, 0xBA, 0xDC, 0x24, 0x0E, 0xE5, 0xA9, 0xE0, 0x93, 0xF3, 0xA3, 0xB5, 0x01, 0x00, 0x40, 0x6E};
static const uint8_t spp_data_receive_uuid[16]	= {0xAB, 0xBA, 0xDC, 0x24, 0x0E, 0xE5, 0xA9, 0xE0, 0x93, 0xF3, 0xA3, 0xB5, 0x02, 0x00, 0x40, 0x6E};
static const uint8_t spp_data_receive_val[20]	= {0x00, };
static const uint8_t spp_data_notify_uuid[16]	= {0xAB, 0xBA, 0xDC, 0x24, 0x0E, 0xE5, 0xA9, 0xE0, 0x93, 0xF3, 0xA3, 0xB5, 0x03, 0x00, 0x40, 0x6E};
static const uint8_t spp_data_notify_val[20]	= {0x00, };
static const uint8_t spp_data_notify_ccc[2]		= {0x00, 0x00};
#endif

#define CHAR_DECLARATION_SIZE   (sizeof(uint8_t))
static const uint16_t primary_service_uuid = ESP_GATT_UUID_PRI_SERVICE;
static const uint16_t character_declaration_uuid = ESP_GATT_UUID_CHAR_DECLARE;
static const uint16_t character_client_config_uuid = ESP_GATT_UUID_CHAR_CLIENT_CONFIG;

static const uint8_t char_prop_read_notify = ESP_GATT_CHAR_PROP_BIT_NOTIFY;
static const uint8_t char_prop_read_write = ESP_GATT_CHAR_PROP_BIT_WRITE_NR|ESP_GATT_CHAR_PROP_BIT_WRITE;

static bool is_connect = false;

///Full HRS Database Description - Used to add attributes into the database
static const esp_gatts_attr_db_t spp_gatt_db[SPP_IDX_NB] =
{
	//SPP -  Service Declaration
	[SPP_IDX_SVC]					  	=
	{{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_16, (uint8_t *)&primary_service_uuid, ESP_GATT_PERM_READ,
	sizeof(spp_service_uuid), sizeof(spp_service_uuid), (uint8_t *)&spp_service_uuid}},

	//SPP -  data receive characteristic Declaration
	[SPP_IDX_SPP_DATA_RECV_CHAR]			=
	{{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_16, (uint8_t *)&character_declaration_uuid, ESP_GATT_PERM_READ,
	CHAR_DECLARATION_SIZE, CHAR_DECLARATION_SIZE, (uint8_t *)&char_prop_read_write}},

	//SPP -  data receive characteristic Value
	[SPP_IDX_SPP_DATA_RECV_VAL]			 	=
	{{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_128, (uint8_t *)&spp_data_receive_uuid, ESP_GATT_PERM_READ|ESP_GATT_PERM_WRITE,
	SPP_DATA_MAX_LEN, sizeof(spp_data_receive_val), (uint8_t *)spp_data_receive_val}},

	//SPP -  data notify characteristic Declaration
	[SPP_IDX_SPP_DATA_NOTIFY_CHAR]  =
	{{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_16, (uint8_t *)&character_declaration_uuid, ESP_GATT_PERM_READ,
	CHAR_DECLARATION_SIZE, CHAR_DECLARATION_SIZE, (uint8_t *)&char_prop_read_notify}},

	//SPP -  data notify characteristic Value
	[SPP_IDX_SPP_DATA_NTY_VAL]   =
	{{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_128, (uint8_t *)&spp_data_notify_uuid, ESP_GATT_PERM_READ,
	SPP_DATA_MAX_LEN, sizeof(spp_data_notify_val), (uint8_t *)spp_data_notify_val}},

	//SPP -  data notify characteristic - Client Characteristic Configuration Descriptor
	[SPP_IDX_SPP_DATA_NTF_CFG]		 =
	{{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_16, (uint8_t *)&character_client_config_uuid, ESP_GATT_PERM_READ|ESP_GATT_PERM_WRITE,
	sizeof(uint16_t),sizeof(spp_data_notify_ccc), (uint8_t *)spp_data_notify_ccc}},
};

static uint8_t find_char_and_desr_index(uint16_t handle)
{
	uint8_t error = 0xff;

	for(int i = 0; i < SPP_IDX_NB ; i++){
		if(handle == spp_handle_table[i]){
			return i;
		}
	}

	return error;
}

static bool store_wr_buffer(esp_ble_gatts_cb_param_t *p_data)
{
    //show_all_heap_info(TAG);
	temp_spp_recv_data_node_p1 = (spp_receive_data_node_t *)malloc(sizeof(spp_receive_data_node_t));

	if(temp_spp_recv_data_node_p1 == NULL){
		ESP_LOGE(TAG, "malloc error %s %d", __func__, __LINE__);
		return false;
	}
	if(temp_spp_recv_data_node_p2 != NULL){
		temp_spp_recv_data_node_p2->next_node = temp_spp_recv_data_node_p1;
	}
	temp_spp_recv_data_node_p1->len = p_data->write.len;
	SppRecvDataBuff.buff_size += p_data->write.len;
	temp_spp_recv_data_node_p1->next_node = NULL;
	temp_spp_recv_data_node_p1->node_buff = (uint8_t *)malloc(p_data->write.len);
	temp_spp_recv_data_node_p2 = temp_spp_recv_data_node_p1;
	memcpy(temp_spp_recv_data_node_p1->node_buff,p_data->write.value,p_data->write.len);
	if(SppRecvDataBuff.node_num == 0){
		SppRecvDataBuff.first_node = temp_spp_recv_data_node_p1;
		SppRecvDataBuff.node_num++;
	}else{
		SppRecvDataBuff.node_num++;
	}

    //show_all_heap_info(TAG);
	return true;
}

static void free_write_buffer(void)
{
	temp_spp_recv_data_node_p1 = SppRecvDataBuff.first_node;

	while(temp_spp_recv_data_node_p1 != NULL){
		temp_spp_recv_data_node_p2 = temp_spp_recv_data_node_p1->next_node;
		free(temp_spp_recv_data_node_p1->node_buff);
		free(temp_spp_recv_data_node_p1);
		temp_spp_recv_data_node_p1 = temp_spp_recv_data_node_p2;
	}

	SppRecvDataBuff.node_num = 0;
	SppRecvDataBuff.buff_size = 0;
	SppRecvDataBuff.first_node = NULL;
}

static void print_write_buffer(void)
{
	temp_spp_recv_data_node_p1 = SppRecvDataBuff.first_node;

	while(temp_spp_recv_data_node_p1 != NULL){
		uart_write_bytes(UART_NUM_0, (char *)(temp_spp_recv_data_node_p1->node_buff), temp_spp_recv_data_node_p1->len);
		temp_spp_recv_data_node_p1 = temp_spp_recv_data_node_p1->next_node;
	}
}

extern int8_t g_gate_rssi_th;

//static void cc_parse_adv_rx(const esp_ble_gap_cb_param_t * scan_result)
static void cc_parse_adv_rx(esp_ble_gap_cb_param_t * scan_result)
{
	uint8_t * p_payload	= scan_result->scan_rst.ble_adv;
	int8_t rcvd_rssi	= scan_result->scan_rst.rssi;
	uint8_t * p_addr = (uint8_t*)scan_result->scan_rst.bda;

	uint8_t bda_le[4];
	bda_le[0] = p_addr[5];
	bda_le[1] = p_addr[4];
	bda_le[2] = p_addr[3];
	bda_le[3] = p_addr[2];

	uint32_t tmp_addr	= *(uint32_t*)bda_le;

	if( g_gate_param.radar_enable && !front_movement_detected ) {
		return;
	}

	if( rcvd_rssi < g_gate_rssi_th ) return;

	smartphone_beacon_t * p_phone = (smartphone_beacon_t*)p_payload;

	uint8_t na_len = 0;
	uint8_t mf_len = 0;
	uint8_t * p_name = esp_ble_resolve_adv_data(scan_result->scan_rst.ble_adv, ESP_BLE_AD_TYPE_NAME_CMPL, &na_len);
	uint8_t * p_mf = esp_ble_resolve_adv_data(scan_result->scan_rst.ble_adv, ESP_BLE_AD_MANUFACTURER_SPECIFIC_TYPE, &mf_len); // first 4 bytes are fixed as 0x59, 0x00, 0x0A, 0x09

	if(na_len == 0 && mf_len == 0) {
		return;	//no complete_local_name, no_manuf_specific_data
	}

	uint8_t * p_band = p_mf + 4;
	ibeacon_t * p_ibeacon = (ibeacon_t*)p_mf; // 0x4C 0x00 0x02 0x15 fixed for iBeacon

	onepass_pkt_t onepass_pkt = { 0, };
	onepass_pkt.header.opcode = ONEPASS_GATE_ENT_REQ;
	onepass_pkt.header.len = sizeof(gate_ent_req_t)+1;
	onepass_pkt.payload.ch = PP_STANDALONE_CHANNEL_NUMBER; // fixed as 100 for Stand-alone LM


	if( p_mf && !memcmp(p_band, PP_DEVICE_NAME_SB, strlen(PP_DEVICE_NAME_SB)) ) rcvd_rssi += 10;	// because SB's TX strength is lower than others.

	if( (p_mf && p_phone->mfg_id == 0x01AA && !memcmp(p_phone->dev_name, PP_DEVICE_NAME_SP, strlen(PP_DEVICE_NAME_SP)) ) ||
		(p_mf && p_ibeacon->mfg_id == 0x004C && !memcmp(p_ibeacon->dev_name, PP_DEVICE_NAME_SP, strlen(PP_DEVICE_NAME_SP)) ) ) {

		phone_num_t	* p_tmp_num = (p_phone->mfg_id == 0x01AA) ? &p_phone->phone_num : &p_ibeacon->phone_num ;
		uint32_t tmp_key = (p_phone->mfg_id == 0x01AA) ? p_phone->key : p_ibeacon->key;
		uint32_t tmp_site_code = (p_phone->mfg_id == 0x01AA) ? p_phone->site_code : p_ibeacon->site_code;

		if(g_site_code == SITE_ID_NOT_ASSIGNED) {
			ESP_LOGW(TAG, "g_site_code is not assigend yet.");
			if(onepass_tcp_client_is_running) {
				onepass_pkt.header.opcode = ONEPASS_SITE_CODE_REQ;
				onepass_pkt.header.len = 0;
				xQueueSendToBack( qhandle_to_OnepassServer, &onepass_pkt, pdMS_TO_TICKS(10) );
			}
			return;
		}

		if(tmp_site_code != g_site_code) {
			ESP_LOGW(TAG, "!! incoming site_code : 0x%08lX, expected site_code : 0x%08lX", tmp_site_code, g_site_code);
			if(onepass_tcp_client_is_running) {
				onepass_pkt.header.opcode = ONEPASS_SITE_CODE_REQ;
				onepass_pkt.header.len = 0;
				xQueueSendToBack( qhandle_to_OnepassServer, &onepass_pkt, pdMS_TO_TICKS(10) );
			}
			return;
		}

		onepass_pkt.payload.data.gate_ent_req.type = TERMINAL_TYPE_SMART_PHONE;
		onepass_pkt.payload.data.gate_ent_req.phone_num.top = p_tmp_num->top;
		onepass_pkt.payload.data.gate_ent_req.phone_num.mid = p_tmp_num->mid;
		onepass_pkt.payload.data.gate_ent_req.phone_num.btm = p_tmp_num->btm;
		onepass_pkt.payload.data.gate_ent_req.key = tmp_key;

		UBaseType_t skip_count = uxQueueMessagesWaiting(qhandle_ios_skip_list);
		skip_list_t tmp_pkt;
		if( skip_count ) {
			bool overlapped = false;
			for(int i=0; i<skip_count; i++) {
				xQueueReceive( qhandle_ios_skip_list, &tmp_pkt, pdMS_TO_TICKS(1) );
				if(!is_Time_Elapsed(tmp_pkt.timestamp, pdMS_TO_TICKS(IOS_SKIP_DURATION_MS))) {
					xQueueSendToBack( qhandle_ios_skip_list, &tmp_pkt, pdMS_TO_TICKS(1) );	// re-enqueue if the pkt is NOT old
					if(!overlapped &&
							!memcmp(&tmp_pkt.onepass_pkt.payload.data.gate_ent_req.phone_num, &onepass_pkt.payload.data.gate_ent_req.phone_num, sizeof(phone_num_t))) {
						overlapped = true;
					}
				}
			}
			if(overlapped) return;	// Skip because the same packet was already queued within IOS_SKIP_DURATION_MS secs.
		}

		tmp_pkt.timestamp = xTaskGetTickCount();
		memcpy(&tmp_pkt.onepass_pkt, &onepass_pkt, sizeof(onepass_pkt_t));
		xQueueSendToBack( qhandle_ios_skip_list, &tmp_pkt, pdMS_TO_TICKS(1) );	// add new packet into skip list

		ESP_LOGI(TAG, "sp rssi : %d dBm", rcvd_rssi);
		if(p_phone->mfg_id == 0x01AA) {
			ESP_LOGI(TAG, "Android-Phone# %03d-%04d-%04d", p_tmp_num->top, p_tmp_num->mid, p_tmp_num->btm);
		}
		else {
			ESP_LOGI(TAG, "i-Phone# %03d-%04d-%04d", p_tmp_num->top, p_tmp_num->mid, p_tmp_num->btm);
		}

	}
	else if( p_name && !memcmp(p_name, PP_DEVICE_NAME_SK, strlen(PP_DEVICE_NAME_SK)) ) {
		onepass_pkt.payload.data.gate_ent_req.type = TERMINAL_TYPE_SMART_KEY;
		onepass_pkt.payload.data.gate_ent_req.id = tmp_addr;
		ESP_LOGI(TAG, ">>>>>> Smart Key <<<<< rcvd_rssi : %d dBm, addr 0x%08lX", rcvd_rssi, tmp_addr);
	}
	else if( p_mf && !memcmp(p_band, PP_DEVICE_NAME_SB, strlen(PP_DEVICE_NAME_SB)) ) {
		onepass_pkt.payload.data.gate_ent_req.type = TERMINAL_TYPE_SMART_BAND;
		onepass_pkt.payload.data.gate_ent_req.id = tmp_addr;
		ESP_LOGI(TAG, ">>>>>> Smart Band <<<<< rcvd_rssi : %d dBm, addr 0x%08lX", rcvd_rssi, tmp_addr);
	}
	else return;

	if(!cc_Rblink_timer_running) ff_blink_Rled(100, 80, 2);

	if(onepass_tcp_client_is_running) {
		xQueueSendToBack( qhandle_to_OnepassServer, &onepass_pkt, pdMS_TO_TICKS(10) );
	}

}

static void gap_event_handler(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t *param)
{
//	uint8_t *adv_name = NULL;
//	uint8_t adv_name_len = 0;
	esp_err_t err;

	switch (event)
	{

	case ESP_GAP_BLE_SCAN_PARAM_SET_COMPLETE_EVT: {
		ESP_LOGI(TAG, "ESP_GAP_BLE_SCAN_PARAM_SET_COMPLETE_EVT");
		if((err = param->scan_param_cmpl.status) != ESP_BT_STATUS_SUCCESS){
			ESP_LOGE(TAG, "Scan param set failed: %s", esp_err_to_name(err));
			break;
		}
		//the unit of the duration is second
		uint32_t duration = 0xFFFF;
		ESP_LOGI(TAG, "Enable Ble Scan:during time 0x%04lX minutes",duration);
		esp_ble_gap_start_scanning(duration);
		break;
	}
	case ESP_GAP_BLE_SCAN_START_COMPLETE_EVT:
		ESP_LOGI(TAG, "ESP_GAP_BLE_SCAN_START_COMPLETE_EVT");
		//scan start complete event to indicate scan start successfully or failed
		if ((err = param->scan_start_cmpl.status) != ESP_BT_STATUS_SUCCESS) {
			ESP_LOGE(TAG, "Scan start failed: %s", esp_err_to_name(err));
			break;
		}
		ESP_LOGI(TAG, "Scan start ok");
		break;
	case ESP_GAP_BLE_SCAN_STOP_COMPLETE_EVT:
		ESP_LOGI(TAG, "ESP_GAP_BLE_SCAN_STOP_COMPLETE_EVT");
		if ((err = param->scan_stop_cmpl.status) != ESP_BT_STATUS_SUCCESS) {
			ESP_LOGE(TAG, "Scan stop failed: %s", esp_err_to_name(err));
			break;
		}
		ESP_LOGI(TAG, "Scan stop ok");
		if (is_connect == false) {
//			ESP_LOGI(TAG, "Connect to the remote device.");
//			esp_ble_gattc_open(gl_profile_tab[PROFILE_APP_ID].gattc_if, scan_rst.scan_rst.bda, scan_rst.scan_rst.ble_addr_type, true);
		}
		break;
	case ESP_GAP_BLE_SCAN_RESULT_EVT: 
	{
		esp_ble_gap_cb_param_t * p_scan_result = (esp_ble_gap_cb_param_t *)param;
		cc_parse_adv_rx(p_scan_result);
		break;
	}
	case ESP_GAP_BLE_ADV_DATA_RAW_SET_COMPLETE_EVT:
		ESP_LOGI(TAG, "ESP_GAP_BLE_ADV_DATA_RAW_SET_COMPLETE_EVT");
		esp_ble_gap_start_advertising(&spp_adv_params);
		break;
	case ESP_GAP_BLE_ADV_START_COMPLETE_EVT:
		ESP_LOGI(TAG, "ESP_GAP_BLE_ADV_START_COMPLETE_EVT");
		//advertising start complete event to indicate advertising start successfully or failed
		if((err = param->adv_start_cmpl.status) != ESP_BT_STATUS_SUCCESS) {
			ESP_LOGE(TAG, "Advertising start failed: %s", esp_err_to_name(err));
		}
		break;
	case ESP_GAP_BLE_SET_PKT_LENGTH_COMPLETE_EVT:
		ESP_LOGI(TAG, "ESP_GAP_BLE_SET_PKT_LENGTH_COMPLETE_EVT");
		if((err = param->pkt_data_length_cmpl.status) != ESP_BT_STATUS_SUCCESS){
			ESP_LOGE(TAG, "set packet length failed: %s", esp_err_to_name(err));
		}
		else{
			ESP_LOGI(TAG, "set packet length to %d successfully", param->pkt_data_length_cmpl.params.tx_len);
		}
		break;
	default:
		ESP_LOGI(TAG, "NOT handled GAP_EVT, event %d", event);
		break;
	}
}

#if PP_IP_BY_PROVISION_20220713
static void print_ip_info(ip_thru_ble_t * p_ip)
{
	ESP_LOGI(TAG, "local ip : %u.%u.%u.%u/%u", p_ip->loc_ip[0], p_ip->loc_ip[1], p_ip->loc_ip[2], p_ip->loc_ip[3], p_ip->loc_ip[4]);
	ESP_LOGI(TAG, "local gw : %u.%u.%u.%u", p_ip->loc_gw[0], p_ip->loc_gw[1], p_ip->loc_gw[2], p_ip->loc_gw[3]);
	ESP_LOGI(TAG, "remot ip : %u.%u.%u.%u", p_ip->rmt_ip[0], p_ip->rmt_ip[1], p_ip->rmt_ip[2], p_ip->rmt_ip[3]);
}
#endif

static void gatts_profile_event_handler(esp_gatts_cb_event_t event, esp_gatt_if_t gatts_if, esp_ble_gatts_cb_param_t *param)
{
	static bool rebooting_required = false;
	esp_ble_gatts_cb_param_t *p_data = (esp_ble_gatts_cb_param_t *) param;
	uint8_t res = 0xff;

	switch (event) {
		case ESP_GATTS_REG_EVT:
			esp_ble_gap_set_device_name(SAMPLE_DEVICE_NAME);
			esp_ble_gap_config_adv_data_raw((uint8_t *)spp_adv_data, sizeof(spp_adv_data));
			esp_ble_gatts_create_attr_tab(spp_gatt_db, gatts_if, SPP_IDX_NB, SPP_SVC_INST_ID);
			break;
		case ESP_GATTS_READ_EVT:
			res = find_char_and_desr_index(p_data->read.handle);
//			ESP_LOGI(TAG, "ESP_GATTS_READ_EVT : handle = %d", res);
//			if(res == SPP_IDX_SPP_STATUS_VAL){
//			}
			break;
		case ESP_GATTS_WRITE_EVT: {
			res = find_char_and_desr_index(p_data->write.handle);
			if(p_data->write.is_prep == false){
				ESP_LOGI(TAG, "ESP_GATTS_WRITE_EVT : handle = %d", res);

				if(res == SPP_IDX_SPP_DATA_NTF_CFG){
					if((p_data->write.len == 2)&&(p_data->write.value[0] == 0x01)&&(p_data->write.value[1] == 0x00)){
						enable_data_ntf = true;
					}
					else if((p_data->write.len == 2)&&(p_data->write.value[0] == 0x00)&&(p_data->write.value[1] == 0x00)){
						enable_data_ntf = false;
					}
				}
				else if(res == SPP_IDX_SPP_DATA_RECV_VAL){
					CC_LOGN("Rcvd Data : ");
					for(int i=0; i<p_data->write.len; i++) {
						ESP_LOGI(TAG, "0x%02X ", p_data->write.value[i]);
					}
					CC_LOGN("\n");

					ble_format_t * p_ble = (ble_format_t *)p_data->write.value;

					ESP_LOGI(TAG, "!! CMD : 0x%02X", p_ble->cmd);

					ble_format_t tx;
					esp_err_t result;

#if PP_IP_BY_PROVISION_20220713
					if(p_ble->cmd == GET_ETH_IP) {
						tx.cmd = RPT_ETH_IP;
						memcpy(&tx.ip, &frcd_ip, sizeof(ip_thru_ble_t));
						result = esp_ble_gatts_send_indicate(spp_gatts_if, spp_conn_id,
								spp_handle_table[SPP_IDX_SPP_DATA_NTY_VAL], (1+sizeof(ip_thru_ble_t)), (uint8_t*)&tx, false);
						ff_blink_Gled(100, 100, 3);
						ESP_LOGI(TAG, "!! RPT notified. result %d", result);
						ESP_LOGI(TAG, "*** IP Info. Report ***");
						print_ip_info(&frcd_ip);
					}
					else if(p_ble->cmd == SET_ETH_IP) {
						if(memcmp(&frcd_ip, &p_ble->ip, sizeof(ip_thru_ble_t))) {
							ip_thru_ble_t old_ip;
							memcpy(&old_ip, &frcd_ip, sizeof(ip_thru_ble_t));
							memcpy(&frcd_ip, &p_ble->ip, sizeof(ip_thru_ble_t));
							ESP_LOGI(TAG, "*** IP Info. Changed ***");

							if(memcmp(&frcd_ip.loc_ip, &old_ip.loc_ip, 4)) nvs_add(SEL_NVS0, NVS0_FRCD_LOC_IP);
							if(frcd_ip.loc_ip[4] != old_ip.loc_ip[4]) nvs_add(SEL_NVS0, NVS0_FRCD_LOC_NM);
							if(memcmp(&frcd_ip.loc_gw, &old_ip.loc_gw, 4))nvs_add(SEL_NVS0, NVS0_FRCD_LOC_GW);
							if(memcmp(&frcd_ip.rmt_ip, &old_ip.rmt_ip, 4))nvs_add(SEL_NVS0, NVS0_FRCD_RMT_IP);

							rebooting_required = true;
							ff_blink_Rled(100, 100, 3);
						}
						else {
							ESP_LOGI(TAG, "*** Same IP Info. ***");
							ff_blink_Rled(100, 100, 3);
						}

						print_ip_info(&frcd_ip);

						tx.cmd = ACK_ETH_IP;
						memcpy(&tx.ip, &frcd_ip, sizeof(ip_thru_ble_t));
						result = esp_ble_gatts_send_indicate(spp_gatts_if, spp_conn_id,
								spp_handle_table[SPP_IDX_SPP_DATA_NTY_VAL], 2, (uint8_t*)&tx, false);
						ESP_LOGI(TAG, "!! ACK notified. result %d", result);
					}
#endif
					else {
						ESP_LOGI(TAG, "!! Wrong ble command, cmd %d", p_ble->cmd);
					}
				}
			}
			else if((p_data->write.is_prep == true)&&(res == SPP_IDX_SPP_DATA_RECV_VAL)){
				ESP_LOGI(TAG, "ESP_GATTS_PREP_WRITE_EVT : handle = %d", res);
				store_wr_buffer(p_data);
			}
	  	 	break;
		}
		case ESP_GATTS_EXEC_WRITE_EVT:{
			ESP_LOGI(TAG, "ESP_GATTS_EXEC_WRITE_EVT");
			if(p_data->exec_write.exec_write_flag){
				print_write_buffer();
				free_write_buffer();
			}
			break;
		}
		case ESP_GATTS_MTU_EVT:
			spp_mtu_size = p_data->mtu.mtu;
			break;
		case ESP_GATTS_CONNECT_EVT:
//			ff_blink_leds(100, 100, 3);
			rebooting_required = false;
			spp_conn_id = p_data->connect.conn_id;
			spp_gatts_if = gatts_if;
			is_connected = true;
			memcpy(&spp_remote_bda,&p_data->connect.remote_bda,sizeof(esp_bd_addr_t));
			break;
		case ESP_GATTS_DISCONNECT_EVT:
			if(rebooting_required) {
				ESP_LOGI(TAG, "!! Rebooting in 1 sec");
				fflush(stdout);
				vTaskDelay(pdMS_TO_TICKS(1000));
				esp_restart();
			}
			else {
				is_connected = false;
				enable_data_ntf = false;
				esp_ble_gap_start_advertising(&spp_adv_params);
			}
			break;
		case ESP_GATTS_CREAT_ATTR_TAB_EVT:{
			ESP_LOGI(TAG, "The number handle =%u", param->add_attr_tab.num_handle);
			if (param->add_attr_tab.status != ESP_GATT_OK){
				ESP_LOGE(TAG, "Create attribute table failed, error code=0x%x", param->add_attr_tab.status);
			}
			else if (param->add_attr_tab.num_handle != SPP_IDX_NB){
				ESP_LOGE(TAG, "Create attribute table abnormally, num_handle (%d) doesn't equal to HRS_IDX_NB(%d)", param->add_attr_tab.num_handle, SPP_IDX_NB);
			}
			else {
				memcpy(spp_handle_table, param->add_attr_tab.handles, sizeof(spp_handle_table));
				esp_ble_gatts_start_service(spp_handle_table[SPP_IDX_SVC]);
			}
			break;
		}
		default:
			break;
	}
}


static void gatts_event_handler(esp_gatts_cb_event_t event, esp_gatt_if_t gatts_if, esp_ble_gatts_cb_param_t *param)
{
//	ESP_LOGI(TAG, "EVT %d, gatts if %d", event, gatts_if);
	ESP_LOGI(TAG, "[%s(%d)]", str_gatts_evt(event), event);

	/* If event is register event, store the gatts_if for each profile */
	if (event == ESP_GATTS_REG_EVT) {
		if (param->reg.status == ESP_GATT_OK) {
			spp_profile_tab[SPP_PROFILE_APP_IDX].gatts_if = gatts_if;
		} else {
			ESP_LOGI(TAG, "Reg app failed, app_id %04x, status %d",param->reg.app_id, param->reg.status);
			return;
		}
	}

	do {
		int idx;
		for (idx = 0; idx < SPP_PROFILE_NUM; idx++) {
			if (gatts_if == ESP_GATT_IF_NONE || /* ESP_GATT_IF_NONE, not specify a certain gatt_if, need to call every profile cb function */
					gatts_if == spp_profile_tab[idx].gatts_if) {
				if (spp_profile_tab[idx].gatts_cb) {
					spp_profile_tab[idx].gatts_cb(event, gatts_if, param);
				}
			}
		}
	} while (0);
}

static esp_ble_scan_params_t ble_scan_params = {
    .scan_type              = BLE_SCAN_TYPE_ACTIVE,
    .own_addr_type          = BLE_ADDR_TYPE_PUBLIC,
    .scan_filter_policy     = BLE_SCAN_FILTER_ALLOW_ALL,
    .scan_interval          = MSEC_TO_UNITS(200, UNIT_0_625_MS),
    .scan_window            = MSEC_TO_UNITS(195, UNIT_0_625_MS),
    .scan_duplicate         = BLE_SCAN_DUPLICATE_DISABLE
};


void ff_ble_server_config(void)
{
	esp_err_t ret;
	esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();

	ESP_ERROR_CHECK(esp_bt_controller_mem_release(ESP_BT_MODE_CLASSIC_BT));

	ret = esp_bt_controller_init(&bt_cfg);
	if (ret) {
		ESP_LOGE(TAG, "%s enable controller failed: %s", __func__, esp_err_to_name(ret));
		return;
	}

	ret = esp_bt_controller_enable(ESP_BT_MODE_BLE);
	if (ret) {
		ESP_LOGE(TAG, "%s enable controller failed: %s", __func__, esp_err_to_name(ret));
		return;
	}

	ESP_LOGI(TAG, "%s init bluetooth", __func__);
	ret = esp_bluedroid_init();
	if (ret) {
		ESP_LOGE(TAG, "%s init bluetooth failed: %s", __func__, esp_err_to_name(ret));
		return;
	}
	ret = esp_bluedroid_enable();
	if (ret) {
		ESP_LOGE(TAG, "%s enable bluetooth failed: %s", __func__, esp_err_to_name(ret));
		return;
	}

	esp_ble_gatts_register_callback(gatts_event_handler);
	esp_ble_gap_register_callback(gap_event_handler);
	esp_ble_gatts_app_register(ESP_SPP_APP_ID);

	esp_ble_gap_set_scan_params(&ble_scan_params);

	qhandle_ios_skip_list	= xQueueCreate( 32, sizeof(skip_list_t) );

}
