#pragma once
#include <Arduino.h>

#define SETTINGS_VERSION 1

struct Settings {
  uint8_t  tempF      = 1;      // 1 = Fahrenheit
  uint8_t  pressPsi   = 1;      // 1 = psi
  uint8_t  speedMph   = 1;      // 1 = mph
  uint16_t shiftRpm   = 6000;
  uint8_t  coolWarnC  = 108;    // coolant warning, deg C
  uint8_t  brightness = 0;      // 0 auto, 1 low, 2 med, 3 high
  uint8_t  invert     = 0;
  uint8_t  demo       = 1;      // demo data until a dongle is paired
  uint8_t  screen     = 0;      // last screen shown
  uint8_t  mainBig    = 0;      // V_RPM
  uint8_t  mainSmall[3] = {2, 9, 1};    // coolant, volts, speed
  uint8_t  quad[4]      = {2, 3, 9, 6}; // coolant, intake, volts, load
  char     btAddr[18] = "";
  char     btName[33] = "";
};

extern Settings cfg;

void settingsLoad();
void settingsSave();          // write now
void settingsMarkDirty();     // write a couple of seconds after the last change
void settingsLoop();
