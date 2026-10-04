#include "pids.h"
#include "settings.h"

const ValDef VALS[V_COUNT] = {
  {"RPM",      0x0C, K_RPM,     0,    8000},
  {"SPEED",    0x0D, K_SPEED,   0,    200},
  {"COOLANT",  0x05, K_TEMP,    40,   120},
  {"INTAKE",   0x0F, K_TEMP,   -10,   70},
  {"BOOST",    0x0B, K_PRESS,  -100,  150},
  {"THROTTLE", 0x11, K_PCT,     0,    100},
  {"LOAD",     0x04, K_PCT,     0,    100},
  {"ST TRIM",  0x06, K_TRIM,   -25,   25},
  {"LT TRIM",  0x07, K_TRIM,   -25,   25},
  {"VOLTS",    0x42, K_VOLT,    10,   16},
  {"OIL TEMP", 0x5C, K_TEMP,    40,   150},
};

float toDisplay(ValId id, float v) {
  switch (VALS[id].kind) {
    case K_TEMP:  return cfg.tempF ? v * 9.0f / 5.0f + 32.0f : v;
    case K_PRESS: return cfg.pressPsi ? v * 0.145038f : v;
    case K_SPEED: return cfg.speedMph ? v * 0.621371f : v;
    default:      return v;
  }
}

const char* unitLabel(ValId id) {
  switch (VALS[id].kind) {
    case K_RPM:   return "rpm";
    case K_SPEED: return cfg.speedMph ? "mph" : "km/h";
    case K_TEMP:  return cfg.tempF ? "F" : "C";
    case K_PRESS: return cfg.pressPsi ? "psi" : "kPa";
    case K_PCT:   return "%";
    case K_TRIM:  return "%";
    case K_VOLT:  return "V";
  }
  return "";
}

void formatValue(ValId id, float metric, char* buf, size_t len) {
  float d = toDisplay(id, metric);
  Kind k = VALS[id].kind;
  bool oneDecimal = (k == K_VOLT) || (k == K_TRIM) || (k == K_PRESS && cfg.pressPsi);
  if (oneDecimal) snprintf(buf, len, "%.1f", d);
  else            snprintf(buf, len, "%d", (int)lroundf(d));
}

struct DtcDesc { const char* code; const char* text; };
static const DtcDesc DTCS[] = {
  {"P0011", "Cam timing over-advanced B1"},
  {"P0016", "Crank/cam correlation B1"},
  {"P0101", "MAF sensor range/perf"},
  {"P0113", "Intake air temp sensor high"},
  {"P0118", "Coolant temp sensor high"},
  {"P0128", "Thermostat below temp"},
  {"P0171", "System too lean B1"},
  {"P0172", "System too rich B1"},
  {"P0174", "System too lean B2"},
  {"P0175", "System too rich B2"},
  {"P0300", "Random misfire"},
  {"P0301", "Cylinder 1 misfire"},
  {"P0302", "Cylinder 2 misfire"},
  {"P0303", "Cylinder 3 misfire"},
  {"P0304", "Cylinder 4 misfire"},
  {"P0305", "Cylinder 5 misfire"},
  {"P0306", "Cylinder 6 misfire"},
  {"P0307", "Cylinder 7 misfire"},
  {"P0308", "Cylinder 8 misfire"},
  {"P0325", "Knock sensor circuit B1"},
  {"P0335", "Crank position sensor"},
  {"P0340", "Cam position sensor"},
  {"P0351", "Ignition coil A circuit"},
  {"P0401", "EGR flow insufficient"},
  {"P0420", "Catalyst efficiency B1"},
  {"P0430", "Catalyst efficiency B2"},
  {"P0440", "EVAP system malfunction"},
  {"P0442", "EVAP small leak"},
  {"P0446", "EVAP vent control"},
  {"P0455", "EVAP large leak"},
  {"P0456", "EVAP very small leak"},
  {"P0500", "Vehicle speed sensor"},
  {"P0505", "Idle control system"},
  {"P0562", "System voltage low"},
  {"P0700", "Transmission fault"},
};

const char* dtcText(const char* code) {
  for (auto& d : DTCS) {
    if (strncmp(d.code, code, 5) == 0) return d.text;
  }
  if (code[0] == 'P') return "Powertrain code";
  if (code[0] == 'B') return "Body code";
  if (code[0] == 'C') return "Chassis code";
  if (code[0] == 'U') return "Network code";
  return "";
}
