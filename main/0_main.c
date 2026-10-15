
#include "string.h"
#include "freertos/FreeRTOSConfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"

#include "xtensa/core-macros.h"

#include "esp_system.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_eth.h"
#include "esp_task_wdt.h"

#include "driver/uart.h"
#include "driver/gpio.h"

#include "soc/uart_struct.h"
#include "nvs_flash.h"

#include "lwip/api.h"
#include "lwip/err.h"
#include "lwip/netdb.h"
#include "lwip/ip.h"

#include "hh_EMC_process.h"
#include "hh_parameters.h"
#include "hh_util.h"
#include "hh_spi.h"
#include "hh_tcp_packet.h"
#include "hh_ota_server.h"
#include "hh_ledc.h"
#include "hh_spiffs.h"
#include "hh_littlefs.h"
#include "hh_nvs.h"
#include "prompts_uri.h"

#include "wm8960.h"

bool debug_log_on = 1;
bool LEARNING_MODE = false;
bool under_face_learning = false;

esp_log_level_t esp_log_level = ESP_LOG_INFO;

extern void task_Menu(void *pvParameters);
extern void ff_set_loc_ip(void);
extern void ff_eth_init(void);
extern void ff_ble_server_config(void);
extern void ff_init_uart0(void);
extern void ff_init_nvs0(void);
extern void print_loc_ip(void);
extern void ff_door_control_init(void);
extern void open_gate_and_start_keeping_timer(void);
extern void ff_play_prompt(tone_type_t type);

#ifdef SPI_SLAVE_ROLE
extern void init_spi_slave(void);
#endif

uint32_t PP_CURRENT_VERSION;
uint32_t global_eth_timestamp;

extern ip_thru_ble_t frcd_ip;
extern bool esp_restart_requested;
extern uint32_t onepass_heartbeat_timestamp;
extern bool onepass_server_is_connected;
extern QueueHandle_t qhandle_to_OnepassServer;

static char *TAG = "main";

/**
 * GPIO configuration
 */
void ff_gpio_setup(void)
{
	gpio_config_t io_conf;
	io_conf.pin_bit_mask 	= GPIO_OUTPUT_PIN_SEL;		//bit mask of the LED pins
	io_conf.mode 			= GPIO_MODE_OUTPUT;			//set as output mode
	io_conf.pull_down_en 	= false;					//disable pull-down mode
	io_conf.pull_up_en 		= true;						//disable pull-up mode
	io_conf.intr_type 		= GPIO_INTR_DISABLE;		//disable interrupt
	gpio_config(&io_conf);								//configure GPIO with the given settings

	io_conf.pin_bit_mask 	= GPIO_INPUT_PIN_SEL;		//bit mask of interrupt pins
	io_conf.mode 			= GPIO_MODE_INPUT;			//set as input mode
	io_conf.pull_down_en 	= false;					//disable pull-down mode
	io_conf.pull_up_en 		= false;					//enable pull-up mode
	io_conf.intr_type 		= GPIO_INTR_POSEDGE;		//interrupt of rising edge
	gpio_config(&io_conf);								//configure GPIO with the given settings

	gpio_set_level(RSTn_W5500, OFF);
	vTaskDelay(pdMS_TO_TICKS(10));
	gpio_set_level(RSTn_W5500, ON);

	gpio_set_level(RED_LED, ON);
	gpio_set_level(SPI_CS1, ON);
	gpio_set_level(CSB_W5500, ON);
}

/**
 * Run-time flash store function
 * Only one flash store-access at a time for safety
 */
static void cc_store_to_flash(void)
{
	//todo: add flash store function if needed
}

tone_type_t tone_type_to_play = TONE_TYPE_MAX;

extern void ff_wwe_init(void);

#define PERIODIC_PROCESS_ENABLE			1

#if PERIODIC_PROCESS_ENABLE
static void task_periodic_process(void * arg)
{
	uint32_t period_random;

	while(1)
	{
#ifdef OTA_USE_HTTPD
		if(esp_restart_requested) {
			CC_LOGN("esp_restart_requested, reboot!!\n");
			vTaskDelay(pdMS_TO_TICKS(1000));
			esp_restart();
		}
#endif
		if(is_Time_Elapsed(global_eth_timestamp, pdMS_TO_TICKS(GLOBAL_TCP_TIMEOUT))) {
			//CC_LOGW("!! global_tcp_timeout. Auto reboot in 1 sec.\n");
			//vTaskDelay(pdMS_TO_TICKS(1000));
			//esp_restart();
		}

		period_random = (ONEPASS_HEARTBEAT_TIME_MS/2) - ( esp_random() & ONEPASS_HEARTBEAT_RANDOMISE );
		if(onepass_server_is_connected && ( onepass_heartbeat_timestamp == 0 || is_Time_Elapsed(onepass_heartbeat_timestamp, pdMS_TO_TICKS(period_random)) ) ) {
			onepass_pkt_t onepass_pkt = { 0, };
			onepass_pkt.header.opcode = ONEPASS_TCP_HEARTBEAT;
			xQueueSendToBack( qhandle_to_OnepassServer, &onepass_pkt, pdMS_TO_TICKS(10) );
			onepass_heartbeat_timestamp = xTaskGetTickCount();
			//CC_LOGN("\033[1;32mHeartBeat... (period %lu ms)\n\033[1;37m", period_random);
		}

		cc_store_to_flash();

		if(tone_type_to_play < TONE_TYPE_MAX) {
			ff_play_prompt(tone_type_to_play);
			tone_type_to_play = TONE_TYPE_MAX;
		}

		else {
			vTaskDelay(pdMS_TO_TICKS(PERIODIC_TASK_PERIOD));
		}
	} //while(1)
}
#endif

/**
 * Main Task
 */
esp_task_wdt_config_t twdt_config_short = 
{
	.timeout_ms = 5000,
	.idle_core_mask = (1 << portNUM_PROCESSORS) - 1,    // Bitmask of all cores
	.trigger_panic = true,
};

esp_task_wdt_config_t twdt_config_long = // for initial NVS and SPIFFS init
{
	.timeout_ms = 180000,
	.idle_core_mask = (1 << portNUM_PROCESSORS) - 1,    // Bitmask of all cores
	.trigger_panic = true,
};

#if CONFIG_HEAP_TRACING_STANDALONE
#include "esp_heap_trace.h"
#define NUM_RECORDS 500
EXT_RAM_BSS_ATTR static heap_trace_record_t trace_record[NUM_RECORDS];
#endif

void app_main(void)
{
	esp_err_t err;

#if CONFIG_HEAP_TRACING_STANDALONE
	heap_trace_init_standalone(trace_record, NUM_RECORDS);
	heap_trace_start(HEAP_TRACE_ALL);
#endif

	//show_all_heap_info(TAG);

	PP_CURRENT_VERSION = (PP_YEAR*1000000 + PP_MONTH*10000 + PP_DATE*100 + PP_HOUR);
	esp_log_level_set("*", esp_log_level);

	ESP_ERROR_CHECK(esp_task_wdt_init(&twdt_config_long));

	err = nvs_flash_init();
	if (err == ESP_ERR_NVS_NO_FREE_PAGES) {
		ESP_ERROR_CHECK(nvs_flash_erase());
		err = nvs_flash_init();
	}
	ESP_ERROR_CHECK( err );

	//show_all_heap_info(TAG);
	//ff_spiffs_init();

	ff_littlefs_init();

	ESP_ERROR_CHECK(esp_task_wdt_deinit());
	ESP_ERROR_CHECK(esp_task_wdt_init(&twdt_config_short));

	ff_init_nvs0();
	ff_gpio_setup();
	ff_init_uart0();

	vTaskDelay(pdMS_TO_TICKS(100));

	ff_ota_partition_init();

	show_all_heap_info(TAG);

	char * my_idf_version = "v5.5.1-145-ga2a0e0b5ca";
	char * idf_version = (char*)esp_get_idf_version();

	if(strcmp(my_idf_version, idf_version)) {
		CC_LOGN("\r\n!! different idf_version!!\r\n");
		CC_LOGN("   expected : [%s], read : [%s]\r\n", my_idf_version, idf_version);
	}

	CC_LOGNI("\n");
	CC_LOGNI("********************************************\n");
	CC_LOGNI("**   \033[1;36mSmart LobbyMaster\033[1;37m \033[1;32mv1.1 \033[1;37m(%lu)\n", PP_CURRENT_VERSION);
	print_loc_ip();
	CC_LOGNI("********************************************\n");
	CC_LOGNI("\n");

	vTaskDelay(pdMS_TO_TICKS(500)); //added on 20191205 in order to wait till PHY power on reset is terminated
#if 1 //todo remove later!!
	if(!frcd_ip.loc_ip[0]) {
		ff_set_loc_ip();
		while(1) {
			vTaskDelay(pdMS_TO_TICKS(100));
		}
	}
#endif
	ff_ledc_config();
	ff_ble_server_config();
	ff_door_control_init();
	//show_all_heap_info(TAG);

	xTaskCreatePinnedToCore(task_periodic_process, NAME_PERIOD, STACK_PERIOD, NULL, PRIORITY_PERIOD, NULL, CORE_PERIOD);
	vTaskDelay(pdMS_TO_TICKS(10));

	if(frcd_ip.loc_ip[0]) {
		ff_eth_init();
		CC_LOGN("!! Forced Static IP : %d.%d.%d.%d\n", IP2STR((ip4_addr_t*)&frcd_ip.loc_ip[0]));
		init_spi_slave();
		vTaskDelay(pdMS_TO_TICKS(10));

		xTaskCreatePinnedToCore(task_Menu, NAME_MENU, STACK_MENU, NULL, PRIORITY_MENU, NULL, CORE_MENU);
		vTaskDelay(pdMS_TO_TICKS(10));
	}
	else {
		ff_blink_Rled(100, 1000, 0x7FFFFFFF);
		open_gate_and_start_keeping_timer();
		ff_set_loc_ip();
	}

	if(frcd_ip.loc_ip[3] < 200) {
		CC_LOGN("frcd_ip.loc_ip[3]=(%d) < 200, so enable LEARNING_MODE\n", frcd_ip.loc_ip[3]);
		LEARNING_MODE = true;
		request_spi_magic_to_master(SPI_MODE_LEARN);
	}
	else {
		CC_LOGN("frcd_ip.loc_ip[3]=(%d) >= 200, so disable LEARNING_MODE\n", frcd_ip.loc_ip[3]);
		LEARNING_MODE = false;
		request_spi_magic_to_master(SPI_MODE_NORMAL);
	}
		//show_all_heap_)

#if CONFIG_HEAP_TRACING_STANDALONE
	vTaskDelay(pdMS_TO_TICKS(10000));
	heap_trace_stop();
	heap_trace_dump();
#endif

	ff_wwe_init();

	while(1) {
		if(tone_type_to_play < TONE_TYPE_MAX) {
			ff_play_prompt(tone_type_to_play);
			tone_type_to_play = TONE_TYPE_MAX;
		} else
		{
			vTaskDelay(pdMS_TO_TICKS(1000));
		}
	} //while(1)

	vTaskDelete(NULL);
}

