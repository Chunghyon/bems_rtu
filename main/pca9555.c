#include "project.h"
#include "board.h"
#include "pca9555.h"

#include "driver/i2c_master.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

static const char *TAG = "pca9555";

// Address: base 0x20 + (A2<<2 | A1<<1 | A0) = 0x20 + 0x01 = 0x21
#define PCA9555_ADDR            0x21
#define PCA9555_REG_OUTPUT0     0x02
#define PCA9555_REG_OUTPUT1     0x03
#define PCA9555_REG_CONFIG0     0x06
#define PCA9555_REG_CONFIG1     0x07

// PCA9555PWR ports (16-bit expander)
typedef enum {
    PCA_10V_SEL1 = 0,
    PCA_10V_SEL2,
    PCA_10V_SEL3,
    PCA_10V_SEL4,
    PCA_10V_SEL5,
    PCA_10V_SEL6,
    PCA_DI_SEL1,
    PCA_DI_SEL2,
    PCA_CUR_SEL1,
    PCA_CUR_SEL2,
    PCA_CUR_SEL3,
    PCA_CUR_SEL4,
    PCA_CUR_SEL5,
    PCA_CUR_SEL6,
    PCA_DI_SEL3,
    PCA_DI_SEL4,
    PCA_PORT_COUNT
} pca9555_port_t;

static const char * const pca_port_names[PCA_PORT_COUNT] = {
    "PCA_P0.0 (10V_SEL1)",
    "PCA_P0.1 (10V_SEL2)",
    "PCA_P0.2 (10V_SEL3)",
    "PCA_P0.3 (10V_SEL4)",
    "PCA_P0.4 (10V_SEL5)",
    "PCA_P0.5 (10V_SEL6)",
    "PCA_P0.6 (DI_SEL1)",
    "PCA_P0.7 (DI_SEL2)",
    "PCA_P1.0 (CUR_SEL1)",
    "PCA_P1.1 (CUR_SEL2)",
    "PCA_P1.2 (CUR_SEL3)",
    "PCA_P1.3 (CUR_SEL4)",
    "PCA_P1.4 (CUR_SEL5)",
    "PCA_P1.5 (CUR_SEL6)",
    "PCA_P1.6 (DI_SEL3)",
    "PCA_P1.7 (DI_SEL4)"
};

static i2c_master_bus_handle_t i2c_bus = NULL;
static i2c_master_dev_handle_t pca9555_dev = NULL;

static esp_err_t pca9555_write_reg(uint8_t reg, uint8_t val)
{
    if (!pca9555_dev) return ESP_ERR_INVALID_STATE;
    uint8_t buf[2] = {reg, val};
    return i2c_master_transmit(pca9555_dev, buf, 2, pdMS_TO_TICKS(100));
}

void pca9555_init(void)
{
    i2c_master_bus_config_t bus_cfg = {
        .i2c_port = I2C_NUM_0,
        .sda_io_num = I2C_SDA_PIN,
        .scl_io_num = I2C_SCL_PIN,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    ESP_ERROR_CHECK(i2c_new_master_bus(&bus_cfg, &i2c_bus));

    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = PCA9555_ADDR,
        .scl_speed_hz = 100000,
    };
    ESP_ERROR_CHECK(i2c_master_bus_add_device(i2c_bus, &dev_cfg, &pca9555_dev));

    // All pins as outputs (0 = output)
    ESP_ERROR_CHECK(pca9555_write_reg(PCA9555_REG_CONFIG0, 0x00));
    ESP_ERROR_CHECK(pca9555_write_reg(PCA9555_REG_CONFIG1, 0x00));

    // All relays off
    ESP_ERROR_CHECK(pca9555_write_reg(PCA9555_REG_OUTPUT0, 0x00));
    ESP_ERROR_CHECK(pca9555_write_reg(PCA9555_REG_OUTPUT1, 0x00));

    ESP_LOGI(TAG, "PCA9555PWR initialized (addr=0x%02X, new I2C master driver)", PCA9555_ADDR);
}

static volatile bool relay_test_enabled = false;

static void task_relay_test(void *arg)
{
    uint8_t relay = 0;
    while (1) {
        if (!relay_test_enabled) {
            pca9555_write_reg(PCA9555_REG_OUTPUT0, 0x00);
            pca9555_write_reg(PCA9555_REG_OUTPUT1, 0x00);
            vTaskDelay(pdMS_TO_TICKS(200));
            continue;
        }

        uint8_t port0 = 0, port1 = 0;
        if (relay < 8) {
            port0 = (1 << relay);
        } else {
            port1 = (1 << (relay - 8));
        }

        pca9555_write_reg(PCA9555_REG_OUTPUT0, port0);
        pca9555_write_reg(PCA9555_REG_OUTPUT1, port1);

        ESP_LOGI(TAG, "Relay %d (%s) ON (port0=0x%02X port1=0x%02X)",
                 relay, pca_port_names[relay], port0, port1);

        relay = (relay + 1) % 16;
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

void pca9555_start_task(void)
{
    xTaskCreate(task_relay_test, "relay_test", 4096, NULL, 4, NULL);
}
