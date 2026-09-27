#pragma once

#include <AnkiTypes.h>

#include <cstdint>
#include <string>
#include <vector>

#include "activities/Activity.h"

// "Add to Anki" modal over the reader's word-select screen: shows the picked
// word, its sentence and the dictionary translation (if any), lets the user
// pick the account (Up/Down) and deck (Left/Right), and on Confirm appends the
// note to that account's queue (notes.jsonl) for the next sync.
class AnkiAddNoteActivity final : public Activity {
 public:
  // `noteWord`, not `word`: Arduino.h defines word(...) as a macro.
  explicit AnkiAddNoteActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::string noteWord,
                               std::string sentence, std::string translation, std::string bookTitle)
      : Activity("AnkiAddNote", renderer, mappedInput),
        noteWord(std::move(noteWord)),
        sentence(std::move(sentence)),
        translation(std::move(translation)),
        bookTitle(std::move(bookTitle)) {}

  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  enum class Popup : uint8_t { None, Queued, Failed };

  void loadAccounts();
  // Loads the deck list for the current account and preselects its last deck.
  void loadDecks();
  const AnkiAccount* currentAccount() const;
  void cycleAccount(int direction);
  void cycleDeck(int direction);
  void addNote();
  void drawBody(int contentX, int contentWidth, int top, int bottom) const;
  bool sideHintsShown() const;
  void drawHints() const;

  const std::string noteWord;
  const std::string sentence;
  const std::string translation;
  const std::string bookTitle;

  // Store indices of the enabled accounts, in store order.
  std::vector<size_t> accountIndices;
  int accountPos = -1;
  std::vector<std::string> deckNames;
  int deckPos = 0;
  bool decksFromSync = false;  // names came from decks.json rather than a fallback

  Popup popup = Popup::None;
  unsigned long popupTime = 0;
  char popupText[64] = {};
};
