#pragma once

#include <Epub/Page.h>
#include <I18n.h>

#include <memory>
#include <string>
#include <vector>

#include "activities/Activity.h"
#include "anki/AnkiAddNoteActivity.h"
#include "util/Dictionary.h"

// Word selection over the current reader page: Left/Right step through words
// in reading order, Up/Down jump rows, Confirm acts on the word, Back returns
// to the reader. On touch devices a touch-down moves the highlight and a tap
// on a word acts on it directly.
//
// The picked word opens DictionaryDefinitionActivity. With an Anki account
// enabled, that view also offers "Add to Anki" with a note draft: the word,
// the sentence around it on the page, and the dictionary's translation.
// Mode::AnkiAdd works without a dictionary: with none, or when the lookup
// misses, it opens AnkiAddNoteActivity directly with an empty translation.
// `dictionaryName` is the folder picked for the book
// (DictionaryRegistry::pickForBook), "" for none.
class DictionaryWordSelectActivity final : public Activity {
 public:
  enum class Mode : uint8_t { Lookup, AnkiAdd };

  explicit DictionaryWordSelectActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                        std::unique_ptr<Page> page, int marginLeft, int marginTop,
                                        std::string dictionaryName, Mode mode = Mode::Lookup,
                                        std::string bookTitle = {}, int initialX = -1, int initialY = -1)
      : Activity("DictionaryWordSelect", renderer, mappedInput),
        page(std::move(page)),
        marginLeft(marginLeft),
        marginTop(marginTop),
        dictionaryName(std::move(dictionaryName)),
        mode(mode),
        bookTitle(std::move(bookTitle)),
        initialX(initialX),
        initialY(initialY) {}

  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  // Screen box of one selectable word. `text` points into the owned Page's
  // TextBlock arena (NUL-terminated), valid for this activity's lifetime.
  struct WordBox {
    int16_t x;
    int16_t y;
    int16_t width;
    uint16_t row;
    const char* text;
    EpdFontFamily::Style style;
  };

  enum class Popup : uint8_t { None, Busy, NotFound, Error };

  void extractWords();
  int closestInRow(uint16_t row, int centerX) const;
  int wordAt(int x, int y) const;
  void moveVertical(int direction);
  // Confirm / tap on the selected word.
  void activateSelected();
  // `anki`: the definition view offers "Add to Anki".
  void performLookup(bool anki);
  // Draft for the selected word; `definition` (raw, as looked up) supplies
  // the translation, `headword` the dictionary form when it differs.
  AnkiNoteDraft noteDraft(const std::string* definition, std::string headword) const;
  void openAnkiAdd(AnkiNoteDraft draft);
  // Opens the dictionary once per activity and picks the busy popup text.
  void openDictionaryOnce();
  // Builds the index if needed, then looks `token` up. True on a hit; the
  // failure detail lands in dictReady / lastIndexResult / lastLookupResult.
  bool lookupWord(const char* token, std::string& definition, std::string& headword);
  bool drawHighlightWithSnapshot();
  void drawHints() const;

  std::unique_ptr<Page> page;
  const int marginLeft;
  const int marginTop;
  const std::string dictionaryName;
  const Mode mode;
  const std::string bookTitle;
  // Screen point to preselect from (a touch long-press on the page); -1 = none.
  const int initialX;
  const int initialY;
  int fontId = 0;
  int lineHeight = 0;

  std::vector<WordBox> words;
  int selected = 0;
  uint16_t rowCount = 0;
  unsigned long lastHorizontalMoveTime = 0;

  Dictionary dict;
  bool dictOpenAttempted = false;
  bool dictOpenOk = false;
  bool dictNeedsIndex = false;
  bool dictReady = false;  // open succeeded and the index is fresh
  Dictionary::IndexResult lastIndexResult = Dictionary::IndexResult::Ok;
  Dictionary::LookupResult lastLookupResult = Dictionary::LookupResult::NotFound;

  Popup popup = Popup::None;
  StrId popupMsg = StrId::STR_DICT_NOT_FOUND;
  unsigned long popupTime = 0;

  // Differential highlight repaint: the pixels under the current highlight
  // box, so a cursor move restores them and repaints only the two affected
  // boxes instead of re-running the full two-pass page render (which also
  // reloads every SD-font glyph on the page). snapshotIdx is the word whose
  // under-pixels are saved; -1 means the framebuffer no longer holds a clean
  // page (popup drawn, sub-activity shown) and the next render must be full.
  static constexpr size_t SNAPSHOT_CAPACITY = 4096;
  std::unique_ptr<uint8_t[]> snapshot;
  int16_t snapshotX = 0;
  int16_t snapshotY = 0;
  int16_t snapshotW = 0;
  int16_t snapshotH = 0;
  int snapshotIdx = -1;
};
