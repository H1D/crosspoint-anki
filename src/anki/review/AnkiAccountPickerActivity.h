#pragma once

#include <string>
#include <vector>

#include "activities/UiListActivity.h"

// Asks which enabled account to review when more than one is enabled. One row
// per enabled account plus a trailing hint row; selecting a row stores it as
// the review account and returns MenuResult{store index}. Back cancels.
class AnkiAccountPickerActivity final : public UiListActivity {
 public:
  explicit AnkiAccountPickerActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : UiListActivity("AnkiAccountPicker", renderer, mappedInput) {}

  void onEnter() override;

 private:
  // Selectable rows only; the hint row is drawn but never reachable.
  int listCount() const override { return static_cast<int>(rowItems_.size()) - 1; }
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  void onBackButton() override;
  const char* headerTitle() const override;

  // ListItem holds raw pointers, so the "profile@url" subtitles live here.
  std::vector<freeink::ui::ListItem> rowItems_;
  std::vector<std::string> subtitles_;
  void rebuildRowItems();
};
