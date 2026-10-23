idf_component_register(
    SRCS "eth_w5500.c" "0_main.c"
    INCLUDE_DIRS "." "include"
    REQUIRES driver nvs_flash esp_event esp_eth esp_http_server esp_netif esp_driver_gpio
    EMBED_FILES "favicon.ico"
)
