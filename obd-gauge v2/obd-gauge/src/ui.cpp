#include "ui.h"
#include "config.h"
#include "settings.h"
#include "obd.h"
#include "pids.h"
#include <SPI.h>
#include <TFT_eSPI.h>
#include <XPT2046_Touchscreen.h>
#include "splash_img.h"

namespace {

TFT_eSPI tft;
SPIClass touchSpi(VSPI);
XPT2046_Touchscreen ts(TOUCH_CS, TOUCH_IRQ);

// ---------- neon theme (colors taken from the logo) ----------
constexpr uint16_t RGB(uint8_t r, uint8_t g, uint8_t b) {
  return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
}
const uint16_t C_BG     = RGB(7, 5, 26);       // deep indigo
const uint16_t C_GRID   = RGB(22, 14, 58);     // faint backdrop grid
const uint16_t C_BARBG  = RGB(14, 8, 48);      // top bar
const uint16_t C_PANEL  = RGB(12, 9, 40);      // panel fill
const uint16_t C_PANEL2 = RGB(24, 16, 70);     // list items
const uint16_t C_SEGOFF = RGB(28, 18, 70);     // unlit bar segment
const uint16_t C_TXT    = RGB(234, 246, 255);
const uint16_t C_DIM    = RGB(140, 127, 192);  // lavender labels
const uint16_t C_CYAN   = RGB(47, 226, 252);
const uint16_t C_BLUE   = RGB(20, 104, 242);
const uint16_t C_MAG    = RGB(246, 42, 230);
const uint16_t C_HOT    = RGB(255, 42, 85);    // warnings
const uint16_t C_ORANGE = RGB(255, 154, 31);   // status messages
const uint16_t C_OK     = RGB(57, 255, 136);
// aliases used throughout
const uint16_t C_ACC  = C_CYAN;
const uint16_t C_WARN = C_HOT;
const uint16_t C_YEL  = C_ORANGE;

// ---------- screens ----------
enum Scr : uint8_t { SCR_MAIN, SCR_QUAD, SCR_SHIFT, SCR_CODES, SCR_SETTINGS, SCR_PAIR };
const uint8_t NAV_COUNT = 5;   // screens reachable with the arrows
const char* TITLES[] = {"GAUGE", "QUAD", "SHIFT LIGHT", "TROUBLE CODES", "SETTINGS", "PAIR DONGLE"};

Scr scr = SCR_MAIN;
bool needFull = true;
bool confirmClear = false;
bool lastActionClear = false;
uint32_t toastUntil = 0;
LinkState lastLink = (LinkState)255;

// ---------- redraw cache ----------
const uint8_t SLOTS = 24;
char cache[SLOTS][24];
uint8_t cacheFont[SLOTS];

void clearCache() {
  for (uint8_t i = 0; i < SLOTS; i++) { cache[i][0] = 1; cache[i][1] = 0; cacheFont[i] = 0; }
}

bool changed(uint8_t slot, const char* s) {
  if (strcmp(cache[slot], s) == 0) return false;
  strncpy(cache[slot], s, sizeof(cache[0]) - 1);
  cache[slot][sizeof(cache[0]) - 1] = 0;
  return true;
}

// ---------- helpers ----------

bool live()    { return Obd::state() == LINK_LIVE; }
bool hasData() { LinkState s = Obd::state(); return s == LINK_LIVE || s == LINK_DEMO; }

bool moving() {
  return live() && Obd::fresh(V_SPEED) && Obd::value(V_SPEED) > MOVING_KPH;
}

bool warnFor(uint8_t id, float v) {
  switch (id) {
    case V_COOLANT: return v >= cfg.coolWarnC;
    case V_OIL:     return v >= 130;
    case V_VOLT:
      return Obd::fresh(V_RPM) && Obd::value(V_RPM) > 400 && (v < 12.0f || v > 15.2f);
  }
  return false;
}

bool anyWarn() {
  if (!hasData()) return false;
  if (Obd::fresh(V_COOLANT) && warnFor(V_COOLANT, Obd::value(V_COOLANT))) return true;
  if (Obd::fresh(V_VOLT) && warnFor(V_VOLT, Obd::value(V_VOLT))) return true;
  return false;
}

void valueText(uint8_t id, char* buf, size_t len) {
  if (!Obd::fresh((ValId)id)) { snprintf(buf, len, "--"); return; }
  formatValue((ValId)id, Obd::value((ValId)id), buf, len);
}

void labelText(uint8_t id, char* buf, size_t len) {
  snprintf(buf, len, "%s  %s", VALS[id].label, unitLabel((ValId)id));
}

uint8_t cycleVal(uint8_t cur) {
  for (uint8_t step = 1; step <= V_COUNT; step++) {
    uint8_t n = (cur + step) % V_COUNT;
    if (!live() || Obd::supported((ValId)n)) return n;
  }
  return cur;
}

// ---------- drawing primitives ----------

TFT_eSprite spBig(&tft);    // flicker-free glowing main number
TFT_eSprite spTile(&tft);   // shared by the quad tiles
bool haveBig = false, haveTile = false;
const int BIG_W = 296, BIG_H = 58;
const int TILE_W = 144, TILE_H = 54;

void text(const char* s, int x, int y, uint8_t font, uint16_t fg, uint16_t bg,
          uint8_t datum = MC_DATUM, uint16_t pad = 0) {
  tft.setTextDatum(datum);
  tft.setTextColor(fg, bg);
  tft.setTextPadding(pad);
  tft.drawString(s, x, y, font);
  tft.setTextPadding(0);
}

// Text with no background, for drawing over pictures.
void textClear(const char* s, int x, int y, uint8_t font, uint16_t fg, uint8_t datum = MC_DATUM) {
  tft.setTextDatum(datum);
  tft.setTextColor(fg);
  tft.drawString(s, x, y, font);
}

uint16_t mix(uint16_t a, uint16_t b, float t) {
  t = constrain(t, 0.0f, 1.0f);
  int ar = (a >> 11) & 31, ag = (a >> 5) & 63, ab = a & 31;
  int br = (b >> 11) & 31, bg = (b >> 5) & 63, bb = b & 31;
  return ((int)(ar + (br - ar) * t) << 11) | ((int)(ag + (bg - ag) * t) << 5) | (int)(ab + (bb - ab) * t);
}

// cyan -> blue -> magenta -> hot pink across 0..1
uint16_t neonRamp(float t) {
  if (t < 0.40f) return mix(C_CYAN, C_BLUE, t / 0.40f);
  if (t < 0.75f) return mix(C_BLUE, C_MAG, (t - 0.40f) / 0.35f);
  return mix(C_MAG, C_HOT, (t - 0.75f) / 0.25f);
}

void gradientLine(int x, int y, int w, uint16_t a, uint16_t b) {
  for (int i = 0; i < w; i++) tft.drawPixel(x + i, y, mix(a, b, (float)i / (w - 1)));
}

// Dark indigo with a faint grid, like a HUD.
void backdrop() {
  tft.fillRect(0, 29, SCREEN_W, SCREEN_H - 29, C_BG);
  for (int x = 10; x < SCREEN_W; x += 20) tft.drawFastVLine(x, 32, SCREEN_H - 36, C_GRID);
  for (int y = 40; y < SCREEN_H - 4; y += 20) tft.drawFastHLine(4, y, SCREEN_W - 8, C_GRID);
}

// Panel with cut corners (top-left and bottom-right) and a neon edge.
void panel(int x, int y, int w, int h, uint16_t accent) {
  const int c = 10;
  tft.fillRect(x, y, w, h, C_PANEL);
  tft.fillTriangle(x, y, x + c, y, x, y + c, C_BG);
  tft.fillTriangle(x + w - 1, y + h - 1, x + w - 1 - c, y + h - 1, x + w - 1, y + h - 1 - c, C_BG);
  uint16_t dim = mix(C_PANEL, accent, 0.45f);
  tft.drawFastHLine(x + c, y, w - c, dim);
  tft.drawFastVLine(x + w - 1, y, h - c, dim);
  tft.drawFastHLine(x, y + h - 1, w - c, dim);
  tft.drawFastVLine(x, y + c, h - c, dim);
  tft.drawLine(x, y + c, x + c, y, accent);
  tft.drawLine(x + w - 1 - c, y + h - 1, x + w - 1, y + h - 1 - c, accent);
  // bright accent tabs
  tft.fillRect(x + c + 2, y, 26, 2, accent);
  tft.fillRect(x + w - 1 - c - 28, y + h - 2, 26, 2, accent);
  tft.fillRect(x + w - 6, y + 3, 3, 3, accent);
}

void neonButton(int x, int y, int w, int h, const char* label, uint16_t accent) {
  panel(x, y, w, h, accent);
  text(label, x + w / 2, y + h / 2 + 1, 4, accent, C_PANEL);
}

bool inRect(int px, int py, int x, int y, int w, int h) {
  return px >= x && px < x + w && py >= y && py < y + h;
}

// Glowing text into a sprite: soft outer glow, brighter inner glow, bright core.
void glow(TFT_eSprite& s, const char* str, int cx, int cy, uint8_t font,
          uint16_t core, uint16_t accent) {
  uint16_t outer = mix(C_PANEL, accent, 0.30f);
  uint16_t inner = mix(C_PANEL, accent, 0.75f);
  s.setTextDatum(MC_DATUM);
  s.setTextColor(outer);
  s.drawString(str, cx - 2, cy, font); s.drawString(str, cx + 2, cy, font);
  s.drawString(str, cx, cy - 2, font); s.drawString(str, cx, cy + 2, font);
  s.setTextColor(inner);
  s.drawString(str, cx - 1, cy, font); s.drawString(str, cx + 1, cy, font);
  s.drawString(str, cx, cy - 1, font); s.drawString(str, cx, cy + 1, font);
  s.setTextColor(core);
  s.drawString(str, cx, cy, font);
}

uint16_t coreFor(uint16_t accent) { return mix(C_TXT, accent, 0.25f); }

// Segmented bar; redraws only segments that change.
struct SegBar { int16_t lit = -1; bool hot = false; };

void segBar(SegBar& st, int x, int y, int n, int pitch, int w, int h, float frac, bool allHot) {
  int lit = constrain((int)(frac * n + 0.5f), 0, n);
  bool full = st.lit < 0 || st.hot != allHot;
  for (int i = 0; i < n; i++) {
    bool on = i < lit, was = i < st.lit;
    if (!full && on == was) continue;
    uint16_t col = on ? (allHot ? C_HOT : neonRamp((float)i / (n - 1))) : C_SEGOFF;
    tft.fillRect(x + i * pitch, y, w, h, col);
  }
  st.lit = lit;
  st.hot = allHot;
}

float gaugeFrac(uint8_t id, float v) {
  const ValDef& d = VALS[id];
  return (v - d.gMin) / (d.gMax - d.gMin);
}

void toast(const char* msg) {
  panel(30, 96, 260, 48, C_MAG);
  text(msg, 160, 120, 2, C_TXT, C_PANEL);
  toastUntil = millis() + 1800;
}

void setWantedForScreen() {
  uint16_t m = 0;
  switch (scr) {
    case SCR_MAIN:
      m |= 1 << cfg.mainBig;
      for (uint8_t i = 0; i < 3; i++) m |= 1 << cfg.mainSmall[i];
      break;
    case SCR_QUAD:
      for (uint8_t i = 0; i < 4; i++) m |= 1 << cfg.quad[i];
      break;
    case SCR_SHIFT:
      m |= 1 << V_RPM;
      break;
    default: break;
  }
  Obd::setWanted(m);
}

void goScreen(Scr s) {
  scr = s;
  confirmClear = false;
  needFull = true;
  if (s < NAV_COUNT && cfg.screen != s) { cfg.screen = s; settingsMarkDirty(); }
  setWantedForScreen();
}

// ---------- top bar ----------

void drawStatusDot() {
  uint16_t col;
  switch (Obd::state()) {
    case LINK_LIVE:  col = C_OK; break;
    case LINK_DEMO:  col = C_BLUE; break;
    case LINK_CONNECTING: case LINK_ELM_INIT: case LINK_CHECK_CAR: case LINK_SCANNING:
      col = C_ORANGE; break;
    default: col = C_HOT; break;
  }
  tft.fillCircle(42, 14, 7, mix(C_BARBG, col, 0.35f));
  tft.fillCircle(42, 14, 4, col);
}

void drawTopBar() {
  tft.fillRect(0, 0, SCREEN_W, 27, C_BARBG);
  gradientLine(0, 27, SCREEN_W, C_MAG, C_CYAN);
  gradientLine(0, 28, SCREEN_W, mix(C_BG, C_MAG, 0.4f), mix(C_BG, C_CYAN, 0.4f));
  // chevrons
  tft.fillTriangle(8, 13, 22, 4, 22, 22, C_MAG);
  tft.fillTriangle(13, 13, 22, 8, 22, 18, C_BARBG);
  if (scr != SCR_PAIR) {
    tft.fillTriangle(311, 13, 297, 4, 297, 22, C_CYAN);
    tft.fillTriangle(306, 13, 297, 8, 297, 18, C_BARBG);
  }
  text(TITLES[scr], 160, 14, 2, C_CYAN, C_BARBG);
  int tw = tft.textWidth(TITLES[scr], 2);
  tft.fillRect(160 - tw / 2 - 14, 12, 8, 3, C_MAG);
  tft.fillRect(160 + tw / 2 + 6, 12, 8, 3, C_MAG);
  drawStatusDot();
}

// ---------- MAIN ----------

SegBar mainBar;
const int MB_N = 29, MB_PITCH = 10, MB_X = 16, MB_Y = 122;

void mainStatic() {
  panel(4, 33, 312, 124, C_CYAN);
  for (uint8_t i = 0; i < 3; i++) panel(4 + i * 105, 163, 102, 73, C_MAG);
  mainBar = SegBar();
}

void mainDynamic() {
  char buf[24], key[28];
  uint8_t id = cfg.mainBig;

  labelText(id, buf, sizeof(buf));
  if (changed(0, buf)) text(buf, 160, 46, 2, C_DIM, C_PANEL, MC_DATUM, 280);

  valueText(id, buf, sizeof(buf));
  bool fr = Obd::fresh((ValId)id);
  float v = fr ? Obd::value((ValId)id) : 0;
  bool w = fr && warnFor(id, v);
  bool shift = fr && id == V_RPM && v >= cfg.shiftRpm;
  uint16_t acc = (w || shift) ? C_HOT : C_CYAN;
  snprintf(key, sizeof(key), "%s%d", buf, (int)(w || shift));
  if (changed(1, key)) {
    if (haveBig) {
      spBig.fillSprite(C_PANEL);
      glow(spBig, buf, BIG_W / 2, BIG_H / 2, 7, coreFor(acc), acc);
      spBig.pushSprite(160 - BIG_W / 2, 90 - BIG_H / 2);
    } else {
      text(buf, 160, 90, 7, acc, C_PANEL, MC_DATUM, BIG_W);
    }
  }

  segBar(mainBar, MB_X, MB_Y, MB_N, MB_PITCH, 8, 14, fr ? gaugeFrac(id, v) : 0, w || shift);

  const char* st = hasData() ? "" : Obd::stateText();
  if (changed(2, st)) text(st, 160, 146, 2, C_ORANGE, C_PANEL, MC_DATUM, 280);

  for (uint8_t i = 0; i < 3; i++) {
    uint8_t sid = cfg.mainSmall[i];
    int cx = 4 + i * 105 + 51;
    labelText(sid, buf, sizeof(buf));
    if (changed(3 + i, buf)) text(buf, cx, 178, 2, C_DIM, C_PANEL, MC_DATUM, 90);
    valueText(sid, buf, sizeof(buf));
    bool sw = Obd::fresh((ValId)sid) && warnFor(sid, Obd::value((ValId)sid));
    snprintf(key, sizeof(key), "%s%d", buf, (int)sw);
    if (changed(6 + i, key)) text(buf, cx, 210, 4, sw ? C_HOT : C_TXT, C_PANEL, MC_DATUM, 90);
  }
}

void mainTap(int x, int y) {
  if (y < 160) {
    cfg.mainBig = cycleVal(cfg.mainBig);
  } else {
    uint8_t i = constrain(x / 107, 0, 2);
    cfg.mainSmall[i] = cycleVal(cfg.mainSmall[i]);
  }
  settingsMarkDirty();
  setWantedForScreen();
}

// ---------- QUAD ----------

uint16_t quadAccent(uint8_t i) { return (i == 0 || i == 3) ? C_CYAN : C_MAG; }

void quadStatic() {
  for (uint8_t i = 0; i < 4; i++) {
    int x0 = (i % 2) * 160, y0 = 30 + (i / 2) * 105;
    panel(x0 + 4, y0 + 3, 152, 100, quadAccent(i));
  }
}

void quadDynamic() {
  char buf[24], key[28];
  for (uint8_t i = 0; i < 4; i++) {
    uint8_t id = cfg.quad[i];
    int x0 = (i % 2) * 160, y0 = 30 + (i / 2) * 105;
    int cx = x0 + 80;
    labelText(id, buf, sizeof(buf));
    if (changed(i, buf)) text(buf, cx, y0 + 18, 2, C_DIM, C_PANEL, MC_DATUM, 136);

    valueText(id, buf, sizeof(buf));
    uint8_t font = tft.textWidth(buf, 7) <= TILE_W - 6 ? 7 : 4;
    bool w = Obd::fresh((ValId)id) && warnFor(id, Obd::value((ValId)id));
    uint16_t acc = w ? C_HOT : quadAccent(i);
    snprintf(key, sizeof(key), "%s%d%d", buf, (int)w, (int)font);
    if (!changed(4 + i, key)) continue;
    if (haveTile) {
      spTile.fillSprite(C_PANEL);
      glow(spTile, buf, TILE_W / 2, TILE_H / 2, font, coreFor(acc), acc);
      spTile.pushSprite(cx - TILE_W / 2, y0 + 34);
    } else {
      if (cacheFont[i] != font) {
        tft.fillRect(cx - TILE_W / 2, y0 + 34, TILE_W, TILE_H, C_PANEL);
        cacheFont[i] = font;
      }
      text(buf, cx, y0 + 34 + TILE_H / 2, font, acc, C_PANEL, MC_DATUM, TILE_W);
    }
  }
}

void quadTap(int x, int y) {
  uint8_t i = (x >= 160 ? 1 : 0) + (y >= 135 ? 2 : 0);
  cfg.quad[i] = cycleVal(cfg.quad[i]);
  settingsMarkDirty();
  setWantedForScreen();
}

// ---------- SHIFT ----------

const uint8_t SEGS = 10;

void shiftStatic() {
  panel(4, 33, 312, 82, C_MAG);
  panel(4, 121, 312, 97, C_CYAN);
  for (uint8_t i = 0; i < SEGS; i++) tft.drawRect(12 + i * 30, 42, 26, 64, C_SEGOFF);
  char buf[40];
  snprintf(buf, sizeof(buf), "SHIFT POINT %u RPM", (unsigned)cfg.shiftRpm);
  text(buf, 160, 229, 2, C_DIM, C_BG);
}

void shiftDynamic() {
  char buf[24], key[32];
  bool have = Obd::fresh(V_RPM);
  float rpm = have ? Obd::value(V_RPM) : 0;
  bool over = have && rpm >= cfg.shiftRpm;
  bool flashOn = over && ((millis() / 120) % 2 == 0);
  int lit = have ? (int)(rpm / cfg.shiftRpm * SEGS + 0.5f) : 0;
  lit = constrain(lit, 0, SEGS);

  snprintf(key, sizeof(key), "%d%d%d", lit, (int)over, (int)flashOn);
  if (changed(0, key)) {
    for (uint8_t i = 0; i < SEGS; i++) {
      uint16_t col;
      if (over) col = flashOn ? C_HOT : C_PANEL;
      else if (i < lit) col = neonRamp((float)i / (SEGS - 1));
      else col = C_PANEL;
      tft.fillRect(13 + i * 30, 43, 24, 62, col);
    }
  }

  valueText(V_RPM, buf, sizeof(buf));
  snprintf(key, sizeof(key), "%s%d", buf, (int)over);
  if (changed(1, key)) text(buf, 160, 162, 8, over ? C_HOT : C_CYAN, C_PANEL, MC_DATUM, 296);

  const char* st = hasData() ? "" : Obd::stateText();
  if (changed(2, st)) text(st, 160, 207, 2, C_ORANGE, C_PANEL, MC_DATUM, 290);
}

// ---------- CODES ----------

void codesStatic() {
  panel(4, 33, 312, 156, C_CYAN);
  neonButton(8, 196, 146, 38, "READ", C_CYAN);
  neonButton(166, 196, 146, 38, "CLEAR", moving() ? C_DIM : C_HOT);
}

void drawConfirm() {
  panel(16, 40, 288, 150, C_HOT);
  text("CLEAR ALL CODES?", 160, 64, 4, C_TXT, C_PANEL);
  text("This turns off the check-engine light and", 160, 92, 2, C_DIM, C_PANEL);
  text("resets emissions readiness. The car may", 160, 108, 2, C_DIM, C_PANEL);
  text("fail inspection until it re-tests itself.", 160, 124, 2, C_DIM, C_PANEL);
  neonButton(36, 142, 110, 38, "YES", C_HOT);
  neonButton(174, 142, 110, 38, "NO", C_CYAN);
}

void codesDynamic() {
  if (confirmClear) return;
  char key[40];
  snprintf(key, sizeof(key), "%d%d%d%d", (int)Obd::state(), (int)Obd::dtcState(),
           (int)Obd::codeCount(), (int)lastActionClear);
  if (!changed(0, key)) return;

  tft.fillRect(8, 38, 304, 146, C_PANEL);
  if (!hasData()) {
    text("CONNECT TO A CAR FIRST", 160, 96, 4, C_TXT, C_PANEL);
    text(Obd::stateText(), 160, 128, 2, C_ORANGE, C_PANEL);
    return;
  }
  switch (Obd::dtcState()) {
    case Obd::DTC_IDLE:
      text("Tap READ to check for codes", 160, 110, 2, C_DIM, C_PANEL);
      break;
    case Obd::DTC_BUSY:
      text("TALKING TO THE CAR...", 160, 110, 4, C_ORANGE, C_PANEL);
      break;
    case Obd::DTC_FAILED:
      text("Couldn't read codes. Try again.", 160, 110, 2, C_HOT, C_PANEL);
      break;
    case Obd::DTC_DONE: {
      uint8_t n = Obd::codeCount();
      if (n == 0) {
        text("NO TROUBLE CODES", 160, 100, 4, C_OK, C_PANEL);
        if (lastActionClear)
          text(Obd::lastClearOk() ? "Codes cleared" : "Clear may have failed", 160, 132, 2, C_DIM, C_PANEL);
        break;
      }
      uint8_t shown = n > 6 ? 5 : n;
      for (uint8_t i = 0; i < shown; i++) {
        int y = 50 + i * 26;
        tft.fillRect(12, y - 9, 3, 18, C_MAG);
        text(Obd::code(i), 22, y, 4, C_CYAN, C_PANEL, ML_DATUM);
        text(dtcText(Obd::code(i)), 110, y, 2, C_TXT, C_PANEL, ML_DATUM);
      }
      if (n > shown) {
        char buf[24];
        snprintf(buf, sizeof(buf), "+%u more", (unsigned)(n - shown));
        text(buf, 160, 50 + shown * 26, 2, C_DIM, C_PANEL);
      }
      break;
    }
  }
}

void codesTap(int x, int y) {
  if (confirmClear) {
    if (inRect(x, y, 36, 142, 110, 38)) {
      confirmClear = false;
      lastActionClear = true;
      Obd::requestClearCodes();
      needFull = true;
    } else if (inRect(x, y, 174, 142, 110, 38)) {
      confirmClear = false;
      needFull = true;
    }
    return;
  }
  if (inRect(x, y, 8, 196, 146, 38)) {
    if (!hasData()) { toast("Not connected to a car"); return; }
    lastActionClear = false;
    Obd::requestReadCodes();
  } else if (inRect(x, y, 166, 196, 146, 38)) {
    if (!hasData()) { toast("Not connected to a car"); return; }
    if (moving()) { toast("Stop the car to clear codes"); return; }
    confirmClear = true;
    drawConfirm();
  }
}

// ---------- SETTINGS ----------

const uint8_t ROWS = 9;
const int ROW_Y0 = 31, ROW_H = 23;
const char* ROW_LABELS[ROWS] = {
  "TEMPERATURE", "PRESSURE", "SPEED", "SHIFT POINT", "COOLANT WARNING",
  "BRIGHTNESS", "DISPLAY", "DEMO DATA", "BLUETOOTH DONGLE"
};
float ldrAvg = 0;

void rowValue(uint8_t r, char* buf, size_t len) {
  switch (r) {
    case 0: snprintf(buf, len, "%s", cfg.tempF ? "F" : "C"); break;
    case 1: snprintf(buf, len, "%s", cfg.pressPsi ? "psi" : "kPa"); break;
    case 2: snprintf(buf, len, "%s", cfg.speedMph ? "mph" : "km/h"); break;
    case 3: snprintf(buf, len, "-   %u   +", (unsigned)cfg.shiftRpm); break;
    case 4: {
      int v = cfg.tempF ? (int)lroundf(cfg.coolWarnC * 9.0f / 5.0f + 32) : cfg.coolWarnC;
      snprintf(buf, len, "-   %d%s   +", v, cfg.tempF ? "F" : "C");
      break;
    }
    case 5: {
      const char* n[] = {"Auto", "Low", "Medium", "High"};
      if (cfg.brightness == 0) snprintf(buf, len, "Auto (light %d)", (int)ldrAvg);
      else snprintf(buf, len, "%s", n[cfg.brightness]);
      break;
    }
    case 6: snprintf(buf, len, "%s", cfg.invert ? "Inverted" : "Normal"); break;
    case 7: snprintf(buf, len, "%s", cfg.demo ? "On" : "Off"); break;
    case 8: snprintf(buf, len, "%s  >", cfg.btName[0] ? cfg.btName : "Pair"); break;
  }
}

void settingsStatic() {
  for (uint8_t r = 0; r < ROWS; r++) {
    int y = ROW_Y0 + r * ROW_H;
    tft.fillRect(4, y, 312, ROW_H - 2, C_PANEL);
    tft.fillRect(4, y, 3, ROW_H - 2, (r % 2) ? C_CYAN : C_MAG);
    text(ROW_LABELS[r], 14, y + ROW_H / 2 - 1, 2, C_DIM, C_PANEL, ML_DATUM);
  }
}

void settingsDynamic() {
  char buf[40];
  for (uint8_t r = 0; r < ROWS; r++) {
    rowValue(r, buf, sizeof(buf));
    if (changed(r, buf)) {
      int y = ROW_Y0 + r * ROW_H;
      text(buf, 310, y + ROW_H / 2 - 1, 2, C_CYAN, C_PANEL, MR_DATUM, 160);
    }
  }
}

void settingsTap(int x, int y) {
  if (moving()) { toast("Settings locked while moving"); return; }
  int r = (y - ROW_Y0) / ROW_H;
  if (r < 0 || r >= ROWS) return;
  bool plus = x > 280;
  bool minus = x < 280 && x > 150;
  switch (r) {
    case 0: cfg.tempF ^= 1; needFull = true; break;
    case 1: cfg.pressPsi ^= 1; break;
    case 2: cfg.speedMph ^= 1; break;
    case 3:
      if (plus && cfg.shiftRpm < 9000) cfg.shiftRpm += 250;
      else if (minus && cfg.shiftRpm > 2000) cfg.shiftRpm -= 250;
      break;
    case 4:
      if (plus && cfg.coolWarnC < 125) cfg.coolWarnC++;
      else if (minus && cfg.coolWarnC > 90) cfg.coolWarnC--;
      break;
    case 5: cfg.brightness = (cfg.brightness + 1) % 4; break;
    case 6: cfg.invert ^= 1; tft.invertDisplay(cfg.invert); break;
    case 7: cfg.demo ^= 1; Obd::reconnect(); break;
    case 8: goScreen(SCR_PAIR); return;
  }
  settingsMarkDirty();
}

// ---------- PAIR ----------

const int PAIR_Y0 = 60, PAIR_PITCH = 25, PAIR_H = 22;

void pairStatic() {
  panel(4, 33, 312, 157, C_CYAN);
  char buf[48];
  snprintf(buf, sizeof(buf), "CURRENT: %s", cfg.btName[0] ? cfg.btName : "none");
  text(buf, 160, 46, 2, C_DIM, C_PANEL);
  neonButton(8, 196, 304, 38, "SCAN", C_MAG);
}

void pairDynamic() {
  char key[16];
  bool busy = Obd::scanBusy();
  snprintf(key, sizeof(key), "%d%d", (int)busy, (int)Obd::foundCount());
  if (!changed(0, key)) return;
  tft.fillRect(8, 56, 304, 130, C_PANEL);
  if (busy) {
    text("SEARCHING (ABOUT 8 SECONDS)...", 160, 120, 2, C_ORANGE, C_PANEL);
    return;
  }
  uint8_t n = Obd::foundCount();
  if (n == 0) {
    text("Plug the dongle into the car, turn the", 160, 106, 2, C_DIM, C_PANEL);
    text("ignition on, then tap SCAN.", 160, 124, 2, C_DIM, C_PANEL);
    return;
  }
  if (n > 5) n = 5;
  for (uint8_t i = 0; i < n; i++) {
    Obd::Found f = Obd::found(i);
    int y = PAIR_Y0 + i * PAIR_PITCH;
    tft.fillRect(10, y, 300, PAIR_H, C_PANEL2);
    tft.fillRect(10, y, 3, PAIR_H, C_CYAN);
    text(f.name, 20, y + PAIR_H / 2, 2, C_TXT, C_PANEL2, ML_DATUM);
    text(f.addr, 304, y + PAIR_H / 2, 1, C_DIM, C_PANEL2, MR_DATUM);
  }
}

void pairTap(int x, int y) {
  if (Obd::scanBusy()) return;
  if (inRect(x, y, 8, 196, 304, 38)) {
    Obd::requestScan();
    return;
  }
  uint8_t n = Obd::foundCount();
  if (n > 5) n = 5;
  for (uint8_t i = 0; i < n; i++) {
    if (inRect(x, y, 10, PAIR_Y0 + i * PAIR_PITCH, 300, PAIR_H)) {
      Obd::Found f = Obd::found(i);
      strncpy(cfg.btAddr, f.addr, sizeof(cfg.btAddr) - 1);
      strncpy(cfg.btName, f.name, sizeof(cfg.btName) - 1);
      cfg.demo = 0;
      settingsSave();
      Obd::reconnect();
      goScreen(SCR_MAIN);
      return;
    }
  }
}

// ---------- drawing dispatch ----------

void fullRedraw() {
  clearCache();
  drawTopBar();
  backdrop();
  lastLink = Obd::state();
  switch (scr) {
    case SCR_MAIN:     mainStatic(); break;
    case SCR_QUAD:     quadStatic(); break;
    case SCR_SHIFT:    shiftStatic(); break;
    case SCR_CODES:    codesStatic(); break;
    case SCR_SETTINGS: settingsStatic(); break;
    case SCR_PAIR:     pairStatic(); break;
  }
  if (scr == SCR_CODES && confirmClear) drawConfirm();
  needFull = false;
}

void dynamicRedraw() {
  if (toastUntil) return;   // leave the toast on screen
  switch (scr) {
    case SCR_MAIN:     mainDynamic(); break;
    case SCR_QUAD:     quadDynamic(); break;
    case SCR_SHIFT:    shiftDynamic(); break;
    case SCR_CODES:    codesDynamic(); break;
    case SCR_SETTINGS: settingsDynamic(); break;
    case SCR_PAIR:     pairDynamic(); break;
  }
}

// ---------- warnings, LED, backlight ----------

bool frameOn = false;

void warnFrame() {
  bool w = anyWarn() && scr != SCR_PAIR && scr != SCR_SETTINGS && !confirmClear && !toastUntil;
  bool on = w && ((millis() / 400) % 2 == 0);
  if (on == frameOn) return;
  frameOn = on;
  uint16_t col = on ? C_HOT : C_BG;
  tft.drawRect(0, 29, SCREEN_W, SCREEN_H - 29, col);
  tft.drawRect(1, 30, SCREEN_W - 2, SCREEN_H - 31, col);
}

void led(bool r, bool g, bool b) {
  digitalWrite(PIN_LED_R, r ? LOW : HIGH);
  digitalWrite(PIN_LED_G, g ? LOW : HIGH);
  digitalWrite(PIN_LED_B, b ? LOW : HIGH);
}

void updateLed() {
  bool blink = (millis() / 150) % 2 == 0;
  if (anyWarn()) { led(blink, false, false); return; }
  if (hasData() && Obd::fresh(V_RPM)) {
    float frac = Obd::value(V_RPM) / cfg.shiftRpm;
    if (frac >= 1.0f)  { led(blink, false, false); return; }
    if (frac >= 0.9f)  { led(true, false, true); return; }   // magenta
    if (frac >= 0.75f) { led(false, true, true); return; }   // cyan
  }
  led(false, false, false);
}

int backlightLevel() {
  switch (cfg.brightness) {
    case 1: return 50;
    case 2: return 140;
    case 3: return 255;
  }
  float f = (ldrAvg - LDR_BRIGHT_RAW) / (float)(LDR_DARK_RAW - LDR_BRIGHT_RAW);
  f = constrain(f, 0.0f, 1.0f);
  return 255 - (int)(f * (255 - BL_MIN));
}

void updateBacklight() {
  int raw = analogRead(PIN_LDR);
  ldrAvg = ldrAvg * 0.85f + raw * 0.15f;
  ledcWrite(0, backlightLevel());
}

// ---------- boot splash ----------

void splash() {
  ledcWrite(0, 0);
  tft.setSwapBytes(true);
  tft.pushImage(0, 0, SPLASH_W, SPLASH_H, SPLASH_IMG);
  tft.setSwapBytes(false);

  // glowing title over the picture
  const char* title = SPLASH_TITLE;
  uint16_t outer = mix(C_BG, C_MAG, 0.55f);
  textClear(title, 158, 199, 4, outer); textClear(title, 162, 199, 4, outer);
  textClear(title, 160, 197, 4, outer); textClear(title, 160, 201, 4, outer);
  textClear(title, 160, 199, 4, C_TXT);
  textClear("v" FW_VERSION, 160, 231, 1, C_DIM);

  // fade the backlight up
  int target = max(backlightLevel(), 80);
  for (int i = 1; i <= 25; i++) {
    ledcWrite(0, target * i / 25);
    delay(20);
  }

  // boot bar
  const int bx = 70, bw = 180, by = 215;
  tft.fillRect(bx, by, bw, 4, C_SEGOFF);
  for (int i = 0; i < bw; i += 4) {
    for (int j = i; j < i + 4 && j < bw; j++)
      tft.drawFastVLine(bx + j, by, 4, neonRamp((float)j / (bw - 1)));
    delay(20);
  }
  delay(350);
}

// ---------- touch ----------

bool wasDown = false;
uint32_t lastTap = 0;

void onTap(int x, int y) {
  if (toastUntil) { toastUntil = 0; needFull = true; return; }
  if (y < 28) {
    if (x < 70) {
      if (scr == SCR_PAIR) goScreen(SCR_SETTINGS);
      else goScreen((Scr)((scr + NAV_COUNT - 1) % NAV_COUNT));
    } else if (x > 250 && scr != SCR_PAIR) {
      goScreen((Scr)((scr + 1) % NAV_COUNT));
    }
    return;
  }
  switch (scr) {
    case SCR_MAIN:     mainTap(x, y); break;
    case SCR_QUAD:     quadTap(x, y); break;
    case SCR_SHIFT:    break;
    case SCR_CODES:    codesTap(x, y); break;
    case SCR_SETTINGS: settingsTap(x, y); break;
    case SCR_PAIR:     pairTap(x, y); break;
  }
}

void readTouch() {
  if (!ts.touched()) { wasDown = false; return; }
  TS_Point p = ts.getPoint();
  if (p.z < TOUCH_MIN_PRESSURE) return;
  if (wasDown) return;
  wasDown = true;
  if (millis() - lastTap < 180) return;
  lastTap = millis();
  int x = map(p.x, TOUCH_X_MIN, TOUCH_X_MAX, 0, SCREEN_W);
  int y = map(p.y, TOUCH_Y_MIN, TOUCH_Y_MAX, 0, SCREEN_H);
  if (TOUCH_FLIP_X) x = SCREEN_W - x;
  if (TOUCH_FLIP_Y) y = SCREEN_H - y;
  x = constrain(x, 0, SCREEN_W - 1);
  y = constrain(y, 0, SCREEN_H - 1);
  Serial.printf("[touch] raw %d,%d z%d -> %d,%d\n", p.x, p.y, p.z, x, y);
  onTap(x, y);
}

}  // namespace

// ---------- public ----------

namespace Ui {

void begin() {
  pinMode(PIN_LED_R, OUTPUT);
  pinMode(PIN_LED_G, OUTPUT);
  pinMode(PIN_LED_B, OUTPUT);
  led(false, false, false);

  Serial.println("[ui] display init");
  tft.init();
  tft.setRotation(1);
  tft.invertDisplay(cfg.invert);
  tft.fillScreen(C_BG);

  // Backlight PWM must be attached AFTER tft.init(): init() sets the
  // backlight pin as a plain output, which disconnects the PWM.
  ledcSetup(0, 5000, 8);
  ledcAttachPin(PIN_BL, 0);
  ledcWrite(0, 255);
  Serial.println("[ui] backlight on");

  analogSetPinAttenuation(PIN_LDR, ADC_0db);
  ldrAvg = analogRead(PIN_LDR);
  Serial.printf("[ui] light sensor %d, backlight level %d\n", (int)ldrAvg, backlightLevel());

  touchSpi.begin(TOUCH_CLK, TOUCH_MISO, TOUCH_MOSI, TOUCH_CS);
  ts.begin(touchSpi);
  ts.setRotation(1);
  Serial.println("[ui] touch ready");

  // Sprites for flicker-free glowing numbers. If memory is short the
  // gauge falls back to plain text, so this can't stop it from working.
  haveBig = spBig.createSprite(BIG_W, BIG_H) != nullptr;
  haveTile = spTile.createSprite(TILE_W, TILE_H) != nullptr;
  Serial.printf("[ui] sprites: big %d tile %d\n", (int)haveBig, (int)haveTile);

  Serial.println("[ui] splash");
  splash();
  Serial.println("[ui] ready");

  scr = cfg.screen < NAV_COUNT ? (Scr)cfg.screen : SCR_MAIN;
  goScreen(scr);
}

void loop() {
  static uint32_t tDyn = 0, tSlow = 0;
  uint32_t now = millis();

  readTouch();

  if (toastUntil && (int32_t)(now - toastUntil) >= 0) { toastUntil = 0; needFull = true; }
  if (needFull) fullRedraw();

  if (now - tDyn >= 80) {
    tDyn = now;
    if (Obd::state() != lastLink) {
      lastLink = Obd::state();
      if (scr == SCR_CODES && !confirmClear) needFull = true;  // button colors etc.
      else drawStatusDot();
    }
    dynamicRedraw();
    warnFrame();
    updateLed();
  }
  if (now - tSlow >= 250) {
    tSlow = now;
    updateBacklight();
  }
  settingsLoop();
}

}  // namespace Ui
