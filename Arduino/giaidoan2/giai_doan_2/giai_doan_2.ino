/*
  GIAI DOAN 2 - AES-256-GCM (KHONG TLS)
  Doc cam bien DHT11, ma hoa payload bang AES-256-GCM,
  publish qua MQTT KHONG ma hoa kenh truyen (port 1883, plain TCP)
  Dung de do overhead rieng cua lop ma hoa payload (chua cong TLS)
  So sanh voi: Baseline (Giai doan 1) va TLS (Giai doan 3)

  JSON plaintext (truoc khi ma hoa) dung chung schema voi Giai doan 1 de
  auto_listenerv2.py bao cao day du: id, temp, humid, ts, heap, enc_us (thoi
  gian ma hoa AES-256-GCM tren ESP32, don vi us)
*/

#include <WiFi.h>
#include <PubSubClient.h>
#include <DHT.h>
#include "mbedtls/gcm.h"
#include "mbedtls/base64.h"

// ===== CAU HINH WIFI =====
const char* ssid = "Tinhdeptrai";
const char* password = "11111111";

// ===== CAU HINH MQTT BROKER (KHONG TLS) =====
const char* mqtt_server = "172.20.10.6";
const int mqtt_port = 1883;
const char* mqtt_topic = "dothi/sensor/aes256gcm";
const char* client_id = "ESP32S3_AES_Only";

// ===== CAU HINH DHT11 =====
#define DHTPIN 4
#define DHTTYPE DHT11
DHT dht(DHTPIN, DHTTYPE);

// ===== KEY AES-256 CO DINH (32 bytes = 256 bit) - PHAI KHOP VOI auto_listenerv2.py =====
const unsigned char aes_key[32] = {
  0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
  0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F,
  0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17,
  0x18, 0x19, 0x1A, 0x1B, 0x1C, 0x1D, 0x1E, 0x1F
};

#define NONCE_LEN 12
#define TAG_LEN   16
#define MAX_PLAIN_LEN 160

WiFiClient espClient;
PubSubClient client(espClient);
unsigned long msgCount = 0;
unsigned long lastPublish = 0;
const unsigned long PUBLISH_INTERVAL = 10000;

void connectWiFi() {
  Serial.print("Connecting to WiFi");
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi connected. IP: " + WiFi.localIP().toString());
}

void connectMQTT() {
  while (!client.connected()) {
    Serial.print("Connecting to MQTT broker...");
    if (client.connect(client_id)) {
      Serial.println("connected!");
    } else {
      Serial.print("failed, rc=");
      Serial.print(client.state());
      Serial.println(" retry in 2s");
      delay(2000);
    }
  }
}

// Ma hoa AES-256-GCM roi base64-encode: nonce(12) || ciphertext || tag(16)
size_t encryptAndEncode(const char* plaintext, char* outBase64, size_t outBase64Size) {
  size_t plainLen = strlen(plaintext);
  if (plainLen == 0 || plainLen > MAX_PLAIN_LEN) {
    Serial.println("Plaintext length invalid");
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
    &gcm, MBEDTLS_GCM_ENCRYPT, plainLen,
    nonce, NONCE_LEN,
    NULL, 0,
    (const unsigned char*)plaintext, ciphertext,
    TAG_LEN, tag
  );
  mbedtls_gcm_free(&gcm);

  if (ret != 0) {
    Serial.println("AES-GCM encrypt FAILED");
    return 0;
  }

  unsigned char packet[NONCE_LEN + MAX_PLAIN_LEN + TAG_LEN];
  memcpy(packet, nonce, NONCE_LEN);
  memcpy(packet + NONCE_LEN, ciphertext, plainLen);
  memcpy(packet + NONCE_LEN + plainLen, tag, TAG_LEN);
  size_t packetLen = NONCE_LEN + plainLen + TAG_LEN;

  size_t outLen = 0;
  int b64ret = mbedtls_base64_encode((unsigned char*)outBase64, outBase64Size, &outLen, packet, packetLen);
  if (b64ret != 0) {
    Serial.println("Base64 encode FAILED (buffer too small?)");
    return 0;
  }
  return outLen;
}

void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("\n=== STAGE 2: AES-256-GCM only (No TLS) ===");

  dht.begin();
  delay(2000);

  connectWiFi();
  client.setServer(mqtt_server, mqtt_port);
}

void loop() {
  if (!client.connected()) {
    connectMQTT();
  }
  client.loop();

  if (millis() - lastPublish < PUBLISH_INTERVAL) {
    return;
  }
  lastPublish = millis();

  float temp = dht.readTemperature();
  float humid = dht.readHumidity();

  bool invalid = isnan(temp) || isnan(humid) ||
                 temp < 0.0 || temp > 50.0 ||
                 humid < 20.0 || humid > 90.0;

  if (invalid) {
    Serial.print("Failed to read DHT11! raw temp=");
    Serial.print(temp);
    Serial.print(" raw humid=");
    Serial.println(humid);
    return;
  }

  uint32_t freeHeapBefore = ESP.getFreeHeap();

  unsigned long sendTime = millis();
  // heap duoc do NGAY TRUOC khi ma hoa -> phan anh dung RAM con trong luc xu ly goi tin nay
  uint32_t heapAtSend = ESP.getFreeHeap();

  // BUOC HIEU CHUAN: AES-256-GCM la phep toan khoi luong co dinh (thoi gian phu
  // thuoc do dai plaintext, gan nhu khong doi giua cac lan goi cung do dai) nen
  // ta ma hoa thu 1 lan voi ban nhap CHUA co enc_us de biet truoc thoi gian ma
  // hoa xap xi, roi nhung gia tri do vao JSON that va ma hoa lai lan cuoi cung
  // (ma hoa lan cuoi la ban se publish, thoi gian ma hoa lan cuoi duoc log that)
  char draftPlain[MAX_PLAIN_LEN];
  snprintf(draftPlain, sizeof(draftPlain),
           "{\"id\":%lu,\"temp\":%.1f,\"humid\":%.1f,\"ts\":%lu,\"heap\":%u,\"enc_us\":0}",
           msgCount, temp, humid, sendTime, heapAtSend);
  char calibPayload[288];
  unsigned long calibStart = micros();
  encryptAndEncode(draftPlain, calibPayload, sizeof(calibPayload));
  unsigned long encUsEstimate = micros() - calibStart;

  char plaintext[MAX_PLAIN_LEN];
  snprintf(plaintext, sizeof(plaintext),
           "{\"id\":%lu,\"temp\":%.1f,\"humid\":%.1f,\"ts\":%lu,\"heap\":%u,\"enc_us\":%lu}",
           msgCount, temp, humid, sendTime, heapAtSend, encUsEstimate);

  unsigned long encStart = micros();
  char payload[288];
  size_t encLen = encryptAndEncode(plaintext, payload, sizeof(payload));
  unsigned long encTime = micros() - encStart;

  if (encLen == 0) {
    Serial.println("Encryption failed, skip publish");
    return;
  }
  payload[encLen] = '\0';

  unsigned long pubStart = micros();
  bool success = client.publish(mqtt_topic, payload);
  unsigned long pubTime = micros() - pubStart;

  uint32_t freeHeapAfter = ESP.getFreeHeap();

  Serial.println("----------------------------------------");
  Serial.print("Msg #"); Serial.println(msgCount);
  Serial.print("Plaintext: "); Serial.println(plaintext);
  Serial.print("Plaintext size: "); Serial.print(strlen(plaintext)); Serial.println(" bytes");
  Serial.print("Encrypted (base64) payload: "); Serial.println(payload);
  Serial.print("Encrypted payload size: "); Serial.print(encLen); Serial.println(" bytes");
  Serial.print("Encryption time: "); Serial.print(encTime); Serial.println(" us");
  Serial.print("Publish (no TLS) time: "); Serial.print(pubTime); Serial.println(" us");
  Serial.print("Publish: "); Serial.println(success ? "OK" : "FAILED");
  Serial.print("Free heap before/after: ");
  Serial.print(freeHeapBefore); Serial.print(" / "); Serial.println(freeHeapAfter);
  Serial.print("Send timestamp (ms): "); Serial.println(sendTime);

  msgCount++;
}
