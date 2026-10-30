idf_component_register(
    SRCS "modbus_rtu.c" "0_main.c" "eth_w5500.c" "eth_app.c" "ads131m08.c" "pulse.c" "pca9555.c" "webserver.c" "console.c" "board.c"
    INCLUDE_DIRS "." "include"
    REQUIRES driver nvs_flash esp_event esp_eth esp_http_server esp_netif esp_driver_gpio esp_driver_i2c esp_driver_uart
    EMBED_FILES "favicon.ico"
)
