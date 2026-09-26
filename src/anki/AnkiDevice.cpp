#include "AnkiDevice.h"

#include <AnkiPaths.h>
#include <HalClock.h>
#include <Logging.h>
#include <esp_mac.h>

#include <array>
#include <cstdio>
#include <ctime>

#include "AnkiStorageFs.h"

namespace ankidevice {

namespace {
uint32_t counter = 0;
bool counterLoaded = false;

void loadCounter() {
  if (counterLoaded) return;
  counterLoaded = true;
  std::string text;
  if (!AnkiStorageFs::instance().readAll(ankipaths::stateFile(), text)) return;
  // {"counter":N} — tiny, hand-parsed to keep ArduinoJson out of this TU.
  const size_t at = text.find("\"counter\":");
  if (at != std::string::npos) counter = static_cast<uint32_t>(strtoul(text.c_str() + at + 10, nullptr, 10));
}

void saveCounter() {
  AnkiStorageFs::instance().mkdirs(ankipaths::root());
  const std::string text = "{\"counter\":" + std::to_string(counter) + "}";
  if (!AnkiStorageFs::instance().writeAllAtomic(ankipaths::stateFile(), text)) {
    LOG_ERR("ANKI", "Cannot persist client id counter");
  }
}
}  // namespace

const std::string& deviceId() {
  static std::string id;
  if (id.empty()) {
    std::array<uint8_t, 6> mac{};
    esp_efuse_mac_get_default(mac.data());
    char buf[16];
    snprintf(buf, sizeof(buf), "cp%02x%02x%02x", mac[3], mac[4], mac[5]);
    id = buf;
  }
  return id;
}

std::string nextClientId() {
  loadCounter();
  counter++;
  saveCounter();
  return deviceId() + "-" + std::to_string(counter);
}

int64_t nowEpoch() {
  struct tm local {};
  if (!halClock.localTime(local)) return 0;
  if (local.tm_year + 1900 < 2024) return 0;  // RTC never set: not trustworthy
  // localTime() fills a local-time tm under the configured TZ; mktime maps it
  // back to UTC epoch seconds.
  const time_t t = mktime(&local);
  return t <= 0 ? 0 : static_cast<int64_t>(t);
}

}  // namespace ankidevice
