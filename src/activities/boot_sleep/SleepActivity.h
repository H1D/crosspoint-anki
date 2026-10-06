#pragma once
#include <string>

#include "activities/Activity.h"

class Bitmap;
class HalFile;

class SleepActivity final : public Activity {
 public:
  // showPopup=false skips the "going to sleep" popup (refresh wakes redraw
  // the sleep screen with nobody looking).
  explicit SleepActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, bool fromTimeout = false,
                         bool showPopup = true)
      : Activity("Sleep", renderer, mappedInput), fromTimeout(fromTimeout), showPopup(showPopup) {}
  void onEnter() override;

 private:
  void renderDefaultSleepScreen() const;
  void renderCustomSleepScreen() const;
  void renderCoverSleepScreen() const;
  void renderBitmapSleepScreen(const Bitmap& bitmap, bool preserveBackground = false) const;
  bool renderSleepOverlayFile(HalFile& file, const char* pathForLog) const;
  bool renderTransparentOverlayPng(const std::string& path) const;
  bool renderSleepOverlayPath(const std::string& path) const;
  void renderLastScreenSleepScreen() const;
  void renderTransparentCustomSleepScreen() const;
  void renderBlankSleepScreen() const;

  bool fromTimeout = false;
  bool showPopup = true;
};
