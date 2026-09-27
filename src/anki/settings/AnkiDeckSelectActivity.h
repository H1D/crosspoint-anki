#pragma once

#include <string>
#include <vector>

#include "activities/UiListActivity.h"

/**
 * Multi-select deck picker for one account. Rows are toggles: "All decks"
 * (clears the selection) followed by the deck names cached by the last sync,
 * plus any selected deck missing from the cache (typed on the web page). The
 * selection is written to the account's `decks` on Back.
 */
class AnkiDeckSelectActivity final : public UiListActivity {
 public:
  explicit AnkiDeckSelectActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, int accountIndex);

  void onEnter() override;

 private:
  int listCount() const override { return static_cast<int>(rowItems_.size()); }
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  void onBackButton() override;
  const char* headerTitle() const override;

  int accountIndex;
  bool dirty = false;
  // Deck names in row order (row i+1); ListItem labels point into them, so
  // the vector is built once in onEnter() and never resized afterwards.
  std::vector<std::string> names_;
  std::vector<std::string> selected_;
  std::vector<freeink::ui::ListItem> rowItems_;

  bool isSelected(const std::string& name) const;
  void refreshChecks();
};
