# ESP32-CAM All-In-One (AIO) Security Hub & DVR

An autonomous, full-featured security camera and DVR running on a single **AI-Thinker ESP32-CAM** ($6 board). Combines an OV2640 camera, a hardware PIR motion sensor, FAT32 MicroSD video recording, and a responsive web control dashboard.

Also includes sketches for an optional **dual-module distributed setup** communicating over peer-to-peer ESP-NOW radio. (These used to live in their own repo, [esp32cam-security-hub](https://github.com/nicklaststop22/esp32cam-security-hub) — merged in here since the sketches were identical; that repo now just points back here.)

---

## 🌟 Key Features

* **Single-Board All-In-One (AIO)**: Camera, PIR motion detector, MicroSD DVR, and Web Server all run on one ESP32-CAM.
* **Automated 2-Minute Video Clips**: Automatically records motion events into standard `.avi` (MJPEG) video files on the MicroSD card.
* **1-Bit MicroSD Mode Trick**: Runs the MicroSD bus in 1-bit MMC mode (`SD_MMC.begin("/sdcard", true)`). This completely frees **GPIO 13** for the PIR sensor and stops the blinding front flash LED from strobing during SD read/write operations.
* **Responsive Web Dashboard (Port 80)**:
  * 🔴 Live MJPEG streaming preview (`/stream`)
  * 📁 MicroSD media library browser — view, download, or delete recorded videos and photos directly from your browser
  * 🔘 Manual controls: 1-minute, 2-minute, and 5-minute video recordings, recording stop button, and instant JPEG snapshots
  * 💡 Front flashlight PWM dimmer slider (0–255) with quick presets
  * 🔄 Real-time camera orientation controls: Vertical Flip (fixes upside-down video) & Horizontal Mirror
  * 📐 Live resolution switcher (UXGA 1600x1200 down to QVGA 320x240)
  * 🛡️ Motion tripwire toggle: One-click PIR Arm / Disarm switch
* **Zero External Dependencies**: Uses only standard ESP32 Arduino Core built-in libraries.

---

## 📂 Repository Contents

```
esp32-cam-aio/
├── ESP32CAM_AllInOne_PIR/      # ⭐ PRIMARY: All-in-One single board sketch (Camera + PIR + SD + Web UI)
│   └── ESP32CAM_AllInOne_PIR.ino
├── ESP32CAM_SD_Recorder/       # Dual-module setup: Camera & SD Hub receiver
│   └── ESP32CAM_SD_Recorder.ino
├── PIR_Trigger/                # Dual-module setup: Remote wireless PIR transmitter (ESP-NOW + Wi-Fi)
│   └── PIR_Trigger.ino
├── .gitignore
└── README.md
```

---

## 📦 Dependencies & Prerequisites

### 1. Arduino IDE
* **Arduino IDE 2.x** (or Arduino CLI) installed.

### 2. ESP32 Board Core
Install the official Espressif ESP32 board package:
1. In Arduino IDE, go to **File > Preferences**.
2. Add the following URL into **Additional Boards Manager URLs**:
   ```
   https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json
   ```
3. Open **Tools > Board > Boards Manager**, search for `esp32`, and install version **2.0.14** (or latest **3.x**).

### 3. Required Libraries
All required libraries are **built directly into the ESP32 Arduino Core** — no third-party library manager downloads needed:
* `esp_camera.h` — OV2640 camera driver
* `WiFi.h` — Wi-Fi networking stack
* `WebServer.h` — Lightweight HTTP server
* `FS.h` & `SD_MMC.h` — High-speed 1-bit MMC MicroSD file system
* `esp_now.h` & `HTTPClient.h` *(used in dual-module sketches)*

---

## ⚙️ Arduino IDE Board Settings (CRITICAL)

Configure these settings under the **Tools** menu before compiling:

| Setting | Value | Notes |
| :--- | :--- | :--- |
| **Board** | **AI Thinker ESP32-CAM** | Selects the correct pin map |
| **CPU Frequency** | **240MHz (WiFi/BT)** | Required for smooth video recording |
| **Flash Frequency** | **80MHz** | Fast flash access |
| **Flash Mode** | **QIO** | Standard SPI mode |
| **Partition Scheme** | **Huge APP (3MB No OTA/1MB SPIFFS)** | **MANDATORY**: Sketch exceeds default 1.2MB partition |
| **Core Debug Level** | **None** (or Info) | |
| **Upload Speed** | **115200** | Stable baud rate over jumper wires |

---

## 🔌 Hardware Wiring

### 1. All-in-One Hardware Connections
Connect the PIR motion sensor (HC-SR501, AM312, or SR602) directly to the ESP32-CAM header:

```
    ┌────────────────────────┐
    │  AI-Thinker ESP32-CAM  │
    │                        │
    │   [5V]  ───────────────┼────> PIR Sensor VCC
    │   [GND] ───────────────┼────> PIR Sensor GND
    │   [IO13] ──────────────┼────> PIR Sensor OUT (Signal)
    │                        │
    │   [MicroSD Slot]       │<─── MicroSD Card (FAT32)
    │   [OV2640 Ribbon]      │<─── Camera Sensor
    │   [IO4]                │<─── White Flash LED (Onboard PWM)
    │   [IO33]               │<─── Red Indicator LED (Onboard)
    └────────────────────────┘
```

| Sensor Pin | ESP32-CAM Pin | Details |
| :--- | :--- | :--- |
| **VCC** | **5V** | Sensors like HC-SR501 need 5V; AM312 works on 3.3V or 5V |
| **GND** | **GND** | Common ground |
| **OUT (Signal)** | **GPIO 13 (`IO13`)** | Reads `HIGH` on motion. Safe because SD runs in 1-bit mode! |

---

### 2. Flashing / Programming Wiring

The ESP32-CAM does not have an onboard USB chip. You can flash it using a **USB-TTL Adapter (FTDI)** or an **Arduino Mega/Uno** in serial passthrough mode.

#### Method A: USB-to-TTL Serial Adapter (FTDI / CP2102 / CH340)
* Adapter **5V** &rarr; ESP32-CAM **5V**
* Adapter **GND** &rarr; ESP32-CAM **GND**
* Adapter **TX** &rarr; ESP32-CAM **U0R** (GPIO 3)
* Adapter **RX** &rarr; ESP32-CAM **U0T** (GPIO 1)
* **ESP32-CAM `IO0` jumpered to `GND`** (enter bootloader mode before powering on)

#### Method B: Arduino Mega 2560 Passthrough (No FTDI Needed)
1. **Bridge Mega `RESET` to Mega `GND`** (Pin 3 `RESET` to Pin 6 `GND` on the Mega power header). This holds the ATmega2560 chip in reset, allowing the onboard USB chip to talk directly to the ESP32.
2. Connect wires:
   * Mega **5V** &rarr; ESP32-CAM **5V**
   * Mega **GND** &rarr; ESP32-CAM **GND**
   * Mega **TX0 (Pin 1)** &rarr; ESP32-CAM **U0R**
   * Mega **RX0 (Pin 0)** &rarr; ESP32-CAM **U0T**
   * ESP32-CAM **IO0** &rarr; ESP32-CAM **GND** (bridge for flashing, remove for running)

---

## 💾 MicroSD Card Preparation

1. Use a MicroSD card between **4 GB and 64 GB** (Class 10 / U1 or better).
2. Format the card to **FAT32** (exFAT or NTFS will not mount).
   * *Windows*: Right-click card in File Explorer &rarr; Format &rarr; File system: **FAT32** &rarr; Allocation unit size: **32 kilobytes**.
3. Insert into the card slot on the bottom of the ESP32-CAM before boot.

---

## 🚀 Quick Start Guide

1. **Clone the repository**:
   ```bash
   git clone https://github.com/nicklaststop22/esp32-cam-aio.git
   cd esp32-cam-aio
   ```
2. **Open the sketch**:
   * Open `ESP32CAM_AllInOne_PIR/ESP32CAM_AllInOne_PIR.ino` in Arduino IDE.
3. **Configure Wi-Fi**:
   * Update lines 11–12 with your 2.4GHz network credentials:
     ```cpp
     const char* ssid = "YOUR_WIFI_SSID";
     const char* password = "YOUR_WIFI_PASSWORD";
     ```
4. **Enter Flash Mode**:
   * Connect **IO0 to GND**.
   * Connect the USB cable and press the **RST** button on the back of the ESP32-CAM.
5. **Upload**:
   * Select your COM port and click **Upload** (Upload Speed: 115200).
6. **Run**:
   * **Disconnect IO0 from GND**.
   * Press the **RST** button on the back of the ESP32-CAM.
   * Open the **Serial Monitor at 115200 baud** to see the IP address:
     ```
     WiFi connected! IP Address: 192.168.1.104
     Web Server started on port 80.
     PIR Sensor is armed and actively monitoring GPIO 13!
     ```
7. **Access Dashboard**:
   * Open `http://192.168.1.104` (replace with your board's IP) on your phone, tablet, or PC!

---

## 🌐 Web Server Endpoints Reference

| Endpoint | Method | Description |
| :--- | :--- | :--- |
| `/` | `GET` | Interactive Web Dashboard (status, settings, media gallery) |
| `/stream` | `GET` | Live MJPEG video stream preview |
| `/record?duration=120` | `GET` | Starts video recording for specified seconds (default: 120) |
| `/stop` | `GET` | Manually stops recording and writes AVI indexes |
| `/capture` | `GET` | Captures an immediate JPEG still photo to SD card |
| `/led?val=X` | `GET` | Sets front flashlight brightness (0 to 255) |
| `/flip?v=1&h=1` | `GET` | Sets vertical flip (`v=0/1`) and horizontal mirror (`h=0/1`) |
| `/resolution?val=X` | `GET` | Changes camera resolution (4=QVGA, 6=VGA, 7=SVGA, 8=XGA, 9=SXGA, 10=UXGA) |
| `/pir_toggle` | `GET` | Arms or disarms the PIR motion detector |
| `/download?file=/vid_0001.avi` | `GET` | Streams or downloads a file from the MicroSD card |
| `/delete?file=/vid_0001.avi` | `GET` | Deletes a file from the MicroSD card |

---

## 🛠️ Troubleshooting & Tips

* **Brownout detector was triggered / Random Reboots**:
  * The ESP32-CAM draws up to 500mA during Wi-Fi and flash bursts. Power it with a stable 5V power supply providing at least 1A. Solder a 100µF–470µF electrolytic capacitor across 5V and GND if you experience resets.
* **Camera init failed: 0x20003**:
  * Unseat and re-seat the OV2640 camera ribbon connector carefully in its latch.
* **Mount Failed! Check card formatting**:
  * The MicroSD card must be formatted as **FAT32**. Cards larger than 32GB often format as exFAT by default on Windows — use a utility like GUIFormat or SD Card Formatter to force FAT32.
* **Playing Recorded `.avi` Videos**:
  * Videos are encoded in standard Motion JPEG (MJPEG) at SVGA (800x600). They play natively in **VLC Media Player**, Windows Media Player, MPC-HC, or directly via Chrome/Firefox.
* **PIR Sensor False Alarms**:
  * Keep the PIR sensor at least 10–15 cm away from the ESP32 Wi-Fi antenna to prevent RF interference.
  * Adjust the sensitivity potentiometer on HC-SR501 sensors clockwise/counterclockwise to tune detection distance.


## Disclaimer

For educational purposes only. See [DISCLAIMER.md](DISCLAIMER.md).
