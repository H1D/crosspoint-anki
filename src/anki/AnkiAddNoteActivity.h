#pragma once

#include <AnkiTypes.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "activities/UiListActivity.h"

// "Add to Anki" checklist over the reader's word-select screen. A band above
// the list shows the picked word, its sentence and the dictionary translation
// (if any); below it, per enabled account, a checkbox row and a "Deck" row
// that opens the deck list (Left/Right also cycle it; never persisted), then
// an "Add" row that queues one note per checked account (notes.jsonl) for
// the next sync. The checked set is remembered across adds. The result is
// cancelled unless notes were queued.
class AnkiAddNoteActivity final : public UiListActivity {
 public:
  // `noteWord`, not `word`: Arduino.h defines word(...) as a macro.
  explicit AnkiAddNoteActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::string noteWord,
                               std::string sentence, std::string translation, std::string bookTitle)
      : UiListActivity("AnkiAddNote", renderer, mappedInput),
        noteWord(std::move(noteWord)),
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
  bool handleCustomInput() override;
  const char* headerTitle() const override;
  void drawFooter() override;

  void buildTargets();
  static std::vector<std::string> deckChoices(const AnkiAccount& account);
  void refreshRows();
  void cycleDeck(int direction);
  void openDeckPicker(size_t targetIndex);
  void setDeck(size_t targetIndex, const std::string& deck);
  void queueNotes();
  bool anyChecked() const;

  const std::string noteWord;
  const std::string sentence;
  const std::string translation;
  const std::string bookTitle;
  // `sentence` as shown in the two-line band: cut in front so the word stays visible.
  std::string sentenceShown;

  std::vector<Target> targets;
  // Two rows per target (checkbox, deck) + the Add row.
  std::vector<freeink::ui::ListItem> rowItems;

  Popup popup = Popup::None;
  unsigned long popupTime = 0;
  char popupText[64] = {};
};
