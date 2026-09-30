# Deauth Attack with ESP32

A fully functional ESP32-based Wi-Fi security research tool designed for **educational purposes** in controlled laboratory environments. The project combines an ESP32, a small OLED display, and a single push button to scan nearby Wi-Fi networks, navigate through them, and select a target for a deauthentication test.

> **⚠️ LEGAL DISCLAIMER**
>
> This project is intended **strictly for educational use** in isolated, controlled environments (for example, a Faraday cage or a laboratory containing equipment you own or are explicitly authorized to test).
>
> Unauthorized deauthentication attacks against networks you do not own or have explicit permission to test are illegal in many jurisdictions and may violate computer fraud and abuse laws. The author assumes no responsibility for misuse of this project.

---

## 📋 Table of Contents

1. [Features](#-features)
2. [Hardware Requirements](#-hardware-requirements)
3. [Wiring](#-wiring)
4. [Software Requirements](#-software-requirements)
5. [Installation & Setup](#-installation--setup)
   - [Arduino IDE Configuration](#arduino-ide-configuration)
   - [Library Installation](#library-installation)
   - [Fixing the Linker Error](#-fixing-the-linker-error)
6. [How to Use](#-how-to-use)
7. [Code Explanation](#-code-explanation)
   - [System Architecture](#system-architecture)
   - [State Machine](#state-machine)
   - [Wi-Fi Scanning](#wi-fi-scanning)
   - [OLED Display](#oled-display)
   - [Button Handling](#button-handling)
   - [Deauthentication Test](#deauthentication-test)
8. [Troubleshooting](#-troubleshooting)
9. [References](#-references)
10. [License](#-license)

---

## ✨ Features

- **Wi-Fi Network Scanning**: Automatically scans for nearby Wi-Fi networks and displays them on a 128×64 OLED screen.
- **OLED Menu Navigation**: Scroll through detected networks using a single push button.
- **Auto-Refresh**: The network list refreshes every 60 seconds when idle.
- **Manual Refresh**: Triple-press the button to force an immediate rescan.
- **Long-Press Test Control**: Hold the button for 5 seconds to start the configured deauthentication test on the selected network.
- **Long-Press Cancel**: Hold the button for 5 seconds again to stop the active test.
- **Targeted Testing**: The selected access point's BSSID and channel are used by the test logic.
- **Simple Standalone Interface**: The project can be operated without a keyboard, computer, or external display after programming.

---

## 🧰 Hardware Requirements

| Component | Quantity | Notes |
| :--- | :---: | :--- |
| ESP32 DevKit (e.g., ESP32-WROOM-32) | 1 | Any ESP32 board with Wi-Fi |
| SSD1306 OLED Display (128×64, I2C) | 1 | 0.96" or 1.3" |
| Push Button | 1 | Momentary, normally open |
| Jumper Wires | ~6 | For connections |
| Breadboard | 1 | Optional but recommended |
| USB Cable | 1 | For power and programming |

---

## 🔌 Wiring

Connect the components as follows:

| Component | ESP32 Pin | Notes |
| :--- | :--- | :--- |
| **OLED SDA** | GPIO 21 | I2C Data |
| **OLED SCL** | GPIO 22 | I2C Clock |
| **OLED VCC** | 3.3V | Power |
| **OLED GND** | GND | Ground |
| **Button** | GPIO 4 | Other leg to GND |

The button uses the ESP32's internal pull-up resistor (`INPUT_PULLUP`), so no external resistor is required.

---

## 💻 Software Requirements

- **Arduino IDE** 1.8.x or 2.x
- **ESP32 Arduino Core** 3.3.11 or similar
- **Adafruit SSD1306** library
- **Adafruit GFX Library**

> **Compatibility note:** The original project documentation references ESP32 Arduino Core 3.x and also provides an alternative using Core 2.0.17 for the linker issue described below.

---

## 🚀 Installation & Setup

### Arduino IDE Configuration

1. Install the **ESP32 Arduino Core** through Boards Manager.
2. Open **File > Preferences**.
3. Add the following URL to **Additional Boards Manager URLs**:
   ```
   https://espressif.github.io/arduino-esp32/package_esp32_index.json
   ```
4. Go to **Tools > Board > Boards Manager**.
5. Search for `esp32` and install the ESP32 board package.
6. Select:
   **Tools > Board > ESP32 Arduino > ESP32 Dev Module**

### Library Installation

Open:

**Sketch > Include Library > Manage Libraries**

Install:

- **Adafruit SSD1306**
- **Adafruit GFX Library**

---

## 🛠 Fixing the Linker Error

When compiling projects that use low-level ESP32 Wi-Fi functionality with newer ESP32 Arduino cores (for example, Core 3.x), you may encounter a linker error similar to:

```text
multiple definition of `ieee80211_raw_frame_sanity_check';
libnet80211.a(ieee80211_output.o): first defined here
collect2.exe: error: ld returned 1 exit status
```

This occurs when the compiled project and the ESP32 core both provide definitions for the same low-level Wi-Fi symbol.

### Alternative Version

The original project documentation notes that **ESP32 Arduino Core 2.0.17** can avoid this particular conflict because the relevant function was treated as a weak symbol in older releases.

If you need to reproduce the original low-level research environment, using the documented compatible core version may be preferable to modifying the installed platform configuration.

> **Important:** Changes to the ESP32 Arduino core's internal build configuration affect other projects using that installation. Make a backup before modifying core files.

---

## 🎮 How to Use

Once the code is uploaded and the ESP32 reboots:

| Action | Button Gesture | Result |
| :--- | :--- | :--- |
| **Scroll Networks** | Single short press | Move to the next network in the list |
| **Force Rescan** | Triple short press within 800 ms | Immediately rescan for Wi-Fi networks |
| **Start Test** | Long press, 5 seconds | Begin the configured deauthentication test |
| **Stop Test** | Long press, 5 seconds | Stop the active test |
| **Auto-Refresh** | None | Network list refreshes every 60 seconds when idle |

### OLED Display Guide

- The top line shows the number of networks found.
- During an active test, the header indicates the active state.
- The middle section displays up to four networks at a time.
- The selected network is highlighted.
- The bottom line displays button hints.
- The display refreshes approximately every 200 ms to balance responsiveness and I2C bus activity.

---

# 🧠 Code Explanation

## System Architecture

The project is organized around a **finite state machine (FSM)** with three primary states:

| State | Description |
| :--- | :--- |
| `STATE_SCAN_IDLE` | Default state. Scans networks periodically and displays the list. |
| `STATE_NAVIGATE` | User is navigating through the detected network list. |
| `STATE_ATTACK_ACTIVE` | The deauthentication test is currently active on the selected target. |

Transitions are triggered by button events such as short presses, triple presses, and long presses.

---

## State Machine

The project uses the following state definitions:

```cpp
enum SystemState {
  STATE_SCAN_IDLE,
  STATE_NAVIGATE,
  STATE_ATTACK_ACTIVE
};
```

### Main Transitions

- **SCAN_IDLE → NAVIGATE**: Single short press.
- **NAVIGATE → ATTACK_ACTIVE**: Long press of 5 seconds on a valid network.
- **ATTACK_ACTIVE → NAVIGATE**: Long press of 5 seconds to cancel.
- **Rescan → SCAN_IDLE/NAVIGATE**: Auto-refresh or triple-press rescan depending on the current UI state.

---

## Wi-Fi Scanning

The `scanNetworks()` function uses the ESP32 Wi-Fi scanning API to retrieve nearby networks.

Detected networks are stored in a `NetworkInfo` structure:

```cpp
struct NetworkInfo {
  String ssid;
  uint8_t bssid[6];
  int channel;
  int rssi;
  bool selected;
};
```

The project stores up to `MAX_NETWORKS` results, with the original configuration using a maximum of **15 networks**.

### Scan Behavior

- Hidden SSIDs are displayed as `[Hidden]`.
- A scan occurs during boot.
- The list automatically refreshes every 60 seconds while idle.
- A triple button press triggers an immediate scan.
- The selected network's SSID, BSSID, channel, and signal strength are available to the application.
- During low-level Wi-Fi testing, channel changes need to be handled carefully because scanning and packet transmission use the same radio.

---

## OLED Display

The `updateDisplay()` function renders the interface on the SSD1306 OLED.

The display contains three main areas:

### Header

Displays either:

- The number of detected networks, or
- An indication that the test is active.

### Network List

- Displays up to four networks simultaneously.
- Uses a scrolling window when more networks are available.
- Highlights the currently selected network with an inverted rectangle.

### Footer

Displays button hints such as:

```text
1x:Next 3x:Scan 5s:Test
```

or, while active:

```text
Hold 5s: STOP
```

The display is updated approximately every **200 ms**.

---

## Button Handling

The `handleButton()` function implements a non-blocking button state machine using `millis()` for timing.

### Debouncing

Button noise is ignored for:

```cpp
DEBOUNCE_MS = 50
```

after a state change.

### Short Press

When the button is released before the long-press threshold, the short-press handler is executed.

A single press moves to the next network.

### Triple Press

Successive button releases are counted when they occur within:

```cpp
TRIPLE_PRESS_WINDOW_MS = 800
```

Three presses trigger an immediate Wi-Fi rescan.

### Long Press

A long press is detected after:

```cpp
LONG_PRESS_MS = 5000
```

Once the threshold is reached, the handler fires once. A `longPressHandled` flag prevents the same physical press from triggering the action repeatedly.

Example structure:

```cpp
void handleShortPress() {
  if (pressCount == 1) {
    // Navigate to next network
  }

  if (pressCount == 3) {
    // Force rescan
  }
}

void handleLongPress() {
  if (currentState == STATE_ATTACK_ACTIVE) {
    // Cancel test
  } else {
    // Start test
  }
}
```

---

## Deauthentication Test

The original project uses the ESP32's low-level Wi-Fi functionality to conduct a **802.11 deauthentication-frame transmission test**.

The test is designed around the following concepts:

1. Select a nearby access point from the OLED menu.
2. Obtain the access point's BSSID and operating channel.
3. Switch the ESP32 to the relevant channel.
4. Construct and transmit deauthentication frames using low-level Wi-Fi functionality.
5. Repeat the transmission according to the configured test loop.
6. Stop the test when the user performs the long-press cancel action.

### Raw Frame Testing

The original implementation uses `esp_wifi_80211_tx()` from the ESP-IDF Wi-Fi API rather than the higher-level `WiFi.h` interface.

It also relies on a low-level ESP32 Wi-Fi stack behavior involving the `ieee80211_raw_frame_sanity_check` symbol. This is the reason the original project documentation includes a linker-configuration workaround.

### Frame Structure

At a conceptual level, an IEEE 802.11 deauthentication frame contains fields including:

- Frame control
- Duration
- Destination address
- Source address
- Access-point/BSSID address
- Sequence information
- Reason code

The original implementation targets the selected access point and uses a broadcast destination so that the test can affect associated clients.

> **Security note:** The low-level raw-frame construction, sanity-check bypass, and repeated transmission implementation are intentionally not reproduced here as operational attack instructions. Use the project only in an isolated environment with equipment you own or are explicitly authorized to test.

---

## 🔧 Troubleshooting

| Issue | Possible Cause | Solution |
| :--- | :--- | :--- |
| **Linker error: multiple definition** | ESP32 core and project both define the same low-level Wi-Fi symbol | Use a compatible ESP32 Arduino Core version such as the documented 2.0.17 environment, or follow the project's documented core-configuration approach in an isolated development setup |
| **OLED shows nothing** | Wrong I2C address or wiring | Check wiring; try address `0x3C` or `0x3D` |
| **Button not responding** | Floating pin or incorrect wiring | Ensure the button is connected between GPIO 4 and GND and that `INPUT_PULLUP` is configured |
| **Wi-Fi test has no effect** | Incorrect channel, target conditions, or radio limitations | Verify the selected target and channel in your controlled test environment |
| **ESP32 crashes/reboots** | Insufficient power | Use a stable 5V USB power source; avoid powering a 3.3V OLED from an inappropriate voltage |
| **Wi-Fi scan returns no results** | Interference or scan timeout | Move to a less congested test environment and rescan |

---

## 📚 References

- [ESP32 Arduino Core Documentation](https://docs.espressif.com/projects/arduino-esp32/en/latest/)
- [Adafruit SSD1306 Library](https://github.com/adafruit/Adafruit_SSD1306)
- [Adafruit GFX Library](https://github.com/adafruit/Adafruit-GFX-Library)
- [ESP-IDF Wi-Fi API Reference](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/network/esp_wifi.html)
- [NicheSecTech/esp32-micropython-wifi-sniff-inject](https://github.com/NicheSecTech/esp32-micropython-wifi-sniff-inject) — reference for MicroPython raw Wi-Fi injection
- [RMNO21/ESP32-Deauth](https://github.com/RMNO21/ESP32-Deauth) — similar ESP32 project with OLED menu functionality

---

## 📄 License

This project is provided for **educational purposes only**.

The author is not responsible for misuse, damage, or disruption caused by this project. Use it only in controlled environments and only against networks and devices for which you have explicit authorization.

---

## 📌 Project Information

**Repository:** [Deauth-attack-with-ESP32](https://github.com/yourusername/Deauth-attack-with-ESP32)

**Author:** [Ali Al-Balushi]

**Last Updated:** September 2026
