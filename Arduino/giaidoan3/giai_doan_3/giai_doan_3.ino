/*
  GIAI DOAN 3 - TLS 1.3
  Doc cam bien DHT11, publish JSON qua MQTT over TLS 1.3 (port 8883)
  So sanh overhead (thoi gian ket noi, heap) so voi Baseline
*/

#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <PubSubClient.h>
#include <DHT.h>

// ===== CAU HINH WIFI =====
const char* ssid = "Tinhdeptrai";
const char* password = "11111111";

// ===== CAU HINH MQTT BROKER (TLS) =====
const char* mqtt_server = "172.20.10.6";
const int mqtt_port = 8883;
const char* mqtt_topic = "dothi/sensor/tls13";
const char* client_id = "ESP32S3_TLS13";

// ===== CAU HINH DHT11 =====
#define DHTPIN 4
#define DHTTYPE DHT11
DHT dht(DHTPIN, DHTTYPE);

// ===== CA CERTIFICATE (tu file ca.crt) =====
const char* ca_cert = \
"-----BEGIN CERTIFICATE-----\n" \
"MIIDYDCCAkigAwIBAgIUEgd7R73XUvM6v7hJgv1an9qAi+cwDQYJKoZIhvcNAQEL\n" \
"BQAwUDELMAkGA1UEBhMCVk4xDDAKBgNVBAgMA0hDTTEMMAoGA1UEBwwDSENNMREw\n" \
"DwYDVQQKDAhEb0FuRFRWVDESMBAGA1UEAwwJTXlNUVRULUNBMB4XDTI2MDcxMTA4\n" \
"NDEyOFoXDTM2MDcwODA4NDEyOFowUDELMAkGA1UEBhMCVk4xDDAKBgNVBAgMA0hD\n" \
"TTEMMAoGA1UEBwwDSENNMREwDwYDVQQKDAhEb0FuRFRWVDESMBAGA1UEAwwJTXlN\n" \
"UVRULUNBMIIBIjANBgkqhkiG9w0BAQEFAAOCAQ8AMIIBCgKCAQEA4ghJmHgDmlJM\n" \
"hJcjTx59zN6UY6CRZ7+8+OEtooyQueoyjkgVP1IwFJ2cnafXkntv7t8sEYtF12kp\n" \
"kmRWoRj0elEKX7bNsJkuB+Pr4GM2tV3gzyUQCqdTPcc19vl4v5a5QIbiSGHGPFRM\n" \
"mvHdgG6jp1Pcd5F5yUYfk+M/yRkDAcR2irFcCQeUynCnkqW4qPEEW2C/JvyPY7hu\n" \
"G5ZWpMuixgeZaT0B13QjDzkqxafFytxKXHnx4+KzjIbh+thCiCYzkl9O2BJeB1j0\n" \
"pa4JDI/AkZjlbO3a3KHxGoc0HontdrV842gqSLviHhfPOi+hbyYqjYKuOQHco1vu\n" \
"Dds/wxZbxwIDAQABozIwMDAdBgNVHQ4EFgQUY1F0CAoY3b/Tegdl8tGWAjalYW8w\n" \
"DwYDVR0TAQH/BAUwAwEB/zANBgkqhkiG9w0BAQsFAAOCAQEAKP1wFJ+SPL9zh1XM\n" \
"QSlYXQqlgI6NfHNYFBLPIxOwhqg+DZkkT9PhCCtLtVMqLdEmpFTzNZjq5Fari/mr\n" \
"zrrUIrYig6dcY1uC3hYxWaCf46YehXYChGXiOWDlwTSgCGU+9iefdYwj+8aTcply\n" \
"F5lVeu64R5n9c9HlFqF3O7Thux1OUeI672cWLxur5clPwCLnmRj5Ma+Qc3u/MHkr\n" \
"/D+ez0Ep68KFvIAQFDUQBvErZSbR4kj+b+JLioQvei+D6pdifb1o7qfbOC4TunXq\n" \
"mHijvnOMi+DMzHBVRCS2TBj5WiWFayHoshMx79m6xZOxx88VWYjmr/l42BdXsQ0V\n" \
"rpTF7A==\n" \
"-----END CERTIFICATE-----\n";

WiFiClientSecure espClient;
PubSubClient client(espClient);
unsigned long msgCount = 0;
unsigned long lastPublish = 0;
const unsigned long PUBLISH_INTERVAL = 10000;
unsigned long lastTlsHandshakeMs = 0;  // thoi gian bat tay TLS gan nhat (ms), 0 = chua ket noi lan nao trong phien nay

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
    Serial.print("Connecting to MQTT broker via TLS...");
    unsigned long tlsStart = millis();
    if (client.connect(client_id)) {
      unsigned long tlsTime = millis() - tlsStart;
      lastTlsHandshakeMs = tlsTime;
      Serial.print("connected! TLS handshake time: ");
      Serial.print(tlsTime);
      Serial.println(" ms");
    } else {
      Serial.print("failed, rc=");
      Serial.print(client.state());
      Serial.println(" retry in 2s");
      delay(2000);
    }
  }
}

void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("\n=== STAGE 3: TLS 1.3 (Plaintext payload over encrypted channel) ===");

  dht.begin();
  delay(2000);

  connectWiFi();

  espClient.setCACert(ca_cert);   // xac thuc CA cert cua broker
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

  char payload[160];
  unsigned long sendTime = millis();
  // heap duoc do NGAY TRUOC khi publish -> phan anh dung RAM con trong luc gui goi tin
  uint32_t heapAtSend = ESP.getFreeHeap();
  snprintf(payload, sizeof(payload),
           "{\"id\":%lu,\"temp\":%.1f,\"humid\":%.1f,\"ts\":%lu,\"heap\":%u,\"enc_us\":0,\"tls_ms\":%lu}",
           msgCount, temp, humid, sendTime, heapAtSend, lastTlsHandshakeMs);

  unsigned long pubStart = micros();
  bool success = client.publish(mqtt_topic, payload);
  unsigned long pubTime = micros() - pubStart;

  uint32_t freeHeapAfter = ESP.getFreeHeap();

  Serial.println("----------------------------------------");
  Serial.print("Msg #"); Serial.println(msgCount);
  Serial.print("Payload: "); Serial.println(payload);
  Serial.print("Payload size: "); Serial.print(strlen(payload)); Serial.println(" bytes");
  Serial.print("Publish time: "); Serial.print(pubTime); Serial.println(" us");
  Serial.print("Publish: "); Serial.println(success ? "OK" : "FAILED");
  Serial.print("Free heap before/after: ");
  Serial.print(freeHeapBefore); Serial.print(" / "); Serial.println(freeHeapAfter);
  Serial.print("Send timestamp (ms): "); Serial.println(sendTime);

  msgCount++;
}
