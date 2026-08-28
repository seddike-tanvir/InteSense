#include <WiFi.h>
#include <DHT.h>
#include <time.h>
#include <FirebaseESP32.h>       // Correct header for "Firebase ESP32 Client" library
#include <addons/RTDBHelper.h>   // Helper for FirebaseJson

// ============================================================
// 🔑 Firebase Credentials
// ============================================================
#define API_KEY        "AIzaSyA8WKGXIAioDQ14Q_z1XpaJvCUkTvBD6_I"
#define DATABASE_URL   "https://intesense-ae5aa-default-rtdb.asia-southeast1.firebasedatabase.app"
#define USER_EMAIL     "admin@intesense.project.tanvirseddike.pro.bd"
#define USER_PASSWORD  "admin123"

// ============================================================
// 🌐 Multiple Wi-Fi Credentials
// ============================================================
struct WiFiNetwork {
  const char* ssid;
  const char* password;
};

WiFiNetwork networks[] = {
  {"Star Link", "12345678000"},
  {"netis", "123456789"},
  {"Seddike's Galaxy M12", "jaMonChayDe"}
};
const int numNetworks = sizeof(networks) / sizeof(networks[0]);

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
int UPLOAD_INTERVAL = 5000; // 5 seconds

unsigned long lastUploadTime = 0;
unsigned long lastSettingsCheck = 0;
unsigned long lastReconnectAttempt = 0;
const unsigned long RECONNECT_COOLDOWN = 15000; // 15 seconds cooldown for Wi-Fi retry

// ============================================================
// 🔥 Firebase Objects
// ============================================================
FirebaseData fbdo;
FirebaseAuth auth;
FirebaseConfig config;

bool firebaseReady = false;

unsigned long long getEpochMillis() {
  time_t now;
  time(&now);
  return static_cast<unsigned long long>(now) * 1000ULL;
}

// Function Declarations
void fetchSettings();
void sendToFirebase(float temp, float hum, int smoke, bool flame, bool motion, bool alarm);
bool connectToWiFi();

// ============================================================
// 🛠 SETUP
// ============================================================
void setup() {
  Serial.begin(115200);
  delay(1000);

  Serial.println("\n\n=====================================");
  Serial.println("   INTESENSE - Firebase ESP32 Client");
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

  // Connect to Wi-Fi
  setCpuFrequencyMhz(80); // Helps prevent brownout by slightly reducing CPU power draw
  bool wifiConnected = connectToWiFi();
  
  if (wifiConnected) {
    digitalWrite(BLUE_LED, HIGH);
    configTime(0, 0, "pool.ntp.org", "time.nist.gov");
    Serial.println("⏳ Synchronizing clock...");
    time_t now = 0;
    int timeAttempts = 0;
    while (now < 1700000000 && timeAttempts < 20) {
      delay(500);
      time(&now);
      timeAttempts++;
    }
    if (now >= 1700000000) Serial.println("✅ Clock synchronized.");
    else Serial.println("⚠️ Clock synchronization failed; timestamps may be invalid.");
  } else {
    Serial.println("⚠️ Proceeding without Wi-Fi. Will retry in background.");
  }

  // Initialize Firebase
  Serial.println("🔑 Initializing Firebase...");
  config.api_key = API_KEY;
  config.database_url = DATABASE_URL;
  auth.user.email = USER_EMAIL;
  auth.user.password = USER_PASSWORD;

  Firebase.reconnectWiFi(true);
  Firebase.begin(&config, &auth);

  Serial.println("⏳ Waiting for Firebase Authentication...");
  int authAttempts = 0;
  while (!Firebase.ready() && authAttempts < 40) {
    Serial.print(".");
    delay(500);
    authAttempts++;
  }
  
  if (Firebase.ready()) {
    Serial.println("\n✅ Firebase Authenticated Successfully!");
    firebaseReady = true;
  } else {
    Serial.println("\n❌ Firebase Authentication Failed or Timed Out!");
  }

  fetchSettings();

  Serial.println("🟢 System ready!");
  Serial.println("=====================================");
}

// ============================================================
// 📶 Wi-Fi Connection Helper Function (Auto-tries ALL networks)
// ============================================================
bool connectToWiFi() {
  Serial.println("🔄 Initiating Wi-Fi Connection...");
  
  WiFi.disconnect(true); // Clear previous connections
  delay(100);
  
  WiFi.mode(WIFI_STA);
  WiFi.setTxPower(WIFI_POWER_13dBm); // Balanced power to prevent brownout
  
  for (int i = 0; i < numNetworks; i++) {
    Serial.print("📶 Trying: ");
    Serial.println(networks[i].ssid);
    
    WiFi.begin(networks[i].ssid, networks[i].password);
    
    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 20) { // 10 seconds max per network
      delay(500);
      Serial.print(".");
      attempts++;
    }
    Serial.println();
    
    if (WiFi.status() == WL_CONNECTED) {
      Serial.println("✅ Wi-Fi Connected!");
      Serial.print("📌 IP: ");
      Serial.println(WiFi.localIP());
      return true;
    } else {
      Serial.println("❌ Failed. Cleaning up and trying next...");
      WiFi.disconnect(true);
      delay(100);
    }
  }
  
  Serial.println("⚠️ All Wi-Fi networks failed.");
  return false;
}

// ============================================================
// 📥 Read Settings from Firebase
// ============================================================
void fetchSettings() {
  if (!firebaseReady) return;

  Serial.println("⚙️ Reading settings from Firebase...");
  
  if (Firebase.getJSON(fbdo, "/settings")) {
    FirebaseJson json = fbdo.jsonObject();
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
// 📤 Send Data to Firebase
// ============================================================
void sendToFirebase(float temp, float hum, int smoke, bool flame, bool motion, bool alarm) {
  if (!firebaseReady || WiFi.status() != WL_CONNECTED) {
    Serial.println("⚠️ Firebase or Wi-Fi not ready.");
    digitalWrite(BLUE_LED, LOW);
    return;
  }

  digitalWrite(BLUE_LED, HIGH); // Indicate upload start

  FirebaseJson json;
  json.set("temperature", temp);
  json.set("humidity", hum);
  json.set("smoke", smoke);
  json.set("flame", flame);
  json.set("motion", motion);
  json.set("alarm", alarm);
  json.set("online", true);
  json.set("timestamp", getEpochMillis());

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

  digitalWrite(BLUE_LED, LOW); // Indicate upload end
}

// ============================================================
// 🔁 MAIN LOOP
// ============================================================
void loop() {
  // Smart Re-try Wi-Fi connection in the background if it drops
  if (WiFi.status() != WL_CONNECTED) {
    if (millis() - lastReconnectAttempt > RECONNECT_COOLDOWN) {
      lastReconnectAttempt = millis();
      connectToWiFi(); // This will try ALL networks in your list
    }
  }

  // Read Sensors
  float temperature = dht.readTemperature();
  float humidity = dht.readHumidity();
  int mq2_raw = analogRead(MQ2_PIN);
  bool flame_detected = (digitalRead(FLAME_PIN) == LOW); // LOW means flame detected for most modules
  bool motion_detected = (digitalRead(PIR_PIN) == HIGH);

  // Alarm Logic
  bool smoke_high = (mq2_raw > SMOKE_THRESHOLD);
  bool temp_high = (temperature > TEMP_THRESHOLD);
  bool alarm = flame_detected || (smoke_high && temp_high);

  if (alarm) {
    digitalWrite(BUZZER_PIN, HIGH);
    digitalWrite(RED_LED, HIGH);
    digitalWrite(GREEN_LED, LOW);
  } else {
    digitalWrite(BUZZER_PIN, LOW);
    digitalWrite(RED_LED, LOW);
    digitalWrite(GREEN_LED, HIGH);
  }

  // Periodically fetch updated settings from cloud (every 5 seconds)
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