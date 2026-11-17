#include <Wire.h>
#include <Adafruit_SGP30.h>
#include <Adafruit_SSD1306.h>
#include <Grove_LED_Bar.h>
#include <DHT.h>

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);
Grove_LED_Bar bar(16, 2, 1);
DHT dht2(27, DHT22);

Adafruit_SGP30 sgp;

#define LED_PIN 12
#define RELAY_PIN 15
#define BUTTON_PIN 18

// 狀態控制變數
bool relayState = false;          // 目前 Relay 狀態 (true: 開, false: 關)
int lastBtnState = LOW;           // 按鈕前次狀態
uint16_t triggeredCO2 = 0;        // 開啟當下的 CO2 濃度基準
unsigned long relayStartTime = 0; // 開啟時的時間戳記

// 時間參數設定 (毫秒)
const unsigned long MIN_RUN_TIME = 15000;    // 最短開啟時間 (15 秒，避免開關頻繁切換)
const unsigned long MAX_RUN_TIME = 180000;   // 超時強制關閉時間 (3 分鐘，避免一直降不下來卡住)

void setRelay(bool state, uint16_t currentCO2) {
  relayState = state;
  digitalWrite(RELAY_PIN, relayState ? HIGH : LOW);
  
  if (relayState) {
    triggeredCO2 = currentCO2;     // 記錄觸發基準值
    relayStartTime = millis();     // 記錄啟動時間
    Serial.printf("[Relay ON] 基準濃度: %d ppm\n", triggeredCO2);
  } else {
    Serial.println("[Relay OFF]");
  }
}

void setup() {
  Serial.begin(115200);
  pinMode(LED_PIN, OUTPUT);
  pinMode(RELAY_PIN, OUTPUT);
  pinMode(BUTTON_PIN, INPUT); // 若按鈕沒有外接下拉電阻，可改用 INPUT_PULLUP
  
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
  
  Serial.println("系統初始化完成");
}

void loop() {
  // 1. 讀取按鈕 (加入邊緣檢測)
  int btn = digitalRead(BUTTON_PIN);
  if (btn == HIGH && lastBtnState == LOW) {
    delay(20); // 簡易軟體防彈跳
    // 按鈕切換：如果開著就直接手動關閉；如果關著就手動打開
    setRelay(!relayState, sgp.eCO2);
  }
  lastBtnState = btn;

  // 2. 讀取感測器數值
  if (!sgp.IAQmeasure()) {
    Serial.println("Measurement failed");
    return;
  }
  
  uint16_t currentCO2 = sgp.eCO2;
  unsigned long runDuration = millis() - relayStartTime;

  // 3. LED 警示：超過 800 ppm 恆亮
  if (currentCO2 > 800) {
    digitalWrite(LED_PIN, HIGH);
  } else {
    digitalWrite(LED_PIN, LOW);
  }

  // 4. Relay 自動控制邏輯
  if (!relayState) {
    // 【情境 A：Relay 目前為關閉】
    // 若濃度高於 800，自動開啟
    if (currentCO2 > 800) {
      setRelay(true, currentCO2);
    }
  } else {
    // 【情境 B：Relay 目前為開啟】
    // 必須先運轉滿「最短開啟時間」以保護電驛
    if (runDuration >= MIN_RUN_TIME) {
      
      // 條件 1：濃度已低於 800 且 相比啟動時已下降 300 ppm 以上
      bool droppedEnough = (currentCO2 < 800) && (currentCO2 <= (triggeredCO2 > 300 ? triggeredCO2 - 300 : 0));
      
      // 條件 2：持續運轉超過設定的最長時間 (避免空氣無法改善時馬達無限運轉)
      bool timeOut = (runDuration >= MAX_RUN_TIME);

      if (droppedEnough || timeOut) {
        if (timeOut) {
          Serial.println("達最長通風時間，關閉 Relay");
        } else {
          Serial.println("濃度已降低 300 ppm 且低於 800，關閉 Relay");
        }
        setRelay(false, currentCO2);
      }
    }
  }

  if(currentCO2-200>1200)bar.setLevel(10);
  else bar.setLevel((currentCO2-200)/120);

  // 5. OLED 顯示
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.println("Air Quality Monitor");
  display.printf("eCO2: %d ppm\n", currentCO2);
  display.printf("TVOC: %d ppb\n", sgp.TVOC);
  display.printf("Temp: %.1f C\n", dht2.readTemperature());
  display.printf("Humi: %.1f %% RH\n", dht2.readHumidity());
  display.printf("Relay: %s\n", relayState ? "ON" : "OFF");
  
  display.display();

  delay(1000); // SGP30 建議每 1 秒採樣一次
}
