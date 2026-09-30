#pragma once

#include <AnkiTypes.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "activities/UiListActivity.h"

// One word's note-to-be, gathered by the word picker.
struct AnkiNoteDraft {
  // As picked on the page.
  std::string word;
  // Dictionary form of `word` when a lookup found a different one ("komen"
  // for "kwam"), else empty. Goes on the card instead of `word`.
  std::string headword;
  std::string sentence;
  std::string translation;
  std::string bookTitle;
};

// "Add to Anki" screen: a preview of the card (front, example sentence,
// back) above one checkbox row per enabled account with its target deck as
// subtitle, then an "Add" row that queues one note per checked account
// (notes.jsonl) for the next sync. Left/Right cycle the highlighted account's
// deck; a long press on its row (Confirm hold or touch) opens the deck list.
// The deck a note is added to becomes the account's default deck; the
// checked set is remembered across adds. The result is cancelled unless
// notes were queued.
class AnkiAddNoteActivity final : public UiListActivity {
 public:
  explicit AnkiAddNoteActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, AnkiNoteDraft draft)
      : UiListActivity("AnkiAddNote", renderer, mappedInput, /*wantsTouchLongPress=*/true), draft(std::move(draft)) {}

  void onEnter() override;
  void render(RenderLock&&) override;

 private:
  enum class Popup : uint8_t { None, Queued, Failed };

  // One enabled account. ListItem label/value point into `label` and
  // `decks`, so rows are rebuilt (refreshRows) whenever either changes.
  struct Target {
    size_t storeIndex;
    uint32_t id;
    std::string label;
    std::vector<std::string> decks;  // never empty ("Default" at worst)
    int deckPos;
    bool checked;
  };

  int listCount() const override { return static_cast<int>(rowItems.size()); }
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  void onRowLongPress(int index) override;
  bool handleCustomInput() override;
  bool handleButtons() override;
  const char* headerTitle() const override;
  void drawFooter() override;

  void buildTargets();
  static std::vector<std::string> deckChoices(const AnkiAccount& account);
  void refreshRows();
  void cycleDeck(int direction);
  // Ticks the account (choosing a deck implies adding to it), then opens its deck list.
  void openDeckPicker(size_t targetIndex);
  void setDeck(size_t targetIndex, const std::string& deck);
  void queueNotes();
  bool anyChecked() const;

  // Card preview above the list, then the dimmed "hold to change" after
  // each account row's deck name.
  void buildPreview(UiScreen& screen);
  void drawDeckHints(UiScreen& screen, const freeink::ui::ListProps& props, freeink::ui::Rect area);
  // Text in `style`, its ink thinned to a 50% checkerboard: the renderer has
  // no dithered text, so FUI's gray text colors draw solid black.
  void drawDimmedText(UiScreen& screen, freeink::ui::Rect rect, const char* text, const freeink::ui::TextStyle& style);

  const AnkiNoteDraft draft;

  std::vector<Target> targets;
  // One row per target + the Add row.
  std::vector<freeink::ui::ListItem> rowItems;

  Popup popup = Popup::None;
  unsigned long popupTime = 0;
  char popupText[64] = {};
};
