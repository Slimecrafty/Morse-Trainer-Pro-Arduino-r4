# Morse Trainer Pro

This project is a comprehensive Morse Code training system consisting of a hardware implementation for the **Arduino UNO R4 WiFi** and a companion **Web App** for browser-based training.

## Overview
The system allows you to practice Morse Code using a physical paddle or the web interface. It supports:
- **Physical Paddle Input**: Connect a single-lever paddle to your Arduino.
- **Bluetooth BLE Connectivity**: Send your practice sessions wirelessly to the web trainer.
- **Interactive Web UI**: Train your skills using the Koch method and monitor band conditions.

## Components
1. **`MorseTrainer_R4WiFi_v13.ino`**: Arduino firmware. Designed for the UNO R4 WiFi. It features debouncing, Bluetooth BLE support, and LED Matrix output.
2. **`MorseTrainer_v13.html`**: A feature-rich web dashboard. It acts as a receiver for the Arduino BLE stream and includes a built-in trainer and HF band condition monitor.

## Hardware Setup
### Physical Paddle
You need a single-lever Morse key. You can find a great 3D-printable model here:
[**New Single Lever Paddle (Print-in-place)**](https://makerworld.com/de/models/579172-new-single-lever-paddle-morse-cw-print-in-place)

### Wiring
Connect your paddle to the Arduino UNO R4 WiFi as follows:
- **DIT (Dot) Paddle**: Pin `D2` → GND
- **DASH (Dash) Paddle**: Pin `D4` → GND

*Note: Use the internal `INPUT_PULLUP` resistors (handled by the code).*

## How to Build & Run

### 1. Arduino Firmware
1. Open `MorseTrainer_R4WiFi_v13.ino` in the **Arduino IDE**.
2. Install the necessary libraries via Library Manager:
    - `ArduinoBLE`
3. Select your board (**Arduino UNO R4 WiFi**).
4. Upload the code to your board.
5. Open the Serial Monitor (`115200 Baud`) to see the debug output.

### 2. Web Trainer
1. Simply open `MorseTrainer_v13.html` in any modern web browser.
2. If you are using the Bluetooth feature, ensure your browser supports Web Bluetooth (Chrome/Edge recommended).
3. Connect to the device named **"Arduino-MorseTrainer"**.
Or use online variant with real band propagation:
https://slimecrafty.github.io/Morse-Trainer-Pro-Arduino-r4/

## Dependencies
- **Arduino IDE**
- **ArduinoBLE Library**
- **Arduino_LED_Matrix Library** (included in the R4 WiFi board package)
