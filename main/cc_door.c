
#include "stdint.h"
#include "string.h"
#include "stdlib.h"

#include "freertos/FreeRTOSConfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"

#include "driver/gpio.h"

#include "esp_log.h"

#include "hh_nvs.h"
#include "hh_parameters.h"

static const char *TAG = "cc_door";

int8_t g_gate_rssi_th = RSSI_MAX;
bool store_on_flash_request = false;
bool gate_is_closed = true;
bool gate_keep_timeout = false;
volatile bool front_movement_detected = false;

static TimerHandle_t handle_RadarChkTimer;
static TimerHandle_t handle_GateKeepTimer;
static TimerHandle_t handle_ScannerActivateTimer;

gate_param_t g_gate_param = {
	.sensitivity	= DEFAULT_ENTRANCE_THRESHOLD,
	.open_duration	= DEFAULT_ENTRANCE_DURATION,
	.radar_enable	= false,
};

static void conv_param_to_rssi(void)
{
	g_gate_rssi_th = RSSI_MIN + ((RSSI_MAX - RSSI_MIN) * g_gate_param.sensitivity)/100.0;
	if(g_gate_rssi_th > RSSI_MIN) g_gate_rssi_th = RSSI_MIN;
	else if(g_gate_rssi_th < RSSI_MAX) g_gate_rssi_th = RSSI_MAX;
	ESP_LOGI(TAG, "!! RSSI Threshold : %d dBm", g_gate_rssi_th);
}

static void period_change_gate_keep_timer(uint8_t new_period_sec)
{
	xTimerChangePeriod( handle_GateKeepTimer, pdMS_TO_TICKS(new_period_sec*1000), pdMS_TO_TICKS(100) );
}

void set_new_param(gate_param_t * p_new_param)
{
	uint8_t old_sensitivity = g_gate_param.sensitivity;
	uint8_t old_duration = g_gate_param.open_duration;
	uint8_t old_radar	= g_gate_param.radar_enable;

	memcpy(&g_gate_param, p_new_param, sizeof(gate_param_t));

	if(g_gate_param.open_duration<3) g_gate_param.open_duration = 3;	//minimum duration 3 secs

	ESP_LOGI(TAG, " Gate_sensitivity   %d -> %d %%\n", old_sensitivity, g_gate_param.sensitivity);
	ESP_LOGI(TAG, " Gate_open_duration %d -> %d secs\n", old_duration, g_gate_param.open_duration);
	ESP_LOGI(TAG, " Gate_PIR           %d -> %d\n", old_radar, g_gate_param.radar_enable);

	if(old_sensitivity != g_gate_param.sensitivity) conv_param_to_rssi();
	if(old_duration != g_gate_param.open_duration) period_change_gate_keep_timer(g_gate_param.open_duration);
	if(old_sensitivity!=g_gate_param.sensitivity || old_duration!=g_gate_param.open_duration || old_radar!=g_gate_param.radar_enable) {
		nvs_add(SEL_NVS0, NVS0_LM_PARAMETERS);
	}
}

//#include "hh_spi.h"
extern bool onepass_tcp_client_is_running;
extern QueueHandle_t qhandle_to_OnepassServer;

// [Fix] Use static buffer to prevent Stack Overflow in Timer Task
// Protected by portENTER_CRITICAL if called from multiple contexts interrupt/tasks, 
// but here mostly safe if only used for status reporting. 
// For absolute safety regarding reentrancy, a mutex could be used, 
// but `xQueueSendToBack` copies data, so the critical window is very short.
static onepass_pkt_t s_status_pkt; 

void rpt_gate_status(onepass_opcode_t opcode)
{
	CC_LOGN("%s\n", __func__);
    if(opcode != ONEPASS_GATE_STATUS_RPT && opcode != ONEPASS_GATE_STATUS_GET_ACK) {
        // [Optim] Use lighter logging in critical paths or remove
        // ESP_LOGW(TAG, "Wrong opcode 0x%04X\n", opcode); 
        return;
    }

    // Prepare packet in static buffer (No stack usage for large struct)
    memset(&s_status_pkt, 0, sizeof(onepass_pkt_t));
    s_status_pkt.header.opcode = opcode;
    s_status_pkt.header.len = 2;
    s_status_pkt.payload.ch	= PP_STANDALONE_CHANNEL_NUMBER;
    s_status_pkt.payload.data.gate_status_rpt.state = gate_is_closed;

    if(onepass_tcp_client_is_running && qhandle_to_OnepassServer) {
        // xQueueSendToBack copies the content of s_status_pkt into the queue
        if (xQueueSendToBack( qhandle_to_OnepassServer, &s_status_pkt, pdMS_TO_TICKS(10) ) != pdPASS) {
             // Avoid logging if possible to save stack, or use minimal logging
             // ESP_LOGW(TAG, "Failed to queue gate status");
        }
    }
}

void ff_gate_open(void)
{
	gate_is_closed = false;
	gpio_set_level(GATE_CONTROL, GATE_OPEN_ACTIVE_HIGH);
	ESP_LOGI(TAG, "!! Gate is open.\n");
	rpt_gate_status(ONEPASS_GATE_STATUS_RPT);
}

void ff_gate_close(void)
{
	gate_is_closed = true;
	gpio_set_level(GATE_CONTROL, !GATE_OPEN_ACTIVE_HIGH);
	ESP_LOGI(TAG, "!! Gate is closed.\n");
	rpt_gate_status(ONEPASS_GATE_STATUS_RPT);
}

void open_gate_and_start_keeping_timer(void)
{
	ff_gate_open();
	xTimerStart(handle_GateKeepTimer, 0);
}

static void cb_gate_keep_timer( TimerHandle_t xTimer )
{
	ff_gate_close();
}

static void gate_keep_timer_init(void)
{
	handle_GateKeepTimer = xTimerCreate("GateKeepTimer", pdMS_TO_TICKS(g_gate_param.open_duration*1000-1000), pdFALSE, NULL, cb_gate_keep_timer);
}

static void cb_radar_check_timer( TimerHandle_t xTimer )
{
	if(front_movement_detected) {
		xTimerStart(handle_ScannerActivateTimer, 0);
	}
}

static void radar_check_timer_init(void)
{
	handle_RadarChkTimer = xTimerCreate("RadarChkTimer", pdMS_TO_TICKS(100), pdTRUE, NULL, cb_radar_check_timer);
	xTimerStart(handle_RadarChkTimer, 0);
}

static void cb_scanner_activate_timer( TimerHandle_t xTimer )
{
	front_movement_detected = false;
}

static void scanner_activate_timer_init(void)
{
	handle_ScannerActivateTimer = xTimerCreate("ScannerActTimer", pdMS_TO_TICKS(SCANNER_ACTIVE_DURATION_SEC*1000),
			pdFALSE, NULL, cb_scanner_activate_timer);
}

/**
 * @brief Function for main application entry.
 */
void ff_door_control_init(void)
{
	conv_param_to_rssi();

	gate_keep_timer_init();
	scanner_activate_timer_init();
	radar_check_timer_init();

	rpt_gate_status(ONEPASS_GATE_STATUS_RPT);
}
