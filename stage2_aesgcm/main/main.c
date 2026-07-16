#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_random.h"
#include "nvs_flash.h"
#include "esp_timer.h"
#include "mqtt_client.h"
#include "mbedtls/gcm.h"
#include "mbedtls/base64.h"
#include "dht.h"

#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1

#define NONCE_LEN 12
#define TAG_LEN   16
#define MAX_PLAIN_LEN 160

static const char *TAG = "STAGE2_AESGCM";
static EventGroupHandle_t s_wifi_event_group;
static esp_mqtt_client_handle_t mqtt_client = NULL;
static bool mqtt_connected = false;
static int s_retry_num = 0;

// AES-256 Key (32 bytes)
static const unsigned char aes_key[32] = {
    0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
    0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F,
    0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17,
    0x18, 0x19, 0x1A, 0x1B, 0x1C, 0x1D, 0x1E, 0x1F
};

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
        case MQTT_EVENT_CONNECTED:
            ESP_LOGI(TAG, "MQTT connected to broker.");
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
        .broker.address.uri = "mqtt://172.20.10.6:1883",
    };
    mqtt_client = esp_mqtt_client_init(&mqtt_cfg);
    esp_mqtt_client_register_event(mqtt_client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);
    esp_mqtt_client_start(mqtt_client);
}

size_t encrypt_and_encode(const char* plaintext, char* out_base64, size_t out_base64_size)
{
    size_t plain_len = strlen(plaintext);
    if (plain_len == 0 || plain_len > MAX_PLAIN_LEN) {
        ESP_LOGE(TAG, "Plaintext length invalid: %d", plain_len);
        return 0;
    }

    unsigned char nonce[NONCE_LEN];
    esp_fill_random(nonce, NONCE_LEN);

    unsigned char ciphertext[MAX_PLAIN_LEN];
    unsigned char tag[TAG_LEN];

    mbedtls_gcm_context gcm;
    mbedtls_gcm_init(&gcm);
    mbedtls_gcm_setkey(&gcm, MBEDTLS_CIPHER_ID_AES, aes_key, 256);

    int ret = mbedtls_gcm_crypt_and_tag(
        &gcm, MBEDTLS_GCM_ENCRYPT, plain_len,
        nonce, NONCE_LEN,
        NULL, 0,
        (const unsigned char*)plaintext, ciphertext,
        TAG_LEN, tag
    );
    mbedtls_gcm_free(&gcm);

    if (ret != 0) {
        ESP_LOGE(TAG, "AES-GCM encrypt FAILED: %d", ret);
        return 0;
    }

    // Packet layout: nonce(12) || ciphertext(plain_len) || tag(16)
    size_t packet_len = NONCE_LEN + plain_len + TAG_LEN;
    unsigned char *packet = malloc(packet_len);
    if (!packet) {
        ESP_LOGE(TAG, "Failed to allocate packet memory");
        return 0;
    }

    memcpy(packet, nonce, NONCE_LEN);
    memcpy(packet + NONCE_LEN, ciphertext, plain_len);
    memcpy(packet + NONCE_LEN + plain_len, tag, TAG_LEN);

    size_t out_len = 0;
    int b64ret = mbedtls_base64_encode((unsigned char*)out_base64, out_base64_size, &out_len, packet, packet_len);
    free(packet);

    if (b64ret != 0) {
        ESP_LOGE(TAG, "Base64 encode FAILED (buffer too small? err: %d)", b64ret);
        return 0;
    }

    return out_len;
}

void app_main(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    ESP_LOGI(TAG, "=== STAGE 2: AES-256-GCM only (No TLS) ===");

    wifi_init();
    mqtt_init();

    uint32_t msg_count = 0;
    gpio_set_pull_mode(GPIO_NUM_4, GPIO_PULLUP_ONLY); // DHT11 needs pullup

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

            // Two-pass calibration trick for AES-256-GCM
            char draft_plain[MAX_PLAIN_LEN];
            snprintf(draft_plain, sizeof(draft_plain),
                     "{\"id\":%lu,\"temp\":%.1f,\"humid\":%.1f,\"ts\":%lld,\"heap\":%lu,\"enc_us\":0,\"tls_ms\":0,\"cpu_pct\":0.0}",
                     msg_count, temp, humid, ts, heap_at_send);
            
            char calib_payload[288];
            int64_t calib_start = esp_timer_get_time();
            encrypt_and_encode(draft_plain, calib_payload, sizeof(calib_payload));
            int64_t enc_us_estimate = esp_timer_get_time() - calib_start;

            // Prepare the actual plaintext JSON with the estimated enc_us
            char plaintext[MAX_PLAIN_LEN];
            snprintf(plaintext, sizeof(plaintext),
                     "{\"id\":%lu,\"temp\":%.1f,\"humid\":%.1f,\"ts\":%lld,\"heap\":%lu,\"enc_us\":%lld,\"tls_ms\":0,\"cpu_pct\":0.0}",
                     msg_count, temp, humid, ts, heap_at_send, enc_us_estimate);

            int64_t enc_start = esp_timer_get_time();
            char payload[288];
            size_t enc_len = encrypt_and_encode(plaintext, payload, sizeof(payload));
            int64_t enc_time = esp_timer_get_time() - enc_start;

            if (enc_len == 0) {
                ESP_LOGE(TAG, "Encryption failed, skipping publish");
                vTaskDelay(pdMS_TO_TICKS(10000));
                continue;
            }
            payload[enc_len] = '\0';

            // Bracket processing time to estimate CPU usage (active time)
            int64_t active_time_us = esp_timer_get_time() - start_time;
            float cpu_pct = (float)active_time_us / 100000.0f;

            // Since cpu_pct changes the json length, we inject it in a final format or just keep it as measured in active_time_us.
            // AES-256-GCM of a 160-byte payload is extremely fast (~200-300us). Let's write the final payload.
            // Wait, we can rewrite the JSON with the real cpu_pct and encrypt again? That would trigger infinite loop.
            // Instead, we can write the plaintext with estimated cpu_pct (e.g. baseline cpu_pct is around 0.2%) or just do the 
            // encryption with the calculated cpu_pct. The calibration was for enc_us, but we can also use estimated cpu_pct.
            // Let's just put the computed cpu_pct directly in the plaintext and encrypt it. That is very precise!
            snprintf(plaintext, sizeof(plaintext),
                     "{\"id\":%lu,\"temp\":%.1f,\"humid\":%.1f,\"ts\":%lld,\"heap\":%lu,\"enc_us\":%lld,\"tls_ms\":0,\"cpu_pct\":%.1f}",
                     msg_count, temp, humid, ts, heap_at_send, enc_time, cpu_pct);
            
            enc_len = encrypt_and_encode(plaintext, payload, sizeof(payload));
            if (enc_len > 0) {
                payload[enc_len] = '\0';
                int msg_id = esp_mqtt_client_publish(mqtt_client, "dothi/sensor/aes256gcm", payload, 0, 1, 0);
                ESP_LOGI(TAG, "Sent Msg #%lu (Encrypted): Plaintext=%s (msg_id: %d, enc_time: %lld us, cpu_pct: %.3f)",
                         msg_count, plaintext, msg_id, enc_time, cpu_pct);
                msg_count++;
            }
        } else {
            ESP_LOGI(TAG, "MQTT not connected. Waiting...");
        }

        vTaskDelay(pdMS_TO_TICKS(10000));
    }
}
