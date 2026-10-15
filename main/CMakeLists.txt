idf_component_register(
                        SRC_DIRS "."
                        INCLUDE_DIRS "." "include" "../components/ftpServer"
                        EMBED_FILES "favicon.ico" "upload_script.html"
                        REQUIRES audio_prompts audio_pipeline audio_stream esp_peripherals bt driver ethernet_init esp_eth app_update spiffs littlefs spi_flash nvs_flash esp_timer esp_http_server WM8960
                      )
#spiffs_create_partition_image(mp3 ../mp3_files FLASH_IN_PROJECT)
littlefs_create_partition_image(storage ../littlefs_data FLASH_IN_PROJECT)