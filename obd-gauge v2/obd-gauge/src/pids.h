#pragma once
#include <Arduino.h>

// Every value the gauge can show. Stored internally in metric units.
enum ValId : uint8_t {
  V_RPM, V_SPEED, V_COOLANT, V_IAT, V_BOOST, V_THROTTLE,
  V_LOAD, V_STFT, V_LTFT, V_VOLT, V_OIL, V_COUNT
};

enum Kind : uint8_t { K_RPM, K_SPEED, K_TEMP, K_PRESS, K_PCT, K_TRIM, K_VOLT };

struct ValDef {
  const char* label;
  uint8_t pid;     // Mode 01 PID used to check support
  Kind kind;
  float gMin, gMax; // bar-graph range, metric
};

extern const ValDef VALS[V_COUNT];

// Unit handling (uses current settings)
float toDisplay(ValId id, float metric);
const char* unitLabel(ValId id);
void formatValue(ValId id, float metric, char* buf, size_t len);

// Short descriptions for common generic trouble codes
const char* dtcText(const char* code);
