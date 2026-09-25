#include <WiFi.h>
#include <HTTPClient.h>
#include <esp_now.h>

// ==========================================
// CONFIGURATION
// ==========================================
const char* ssid = "YOUR_WIFI_SSID";
const char* password = "YOUR_WIFI_PASSWORD";

// Camera Module IP (Module 1)
const char* camera_ip = "192.168.1.104";

// Camera Module MAC Address for ESP-NOW instant trigger
uint8_t camera_mac[] = {0xB8, 0xD6, 0x1A, 0xAC, 0x27, 0x4C};

// PIR Sensor Pin (GPIO 13 is easily accessible on ESP32-CAM header)
#define PIR_PIN 13

// Onboard Red Indicator LED (GPIO 33 on AI-Thinker, active LOW)
#define LED_PIN 33

// Recording duration in seconds (2 minutes = 120 seconds)
const int RECORD_DURATION = 120;

// Cooldown between triggers to prevent spamming while recording (in milliseconds)
const unsigned long COOLDOWN_MS = 120000; // 2 minutes

unsigned long last_trigger_time = 0;
bool esp_now_ready = false;

// Trigger packet structure
typedef struct struct_trigger {
  char cmd[16];
  int duration;
} struct_trigger;

struct_trigger trigger_data;

void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("\n--- PIR Motion Trigger Module Initializing ---");

  pinMode(PIR_PIN, INPUT_PULLDOWN); // PIR outputs HIGH (3.3V) on motion
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, HIGH); // Turn off LED (active LOW)

  // Connect to Wi-Fi
  Serial.print("Connecting to Wi-Fi: ");
  Serial.println(ssid);
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);

  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 20) {
    delay(500);
    Serial.print(".");
    attempts++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nWiFi connected! IP: " + WiFi.localIP().toString());
  } else {
    Serial.println("\nWiFi connection timed out. Will rely on ESP-NOW peer link.");
  }

  // Initialize ESP-NOW peer-to-peer link
  if (esp_now_init() == ESP_OK) {
    Serial.println("ESP-NOW Initialized successfully.");
    esp_now_peer_info_t peerInfo = {};
    memcpy(peerInfo.peer_addr, camera_mac, 6);
    peerInfo.channel = 0;
    peerInfo.encrypt = false;
    if (esp_now_add_peer(&peerInfo) == ESP_OK) {
      Serial.println("Camera ESP-NOW Peer registered.");
      esp_now_ready = true;
    } else {
      Serial.println("Failed to add ESP-NOW peer.");
    }
  } else {
    Serial.println("Error initializing ESP-NOW.");
  }

  Serial.println("PIR Sensor is armed on GPIO " + String(PIR_PIN) + ". Ready!");
}

void triggerCamera() {
  Serial.println(">>> MOTION DETECTED! Triggering 5-minute video recording...");
  
  // Blink status LED (active LOW)
  digitalWrite(LED_PIN, LOW);

  // 1. Send via ESP-NOW (instant direct link)
  if (esp_now_ready) {
    strcpy(trigger_data.cmd, "RECORD");
    trigger_data.duration = RECORD_DURATION;
    esp_err_t result = esp_now_send(camera_mac, (uint8_t *) &trigger_data, sizeof(trigger_data));
    if (result == ESP_OK) {
      Serial.println("-> ESP-NOW trigger sent to Camera!");
    } else {
      Serial.println("-> ESP-NOW trigger send error: " + String(result));
    }
  }

  // 2. Send via HTTP GET to Camera Web Server
  if (WiFi.status() == WL_CONNECTED) {
    HTTPClient http;
    String url = "http://" + String(camera_ip) + "/record?duration=" + String(RECORD_DURATION);
    Serial.println("-> Sending HTTP GET: " + url);
    http.begin(url);
    http.setTimeout(4000);
    int httpCode = http.GET();
    if (httpCode > 0) {
      Serial.printf("-> Camera HTTP response: %d\n", httpCode);
    } else {
      Serial.printf("-> Camera HTTP request failed: %s\n", http.errorToString(httpCode).c_str());
    }
    http.end();
  }

  delay(500);
  digitalWrite(LED_PIN, HIGH); // Turn off LED
}

void loop() {
  int pirState = digitalRead(PIR_PIN);

  if (pirState == HIGH) {
    unsigned long now = millis();
    if (now - last_trigger_time > COOLDOWN_MS || last_trigger_time == 0) {
      last_trigger_time = now;
      triggerCamera();
    } else {
      // Still in cooldown period
      unsigned long remainingSec = (COOLDOWN_MS - (now - last_trigger_time)) / 1000;
      Serial.printf("Motion sensed, but recording is currently active (%lu s remaining)\n", remainingSec);
      delay(2000);
    }
  }

  delay(200);
}
