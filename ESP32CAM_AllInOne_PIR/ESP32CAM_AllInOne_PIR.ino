#include <Arduino.h>
#include "esp_camera.h"
#include <WiFi.h>
#include <WebServer.h>
#include "FS.h"
#include "SD_MMC.h"

// ==========================================
// CONFIGURATION
// ==========================================
const char* ssid = "YOUR_WIFI_SSID";
const char* password = "YOUR_WIFI_PASSWORD";

// Camera Pins for AI-THINKER ESP32-CAM
#define PWDN_GPIO_NUM     32
#define RESET_GPIO_NUM    -1
#define XCLK_GPIO_NUM      0
#define SIOD_GPIO_NUM     26
#define SIOC_GPIO_NUM     27
#define Y9_GPIO_NUM       35
#define Y8_GPIO_NUM       34
#define Y7_GPIO_NUM       39
#define Y6_GPIO_NUM       36
#define Y5_GPIO_NUM       21
#define Y4_GPIO_NUM       19
#define Y3_GPIO_NUM       18
#define Y2_GPIO_NUM        5
#define VSYNC_GPIO_NUM    25
#define HREF_GPIO_NUM     23
#define PCLK_GPIO_NUM     22

// Hardware Pins on ESP32-CAM Header
#define FLASH_LED_PIN      4   // Front bright white flash LED (PWM controlled)
#define PIR_PIN           13   // PIR Sensor OUT / Signal pin (IO13 is free in 1-bit SD mode!)
#define RED_LED_PIN       33   // Small red indicator LED on back of module (Active LOW)

WebServer server(80);

// Global State
bool isRecording = false;
unsigned long recordStartTime = 0;
unsigned long recordDurationSec = 120; // 2 minutes default per motion trigger
uint32_t totalFrames = 0;
File aviFile;
String currentFileName = "";
bool sdMounted = false;

// Controls & Settings
int flashDuty = 0;         // Front Flash brightness: 0 (Off) to 255 (Max)
int vflipState = 1;        // Default 1 (flipped) so image is right-side up!
int hmirrorState = 1;      // Default 1 (mirrored)
int currentFrameSize = 7;  // FRAMESIZE_SVGA (800x600) default
bool pirArmed = true;      // Can toggle motion sensing from the web dashboard
unsigned long lastMotionTime = 0;
const unsigned long PIR_COOLDOWN_MS = 5000; // 5-second buffer after recording finishes

// ==========================================
// AVI HEADER GENERATOR HELPERS
// ==========================================
void writeLittleEndian(uint32_t val, uint8_t *buf, int len) {
  for (int i = 0; i < len; i++) {
    buf[i] = (uint8_t)(val & 0xFF);
    val >>= 8;
  }
}

bool startAviFile(String fname, int width, int height, int fps) {
  aviFile = SD_MMC.open(fname, FILE_WRITE);
  if (!aviFile) {
    Serial.println("Failed to create file: " + fname);
    return false;
  }

  uint8_t header[240] = {0};

  // RIFF Chunk
  memcpy(&header[0], "RIFF", 4);
  memcpy(&header[8], "AVI ", 4);

  // hdrl LIST
  memcpy(&header[12], "LIST", 4);
  writeLittleEndian(192, &header[16], 4);
  memcpy(&header[20], "hdrl", 4);

  // avih chunk
  memcpy(&header[24], "avih", 4);
  writeLittleEndian(56, &header[28], 4);
  uint32_t us_per_frame = 1000000 / fps;
  writeLittleEndian(us_per_frame, &header[32], 4);
  writeLittleEndian(0x00000810, &header[44], 4); // Flags
  writeLittleEndian(1, &header[56], 4);          // Streams
  writeLittleEndian(width, &header[64], 4);
  writeLittleEndian(height, &header[68], 4);

  // strl LIST (video stream)
  memcpy(&header[76], "LIST", 4);
  writeLittleEndian(116, &header[80], 4);
  memcpy(&header[84], "strl", 4);

  // strh chunk
  memcpy(&header[88], "strh", 4);
  writeLittleEndian(56, &header[92], 4);
  memcpy(&header[96], "vids", 4);
  memcpy(&header[100], "MJPG", 4);
  writeLittleEndian(1, &header[116], 4);   // Rate scale
  writeLittleEndian(fps, &header[120], 4); // Rate
  writeLittleEndian(1024 * 64, &header[132], 4);
  writeLittleEndian(10000, &header[136], 4);

  // strf chunk
  memcpy(&header[148], "strf", 4);
  writeLittleEndian(40, &header[152], 4);
  writeLittleEndian(40, &header[156], 4);
  writeLittleEndian(width, &header[160], 4);
  writeLittleEndian(height, &header[164], 4);
  header[168] = 1; header[169] = 0;
  header[170] = 24; header[171] = 0;
  memcpy(&header[172], "MJPG", 4);
  writeLittleEndian(width * height * 3, &header[176], 4);

  // movi LIST
  memcpy(&header[200], "LIST", 4);
  memcpy(&header[208], "movi", 4);

  aviFile.write(header, 212);
  return true;
}

void finalizeAviFile(int fps) {
  if (!aviFile) return;

  uint32_t fileSize = aviFile.size();
  uint32_t riffSize = fileSize - 8;
  uint32_t moviSize = fileSize - 212;

  // Update RIFF size
  aviFile.seek(4);
  uint8_t buf4[4];
  writeLittleEndian(riffSize, buf4, 4);
  aviFile.write(buf4, 4);

  // Update total frames in avih
  aviFile.seek(48);
  writeLittleEndian(totalFrames, buf4, 4);
  aviFile.write(buf4, 4);

  // Update stream length in strh
  aviFile.seek(128);
  writeLittleEndian(totalFrames, buf4, 4);
  aviFile.write(buf4, 4);

  // Update movi size
  aviFile.seek(204);
  writeLittleEndian(moviSize, buf4, 4);
  aviFile.write(buf4, 4);

  aviFile.close();
  Serial.printf(">>> Video saved: %s (%u frames, %u bytes)\n", currentFileName.c_str(), totalFrames, fileSize);
}

// ==========================================
// RECORDING ENGINE
// ==========================================
void startRecording(int durationSec) {
  if (isRecording) {
    Serial.println("Recording already in progress.");
    return;
  }
  if (!sdMounted) {
    Serial.println("Cannot record: MicroSD card not mounted!");
    return;
  }

  // Find next sequential filename
  int idx = 1;
  while (true) {
    char buf[32];
    sprintf(buf, "/vid_%04d.avi", idx);
    if (!SD_MMC.exists(buf)) {
      currentFileName = String(buf);
      break;
    }
    idx++;
  }

  Serial.println(">>> Starting video recording: " + currentFileName + " (" + String(durationSec) + " sec)");
  totalFrames = 0;
  recordDurationSec = durationSec;

  // SVGA resolution 800x600 at 10 fps
  if (startAviFile(currentFileName, 800, 600, 10)) {
    isRecording = true;
    recordStartTime = millis();
    digitalWrite(RED_LED_PIN, LOW); // Turn on red indicator LED during record
  }
}

void stopRecording() {
  if (!isRecording) return;
  isRecording = false;
  finalizeAviFile(10);
  digitalWrite(RED_LED_PIN, HIGH); // Turn off red indicator LED
}

void recordFrame() {
  if (!isRecording) return;

  if ((millis() - recordStartTime) >= (recordDurationSec * 1000)) {
    stopRecording();
    return;
  }

  camera_fb_t *fb = esp_camera_fb_get();
  if (!fb) {
    Serial.println("Frame capture dropped!");
    return;
  }

  // Write AVI frame chunk: "00dc" + size + JPEG buffer + padding
  uint32_t len = fb->len;
  uint8_t chunkHeader[8];
  memcpy(&chunkHeader[0], "00dc", 4);
  writeLittleEndian(len, &chunkHeader[4], 4);

  aviFile.write(chunkHeader, 8);
  aviFile.write(fb->buf, len);

  if (len % 2 != 0) {
    uint8_t pad = 0;
    aviFile.write(&pad, 1);
  }

  totalFrames++;
  esp_camera_fb_return(fb);
}

// ==========================================
// WEB SERVER HANDLERS
// ==========================================
void handleRoot() {
  String html = "<!DOCTYPE html><html><head><meta name='viewport' content='width=device-width, initial-scale=1'><title>ESP32-CAM All-in-One Hub</title>";
  html += "<style>body{font-family:Arial,sans-serif;margin:20px;background:#1e1e2f;color:#fff;}";
  html += "h1{color:#4CAF50;} .card{background:#2a2a3e;padding:20px;border-radius:10px;margin-bottom:20px;}";
  html += "a.btn,button{background:#4CAF50;color:#fff;border:none;padding:10px 18px;border-radius:5px;cursor:pointer;text-decoration:none;display:inline-block;margin:4px;}";
  html += "a.btn-red,button.btn-red{background:#e53935;} a.btn-blue{background:#2196F3;} a.btn-grey{background:#555;}";
  html += "table{width:100%;border-collapse:collapse;margin-top:10px;} td,th{padding:10px;text-align:left;border-bottom:1px solid #444;}";
  html += "</style></head><body>";

  html += "<h1>ESP32-CAM All-in-One Security Hub</h1>";

  // Status & Record Card
  html += "<div class='card'>";
  html += "<h3>Status: " + String(isRecording ? "<span style='color:#e53935;'>RECORDING ACTIVE (" + String(totalFrames) + " frames)</span>" : "<span style='color:#4CAF50;'>READY / ARMED</span>") + "</h3>";
  html += "<p><b>PIR Motion Sensor (IO13):</b> " + String(pirArmed ? "<span style='color:#4CAF50;'>ARMED (Active)</span>" : "<span style='color:#FFA000;'>DISARMED (Muted)</span>") + " &mdash; <a href='/pir_toggle' class='btn' style='padding:5px 10px;font-size:12px;background:#777;'>" + String(pirArmed ? "Disarm Motion" : "Arm Motion") + "</a></p>";
  html += "<p><b>MicroSD Card:</b> " + String(sdMounted ? "Mounted (" + String(SD_MMC.totalBytes() / (1024*1024)) + " MB total, " + String((SD_MMC.totalBytes() - SD_MMC.usedBytes()) / (1024*1024)) + " MB free)" : "<span style='color:#e53935;'>Not Mounted</span>") + "</p>";

  if (isRecording) {
    html += "<a href='/stop' class='btn btn-red'>Stop Recording</a> ";
  } else {
    html += "<a href='/record?duration=120' class='btn'>Record 2 Min</a> ";
    html += "<a href='/record?duration=60' class='btn btn-grey'>1 Min</a> ";
    html += "<a href='/record?duration=300' class='btn btn-grey'>5 Min</a> ";
  }
  html += "<a href='/capture' class='btn btn-blue'>Take Still Photo</a> ";
  html += "<a href='/stream' target='_blank' class='btn'>Live Stream</a>";
  html += "</div>";

  // Camera Settings & Controls Card
  html += "<div class='card'>";
  html += "<h3>Camera Settings & Flash</h3>";
  html += "<p style='margin-bottom:12px;'><b>Resolution: </b>";
  html += "<select id='resSelect' onchange='setResolution(this.value)' style='padding:6px 12px;font-size:14px;border-radius:4px;background:#1e1e2f;color:#fff;border:1px solid #555;'>";
  html += "<option value='10'" + String(currentFrameSize == 10 ? " selected" : "") + ">UXGA (1600x1200)</option>";
  html += "<option value='9'" + String(currentFrameSize == 9 ? " selected" : "") + ">SXGA (1280x1024)</option>";
  html += "<option value='8'" + String(currentFrameSize == 8 ? " selected" : "") + ">XGA (1024x768)</option>";
  html += "<option value='7'" + String(currentFrameSize == 7 ? " selected" : "") + ">SVGA (800x600 - Default)</option>";
  html += "<option value='6'" + String(currentFrameSize == 6 ? " selected" : "") + ">VGA (640x480)</option>";
  html += "<option value='4'" + String(currentFrameSize == 4 ? " selected" : "") + ">QVGA (320x240)</option>";
  html += "</select></p>";

  html += "<p style='margin-bottom:15px;'>";
  html += "<label style='margin-right:20px;cursor:pointer;font-size:15px;'>";
  html += "<input type='checkbox' id='vflipCheck' onchange='toggleFlip()' " + String(vflipState ? "checked" : "") + "> <b>Flip Vertical (Upside Down Fix)</b>";
  html += "</label>";
  html += "<label style='cursor:pointer;font-size:15px;'>";
  html += "<input type='checkbox' id='hmirrorCheck' onchange='toggleMirror()' " + String(hmirrorState ? "checked" : "") + "> Mirror Horizontal";
  html += "</label>";
  html += "</p>";

  html += "<hr style='border:0;border-top:1px solid #444;margin:15px 0;'>";
  html += "<h4>Front Flash Light</h4>";
  html += "<p>Brightness: <span id='ledVal' style='font-weight:bold;color:#4CAF50;'>" + String(flashDuty) + "</span> / 255</p>";
  html += "<input type='range' id='ledSlider' min='0' max='255' value='" + String(flashDuty) + "' oninput='updateLed(this.value)' style='width:100%;max-width:360px;'><br>";
  html += "<div style='margin-top:10px;'>";
  html += "<button onclick='setLed(0)' class='btn btn-red'>Off</button> ";
  html += "<button onclick='setLed(25)' class='btn'>Low (25)</button> ";
  html += "<button onclick='setLed(100)' class='btn'>Medium (100)</button> ";
  html += "<button onclick='setLed(255)' class='btn'>Max (255)</button>";
  html += "</div>";
  html += "</div>";

  // SD Card Media Library Card
  html += "<div class='card'>";
  html += "<h3>Media Files on MicroSD Card</h3>";
  html += "<table><tr><th>Type / Filename</th><th>Size</th><th>Actions</th></tr>";

  if (sdMounted) {
    File root = SD_MMC.open("/");
    File file = root.openNextFile();
    int count = 0;
    while (file) {
      String name = String(file.name());
      if (name.endsWith(".avi") || name.endsWith(".AVI") || name.endsWith(".jpg") || name.endsWith(".JPG")) {
        count++;
        String typeTag = (name.endsWith(".avi") || name.endsWith(".AVI")) ? "<span style='color:#FF9800;'>[VIDEO]</span> " : "<span style='color:#2196F3;'>[PHOTO]</span> ";
        html += "<tr><td>" + typeTag + name + "</td><td>" + String(file.size() / 1024) + " KB</td>";
        html += "<td><a href='/download?file=" + name + "' target='_blank' class='btn'>View / Download</a> ";
        html += "<a href='/delete?file=" + name + "' class='btn btn-red' onclick='return confirm(\"Delete?\")'>Delete</a></td></tr>";
      }
      file = root.openNextFile();
    }
    if (count == 0) {
      html += "<tr><td colspan='3'>No recordings or photos found yet.</td></tr>";
    }
  } else {
    html += "<tr><td colspan='3'>Insert an SD card and restart the device.</td></tr>";
  }

  html += "</table></div>";

  // JavaScript for real-time controls
  html += "<script>";
  html += "function updateLed(val) { document.getElementById('ledVal').innerText = val; fetch('/led?val=' + val); }";
  html += "function setLed(val) { document.getElementById('ledSlider').value = val; updateLed(val); }";
  html += "function toggleFlip() { var val = document.getElementById('vflipCheck').checked ? 1 : 0; fetch('/flip?v=' + val); }";
  html += "function toggleMirror() { var val = document.getElementById('hmirrorCheck').checked ? 1 : 0; fetch('/flip?h=' + val); }";
  html += "function setResolution(val) { fetch('/resolution?val=' + val); }";
  html += "</script></body></html>";

  server.send(200, "text/html", html);
}

void handlePirToggle() {
  pirArmed = !pirArmed;
  Serial.printf("PIR state toggled: %s\n", pirArmed ? "ARMED" : "DISARMED");
  server.sendHeader("Location", "/");
  server.send(303);
}

void handleLed() {
  if (server.hasArg("val")) {
    flashDuty = server.arg("val").toInt();
    if (flashDuty < 0) flashDuty = 0;
    if (flashDuty > 255) flashDuty = 255;
    ledcWrite(FLASH_LED_PIN, flashDuty);
    Serial.printf("Flash LED duty: %d\n", flashDuty);
  }
  server.send(200, "text/plain", String(flashDuty));
}

void handleFlip() {
  sensor_t *s = esp_camera_sensor_get();
  if (server.hasArg("v")) {
    vflipState = server.arg("v").toInt();
    if (s) s->set_vflip(s, vflipState);
    Serial.printf("Set vflip: %d\n", vflipState);
  }
  if (server.hasArg("h")) {
    hmirrorState = server.arg("h").toInt();
    if (s) s->set_hmirror(s, hmirrorState);
    Serial.printf("Set hmirror: %d\n", hmirrorState);
  }
  server.send(200, "text/plain", "OK");
}

void handleResolution() {
  if (server.hasArg("val")) {
    int res = server.arg("val").toInt();
    sensor_t *s = esp_camera_sensor_get();
    if (s) {
      s->set_framesize(s, (framesize_t)res);
      currentFrameSize = res;
      Serial.printf("Set resolution framesize: %d\n", res);
    }
  }
  server.send(200, "text/plain", "OK");
}

void handleCapture() {
  if (!sdMounted) {
    server.send(500, "text/plain", "SD Card not mounted");
    return;
  }
  camera_fb_t *fb = esp_camera_fb_get();
  if (!fb) {
    server.send(500, "text/plain", "Camera capture failed");
    return;
  }

  int idx = 1;
  String photoName = "";
  while (true) {
    char buf[32];
    sprintf(buf, "/photo_%04d.jpg", idx);
    if (!SD_MMC.exists(buf)) {
      photoName = String(buf);
      break;
    }
    idx++;
  }

  File file = SD_MMC.open(photoName, FILE_WRITE);
  if (file) {
    file.write(fb->buf, fb->len);
    file.close();
    Serial.println("Saved photo to SD: " + photoName);
  }
  esp_camera_fb_return(fb);

  server.sendHeader("Location", "/");
  server.send(303);
}

void handleRecord() {
  int dur = 120;
  if (server.hasArg("duration")) {
    dur = server.arg("duration").toInt();
    if (dur <= 0) dur = 120;
  }
  startRecording(dur);
  server.sendHeader("Location", "/");
  server.send(303);
}

void handleStop() {
  stopRecording();
  server.sendHeader("Location", "/");
  server.send(303);
}

void handleDownload() {
  if (!server.hasArg("file")) {
    server.send(400, "text/plain", "Missing file parameter");
    return;
  }
  String fname = server.arg("file");
  if (!fname.startsWith("/")) fname = "/" + fname;

  if (!SD_MMC.exists(fname)) {
    server.send(404, "text/plain", "File Not Found");
    return;
  }

  File f = SD_MMC.open(fname, FILE_READ);
  if (fname.endsWith(".jpg") || fname.endsWith(".JPG")) {
    server.streamFile(f, "image/jpeg");
  } else {
    server.streamFile(f, "video/x-msvideo");
  }
  f.close();
}

void handleDelete() {
  if (!server.hasArg("file")) {
    server.send(400, "text/plain", "Missing file parameter");
    return;
  }
  String fname = server.arg("file");
  if (!fname.startsWith("/")) fname = "/" + fname;

  if (SD_MMC.exists(fname)) {
    SD_MMC.remove(fname);
  }
  server.sendHeader("Location", "/");
  server.send(303);
}

// Live Stream Handler
void handleStream() {
  WiFiClient client = server.client();
  String response = "HTTP/1.1 200 OK\r\nContent-Type: multipart/x-mixed-replace; boundary=frame\r\n\r\n";
  client.print(response);

  while (client.connected()) {
    camera_fb_t *fb = esp_camera_fb_get();
    if (!fb) break;

    client.printf("--frame\r\nContent-Type: image/jpeg\r\nContent-Length: %u\r\n\r\n", fb->len);
    client.write(fb->buf, fb->len);
    client.print("\r\n");
    esp_camera_fb_return(fb);
    delay(40); // ~25 fps preview
  }
}

// ==========================================
// SETUP & LOOP
// ==========================================
void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("\n--- ESP32-CAM All-in-One Security Hub (PIR + SD + Web) ---");

  // Status & PIR Pins
  pinMode(RED_LED_PIN, OUTPUT);
  digitalWrite(RED_LED_PIN, HIGH); // Turn off red LED (Active LOW)

  pinMode(PIR_PIN, INPUT_PULLDOWN); // PIR OUT goes HIGH on motion
  Serial.println("PIR Sensor configured on GPIO " + String(PIR_PIN));

  // Camera config
  camera_config_t config;
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer = LEDC_TIMER_0;
  config.pin_d0 = Y2_GPIO_NUM;
  config.pin_d1 = Y3_GPIO_NUM;
  config.pin_d2 = Y4_GPIO_NUM;
  config.pin_d3 = Y5_GPIO_NUM;
  config.pin_d4 = Y6_GPIO_NUM;
  config.pin_d5 = Y7_GPIO_NUM;
  config.pin_d6 = Y8_GPIO_NUM;
  config.pin_d7 = Y9_GPIO_NUM;
  config.pin_xclk = XCLK_GPIO_NUM;
  config.pin_pclk = PCLK_GPIO_NUM;
  config.pin_vsync = VSYNC_GPIO_NUM;
  config.pin_href = HREF_GPIO_NUM;
  config.pin_sccb_sda = SIOD_GPIO_NUM;
  config.pin_sccb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;
  config.xclk_freq_hz = 20000000;
  config.pixel_format = PIXFORMAT_JPEG;
  config.frame_size = FRAMESIZE_SVGA; // 800x600 resolution
  config.jpeg_quality = 12;
  config.fb_count = 2;
  config.grab_mode = CAMERA_GRAB_LATEST;

  // Camera init
  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("Camera init failed: 0x%x\n", err);
  } else {
    Serial.println("Camera initialized successfully.");
    sensor_t *s = esp_camera_sensor_get();
    if (s != NULL) {
      s->set_vflip(s, vflipState);
      s->set_hmirror(s, hmirrorState);
      Serial.println("Camera orientation set: vflip=1 (right-side up), hmirror=1");
    }
  }

  // Initialize Front Flash LED PWM on GPIO 4
  ledcAttach(FLASH_LED_PIN, 5000, 8); // 5kHz frequency, 8-bit resolution (0..255)
  ledcWrite(FLASH_LED_PIN, 0); // start with light off

  // Mount MicroSD card in 1-bit mode (frees GPIO 13 for PIR sensor!)
  Serial.print("Mounting MicroSD card (1-bit mode)... ");
  if (SD_MMC.begin("/sdcard", true)) {
    sdMounted = true;
    Serial.printf("Mounted! Total: %llu MB\n", SD_MMC.totalBytes() / (1024 * 1024));
  } else {
    Serial.println("Mount Failed! Check card formatting (FAT32).");
  }

  // Connect to Wi-Fi
  Serial.print("Connecting to Wi-Fi: ");
  Serial.println(ssid);
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi connected! IP Address: " + WiFi.localIP().toString());

  // Web Server routes
  server.on("/", HTTP_GET, handleRoot);
  server.on("/record", HTTP_GET, handleRecord);
  server.on("/stop", HTTP_GET, handleStop);
  server.on("/download", HTTP_GET, handleDownload);
  server.on("/delete", HTTP_GET, handleDelete);
  server.on("/stream", HTTP_GET, handleStream);
  server.on("/led", HTTP_GET, handleLed);
  server.on("/flip", HTTP_GET, handleFlip);
  server.on("/capture", HTTP_GET, handleCapture);
  server.on("/resolution", HTTP_GET, handleResolution);
  server.on("/pir_toggle", HTTP_GET, handlePirToggle);

  server.begin();
  Serial.println("Web Server started on port 80.");
  Serial.println("PIR Sensor is armed and actively monitoring GPIO 13!");
}

void loop() {
  server.handleClient();

  // Check PIR motion sensor if armed and not already recording
  if (pirArmed && !isRecording) {
    int motion = digitalRead(PIR_PIN);
    if (motion == HIGH) {
      unsigned long now = millis();
      if (now - lastMotionTime > PIR_COOLDOWN_MS || lastMotionTime == 0) {
        Serial.println(">>> [PIR SENSOR] Motion Detected on GPIO 13! Recording 2-min video...");
        startRecording(recordDurationSec);
        lastMotionTime = millis();
      }
    }
  }

  // Record frames while active
  if (isRecording) {
    recordFrame();
  }

  delay(1);
}
