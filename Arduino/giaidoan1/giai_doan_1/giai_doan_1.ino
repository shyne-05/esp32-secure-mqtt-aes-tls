/*
  GIAI DOAN 1 - BASELINE
  Doc cam bien DHT11, publish qua MQTT KHONG ma hoa (plaintext), KHONG TLS
  Dung lam moc doi chung de so sanh overhead o cac giai doan sau

  JSON payload thong nhat schema voi cac giai doan khac de auto_listenerv2.py
  bao cao day du: id, temp, humid, ts, heap (RAM con trong sau khi publish),
  enc_us (=0, khong ma hoa), pub_us (thoi gian goi client.publish)
*/

#include <WiFi.h>
#include <PubSubClient.h>
#include <DHT.h>

const char* ssid = "Tinhdeptrai";
const char* password = "11111111";

const char* mqtt_server = "172.20.10.6";
const int mqtt_port = 1883;
const char* mqtt_topic = "dothi/sensor/baseline";
const char* client_id = "ESP32S3_Baseline";

#define DHTPIN 4
#define DHTTYPE DHT11
DHT dht(DHTPIN, DHTTYPE);

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

void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("\n=== STAGE 1: BASELINE (No Encryption, No TLS) ===");

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

  char payload[160];
  unsigned long sendTime = millis();

  unsigned long pubStart = micros();
  // heap duoc do NGAY TRUOC khi publish -> phan anh dung RAM con trong luc gui goi tin
  uint32_t heapAtSend = ESP.getFreeHeap();
  snprintf(payload, sizeof(payload),
           "{\"id\":%lu,\"temp\":%.1f,\"humid\":%.1f,\"ts\":%lu,\"heap\":%u,\"enc_us\":0}",
           msgCount, temp, humid, sendTime, heapAtSend);
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
