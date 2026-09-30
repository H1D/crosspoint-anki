#pragma once

#include <AnkiTypes.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "activities/UiListActivity.h"

// "Add to Anki" checklist over the reader's word-select screen. One line
// above the list shows the picked word and its dictionary translation (if
// any); below it, one checkbox row per enabled account with the target deck
// as its subtitle, then an "Add" row that queues one note per checked account
// (notes.jsonl) for the next sync. Left/Right cycle the highlighted account's
// deck; a long press on its row (Confirm hold or touch) opens the deck list.
// Deck choices are for this add only. The checked set is remembered across
// adds. The result is cancelled unless notes were queued.
class AnkiAddNoteActivity final : public UiListActivity {
 public:
  // `noteWord`, not `word`: Arduino.h defines word(...) as a macro.
  explicit AnkiAddNoteActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::string noteWord,
                               std::string headword, std::string sentence, std::string translation,
                               std::string bookTitle)
      : UiListActivity("AnkiAddNote", renderer, mappedInput, /*wantsTouchLongPress=*/true),
        noteWord(std::move(noteWord)),
        headword(std::move(headword)),
        sentence(std::move(sentence)),
        translation(std::move(translation)),
        bookTitle(std::move(bookTitle)) {}

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

  const std::string noteWord;
  // Dictionary form of noteWord when a lookup found a different one ("komen"
  // for "kwam"), else empty. Goes on the card instead of noteWord.
  const std::string headword;
  const std::string sentence;
  const std::string translation;
  const std::string bookTitle;
  // The one-line band above the list: "word — translation", or
  // "word (headword) — translation".
  std::string summary;

  std::vector<Target> targets;
  // One row per target + the Add row.
  std::vector<freeink::ui::ListItem> rowItems;

  Popup popup = Popup::None;
  unsigned long popupTime = 0;
  char popupText[64] = {};
};
