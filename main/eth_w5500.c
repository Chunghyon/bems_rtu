#include "rtu_pins.h"
#include "eth_w5500.h"
#include "esp_log.h"
#include "esp_check.h"
#include "esp_mac.h"
#include "driver/gpio.h"
#include "sdkconfig.h"
#include "driver/spi_master.h"

#define SPI_ETHERNETS_NUM           1

typedef struct {
    uint8_t spi_cs_gpio;
    int8_t int_gpio;
    uint32_t polling_ms;
    int8_t phy_reset_gpio;
    uint8_t phy_addr;
    uint8_t *mac_addr;
}spi_eth_module_config_t;

static const char *TAG = "w5500_init";

static bool gpio_isr_svc_init_by_eth = false; // indicates that we initialized the GPIO ISR service
static spi_device_handle_t debug_spi = NULL;

// ---------- Debug SPI register access ----------
static esp_err_t w5500_debug_spi_init(void)
{
    if (debug_spi) return ESP_OK;

    spi_device_interface_config_t devcfg = {
        .mode = 0,
        .clock_speed_hz = ETH_SPI_CLK_SPEED_HZ,
        .queue_size = 1,
        .spics_io_num = ETH_SPI_CS,
    };
    return spi_bus_add_device(ETH_SPI_HOST, &devcfg, &debug_spi);
}

esp_err_t w5500_read_reg(uint16_t addr, uint8_t *val)
{
    if (!val) return ESP_ERR_INVALID_ARG;
    if (!debug_spi) {
        esp_err_t err = w5500_debug_spi_init();
        if (err != ESP_OK) return err;
    }

    // W5500 SPI: 2-byte address + control byte (BSB=0, Read, Variable length) + data
    uint8_t tx[4] = { (uint8_t)(addr >> 8), (uint8_t)(addr & 0xFF), 0x00, 0x00 };
    uint8_t rx[4] = {0};
    spi_transaction_t t = {
        .length = 32,
        .tx_buffer = tx,
        .rx_buffer = rx,
    };
    esp_err_t err = spi_device_transmit(debug_spi, &t);
    if (err == ESP_OK) {
        *val = rx[3];
    }
    return err;
}

static void w5500_hard_reset(void)
{
    gpio_set_level(ETH_RST_PIN, 0);
    vTaskDelay(pdMS_TO_TICKS(10));
    gpio_set_level(ETH_RST_PIN, 1);
    vTaskDelay(pdMS_TO_TICKS(50));
    ESP_LOGI(TAG, "W5500 hard reset done");
}

/**
 * @brief SPI bus initialization (to be used by Ethernet SPI modules)
 *
 * @return
 *          - ESP_OK on success
 */
static esp_err_t spi_bus_init(void)
{
    esp_err_t ret = ESP_OK;

#ifdef ETH_INT_PIN
    gpio_isr_svc_init_by_eth = true;
#endif

#if PP_ETH_USE_SPI3
    // Dedicated SPI3 bus for W5500
    spi_bus_config_t eth_buscfg = {
        .miso_io_num = ETH_SPI_MISO,
        .mosi_io_num = ETH_SPI_MOSI,
        .sclk_io_num = ETH_SPI_CLK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
    };
    ESP_ERROR_CHECK(spi_bus_initialize(ETH_SPI_HOST, &eth_buscfg, SPI_DMA_CH_AUTO));
    ESP_LOGI(TAG, "ETH SPI bus ready (SPI3_HOST, MOSI=%d MISO=%d CLK=%d)",
             ETH_SPI_MOSI, ETH_SPI_MISO, ETH_SPI_CLK);
#else
    ESP_LOGI(TAG, "ETH shares SPI2_HOST with ADCs");
#endif

    return ret;
}

/**
 * @brief Ethernet SPI modules initialization
 *
 * @param[in] spi_eth_module_config specific SPI Ethernet module configuration
 * @param[out] mac_out optionally returns Ethernet MAC object
 * @param[out] phy_out optionally returns Ethernet PHY object
 * @return
 *          - esp_eth_handle_t if init succeeded
 *          - NULL if init failed
 */
static esp_eth_handle_t eth_init_spi(spi_eth_module_config_t *spi_eth_module_config, esp_eth_mac_t **mac_out, esp_eth_phy_t **phy_out)
{
    esp_eth_handle_t ret = NULL;

    // Init common MAC and PHY configs to default
    eth_mac_config_t mac_config = ETH_MAC_DEFAULT_CONFIG();
    eth_phy_config_t phy_config = ETH_PHY_DEFAULT_CONFIG();

    // Update PHY config based on board specific configuration
    phy_config.phy_addr = spi_eth_module_config->phy_addr;
    phy_config.reset_gpio_num = spi_eth_module_config->phy_reset_gpio;

    // Configure SPI interface for specific SPI module
    spi_device_interface_config_t spi_devcfg = {
        .mode = 0,
        .clock_speed_hz = ETH_SPI_CLK_SPEED_HZ,
        .queue_size = 20,
        .spics_io_num = spi_eth_module_config->spi_cs_gpio
    };
    // Init vendor specific MAC config to default, and create new SPI Ethernet MAC instance
    // and new PHY instance based on board configuration
    eth_w5500_config_t w5500_config = ETH_W5500_DEFAULT_CONFIG(ETH_SPI_HOST, &spi_devcfg);
    w5500_config.int_gpio_num = spi_eth_module_config->int_gpio;
    w5500_config.poll_period_ms = spi_eth_module_config->polling_ms;
    esp_eth_mac_t *mac = esp_eth_mac_new_w5500(&w5500_config, &mac_config);
    esp_eth_phy_t *phy = esp_eth_phy_new_w5500(&phy_config);

    // Init Ethernet driver to default and install it
    esp_eth_handle_t eth_handle = NULL;
    esp_eth_config_t eth_config_spi = ETH_DEFAULT_CONFIG(mac, phy);
    ESP_GOTO_ON_FALSE(esp_eth_driver_install(&eth_config_spi, &eth_handle) == ESP_OK, NULL, err, TAG, "SPI Ethernet driver install failed");

    // The SPI Ethernet module might not have a burned factory MAC address, we can set it manually.
    if (spi_eth_module_config->mac_addr != NULL) {
        ESP_GOTO_ON_FALSE(esp_eth_ioctl(eth_handle, ETH_CMD_S_MAC_ADDR, spi_eth_module_config->mac_addr) == ESP_OK,
                                        NULL, err, TAG, "SPI Ethernet MAC address config failed");
    }

    if (mac_out != NULL) {
        *mac_out = mac;
    }
    if (phy_out != NULL) {
        *phy_out = phy;
    }
    return eth_handle;
err:
    if (eth_handle != NULL) {
        esp_eth_driver_uninstall(eth_handle);
    }
    if (mac != NULL) {
        mac->del(mac);
    }
    if (phy != NULL) {
        phy->del(phy);
    }
    return ret;
}

esp_err_t w5500_init(esp_eth_handle_t *eth_handle_out)
{
    w5500_hard_reset();
    
    esp_err_t ret = ESP_OK;
    esp_eth_handle_t eth_handle = NULL;

    ESP_GOTO_ON_FALSE(eth_handle_out != NULL, ESP_ERR_INVALID_ARG, err, TAG, "invalid arguments: initialized handle");

    eth_handle = calloc(SPI_ETHERNETS_NUM, sizeof(esp_eth_handle_t));
    ESP_GOTO_ON_FALSE(eth_handle != NULL, ESP_ERR_NO_MEM, err, TAG, "no memory");

    ESP_GOTO_ON_ERROR(spi_bus_init(), err, TAG, "SPI bus init failed");
    
    // Init specific SPI Ethernet module configuration from Kconfig (CS GPIO, Interrupt GPIO, etc.)
    spi_eth_module_config_t spi_eth_module_config = { 0 };
    spi_eth_module_config.spi_cs_gpio = ETH_SPI_CS;
    spi_eth_module_config.int_gpio = ETH_INT_PIN;
    spi_eth_module_config.polling_ms = 0;
    spi_eth_module_config.phy_reset_gpio = ETH_RST_PIN;
    spi_eth_module_config.phy_addr = 1;
    
    // The SPI Ethernet module(s) might not have a burned factory MAC address, hence use manually configured address(es).
    // In this example, Locally Administered MAC address derived from ESP32x base MAC address is used.
    // Note that Locally Administered OUI range should be used only when testing on a LAN under your control!
    uint8_t base_mac_addr[ETH_ADDR_LEN];
    ESP_GOTO_ON_ERROR(esp_efuse_mac_get_default(base_mac_addr), err, TAG, "get EFUSE MAC failed");
    uint8_t local_mac_1[ETH_ADDR_LEN];
    esp_derive_local_mac(local_mac_1, base_mac_addr);
    spi_eth_module_config.mac_addr = local_mac_1;

    eth_handle = eth_init_spi(&spi_eth_module_config, NULL, NULL);
    ESP_GOTO_ON_FALSE(eth_handle, ESP_FAIL, err, TAG, "SPI Ethernet init failed");

    *eth_handle_out = eth_handle;

    return ret;

err:
    free(eth_handle);
    return ret;

}

esp_err_t w5500_eth_deinit(esp_eth_handle_t *eth_handle)
{
    ESP_RETURN_ON_FALSE(eth_handle != NULL, ESP_ERR_INVALID_ARG, TAG, "Ethernet handle cannot be NULL");

    esp_eth_mac_t *mac = NULL;
    esp_eth_phy_t *phy = NULL;

    if (eth_handle != NULL) {
        esp_eth_get_mac_instance(eth_handle, &mac);
        esp_eth_get_phy_instance(eth_handle, &phy);
        ESP_RETURN_ON_ERROR(esp_eth_driver_uninstall(eth_handle), TAG, "Ethernet %p uninstall failed", eth_handle);
    }

    if (mac != NULL) {
        mac->del(mac);
    }

    if (phy != NULL) {
        phy->del(phy);
    }

#if PP_ETH_USE_SPI3
    spi_bus_free(ETH_SPI_HOST);
#endif

#ifdef ETH_INT_PIN
    // We installed the GPIO ISR service so let's uninstall it too.
    // BE CAREFUL HERE though since the service might be used by other functionality!
    if (gpio_isr_svc_init_by_eth) {
        ESP_LOGW(TAG, "uninstalling GPIO ISR service!");
        gpio_uninstall_isr_service();
    }
#endif

    free(eth_handle);
    return ESP_OK;
}
