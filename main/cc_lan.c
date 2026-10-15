
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"

#include "esp_attr.h"
#include "esp_system.h"
#include "esp_err.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_eth.h"
#include "esp_wifi.h"

#include "lwip/sockets.h"
#include "lwip/api.h"
#include "lwip/err.h"
#include "lwip/netdb.h"
#include "lwip/ip.h"

#include "soc/dport_reg.h"
#include "soc/io_mux_reg.h"
#include "soc/rtc_cntl_reg.h"
#include "soc/gpio_reg.h"
#include "soc/gpio_sig_map.h"

#include "driver/gpio.h"
#include "esp_private/periph_ctrl.h"
#include "nvs_flash.h"
#include "hh_parameters.h"
#include "hh_util.h"
#include "hh_spi.h"

#include "ethernet_init.h"

static const char *TAG = "lan";

ip_thru_ble_t frcd_ip = {0, };
esp_netif_ip_info_t self_eth_ip = {0, };

bool is_ETH_started = false;
bool is_ETH_connected = false;

/**
 * Ethernet initialization
 */

#include "hh_tcp_packet.h"
#include "hh_ota_server.h"
#include "hh_EMC_process.h"

extern void ftp_server_start(void);
extern void start_onepass_tcp_client(void);
extern esp_err_t start_ota_server(void);
extern void print_loc_ip(void);
extern ip_thru_ble_t frcd_ip;

TaskHandle_t handle_waitETH;
TaskHandle_t handle_taskAppTcp;
TaskHandle_t handle_taskOtaTcp;

TickType_t	timestamp_TCP_LAST_TX;
TickType_t	timestamp_TCP_LAST_RX;

int tcp_client_socket;

bool is_APP_TCP_connected		= FALSE;
//bool is_OTA_TCP_connected		= FALSE;

bool is_APP_TCP_task_running	= FALSE;

bool del_APP_TCP_task_requested = FALSE;
bool del_OTA_TCP_task_requested = FALSE;

bool del_conn_requested_lamp	= FALSE;
tcp_pkt_t tcp_rx_buffer;

struct netconn *p_connection_conn_lamp;
struct netconn *p_lamp_conn = NULL;

struct netconn *p_connection_conn_ota;
bool del_conn_requested_ota = false;
static bool wait_task_running_ota = false;
//static TaskHandle_t handle_waitOTA;
struct netconn *p_ota_conn = NULL;

esp_netif_t *eth_netif;

bool create_TCP_socket(struct netconn **conn, esp_ip4_addr_t *ip, u16_t port)
{
	err_t err;
	ip_addr_t ipaddr = IPADDR4_INIT(ip->addr);

	*conn = netconn_new(NETCONN_TCP);

	err = netconn_bind(*conn, (const ip_addr_t*)&ipaddr, port);
	if(err == ERR_USE) //err = -8
	{
		CC_LOGI("The IP Address is bound now. Auto reboot in 1 secs to release it.\n");
		vTaskDelay(pdMS_TO_TICKS(1000));
		esp_restart();
		return FALSE;
	}

	if(err != ERR_OK && err != ERR_USE)
	{
		CC_LOGE("netconn_bind error : %d\n", err);
		return FALSE;
	}

	err = netconn_listen(*conn);
	if(err != ERR_OK)
	{
		CC_LOGE("netconn_listen error : %d\n", err);
		return FALSE;
	}

	return TRUE;
}

/**
 * delete TCP socket
 */
void delete_TCP_socket(bool *del_conn)
{
	*del_conn = TRUE;
	vTaskDelay(pdMS_TO_TICKS(10));
}

/** Event handler for Ethernet events */
static void eth_event_handler(void *arg, esp_event_base_t event_base,
                              int32_t event_id, void *event_data)
{
	uint8_t mac_addr[6] = {0};
	/* we can get the ethernet driver handle from event data */
	esp_eth_handle_t eth_handle = *(esp_eth_handle_t *)event_data;

	switch (event_id) {
	case ETHERNET_EVENT_CONNECTED:
		is_ETH_connected = true;
		esp_eth_ioctl(eth_handle, ETH_CMD_G_MAC_ADDR, mac_addr);
		CC_LOGN("Ethernet Link Up\n");
		CC_LOGN("Ethernet HW Addr %02x:%02x:%02x:%02x:%02x:%02x\n",
					mac_addr[0], mac_addr[1], mac_addr[2], mac_addr[3], mac_addr[4], mac_addr[5]);
		break;
	case ETHERNET_EVENT_DISCONNECTED:
		is_ETH_connected = false;
		CC_LOGN("Ethernet Link Down\n");
		break;
	case ETHERNET_EVENT_START:
		is_ETH_started = true;
		CC_LOGN("Ethernet Started\n");
		break;
	case ETHERNET_EVENT_STOP:
		is_ETH_started = false;
		is_ETH_connected = false;
		CC_LOGN("Ethernet Stopped\n");
		break;
	default:
		break;
	}
}

/** Event handler for IP_EVENT_ETH_GOT_IP */
static void got_ip_event_handler(void *arg, esp_event_base_t event_base,
                                 int32_t event_id, void *event_data)
{
//	BaseType_t result;
	ip_event_got_ip_t *event = (ip_event_got_ip_t *) event_data;
	const esp_netif_ip_info_t *ip_info = &event->ip_info;

	CC_LOGN("Ethernet Got IP Address\n");
	CC_LOGN("~~~~~~~~~~~\n");
	CC_LOGN("ETHIP:" IPSTR "\n", IP2STR(&ip_info->ip));
	CC_LOGN("ETHMASK:" IPSTR "\n", IP2STR(&ip_info->netmask));
	CC_LOGN("ETHGW:" IPSTR "\n", IP2STR(&ip_info->gw));
	CC_LOGN("~~~~~~~~~~~\n");

	esp_netif_ip_info_t eth_ip = {0,};
	uint8_t eth_macaddr[6]={0,};

	ESP_ERROR_CHECK(esp_netif_get_ip_info(eth_netif, &eth_ip));
	esp_netif_get_mac(eth_netif, eth_macaddr);

	CC_LOGNI("----------------------------------------------------\n");
	print_loc_ip();
	CC_LOGNI(" (Mac %02X:%02X:%02X:%02X:%02X:%02X)\n", MAC2STR(eth_macaddr));
	CC_LOGNI("----------------------------------------------------\n\n");

	if(frcd_ip.loc_ip[0]) {
#ifdef PP_ONLY_FOR_DEMO
		extern void start_data_server_interface_only_for_demo(void);
		start_data_server_interface_only_for_demo();
#endif
		start_ota_server();
		start_onepass_tcp_client();
		ftp_server_start();

	}
}

void ff_eth_init(void)
{
	CC_LOGN("[%s]\n", __func__);
	uint8_t eth_port_cnt = 0;
	esp_eth_handle_t *eth_handles;
	ESP_ERROR_CHECK(example_eth_init(&eth_handles, &eth_port_cnt));

	ESP_LOGD(TAG, "eth_port_cnt %u\n", eth_port_cnt);

	ESP_ERROR_CHECK(esp_netif_init());
	ESP_ERROR_CHECK(esp_event_loop_create_default());

	if(self_eth_ip.ip.addr == 0)
	{
		self_eth_ip.ip.addr		 = inet_addr(ETH_DUMMY_IP_STR);
		self_eth_ip.netmask.addr = inet_addr(ETH_DEFAULT_NETMASK_STR);
		self_eth_ip.gw.addr		 = inet_addr(ETH_DEFAULT_GATEWAY_STR);
//		inet_pton(AF_INET, ETH_DUMMY_IP_STR,		&self_eth_ip.ip);
//		inet_pton(AF_INET, ETH_DEFAULT_NETMASK_STR,	&self_eth_ip.netmask);
//		inet_pton(AF_INET, ETH_DEFAULT_GATEWAY_STR,	&self_eth_ip.gw);
	}

	if (eth_port_cnt == 1) {
		// Use ESP_NETIF_DEFAULT_ETH when just one Ethernet interface is used and you don't need to modify
		// default esp-netif configuration parameters.
		esp_netif_config_t cfg = ESP_NETIF_DEFAULT_ETH();
		//esp_netif_t *eth_netif = esp_netif_new(&cfg);
		eth_netif = esp_netif_new(&cfg);

		(void) esp_netif_dhcpc_stop(eth_netif);
		(void) esp_netif_set_ip_info(eth_netif, (esp_netif_ip_info_t*)&self_eth_ip);

		// Attach Ethernet driver to TCP/IP stack
		ESP_ERROR_CHECK(esp_netif_attach(eth_netif, esp_eth_new_netif_glue(eth_handles[0])));
	} else {
		// Use ESP_NETIF_INHERENT_DEFAULT_ETH when multiple Ethernet interfaces are used and so you need to modify
		// esp-netif configuration parameters for each interface (name, priority, etc.).
		esp_netif_inherent_config_t esp_netif_config = ESP_NETIF_INHERENT_DEFAULT_ETH();
		esp_netif_config_t cfg_spi = {
			.base = &esp_netif_config,
			.stack = ESP_NETIF_NETSTACK_DEFAULT_ETH
		};
		char if_key_str[10];
		char if_desc_str[10];
		char num_str[3];
		for (int i = 0; i < eth_port_cnt; i++) {
			itoa(i, num_str, 10);
			strcat(strcpy(if_key_str, "ETH_"), num_str);
			strcat(strcpy(if_desc_str, "eth"), num_str);
			esp_netif_config.if_key = if_key_str;
			esp_netif_config.if_desc = if_desc_str;
			esp_netif_config.route_prio -= i*5;
			esp_netif_t *eth_netif = esp_netif_new(&cfg_spi);

			// Attach Ethernet driver to TCP/IP stack
			ESP_ERROR_CHECK(esp_netif_attach(eth_netif, esp_eth_new_netif_glue(eth_handles[i])));
		}
	}
	// Register user defined event handers
	ESP_ERROR_CHECK(esp_event_handler_register(ETH_EVENT, ESP_EVENT_ANY_ID, &eth_event_handler, NULL));
	ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_ETH_GOT_IP, &got_ip_event_handler, NULL));

	print_loc_ip();

	// Start Ethernet driver state machine
	for (int i = 0; i < eth_port_cnt; i++) {
		ESP_ERROR_CHECK(esp_eth_start(eth_handles[i]));
	}
}

