#include "settings.h"
#include <Preferences.h>

Settings cfg;

static Preferences prefs;
static bool dirty = false;
static uint32_t dirtyAt = 0;

void settingsLoad() {
  prefs.begin("obdg", false);
  uint8_t ver = prefs.getUChar("ver", 0);
  if (ver == SETTINGS_VERSION && prefs.getBytesLength("cfg") == sizeof(Settings)) {
    prefs.getBytes("cfg", &cfg, sizeof(Settings));
  } else {
    cfg = Settings();
    settingsSave();
  }
}

void settingsSave() {
  prefs.putBytes("cfg", &cfg, sizeof(Settings));
  prefs.putUChar("ver", SETTINGS_VERSION);
  dirty = false;
}

void settingsMarkDirty() {
  dirty = true;
  dirtyAt = millis();
}

void settingsLoop() {
  if (dirty && millis() - dirtyAt > 2000) settingsSave();
}
