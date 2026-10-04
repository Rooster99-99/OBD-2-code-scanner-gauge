#pragma once
#include <Arduino.h>
#include "pids.h"
#include "config.h"

enum LinkState : uint8_t {
  LINK_NO_DONGLE,    // nothing paired yet
  LINK_IDLE,         // waiting to retry
  LINK_CONNECTING,   // Bluetooth connecting
  LINK_ELM_INIT,     // talking to the dongle
  LINK_CHECK_CAR,    // asking the car what it supports
  LINK_LIVE,         // streaming data
  LINK_NO_CAR,       // dongle OK, car not answering (ignition off?)
  LINK_DEMO,         // simulated data
  LINK_SCANNING      // searching for dongles
};

namespace Obd {

void begin();                // starts the background task on core 0
LinkState state();
const char* stateText();

bool  supported(ValId id);   // car reports this value (always true in demo)
bool  fresh(ValId id);       // have a recent reading
float value(ValId id);       // metric units

void setWanted(uint16_t mask);  // bit per ValId the current screen shows

// Trouble codes
enum DtcState : uint8_t { DTC_IDLE, DTC_BUSY, DTC_DONE, DTC_FAILED };
void requestReadCodes();
void requestClearCodes();
DtcState dtcState();
bool lastClearOk();
uint8_t codeCount();
const char* code(uint8_t i);

// Pairing
struct Found { char name[33]; char addr[18]; };
void requestScan();
bool scanBusy();
uint8_t foundCount();
Found found(uint8_t i);

void reconnect();   // drop the link and start over (after pairing or demo toggle)

}  // namespace Obd
