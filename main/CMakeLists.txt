idf_component_register(
    SRCS "0_main.c"
    INCLUDE_DIRS "." "include"
    REQUIRES driver nvs_flash esp_adc esp_event esp_eth esp_http_server esp_netif
)
