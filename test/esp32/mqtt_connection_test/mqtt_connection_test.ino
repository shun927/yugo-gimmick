/**
 * mqtt_connection_test.ino
 * システム全体像 疎通確認 — ESP32 側
 *
 * 確認できること
 *   1. Wi-Fi 接続
 *   2. MQTT ブローカへの接続
 *   3. hb（心拍）を 1秒ごとに publish
 *   4. cmd トピックを subscribe → LED / reset に反応
 *   5. TOUCH_PIN（D15）LOW 入力で evt を publish
 *
 * 依存ライブラリ（Arduino IDE ライブラリマネージャーで追加）
 *   - PubSubClient  (Nick O'Leary)
 *   - ArduinoJson   (Benoit Blanchon) v6.x
 */

#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>

// ──────────────────────────────────────────
// 設定（環境に合わせて変更）
// ──────────────────────────────────────────
const char* WIFI_SSID   = "<user>";
const char* WIFI_PASS   = "<user>";
const char* MQTT_BROKER = "<user>";  // 管理PC の IP アドレス
const int   MQTT_PORT   = 1883;

const char* STAGE = "stage1";
const char* NODE  = "nodeA";

const int  TOUCH_PIN    = 1;   // LOW になるとevtを送るピン（ボタン or タッチ）
const long HB_INTERVAL  = 1000; // 心拍間隔 ms
const long DEBOUNCE_MS  = 50;   // デバウンス ms
const long COOLDOWN_MS  = 300;  // 連打防止 ms

// テスト用送信内容（実機ではステージに合わせて変更）
const char* TEST_EVT_TYPE = "touch";
const char* TEST_EVT_ID   = "fish";

// ──────────────────────────────────────────
// グローバル
// ──────────────────────────────────────────
char TOPIC_EVT[64];
char TOPIC_CMD[64];
char TOPIC_HB[64];

WiFiClient   wifiClient;
PubSubClient mqtt(wifiClient);

unsigned long lastHb      = 0;
unsigned long lastEvt     = 0;
bool          lastPinState = HIGH;

// ──────────────────────────────────────────
// MQTT 受信コールバック
// ──────────────────────────────────────────
void onMqttMessage(char* topic, byte* payload, unsigned int length) {
  StaticJsonDocument<128> doc;
  DeserializationError err = deserializeJson(doc, payload, length);
  if (err) {
    Serial.printf("[CMD] JSON parse error: %s\n", err.c_str());
    return;
  }

  const char* t  = doc["t"]  | "";
  const char* id = doc["id"] | "";
  int         v  = doc["v"]  | 0;

  Serial.printf("[CMD] t=%s  id=%s  v=%d\n", t, id, v);

  if (strcmp(t, "led") == 0) {
    digitalWrite(LED_BUILTIN, v ? HIGH : LOW);
    Serial.printf("[OUT] LED_BUILTIN -> %s\n", v ? "ON" : "OFF");
  } else if (strcmp(t, "reset") == 0) {
    Serial.println("[OUT] RESET received -> 状態をリセット");
    // TODO: ステージ固有のリセット処理
  }
}

// ──────────────────────────────────────────
// 接続ヘルパー
// ──────────────────────────────────────────
void connectWifi() {
  Serial.printf("[WiFi] Connecting to %s", WIFI_SSID);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.printf("\n[WiFi] Connected. IP: %s\n", WiFi.localIP().toString().c_str());
}

void connectMqtt() {
  mqtt.setServer(MQTT_BROKER, MQTT_PORT);
  mqtt.setCallback(onMqttMessage);

  while (!mqtt.connected()) {
    String clientId = String("esp32-") + STAGE + "-" + NODE;
    Serial.printf("[MQTT] Connecting as %s ...", clientId.c_str());

    if (mqtt.connect(clientId.c_str())) {
      Serial.println(" connected.");
      mqtt.subscribe(TOPIC_CMD);
      Serial.printf("[MQTT] Subscribed: %s\n", TOPIC_CMD);
    } else {
      Serial.printf(" failed (rc=%d), retry in 3s\n", mqtt.state());
      delay(3000);
    }
  }
}

// ──────────────────────────────────────────
// publish ヘルパー
// ──────────────────────────────────────────
void publishHb() {
  StaticJsonDocument<64> doc;
  doc["ts"] = millis();
  char buf[64];
  serializeJson(doc, buf);
  mqtt.publish(TOPIC_HB, buf);
  // Serial.printf("[HB]  %s\n", buf);  // 確認時だけコメント解除
}

void publishEvt(const char* type, const char* id, int v) {
  StaticJsonDocument<128> doc;
  doc["t"]  = type;
  doc["id"] = id;
  doc["v"]  = v;
  doc["ts"] = millis();
  char buf[128];
  serializeJson(doc, buf);
  mqtt.publish(TOPIC_EVT, buf);
  Serial.printf("[EVT] -> %s  %s\n", TOPIC_EVT, buf);
}

// ──────────────────────────────────────────
// setup / loop
// ──────────────────────────────────────────
void setup() {
  Serial.begin(115200);
  delay(500);

  pinMode(LED_BUILTIN, OUTPUT);
  pinMode(TOUCH_PIN, INPUT_PULLUP);

  snprintf(TOPIC_EVT, sizeof(TOPIC_EVT), "yugo/%s/%s/evt", STAGE, NODE);
  snprintf(TOPIC_CMD, sizeof(TOPIC_CMD), "yugo/%s/%s/cmd", STAGE, NODE);
  snprintf(TOPIC_HB,  sizeof(TOPIC_HB),  "yugo/%s/%s/hb",  STAGE, NODE);

  Serial.println("=== MQTT Connection Test (ESP32) ===");
  Serial.printf("  EVT: %s\n  CMD: %s\n  HB : %s\n", TOPIC_EVT, TOPIC_CMD, TOPIC_HB);

  connectWifi();
  connectMqtt();

  Serial.println("=== READY ===");
  Serial.println("  D15 LOW -> evt publish");
  Serial.println("  hb  送信中 (1秒ごと)");
}

void loop() {
  if (!mqtt.connected()) connectMqtt();
  mqtt.loop();

  unsigned long now = millis();

  // ── 心拍送信 ──────────────────────────────
  if (now - lastHb >= (unsigned long)HB_INTERVAL) {
    lastHb = now;
    publishHb();
  }

  // ── タッチ/ボタン入力（デバウンス + クールダウン）──
  bool pinState = digitalRead(TOUCH_PIN);
  if (lastPinState == HIGH && pinState == LOW) {
    if (now - lastEvt >= (unsigned long)COOLDOWN_MS) {
      delay(DEBOUNCE_MS);
      if (digitalRead(TOUCH_PIN) == LOW) {
        lastEvt = now;
        publishEvt(TEST_EVT_TYPE, TEST_EVT_ID, 1);
      }
    }
  }
  lastPinState = pinState;
}
