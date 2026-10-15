set(COMPONENT_SRCDIRS  .)

set(COMPONENT_ADD_INCLUDEDIRS . "../ftpServer" )

set(COMPONENT_REQUIRES esp_actions audio_sal audio_stream audio_hal audio_board audio_recorder esp_peripherals audio_prompts)

register_component()
