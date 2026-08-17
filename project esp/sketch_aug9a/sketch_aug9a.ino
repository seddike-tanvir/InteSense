#include <WiFi.h>
#include <DHT.h>

// ============================================================
// 🔑 Firebase Credentials (আপনার দেওয়া)
// ============================================================
#define API_KEY        "AIzaSyA8WKGXIAioDQ14Q_z1XpaJvCUkTvBD6_I"
#define DATABASE_URL   "https://intesense-ae5aa-default-rtdb.asia-southeast1.firebasedatabase.app"
#define USER_EMAIL     "admin@intesense.project.tanvirseddike.pro.bd"
#define USER_PASSWORD  "admin123"

// ============================================================
// 🌐 Wi-Fi Credentials
// ============================================================
const char* ssid = "Star Link";
const char* password = "12345678000";

// ============================================================
// 📟 Sensors & Actuators Pin Configuration
// ============================================================
#define DHTPIN 4
#define DHTTYPE DHT11
DHT dht(DHTPIN, DHTTYPE);

#define MQ2_PIN 34
#define FLAME_PIN 13
#define PIR_PIN 14

#define BUZZER_PIN 15
#define RED_LED 16
#define GREEN_LED 17
#define BLUE_LED 18

// ============================================================
// ⚙️ Thresholds & Timers
// ============================================================
float TEMP_THRESHOLD = 40.0;
int SMOKE_THRESHOLD = 1000;
int UPLOAD_INTERVAL = 5000;  // 5 seconds

unsigned long lastUploadTime = 0;
unsigned long lastSettingsCheck = 0;

// ============================================================
// 🔥 Firebase Objects
// ============================================================
#include <FirebaseESP32.h>
#include <addons/RTDBHelper.h>

FirebaseData fbdo;
FirebaseAuth auth;
FirebaseConfig config;

// Function Declarations
void fetchSettings();
void sendToFirebase(float temp, float hum, int smoke, bool flame, bool motion, bool alarm);

// ============================================================
// 🛠 SETUP
// ============================================================
void setup() {
  Serial.begin(115200);
  delay(1000);

  Serial.println("\n\n=====================================");
  Serial.println("   INTESENSE - FIREBASE ESP32 Client");
  Serial.println("=====================================");

  // Pin Initialization
  dht.begin();
  pinMode(FLAME_PIN, INPUT);
  pinMode(PIR_PIN, INPUT);
  pinMode(BUZZER_PIN, OUTPUT);
  pinMode(RED_LED, OUTPUT);
  pinMode(GREEN_LED, OUTPUT);
  pinMode(BLUE_LED, OUTPUT);

  digitalWrite(BUZZER_PIN, LOW);
  digitalWrite(RED_LED, LOW);
  digitalWrite(GREEN_LED, HIGH);
  digitalWrite(BLUE_LED, LOW);
  analogReadResolution(12);

  // ---------- Connect Wi-Fi ----------
  setCpuFrequencyMhz(80);
  WiFi.mode(WIFI_STA);
  WiFi.setTxPower(WIFI_POWER_13dBm);
  Serial.print("📶 Connecting to Wi-Fi...");
  WiFi.begin(ssid, password);
  
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 40) {
    delay(500);
    Serial.print(".");
    attempts++;
  }
  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("✅ Wi-Fi Connected!");
    Serial.print("📌 IP Address: ");
    Serial.println(WiFi.localIP());
    digitalWrite(BLUE_LED, HIGH);
  } else {
    Serial.println("❌ Wi-Fi Connection Failed!");
  }

  // ---------- Firebase Setup ----------
  Serial.println("🔑 Initializing Firebase...");
  
  config.api_key = API_KEY;
  config.database_url = DATABASE_URL;

  // User Email and Password Authentication
  auth.user.email = USER_EMAIL;
  auth.user.password = USER_PASSWORD;

  // Reconnect Wi-Fi automatically if connection drops
  Firebase.reconnectWiFi(true);
  
  Firebase.begin(&config, &auth);

  Serial.println(" Waiting for Firebase Authentication...");
  while (auth.token.uid == "") {
    Serial.print(".");
    delay(500);
  }
  
  Serial.println("\n✅ Firebase Authenticated Successfully!");

  // Fetch initial configuration thresholds
  fetchSettings();

  Serial.println("🟢 System ready!");
  Serial.println("=====================================");
}

// ============================================================
// 📥 Read Settings from Firebase
// ============================================================
void fetchSettings() {
  if (!Firebase.ready()) return;

  Serial.println("⚙️ Reading settings from Firebase...");

  if (Firebase.getJSON(fbdo, "/settings")) {
    FirebaseJson &json = fbdo.jsonObject();
    FirebaseJsonData jsonData;

    if (json.get(jsonData, "temperatureLimit")) {
      TEMP_THRESHOLD = jsonData.stringValue.toFloat();
      Serial.printf("   tempLimit: %.1f°C\n", TEMP_THRESHOLD);
    }
    if (json.get(jsonData, "smokeLimit")) {
      SMOKE_THRESHOLD = jsonData.stringValue.toInt();
      Serial.printf("   smokeLimit: %d\n", SMOKE_THRESHOLD);
    }
    if (json.get(jsonData, "uploadInterval")) {
      UPLOAD_INTERVAL = jsonData.stringValue.toInt();
      Serial.printf("   uploadInterval: %d ms\n", UPLOAD_INTERVAL);
    }
  } else {
    Serial.printf("   ⚠️ Failed to fetch settings: %s\n", fbdo.errorReason().c_str());
  }
}

// ============================================================
// 📤 Send Telemetry Data to Firebase (Blue LED Blink Added)
// ============================================================
void sendToFirebase(float temp, float hum, int smoke, bool flame, bool motion, bool alarm) {
  if (!Firebase.ready() || WiFi.status() != WL_CONNECTED) {
    Serial.println("⚠️ Firebase or Wi-Fi not ready.");
    digitalWrite(BLUE_LED, LOW);
    return;
  }

  // 🟢 ডাটা আপলোড শুরু – Blue LED জ্বালান
  digitalWrite(BLUE_LED, HIGH);

  FirebaseJson json;
  json.set("temperature", temp);
  json.set("humidity", hum);
  json.set("smoke", smoke);
  json.set("flame", flame);
  json.set("motion", motion);
  json.set("alarm", alarm);
  json.set("online", true);
  json.set("timestamp", millis());

  // Update current sensor readings node
  if (Firebase.setJSON(fbdo, "/current", json)) {
    Serial.println("✅ current/ updated!");
    
    // Append entry to history node
    if (Firebase.pushJSON(fbdo, "/history", json)) {
      Serial.println("✅ history/ saved!");
    } else {
      Serial.printf("❌ history/ save failed: %s\n", fbdo.errorReason().c_str());
    }
  } else {
    Serial.printf("❌ current/ update failed: %s\n", fbdo.errorReason().c_str());
  }

  // 🔴 ডাটা আপলোড শেষ – Blue LED নিভান
  digitalWrite(BLUE_LED, LOW);
}

// ============================================================
// 🔁 MAIN LOOP
// ============================================================
void loop() {
  float temperature = dht.readTemperature();
  float humidity = dht.readHumidity();
  int mq2_raw = analogRead(MQ2_PIN);
  bool flame_detected = (digitalRead(FLAME_PIN) == LOW);
  bool motion_detected = (digitalRead(PIR_PIN) == HIGH);

  bool smoke_high = (mq2_raw > SMOKE_THRESHOLD);
  bool temp_high = (temperature > TEMP_THRESHOLD);
  bool alarm = flame_detected || (smoke_high && temp_high);

  // Alarm control logic
  if (alarm) {
    digitalWrite(BUZZER_PIN, HIGH);
    digitalWrite(RED_LED, HIGH);
    digitalWrite(GREEN_LED, LOW);
  } else {
    digitalWrite(BUZZER_PIN, LOW);
    digitalWrite(RED_LED, LOW);
    digitalWrite(GREEN_LED, HIGH);
  }

  // ----- Blue LED এখন আর Wi-Fi স্ট্যাটাস দেখাবে না -----
  // digitalWrite(BLUE_LED, (WiFi.status() == WL_CONNECTED) ? HIGH : LOW); // <-- সরিয়ে ফেলা হয়েছে

  // Periodically fetch updated settings from cloud
  if (millis() - lastSettingsCheck > 5000) {
    lastSettingsCheck = millis();
    fetchSettings();
  }

  // Periodically send sensor readings to cloud
  if (millis() - lastUploadTime >= UPLOAD_INTERVAL) {
    lastUploadTime = millis();
    if (!isnan(temperature) && !isnan(humidity)) {
      Serial.println("\n📤 Sending data to Firebase...");
      Serial.printf("   🌡️ %.1f°C, 💧 %.1f%%\n", temperature, humidity);
      Serial.printf("   🔥 MQ-2: %d, 🚒 Flame: %s, 🚶 Motion: %s\n",
                    mq2_raw, flame_detected ? "ON" : "OFF", motion_detected ? "ON" : "OFF");
      sendToFirebase(temperature, humidity, mq2_raw, flame_detected, motion_detected, alarm);
    } else {
      Serial.println("⚠️ DHT read failed, skipping upload.");
    }
  }

  delay(10);
}