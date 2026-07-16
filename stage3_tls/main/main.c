#include <string.h>
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_timer.h"
#include "mqtt_client.h"
#include "dht.h"

#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1

static const char *TAG = "STAGE3_TLS";
static EventGroupHandle_t s_wifi_event_group;
static esp_mqtt_client_handle_t mqtt_client = NULL;
static bool mqtt_connected = false;
static int s_retry_num = 0;

static int64_t connect_start_us = 0;
static uint32_t tls_handshake_ms = 0;

// Embedded Certificate
extern const uint8_t ca_cert_pem_start[] asm("_binary_ca_cert_pem_start");

static void wifi_event_handler(void* arg, esp_event_base_t event_base,
                               int32_t event_id, void* event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        mqtt_connected = false;
        if (s_retry_num < 20) {
            esp_wifi_connect();
            s_retry_num++;
            ESP_LOGI(TAG, "Retrying WiFi connection...");
        } else {
            xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        s_retry_num = 0;
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

static void wifi_init(void)
{
    s_wifi_event_group = xEventGroupCreate();

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &wifi_event_handler,
                                                        NULL,
                                                        NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                        IP_EVENT_STA_GOT_IP,
                                                        &wifi_event_handler,
                                                        NULL,
                                                        NULL));

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = "Tinhdeptrai",
            .password = "11111111",
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,
        },
    };
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "WiFi STA mode initialized. Waiting for connection...");

    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
            WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
            pdFALSE,
            pdFALSE,
            portMAX_DELAY);

    if (bits & WIFI_CONNECTED_BIT) {
        ESP_LOGI(TAG, "WiFi connected!");
    } else {
        ESP_LOGE(TAG, "WiFi connection failed!");
    }
}

static void mqtt_event_handler(void *handler_args, esp_event_base_t base,
                               int32_t event_id, void *event_data)
{
    esp_mqtt_event_handle_t event = event_data;
    switch (event->event_id) {
        case MQTT_EVENT_BEFORE_CONNECT:
            connect_start_us = esp_timer_get_time();
            ESP_LOGI(TAG, "MQTT connection initiating...");
            break;
        case MQTT_EVENT_CONNECTED:
            if (connect_start_us > 0) {
                int64_t diff = esp_timer_get_time() - connect_start_us;
                tls_handshake_ms = (uint32_t)(diff / 1000);
            }
            ESP_LOGI(TAG, "MQTT connected over TLS 1.3. Handshake time: %lu ms", tls_handshake_ms);
            mqtt_connected = true;
            break;
        case MQTT_EVENT_DISCONNECTED:
            ESP_LOGI(TAG, "MQTT disconnected.");
            mqtt_connected = false;
            break;
        default:
            break;
    }
}

static void mqtt_init(void)
{
    esp_mqtt_client_config_t mqtt_cfg = {
        .broker.address.uri = "mqtts://172.20.10.6:8883",
        .broker.verification.certificate = (const char *)ca_cert_pem_start,
        .broker.verification.skip_cert_common_name_check = true,
    };
    mqtt_client = esp_mqtt_client_init(&mqtt_cfg);
    esp_mqtt_client_register_event(mqtt_client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);
    esp_mqtt_client_start(mqtt_client);
}

void app_main(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    ESP_LOGI(TAG, "=== STAGE 3: TLS 1.3 (Plaintext payload over encrypted channel) ===");

    wifi_init();
    mqtt_init();

    uint32_t msg_count = 0;
    gpio_set_pull_mode(GPIO_NUM_4, GPIO_PULLUP_ONLY);

    vTaskDelay(pdMS_TO_TICKS(2000));

    while (1) {
        if (mqtt_connected) {
            int64_t start_time = esp_timer_get_time();

            float temp = 0.0, humid = 0.0;
            esp_err_t err = dht_read_float_data(DHT_TYPE_DHT11, GPIO_NUM_4, &humid, &temp);

            if (err != ESP_OK) {
                ESP_LOGW(TAG, "Failed to read DHT11! (err: %d). Retrying next cycle.", err);
                vTaskDelay(pdMS_TO_TICKS(10000));
                continue;
            }

            uint32_t heap_at_send = esp_get_free_heap_size();
            int64_t ts = esp_timer_get_time() / 1000;

            int64_t active_time_us = esp_timer_get_time() - start_time;
            float cpu_pct = (float)active_time_us / 100000.0f;

            char payload[200];
            snprintf(payload, sizeof(payload),
                     "{\"id\":%lu,\"temp\":%.1f,\"humid\":%.1f,\"ts\":%lld,\"heap\":%lu,\"enc_us\":0,\"tls_ms\":%lu,\"cpu_pct\":%.1f}",
                     msg_count, temp, humid, ts, heap_at_send, tls_handshake_ms, cpu_pct);

            int msg_id = esp_mqtt_client_publish(mqtt_client, "dothi/sensor/tls13", payload, 0, 1, 0);
            ESP_LOGI(TAG, "Sent Msg #%lu: %s (msg_id: %d, tls_ms: %lu, cpu_pct: %.3f)",
                     msg_count, payload, msg_id, tls_handshake_ms, cpu_pct);

            msg_count++;
        } else {
            ESP_LOGI(TAG, "MQTT not connected. Waiting...");
        }

        vTaskDelay(pdMS_TO_TICKS(10000));
    }
}
