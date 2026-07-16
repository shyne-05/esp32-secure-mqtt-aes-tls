idf_component_register(SRCS "main.c"
                       INCLUDE_DIRS "."
                       EMBED_TXTFILES "ca_cert.pem"
                       REQUIRES esp_wifi esp_event esp_netif nvs_flash mqtt json dht)
