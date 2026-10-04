#pragma once
#include <Arduino.h>

#define FW_VERSION "0.2.1"

// Text shown over the logo at boot. Put your brand name here.
#define SPLASH_TITLE "OBD GAUGE"

// ---- Screen ----
#define SCREEN_W 320
#define SCREEN_H 240

// ---- Touch (XPT2046 on its own SPI bus) ----
#define TOUCH_CLK  25
#define TOUCH_MISO 39
#define TOUCH_MOSI 32
#define TOUCH_CS   33
#define TOUCH_IRQ  36

// Raw touch range -> screen. If taps land in the wrong place, adjust these.
// Open the serial monitor: every tap prints its raw and mapped values.
#define TOUCH_X_MIN 200
#define TOUCH_X_MAX 3700
#define TOUCH_Y_MIN 240
#define TOUCH_Y_MAX 3800
#define TOUCH_FLIP_X 0
#define TOUCH_FLIP_Y 0
#define TOUCH_MIN_PRESSURE 200

// ---- Backlight + light sensor ----
#define PIN_BL  21
#define PIN_LDR 34
// Raw light-sensor readings for bright and dark conditions (auto-dim).
// Settings > Brightness shows the live reading so you can tune these.
#define LDR_BRIGHT_RAW 30
#define LDR_DARK_RAW   900
#define BL_MIN 35        // dimmest auto level (0-255)

// ---- RGB LED on the back (active LOW) ----
#define PIN_LED_R 4
#define PIN_LED_G 16
#define PIN_LED_B 17

// ---- Bluetooth ----
#define BT_LOCAL_NAME "OBD-Gauge"
#define BT_PIN        "1234"   // most ELM327 dongles use 1234 or 0000
#define ELM_TIMEOUT_MS 2000
#define RECONNECT_MS   5000

// ---- Limits ----
#define OBD_MAX_CODES 12
#define OBD_MAX_FOUND 8
#define MOVING_KPH    8   // above this, settings and code clearing are locked
