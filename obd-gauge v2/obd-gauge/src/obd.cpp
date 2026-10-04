#include "obd.h"
#include "settings.h"
#include <BluetoothSerial.h>
#include <ELMduino.h>

#if !defined(CONFIG_BT_ENABLED) || !defined(CONFIG_BLUEDROID_ENABLED)
#error "Bluetooth is not enabled for this board"
#endif

namespace {

BluetoothSerial SerialBT;
ELM327 elm;

volatile LinkState gState = LINK_IDLE;
float    gVal[V_COUNT];
uint32_t gStamp[V_COUNT];
volatile uint16_t gWanted = 0;
uint32_t gSup[3] = {0, 0, 0};
float    gBaro = 101.3f;

volatile bool reqRead = false, reqClear = false, reqScan = false, reqReconnect = false;
volatile Obd::DtcState gDtc = Obd::DTC_IDLE;
volatile bool gClearOk = false;
char gCodes[OBD_MAX_CODES][6];
volatile uint8_t gCodeCount = 0;
bool demoCleared = false;

volatile bool gScanBusy = false;
Obd::Found gFound[OBD_MAX_FOUND];
volatile uint8_t gFoundCount = 0;

uint32_t nextTry = 0;
uint8_t  errStreak = 0;
int8_t   cur = -1;       // value currently being requested
int8_t   lastPolled = -1;
uint8_t  rr = 0;         // round-robin index

// ---------- helpers ----------

bool pidSupported(uint8_t pid) {
  if (pid == 0) return false;
  uint8_t idx = (pid - 1) / 32;
  if (idx > 2) return false;
  uint8_t off = pid - idx * 32;          // 1..32
  return gSup[idx] & (1UL << (32 - off));
}

// Run an ELMduino query until it finishes. Returns true on success.
template <typename T, typename F>
bool blockingQuery(F fn, T& out, uint32_t ms = 4000) {
  uint32_t t0 = millis();
  while (millis() - t0 < ms) {
    T v = fn();
    if (elm.nb_rx_state == ELM_SUCCESS) { out = v; return true; }
    if (elm.nb_rx_state != ELM_GETTING_MSG) return false;
    vTaskDelay(1);
  }
  return false;
}

float readPid(uint8_t id) {
  switch (id) {
    case V_RPM:      return elm.rpm();
    case V_SPEED:    return (float)elm.kph();
    case V_COOLANT:  return elm.engineCoolantTemp();
    case V_IAT:      return elm.intakeAirTemp();
    case V_BOOST:    return (float)elm.manifoldPressure();
    case V_THROTTLE: return elm.throttle();
    case V_LOAD:     return elm.engineLoad();
    case V_STFT:     return elm.shortTermFuelTrimBank_1();
    case V_LTFT:     return elm.longTermFuelTrimBank_1();
    case V_VOLT:     return elm.ctrlModVoltage();
    case V_OIL:      return elm.oilTemp();
  }
  return 0;
}

void store(uint8_t id, float v) {
  if (id == V_BOOST) v = v - gBaro;   // manifold absolute -> gauge pressure
  gVal[id] = v;
  gStamp[id] = millis();
}

// Values always polled for warnings and the moving-lock, plus what's on screen.
uint16_t pollMask() {
  uint16_t m = gWanted | (1 << V_RPM) | (1 << V_SPEED) | (1 << V_COOLANT) | (1 << V_VOLT);
  uint16_t out = 0;
  for (uint8_t i = 0; i < V_COUNT; i++) {
    if ((m & (1 << i)) && pidSupported(VALS[i].pid)) out |= (1 << i);
  }
  return out;
}

int8_t nextToPoll() {
  uint16_t m = pollMask();
  if (!m) return -1;
  // RPM gets every other slot when it's on screen so the shift light stays quick.
  bool rpmOnScreen = gWanted & (1 << V_RPM);
  if (rpmOnScreen && lastPolled != V_RPM && (m & (1 << V_RPM))) return V_RPM;
  for (uint8_t n = 0; n < V_COUNT; n++) {
    rr = (rr + 1) % V_COUNT;
    if (rpmOnScreen && rr == V_RPM) continue;
    if (m & (1 << rr)) return rr;
  }
  return (m & (1 << V_RPM)) ? V_RPM : -1;
}

void goIdle(bool dropBt) {
  if (dropBt && SerialBT.connected()) SerialBT.disconnect();
  cur = -1;
  errStreak = 0;
  gState = LINK_IDLE;
  nextTry = millis() + RECONNECT_MS;
}

// ---------- demo data ----------

void demoTick() {
  float t = millis() / 1000.0f;
  float rpm = 1900 + 1600 * sinf(t * 0.7f) + 1100 * sinf(t * 2.1f);
  if (rpm < 750) rpm = 750;
  float thr = constrain((rpm - 800) / 55.0f, 0.0f, 100.0f);
  uint32_t now = millis();
  gVal[V_RPM] = rpm;
  gVal[V_SPEED] = 70 + 35 * sinf(t * 0.15f);
  gVal[V_COOLANT] = 90 + 5 * sinf(t * 0.05f);
  gVal[V_IAT] = 32 + 4 * sinf(t * 0.1f);
  gVal[V_BOOST] = constrain((rpm - 2600) / 30.0f, -65.0f, 110.0f);
  gVal[V_THROTTLE] = thr;
  gVal[V_LOAD] = 15 + thr * 0.8f;
  gVal[V_STFT] = 2.5f * sinf(t * 1.3f);
  gVal[V_LTFT] = 3.1f;
  gVal[V_VOLT] = 14.1f + 0.15f * sinf(t);
  gVal[V_OIL] = 98 + 6 * sinf(t * 0.04f);
  for (uint8_t i = 0; i < V_COUNT; i++) gStamp[i] = now;
}

// ---------- trouble codes ----------

void handleDtc(bool demo) {
  if (reqClear) {
    reqClear = false;
    gDtc = Obd::DTC_BUSY;
    bool ok = true;
    if (demo) demoCleared = true;
    else ok = elm.resetDTC();
    gClearOk = ok;
    reqRead = true;   // re-read so the list reflects the result
  }
  if (reqRead) {
    reqRead = false;
    gDtc = Obd::DTC_BUSY;
    if (demo) {
      vTaskDelay(pdMS_TO_TICKS(600));
      if (demoCleared) gCodeCount = 0;
      else {
        strcpy(gCodes[0], "P0301");
        strcpy(gCodes[1], "P0420");
        gCodeCount = 2;
      }
      gDtc = Obd::DTC_DONE;
      return;
    }
    elm.DTC_Response.codesFound = 0;
    elm.currentDTCCodes(true);
    if (elm.nb_rx_state == ELM_SUCCESS || elm.nb_rx_state == ELM_NO_DATA) {
      uint8_t n = elm.DTC_Response.codesFound;
      if (n > OBD_MAX_CODES) n = OBD_MAX_CODES;
      for (uint8_t i = 0; i < n; i++) {
        strncpy(gCodes[i], elm.DTC_Response.codes[i], 5);
        gCodes[i][5] = 0;
      }
      gCodeCount = n;
      gDtc = Obd::DTC_DONE;
    } else {
      gDtc = Obd::DTC_FAILED;
    }
  }
}

// ---------- pairing scan ----------

void doScan() {
  gScanBusy = true;
  gState = LINK_SCANNING;
  if (SerialBT.connected()) SerialBT.disconnect();
  gFoundCount = 0;
  BTScanResults* res = SerialBT.discover(8000);
  if (res) {
    int n = res->getCount();
    for (int i = 0; i < n && gFoundCount < OBD_MAX_FOUND; i++) {
      BTAdvertisedDevice* d = res->getDevice(i);
      if (!d) continue;
      Obd::Found f;
      String addr = d->getAddress().toString();
      std::string name = d->haveName() ? d->getName() : std::string(addr.c_str());
      strncpy(f.addr, addr.c_str(), sizeof(f.addr) - 1);
      f.addr[sizeof(f.addr) - 1] = 0;
      strncpy(f.name, name.c_str(), sizeof(f.name) - 1);
      f.name[sizeof(f.name) - 1] = 0;
      gFound[gFoundCount++] = f;
    }
  }
  SerialBT.discoverClear();
  gScanBusy = false;
  goIdle(false);
  nextTry = millis();
}

// ---------- main state machine ----------

void step() {
  if (reqScan) { reqScan = false; doScan(); return; }
  if (reqReconnect) { reqReconnect = false; goIdle(true); nextTry = millis(); }

  if (cfg.demo) {
    if (SerialBT.connected()) SerialBT.disconnect();
    gState = LINK_DEMO;
    demoTick();
    handleDtc(true);
    vTaskDelay(pdMS_TO_TICKS(40));
    return;
  }
  if (cfg.btAddr[0] == 0) {
    gState = LINK_NO_DONGLE;
    vTaskDelay(pdMS_TO_TICKS(200));
    return;
  }
  if (gState == LINK_DEMO || gState == LINK_NO_DONGLE) {
    goIdle(false);
    nextTry = millis();
  }

  switch (gState) {
    case LINK_IDLE:
      if ((int32_t)(millis() - nextTry) < 0) { vTaskDelay(pdMS_TO_TICKS(50)); return; }
      gState = LINK_CONNECTING;
      Serial.printf("[obd] connecting to %s (%s)\n", cfg.btName, cfg.btAddr);
      if (SerialBT.connect(BTAddress(String(cfg.btAddr)))) {
        gState = LINK_ELM_INIT;
      } else {
        Serial.println("[obd] bluetooth connect failed");
        goIdle(false);
      }
      return;

    case LINK_ELM_INIT:
      if (elm.begin(SerialBT, false, ELM_TIMEOUT_MS)) {
        gState = LINK_CHECK_CAR;
      } else {
        Serial.println("[obd] ELM327 init failed");
        goIdle(true);
      }
      return;

    case LINK_CHECK_CAR: {
      uint32_t s = 0;
      if (!blockingQuery<uint32_t>([]() { return elm.supportedPIDs_1_20(); }, s)) {
        gState = LINK_NO_CAR;
        nextTry = millis() + 3000;
        return;
      }
      gSup[0] = s;
      if (blockingQuery<uint32_t>([]() { return elm.supportedPIDs_21_40(); }, s)) gSup[1] = s;
      if (blockingQuery<uint32_t>([]() { return elm.supportedPIDs_41_60(); }, s)) gSup[2] = s;
      if (pidSupported(0x33)) {
        uint8_t b = 0;
        if (blockingQuery<uint8_t>([]() { return elm.absBaroPressure(); }, b) && b > 50) gBaro = b;
      }
      Serial.printf("[obd] supported: %08lX %08lX %08lX\n",
                    (unsigned long)gSup[0], (unsigned long)gSup[1], (unsigned long)gSup[2]);
      errStreak = 0;
      cur = -1;
      gState = LINK_LIVE;
      return;
    }

    case LINK_NO_CAR:
      if (!SerialBT.connected()) { goIdle(false); return; }
      if ((int32_t)(millis() - nextTry) >= 0) gState = LINK_CHECK_CAR;
      else vTaskDelay(pdMS_TO_TICKS(50));
      return;

    case LINK_LIVE: {
      if (!SerialBT.connected()) { Serial.println("[obd] link lost"); goIdle(false); return; }
      if (cur < 0) {
        handleDtc(false);          // only between requests
        cur = nextToPoll();
        if (cur < 0) { vTaskDelay(pdMS_TO_TICKS(20)); return; }
      }
      float v = readPid(cur);
      if (elm.nb_rx_state == ELM_SUCCESS) {
        store(cur, v);
        errStreak = 0;
        lastPolled = cur;
        cur = -1;
      } else if (elm.nb_rx_state != ELM_GETTING_MSG) {
        lastPolled = cur;
        cur = -1;
        if (++errStreak > 10) {    // car probably switched off
          gState = LINK_NO_CAR;
          nextTry = millis() + 3000;
        }
      }
      return;
    }

    default:
      goIdle(false);
      return;
  }
}

void task(void*) {
  for (;;) {
    step();
    vTaskDelay(1);
  }
}

}  // namespace

// ---------- public API ----------

namespace Obd {

void begin() {
  SerialBT.begin(BT_LOCAL_NAME, true);   // master mode
  SerialBT.setPin(BT_PIN);
  nextTry = millis();
  xTaskCreatePinnedToCore(task, "obd", 8192, nullptr, 1, nullptr, 0);
}

LinkState state() { return gState; }

const char* stateText() {
  switch (gState) {
    case LINK_NO_DONGLE:  return "No dongle paired";
    case LINK_IDLE:       return "Waiting to reconnect";
    case LINK_CONNECTING: return "Connecting...";
    case LINK_ELM_INIT:   return "Starting dongle...";
    case LINK_CHECK_CAR:  return "Checking car...";
    case LINK_LIVE:       return "Live";
    case LINK_NO_CAR:     return "Car not responding";
    case LINK_DEMO:       return "Demo mode";
    case LINK_SCANNING:   return "Scanning...";
  }
  return "";
}

bool supported(ValId id) {
  if (gState == LINK_DEMO) return true;
  return pidSupported(VALS[id].pid);
}

bool fresh(ValId id) {
  if (gState != LINK_LIVE && gState != LINK_DEMO) return false;
  return gStamp[id] != 0 && millis() - gStamp[id] < 4000;
}

float value(ValId id) { return gVal[id]; }

void setWanted(uint16_t mask) { gWanted = mask; }

void requestReadCodes()  { reqRead = true; gDtc = DTC_BUSY; }
void requestClearCodes() { reqClear = true; gDtc = DTC_BUSY; }
DtcState dtcState() { return gDtc; }
bool lastClearOk() { return gClearOk; }
uint8_t codeCount() { return gCodeCount; }
const char* code(uint8_t i) { return i < gCodeCount ? gCodes[i] : ""; }

void requestScan() { gScanBusy = true; reqScan = true; }
bool scanBusy() { return gScanBusy; }
uint8_t foundCount() { return gFoundCount; }
Found found(uint8_t i) { return gFound[i]; }

void reconnect() { demoCleared = false; reqReconnect = true; }

}  // namespace Obd
