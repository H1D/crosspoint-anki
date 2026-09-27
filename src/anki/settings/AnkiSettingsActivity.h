#pragma once

#include <string>
#include <vector>

#include "activities/UiListActivity.h"

/**
 * Settings screen listing the configured AnkiDo accounts (Settings > System >
 * Anki accounts). One row per account plus "Add account"; selecting a row
 * opens AnkiAccountActivity. The review account is marked in the value slot.
 */
class AnkiSettingsActivity final : public UiListActivity {
 public:
  explicit AnkiSettingsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput);

  void onEnter() override;

 private:
  int listCount() const override { return static_cast<int>(rowItems_.size()); }
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  const char* headerTitle() const override;

  // Row structure, rebuilt only when the account list reloads (onEnter() and
  // after the editor returns), never per repaint. ListItem holds raw pointers,
  // so the "profile@url" subtitles are owned here alongside the rows.
  std::vector<freeink::ui::ListItem> rowItems_;
  std::vector<std::string> subtitles_;
  void rebuildRowItems();

  void openEditor(int accountIndex);
};
