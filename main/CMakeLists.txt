idf_component_register(
    SRCS "main.c"
    INCLUDE_DIRS "."
    REQUIRES 
        lora
        esp_wifi 
        esp_event 
        mqtt 
        esp_netif
        nvs_flash
)
