// OBD-II Dash Gauge for the 2.8" CYD (ESP32-2432S028)
// Bluetooth ELM327 dongle -> live gauges, shift light, trouble codes.

#include <Arduino.h>
#include "config.h"
#include "settings.h"
#include "obd.h"
#include "ui.h"

void setup() {
  Serial.begin(115200);
  Serial.println("\nOBD Gauge v" FW_VERSION);
  settingsLoad();
  Serial.printf("[boot] free heap %u\n", (unsigned)ESP.getFreeHeap());
  Obd::begin();
  Serial.printf("[boot] bluetooth started, free heap %u\n", (unsigned)ESP.getFreeHeap());   // Bluetooth first (it needs the most memory); runs on core 0
  Ui::begin();
}

void loop() {
  Ui::loop();
  delay(5);
}
