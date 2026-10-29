idf_component_register(SRCS "wifi_app.c"
                       INCLUDE_DIRS "include"
                       REQUIRES esp_wifi nvs_flash esp_http_client esp_netif)