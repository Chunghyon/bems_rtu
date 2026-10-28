idf_component_register(
    SRCS "eth_w5500.c" "0_main.c" "modbus_rtu.c"
    INCLUDE_DIRS "." "include"
    REQUIRES driver nvs_flash esp_event esp_eth esp_http_server esp_netif esp_driver_gpio esp_driver_i2c esp_driver_uart
    EMBED_FILES "favicon.ico"
)
