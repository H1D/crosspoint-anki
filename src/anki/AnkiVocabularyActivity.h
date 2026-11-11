#pragma once

#include <AnkiVocabulary.h>

#include <cstdint>
#include <string>
#include <vector>

#include "activities/Activity.h"
#include "components/UITheme.h"
#include "util/ButtonNavigator.h"

// "Send Words to Anki": the words looked up since the last send (from
// /Vocabulary.txt, written when Settings > Reader > Save Vocabulary Lookups is
// on) as a table, one row per word and one column per enabled account, a
// checkbox in each cell. Send queues one note per ticked cell (notes.jsonl,
// like Add to Anki; the next sync delivers them) and moves the "vocabDone"
// marker in anki.json past the window, so unticked words are dismissed.
//
// Side buttons move between rows (the header row, every word, then Send),
// front Left/Right between accounts, Confirm toggles a cell, a whole column
// on the header row, or sends. Taps toggle a cell or a column header; swipes
// scroll rows and, when the accounts do not fit, columns.
class AnkiVocabularyActivity final : public Activity {
 public:
  // The window holds at most this many words.
  static constexpr size_t MAX_ROWS = 100;

  AnkiVocabularyActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("AnkiVocabulary", renderer, mappedInput) {}

  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  enum class Popup : uint8_t { None, Queued, Failed };
  enum class Empty : uint8_t { No, NoAccounts, SettingOff, NothingNew };

  struct Column {
    uint32_t id;
    std::string name;
    std::string deck;
    std::string model;
  };

  // Table geometry, derived from the screen, never stored: the loop task
  // (taps, scrolling) and the render task compute the same one.
  struct Layout {
    int x, width, scrollBarX;
    int headerTop, rowsTop, rowsBottom;
    int rowHeight;
    int wordWidth, columnWidth;
    int visibleColumns, visibleRows;
  };

  Layout layout() const;
  int rowCount() const { return static_cast<int>(window.labels.size()); }
  // Cursor rows: 0 the column headers, 1..rowCount() the words, then Send.
  int cursorRows() const { return rowCount() + 2; }
  int columnAt(const Layout& l, int x) const;
  int entryAt(const Layout& l, int y) const;

  void moveCursor(int row, int col);
  void scrollRows(int delta);
  void scrollColumns(int delta);
  void followCursor();
  void activate();
  void toggleCell(int row, int col);
  void toggleColumn(int col);
  void send();
  void handleTap(int x, int y);

  void drawTable(const Layout& l) const;
  void drawCheckbox(int x, int y, bool checked) const;
  void drawArrow(int x, int centerY, bool pointRight) const;
  void drawCentered(const Layout& l, const char* text) const;

  ankivocab::Window window;
  std::vector<Column> columns;
  std::vector<uint8_t> masks;  // per row: bit c = column c ticked
  Empty empty = Empty::No;

  int cursorRow = 0;
  int cursorCol = 0;
  int top = 0;       // first visible word/Send entry
  int firstCol = 0;  // first visible account column

  Popup popup = Popup::None;
  unsigned long popupTime = 0;
  char popupText[48] = {};
  ButtonNavigator buttonNavigator;
};
