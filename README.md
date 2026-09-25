# ESP32-CAM Dual-Module Wireless Security Hub

A complete distributed security system using two ESP32 modules communicating via **instant ESP-NOW (< 5ms)** and **Wi-Fi HTTP fallback**.

---

## 🏗️ Architecture

```
 [Module 2: Wireless PIR Trigger]
        (PIR Motion Detected)
                 │
                 ├─── ESP-NOW (< 5ms peer-to-peer radio) ───┐
                 └─── Wi-Fi HTTP (GET /record?duration=120) ─┼──> [Module 1: Camera & SD Hub]
                                                             │      (Records 2-min SVGA AVI video)
                                                             │      (Hosts Web Dashboard & Controls)
                                                             v
                                                   [MicroSD Card Storage]
```

* **Module 1 (Camera & SD Hub):**
  * Hardware: AI-Thinker ESP32-CAM with OV2640 sensor + MicroSD card.
  * Records 2-minute MJPEG videos in standard `.avi` format.
  * Saves JPEG still photos on demand.
  * Mounts MicroSD in 1-bit mode (no strobe flash on GPIO 4 during SD operations).
  * Web Dashboard with live preview stream, resolution changer, PWM flash slider, and in-browser video/photo downloads.

* **Module 2 (PIR Motion Tripwire):**
  * Hardware: Repurposed ESP32-CAM module (camera lens unneeded).
  * Sensor: PIR motion sensor on **GPIO 13**.
  * Dual-stack transmitter (ESP-NOW + Wi-Fi HTTP).
  * 2-minute cooldown timer to prevent duplicate triggers.
  * Indicator LED on GPIO 33 blinks on detection.

---

## 🔌 Hardware Wiring

### 1. Module 1: Camera Hub
* Standard AI-Thinker ESP32-CAM pinout with MicroSD card inserted (FAT32 formatted).
* Power: 5V (>500mA peak capability) and GND.

### 2. Module 2: PIR Trigger
| PIR Sensor Pin | ESP32 Pin | Purpose |
| :--- | :--- | :--- |
| **VCC** | **5V** (or 3.3V) | Sensor power |
| **GND** | **GND** | Ground |
| **OUT / Signal** | **GPIO 13** | Digital HIGH on motion detection |

---

## 🌐 Web Dashboard Endpoints (Port 80)

* `GET /` : Interactive web dashboard showing armed status, SD card space, and media library.
* `GET /stream` : Real-time live video preview stream.
* `GET /record?duration=120` : Triggers video recording for specified duration in seconds.
* `GET /stop` : Manually stops an active video recording.
* `GET /capture` : Takes an instant high-resolution still JPEG photo and saves to SD card.
* `GET /led?val=X` : Sets front flash LED brightness (0 = Off, 255 = Max).
* `GET /flip?v=1&h=1` : Adjusts camera orientation (vertical flip & horizontal mirror).
* `GET /resolution?val=X` : Changes camera resolution (UXGA, SXGA, XGA, SVGA, VGA, QVGA).
* `GET /download?file=/vid_0001.avi` : Downloads or views recorded video/photo.
* `GET /delete?file=/vid_0001.avi` : Deletes a file from the MicroSD card.
