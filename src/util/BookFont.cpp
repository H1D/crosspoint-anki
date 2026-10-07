#include "BookFont.h"

#include <ArduinoJson.h>
#include <HalStorage.h>
#include <Logging.h>
#include <PersistableStore.h>

#include <cstring>

#include "CrossPointSettings.h"
#include "ReaderFontSizes.h"
#include "SdCardFontSystem.h"

namespace {

// font.json of the open book; empty outside a book.
std::string fontPath;

bool load(const std::string& path, CrossPointSettings::ReaderFont& font) {
  JsonDocument doc;
  if (!PersistableStoreBase::readDocFromFile(path.c_str(), doc)) return false;
  const uint8_t pointSize = doc["size"] | static_cast<uint8_t>(0);
  if (pointSize == 0) return false;
  font = CrossPointSettings::ReaderFont();
  font.family = doc["family"] | static_cast<uint8_t>(CrossPointSettings::NOTOSERIF);
  font.pointSize = pointSize;
  strncpy(font.sdFamilyName, doc["sd"] | "", sizeof(font.sdFamilyName) - 1);
  return true;
}

}  // namespace

void BookFont::begin(const std::string& cachePath) {
  fontPath = cachePath + "/font.json";
  // Stash the default at a size its family ships; ensureLoaded() would snap it
  // after the stash and the book would no longer match the default it follows.
  SETTINGS.fontPointSize = snapToNearestPointSize(
      readerFontPointSizes(&sdFontSystem.registry(), SETTINGS.sdFontFamilyName), SETTINGS.fontPointSize);
  CrossPointSettings::ReaderFont font;
  const bool hasOwn = load(fontPath, font);
  if (hasOwn)
    LOG_DBG("BKF", "Book font: %s %u pt", font.sdFamilyName[0] ? font.sdFamilyName : "built-in", font.pointSize);
  SETTINGS.beginBookFontScope(hasOwn ? &font : nullptr);
}

bool BookFont::end() {
  fontPath.clear();
  return SETTINGS.endBookFontScope();
}

void BookFont::remember() {
  if (fontPath.empty() || !SETTINGS.inBookFontScope()) return;
  const CrossPointSettings::ReaderFont font = SETTINGS.readerFont();
  if (font == SETTINGS.defaultReaderFont()) {
    if (Storage.exists(fontPath.c_str()) && !Storage.remove(fontPath.c_str())) {
      LOG_ERR("BKF", "Failed to remove book font: %s", fontPath.c_str());
    }
    return;
  }
  JsonDocument doc;
  doc["family"] = font.family;
  doc["size"] = font.pointSize;
  if (font.sdFamilyName[0] != '\0') doc["sd"] = font.sdFamilyName;
  if (!PersistableStoreBase::writeDocToFile(fontPath.c_str(), doc)) {
    LOG_ERR("BKF", "Failed to save book font: %s", fontPath.c_str());
  }
}
