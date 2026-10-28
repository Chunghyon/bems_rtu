#include "eth_app.h"
#include "eth_w5500.h"
#include "webserver.h"

#include "esp_event.h"
#include "esp_netif.h"
#include "esp_log.h"

static const char *TAG = "eth_app";

static esp_eth_handle_t g_eth_handle = NULL;
static esp_netif_t *g_eth_netif = NULL;
static esp_eth_netif_glue_handle_t g_eth_glue = NULL;

static void eth_event_handler(void *arg, esp_event_base_t event_base,
                              int32_t event_id, void *event_data)
{
    uint8_t mac_addr[6] = {0};
    esp_eth_handle_t eth_handle = *(esp_eth_handle_t *)event_data;

    switch (event_id) {
    case ETHERNET_EVENT_CONNECTED:
        esp_eth_ioctl(eth_handle, ETH_CMD_G_MAC_ADDR, mac_addr);
        ESP_LOGI(TAG, "Ethernet Link Up");
        ESP_LOGI(TAG, "Ethernet HW Addr %02x:%02x:%02x:%02x:%02x:%02x",
                 mac_addr[0], mac_addr[1], mac_addr[2], mac_addr[3], mac_addr[4], mac_addr[5]);
        break;
    case ETHERNET_EVENT_DISCONNECTED:
        ESP_LOGI(TAG, "Ethernet Link Down");
        break;
    case ETHERNET_EVENT_START:
        ESP_LOGI(TAG, "Ethernet Started");
        break;
    case ETHERNET_EVENT_STOP:
        ESP_LOGI(TAG, "Ethernet Stopped");
        break;
    default:
        break;
    }
}

static void got_ip_event_handler(void *arg, esp_event_base_t event_base,
                                 int32_t event_id, void *event_data)
{
    ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
    const esp_netif_ip_info_t *ip_info = &event->ip_info;

    ESP_LOGI(TAG, "Ethernet Got IP Address");
    ESP_LOGI(TAG, "~~~~~~~~~~~");
    ESP_LOGI(TAG, "ETHIP:" IPSTR, IP2STR(&ip_info->ip));
    ESP_LOGI(TAG, "ETHMASK:" IPSTR, IP2STR(&ip_info->netmask));
    ESP_LOGI(TAG, "ETHGW:" IPSTR, IP2STR(&ip_info->gw));
    ESP_LOGI(TAG, "~~~~~~~~~~~");

    webserver_start();
}

static esp_err_t eth_tcpip_stack_init(esp_eth_handle_t eth_handle)
{
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    esp_netif_config_t cfg = ESP_NETIF_DEFAULT_ETH();
    g_eth_netif = esp_netif_new(&cfg);
    g_eth_glue = esp_eth_new_netif_glue(eth_handle);
    ESP_ERROR_CHECK(esp_netif_attach(g_eth_netif, g_eth_glue));

    return ESP_OK;
}

void eth_app_init(esp_eth_handle_t eth_handle)
{
    g_eth_handle = eth_handle;
    ESP_ERROR_CHECK(eth_tcpip_stack_init(eth_handle));

    ESP_ERROR_CHECK(esp_event_handler_register(ETH_EVENT, ESP_EVENT_ANY_ID, &eth_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_ETH_GOT_IP, &got_ip_event_handler, NULL));

    ESP_ERROR_CHECK(esp_eth_start(eth_handle));
}

void eth_app_recover(void)
{
    if (!g_eth_handle) {
        ESP_LOGE(TAG, "No Ethernet handle to recover");
        return;
    }

    if (g_eth_glue) {
        esp_eth_del_netif_glue(g_eth_glue);
        g_eth_glue = NULL;
    }
    if (g_eth_netif) {
        esp_netif_destroy(g_eth_netif);
        g_eth_netif = NULL;
    }

    esp_err_t err = w5500_reinstall_driver(&g_eth_handle);
    if (err != ESP_OK || !g_eth_handle) {
        ESP_LOGE(TAG, "Driver reinstall failed: %s", esp_err_to_name(err));
        return;
    }

    esp_netif_config_t cfg = ESP_NETIF_DEFAULT_ETH();
    g_eth_netif = esp_netif_new(&cfg);
    g_eth_glue = esp_eth_new_netif_glue(g_eth_handle);
    if (!g_eth_netif || !g_eth_glue) {
        ESP_LOGE(TAG, "Failed to recreate netif/glue");
        return;
    }
    ESP_ERROR_CHECK(esp_netif_attach(g_eth_netif, g_eth_glue));

    err = esp_eth_start(g_eth_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_eth_start after reinstall failed: %s", esp_err_to_name(err));
        return;
    }
    ESP_LOGI(TAG, "Full recover done. Wait for Link Up / IP...");
}
