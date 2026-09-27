#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "activities/UiListActivity.h"

/**
 * Deck picker for one account, fed by the deck list cached by the last sync.
 *
 * Multi mode (review decks): toggle rows, "All decks" first (clears the
 * selection), plus any selected deck missing from the cache so it can be
 * unticked. The selection is written to the account's `decks` on Back.
 *
 * Single mode (deck for new words): plain rows, STR_NOT_SET first. Choosing a
 * row returns the name as KeyboardResult{text} ("" for none) and finishes;
 * Back returns a cancelled result. The caller persists the choice.
 */
class AnkiDeckSelectActivity final : public UiListActivity {
 public:
  enum class Mode { Multi, Single };

  explicit AnkiDeckSelectActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, int accountIndex,
                                  Mode mode = Mode::Multi);

  // True when the last sync left a deck list for this account (callers fall
  // back to typing a name otherwise).
  static bool hasCachedDecks(uint32_t accountId);

  void onEnter() override;

 private:
  int listCount() const override { return static_cast<int>(rowItems_.size()); }
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  void onBackButton() override;
  const char* headerTitle() const override;

  int accountIndex;
  Mode mode;
  bool dirty = false;
  // Deck names in row order (row i+1); ListItem labels point into them, so
  // the vector is built once in onEnter() and never resized afterwards.
  std::vector<std::string> names_;
  std::vector<std::string> selected_;
  std::vector<freeink::ui::ListItem> rowItems_;

  bool isSelected(const std::string& name) const;
  void refreshChecks();
  void returnSingle(const std::string& name);
};
