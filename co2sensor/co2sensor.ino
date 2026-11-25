#include <Wire.h>
#include <Adafruit_SGP30.h>
#include <Adafruit_SSD1306.h>
#include <Grove_LED_Bar.h>
#include <DHT.h>
#include <WiFi.h>
#include <PubSubClient.h> // 新增：MQTT 函式庫
#include <ArduinoJson.h>

const char* ssid = "Hima";
const char* pass = "ptis3565";

// MQTT Broker 設定 (使用免費公開 Broker 繞過區網限制)
const char* mqtt_server = "broker.emqx.io";
const int mqtt_port = 1883;
// ★ 請修改為你的專屬主題名稱，避免收到別人的測試資料
const char* mqtt_topic = "ncu/aiot/team1/air_quality"; 

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);
Grove_LED_Bar bar(16, 2, 1);
DHT dht2(27, DHT22);
Adafruit_SGP30 sgp;

WiFiClient espClient;
PubSubClient mqttClient(espClient);

#define LED_PIN 12
#define RELAY_PIN 15
#define BUTTON_PIN 18

bool relayState = false;
int lastBtnState = LOW;
uint16_t triggeredCO2 = 0;
unsigned long relayStartTime = 0;

const unsigned long MIN_RUN_TIME = 15000;
const unsigned long MAX_RUN_TIME = 180000;

unsigned long lastSendTime = 0;
const unsigned long SEND_INTERVAL = 5000; // 每 5 秒發布一次

void setRelay(bool state, uint16_t currentCO2) {
  relayState = state;
  digitalWrite(RELAY_PIN, relayState ? HIGH : LOW);
  
  if (relayState) {
    triggeredCO2 = currentCO2;
    relayStartTime = millis();
    Serial.printf("[Relay ON] 基準濃度: %d ppm\n", triggeredCO2);
  } else {
    Serial.println("[Relay OFF]");
  }
}

// MQTT 斷線自動重連
void reconnectMQTT() {
  while (!mqttClient.connected()) {
    Serial.print("連線 MQTT Broker 中...");
    String clientId = "ESP32_Air_" + String(random(0xffff), HEX);
    if (mqttClient.connect(clientId.c_str())) {
      Serial.println("連線成功!");
    } else {
      Serial.print("失敗，狀態碼=");
      Serial.print(mqttClient.state());
      Serial.println("，3 秒後重試");
      delay(3000);
    }
  }
}

// 打包 JSON 並 Publish 到 MQTT
void publishJsonMQTT(uint16_t eco2, uint16_t tvoc, float temp, float humi, bool relay) {
  if (!mqttClient.connected()) {
    reconnectMQTT();
  }

  StaticJsonDocument<256> doc;
  doc["eco2"] = eco2;
  doc["tvoc"] = tvoc;
  doc["temperature"] = isnan(temp) ? 0.0 : round(temp * 10) / 10.0;
  doc["humidity"] = isnan(humi) ? 0.0 : round(humi * 10) / 10.0;
  doc["relay"] = relay;

  char jsonBuffer[256];
  serializeJson(doc, jsonBuffer);

  if (mqttClient.publish(mqtt_topic, jsonBuffer)) {
    Serial.print("MQTT 發布成功: ");
    Serial.println(jsonBuffer);
  } else {
    Serial.println("MQTT 發布失敗");
  }
}

void setup() {
  Serial.begin(115200);
  pinMode(LED_PIN, OUTPUT);
  pinMode(RELAY_PIN, OUTPUT);
  pinMode(BUTTON_PIN, INPUT);
  
  digitalWrite(RELAY_PIN, LOW);
  digitalWrite(LED_PIN, LOW);
  
  Wire.begin();
  bar.begin();
  dht2.begin();
  
  if (!sgp.begin()) {
    Serial.println("SGP30 not found!");
    while (1);
  }

  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println("SSD1306 allocation failed");
    while (1);
  }
  display.clearDisplay();

  WiFi.begin(ssid, pass);
  Serial.print("連線 Wi-Fi 中");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWi-Fi 連線成功!");

  mqttClient.setServer(mqtt_server, mqtt_port);
  reconnectMQTT();
}

void loop() {
  if (!mqttClient.connected()) {
    reconnectMQTT();
  }
  mqttClient.loop(); // 維持 MQTT 心跳連線

  int btn = digitalRead(BUTTON_PIN);
  if (btn == HIGH && lastBtnState == LOW) {
    delay(20);
    setRelay(!relayState, sgp.eCO2);
  }
  lastBtnState = btn;

  if (!sgp.IAQmeasure()) {
    Serial.println("Measurement failed");
    return;
  }
  
  uint16_t currentCO2 = sgp.eCO2;
  uint16_t currentTVOC = sgp.TVOC;
  float temp = dht2.readTemperature();
  float humi = dht2.readHumidity();
  unsigned long runDuration = millis() - relayStartTime;

  if (currentCO2 > 800) {
    digitalWrite(LED_PIN, HIGH);
  } else {
    digitalWrite(LED_PIN, LOW);
  }

  if (!relayState) {
    if (currentCO2 > 800) {
      setRelay(true, currentCO2);
    }
  } else {
    if (runDuration >= MIN_RUN_TIME) {
      bool droppedEnough = (currentCO2 < 800) && (currentCO2 <= (triggeredCO2 > 300 ? triggeredCO2 - 300 : 0));
      bool timeOut = (runDuration >= MAX_RUN_TIME);
      if (droppedEnough || timeOut) {
        setRelay(false, currentCO2);
      }
    }
  }

  if (currentCO2 - 200 > 1200) bar.setLevel(10);
  else bar.setLevel((currentCO2 - 200) / 120);

  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.println("Air Quality (MQTT)");
  display.printf("eCO2: %d ppm\n", currentCO2);
  display.printf("TVOC: %d ppb\n", currentTVOC);
  display.printf("Temp: %.1f C\n", temp);
  display.printf("Humi: %.1f %% RH\n", humi);
  display.printf("Relay: %s\n", relayState ? "ON" : "OFF");
  display.display();

  if (millis() - lastSendTime >= SEND_INTERVAL) {
    lastSendTime = millis();
    publishJsonMQTT(currentCO2, currentTVOC, temp, humi, relayState);
  }

  delay(200); // 縮短延遲讓 mqttClient.loop() 更順暢
}
