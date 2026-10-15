idf_component_register(
                        SRCS "ftpServer.c" "ftp.c"
                        INCLUDE_DIRS "." "../../main/include"
                        REQUIRES driver ethernet_init esp_eth littlefs spiffs spi_flash nvs_flash esp_timer 
                      )
#littlefs_create_partition_image(storage ../../littlefs_data FLASH_IN_PROJECT)