#pragma once

#include <AnkiTypes.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "activities/UiListActivity.h"

// "Add to Anki" checklist over the reader's word-select screen. A band above
// the list shows the picked word, its sentence and the dictionary translation
// (if any); below it one toggle row per enabled account (subtitle = the deck
// this add will use; Left/Right cycle it for the highlighted row, never
// persisted) and a final "Add" row that queues one note per checked account
// (notes.jsonl) for the next sync. The checked set is remembered across adds.
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

  // One enabled account. ListItem label/subtitle point into `label` and
  // `decks`, so `targets` is built once in onEnter() and never resized.
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
  void queueNotes();
  bool anyChecked() const;

  const std::string noteWord;
  const std::string sentence;
  const std::string translation;
  const std::string bookTitle;

  std::vector<Target> targets;
  std::vector<freeink::ui::ListItem> rowItems;  // targets + the Add row

  Popup popup = Popup::None;
  unsigned long popupTime = 0;
  char popupText[64] = {};
};
