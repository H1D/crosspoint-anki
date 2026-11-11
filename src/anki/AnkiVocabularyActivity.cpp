#include "AnkiVocabularyActivity.h"

#include <AnkiNoteQueue.h>
#include <AnkiPaths.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Logging.h>

#include <algorithm>
#include <cstdio>

#include "CrossPointSettings.h"
#include "MappedInputManager.h"
#include "anki/AnkiAccountStore.h"
#include "anki/AnkiAddNoteActivity.h"
#include "anki/AnkiDevice.h"
#include "anki/AnkiStorageFs.h"
#include "fontIds.h"
#include "vocabulary/VocabularyStore.h"

namespace {

constexpr unsigned long POPUP_DURATION_MS = 1200;
constexpr int ROW_PADDING = 16;
constexpr int MIN_TOUCH_ROW = 44;
constexpr int COLUMN_PADDING = 4;
// Same box as a FreeInkUI list toggle: 2 px outline, filled square inside.
constexpr int BOX = 22;
constexpr int ARROW = 6;
constexpr int SCROLL_BAR_WIDTH = 3;

class FileSource final : public ankivocab::Source {
 public:
  explicit FileSource(HalFile& file) : file(file) {}
  bool seek(const size_t pos) override { return file.seek(pos); }
  int read(void* buf, const size_t len) override { return file.read(buf, len); }

 private:
  HalFile& file;
};

}  // namespace

void AnkiVocabularyActivity::onEnter() {
  Activity::onEnter();
  ANKI_STORE.loadFromFile();

  // ActivityManager runs onEnter without the render lock and a pending render
  // may still draw this activity: build locally, publish in one short block.
  std::vector<Column> newColumns;
  ankivocab::Window newWindow;
  std::vector<uint8_t> newMasks;
  Empty newEmpty = Empty::No;

  const auto& accounts = ANKI_STORE.getAccounts();
  const std::vector<size_t> ticked = ANKI_STORE.defaultAddTargets();
  uint8_t defaults = 0;
  for (size_t i = 0; i < accounts.size(); i++) {
    const AnkiAccount& a = accounts[i];
    if (!a.enabled) continue;
    if (std::find(ticked.begin(), ticked.end(), i) != ticked.end()) {
      defaults = static_cast<uint8_t>(defaults | (1u << newColumns.size()));
    }
    newColumns.push_back({a.id, a.name.empty() ? a.url : a.name, AnkiAddNoteActivity::deckChoices(a).front(), a.model});
  }
  if (newColumns.empty()) {
    newEmpty = Empty::NoAccounts;
  } else {
    HalFile file;
    if (Storage.openFileForRead("ANKI", VocabularyStore::FILE_PATH, file)) {
      FileSource source(file);
      newWindow = ankivocab::scan(source, ANKI_STORE.getVocabDone(), MAX_ROWS);
    }
    newMasks.assign(newWindow.labels.size(), defaults);
    if (newWindow.labels.empty()) newEmpty = SETTINGS.vocabularyHistoryEnabled ? Empty::NothingNew : Empty::SettingOff;
  }

  {
    RenderLock lock;
    columns = std::move(newColumns);
    window = std::move(newWindow);
    masks = std::move(newMasks);
    queued.assign(masks.size(), 0);
    queuedTotal = 0;
    empty = newEmpty;
    cursorRow = cursorCol = top = firstCol = 0;
    popup = Popup::None;
  }
  requestUpdate();
}

AnkiVocabularyActivity::Layout AnkiVocabularyActivity::layout() const {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const bool touch = mappedInput.hasTouch();
  const Rect safe = UITheme::getInstance().getScreenSafeArea(renderer, !touch, false);
  Layout l{};
  l.x = safe.x + metrics.contentSidePadding;
  l.width = safe.width - 2 * metrics.contentSidePadding;
  l.scrollBarX = safe.x + safe.width - SCROLL_BAR_WIDTH - 2;
  l.headerTop = safe.y + metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  l.rowHeight =
      std::max({metrics.listRowHeight, renderer.getLineHeight(UI_10_FONT_ID) + ROW_PADDING, touch ? MIN_TOUCH_ROW : 0});
  l.rowsTop = l.headerTop + l.rowHeight;
  l.rowsBottom = safe.y + safe.height - metrics.verticalSpacing;
  l.visibleRows = std::max(1, (l.rowsBottom - l.rowsTop) / l.rowHeight);

  // A column fits an account name of about eight characters and the box; the
  // word column keeps at least two fifths of the width.
  l.columnWidth = std::max(renderer.getTextWidth(SMALL_FONT_ID, "nnnnnnnn"), BOX) + 2 * COLUMN_PADDING;
  const int fit = std::max(1, (l.width - l.width * 2 / 5) / l.columnWidth);
  l.visibleColumns = std::clamp(static_cast<int>(columns.size()), 1, fit);
  l.wordWidth = l.width - l.visibleColumns * l.columnWidth;
  return l;
}

int AnkiVocabularyActivity::columnAt(const Layout& l, const int x) const {
  if (x < l.x + l.wordWidth || x >= l.x + l.width) return -1;
  const int column = firstCol + (x - l.x - l.wordWidth) / l.columnWidth;
  return column < static_cast<int>(columns.size()) ? column : -1;
}

// The word (or Send, one past the last word) drawn at y, else -1.
int AnkiVocabularyActivity::entryAt(const Layout& l, const int y) const {
  if (y < l.rowsTop || y >= l.rowsBottom) return -1;
  const int slot = (y - l.rowsTop) / l.rowHeight;
  const int entry = top + slot;
  return slot < l.visibleRows && entry <= rowCount() ? entry : -1;
}

void AnkiVocabularyActivity::followCursor() {
  const Layout l = layout();
  const int entries = rowCount() + 1;
  if (cursorRow > 0) {
    const int entry = cursorRow - 1;
    if (entry < top) {
      top = entry;
    } else if (entry >= top + l.visibleRows) {
      top = entry - l.visibleRows + 1;
    }
  }
  top = std::clamp(top, 0, std::max(0, entries - l.visibleRows));
  if (cursorCol < firstCol) {
    firstCol = cursorCol;
  } else if (cursorCol >= firstCol + l.visibleColumns) {
    firstCol = cursorCol - l.visibleColumns + 1;
  }
  firstCol = std::clamp(firstCol, 0, static_cast<int>(columns.size()) - l.visibleColumns);
}

void AnkiVocabularyActivity::moveCursor(const int row, const int col) {
  {
    RenderLock lock;  // render reads the cursor and the viewport
    cursorRow = row;
    cursorCol = col;
    followCursor();
  }
  requestUpdate();
}

// After a swipe the cursor may sit on a row or column that scrolled out of
// view; Confirm would then toggle a cell the user cannot see.
void AnkiVocabularyActivity::clampCursorToView(const Layout& l) {
  if (cursorRow > 0) {
    cursorRow = std::clamp(cursorRow, top + 1, top + l.visibleRows);
    cursorRow = std::min(cursorRow, rowCount() + 1);
  }
  cursorCol = std::clamp(cursorCol, firstCol, firstCol + l.visibleColumns - 1);
}

void AnkiVocabularyActivity::scrollRows(const int delta) {
  {
    RenderLock lock;
    const Layout l = layout();
    top = std::clamp(top + delta, 0, std::max(0, rowCount() + 1 - l.visibleRows));
    clampCursorToView(l);
  }
  requestUpdate();
}

void AnkiVocabularyActivity::scrollColumns(const int delta) {
  {
    RenderLock lock;
    const Layout l = layout();
    firstCol = std::clamp(firstCol + delta, 0, static_cast<int>(columns.size()) - l.visibleColumns);
    clampCursorToView(l);
  }
  requestUpdate();
}

void AnkiVocabularyActivity::toggleCell(const int word, const int col) {
  if (isQueued(static_cast<size_t>(word), col)) return;
  {
    RenderLock lock;
    cursorRow = word + 1;
    cursorCol = col;
    masks[static_cast<size_t>(word)] ^= static_cast<uint8_t>(1u << col);
  }
  requestUpdate();
}

// Unticks the column when it is full, else ticks every word. Queued cells stay.
void AnkiVocabularyActivity::toggleColumn(const int col) {
  const auto bit = static_cast<uint8_t>(1u << col);
  {
    RenderLock lock;
    cursorRow = 0;
    cursorCol = col;
    const bool full = std::all_of(masks.begin(), masks.end(), [bit](const uint8_t m) { return (m & bit) != 0; });
    for (size_t row = 0; row < masks.size(); row++) {
      masks[row] =
          full ? static_cast<uint8_t>(masks[row] & ~(bit & ~queued[row])) : static_cast<uint8_t>(masks[row] | bit);
    }
  }
  requestUpdate();
}

void AnkiVocabularyActivity::activate() {
  if (cursorRow == 0) {
    toggleColumn(cursorCol);
  } else if (cursorRow <= rowCount()) {
    toggleCell(cursorRow - 1, cursorCol);
  } else {
    send();
  }
}

void AnkiVocabularyActivity::handleTap(const int x, const int y) {
  const Layout l = layout();
  if (y >= l.headerTop && y < l.rowsTop) {
    const int col = columnAt(l, x);
    if (col >= 0) toggleColumn(col);
    return;
  }
  const int entry = entryAt(l, y);
  if (entry < 0) return;
  if (entry == rowCount()) {
    const Rect button = sendButton(l, entry - top);
    if (x < button.x || x >= button.x + button.width || y < button.y || y >= button.y + button.height) return;
    {
      RenderLock lock;
      cursorRow = entry + 1;
    }
    send();
    return;
  }
  const int col = columnAt(l, x);
  if (col >= 0) toggleCell(entry, col);
}

// The Send entry as drawn, in the table slot `slot`.
Rect AnkiVocabularyActivity::sendButton(const Layout& l, const int slot) const {
  return Rect{l.x, l.rowsTop + slot * l.rowHeight + 4, l.width, l.rowHeight - 8};
}

// Queues one note per ticked cell that is not queued yet, written the way Add
// to Anki writes them, and marks each cell in `queuedNow` as it is appended.
// False when a write failed or the file is no longer the one that was scanned.
bool AnkiVocabularyActivity::queuePending(std::vector<uint8_t>& queuedNow, const size_t pending, int& added) {
  HalFile file;
  if (!Storage.openFileForRead("ANKI", VocabularyStore::FILE_PATH, file)) return false;
  FileSource source(file);
  AnkiStorageFs& fs = AnkiStorageFs::instance();
  std::vector<AnkiNoteQueue> queues;
  queues.reserve(columns.size());
  for (size_t c = 0; c < columns.size(); c++) {
    const auto bit = static_cast<uint8_t>(1u << c);
    // An account that never synced has no data directory yet.
    for (size_t row = 0; row < masks.size(); row++) {
      if ((masks[row] & ~queuedNow[row] & bit) != 0) {
        fs.mkdirs(ankipaths::accountDir(columns[c].id));
        break;
      }
    }
    queues.emplace_back(fs, ankipaths::notesFile(columns[c].id));
  }

  const uint32_t firstId = ankidevice::reserveClientIds(static_cast<uint32_t>(pending));
  uint32_t used = 0;
  bool ok = true;
  const bool whole = ankivocab::forEachRow(source, window, [&](const size_t row, const ankivocab::Record& record) {
    if (!ok) return;
    if (row >= masks.size() || row >= queuedNow.size()) {
      ok = false;
      return;
    }
    const auto todo = static_cast<uint8_t>(masks[row] & ~queuedNow[row]);
    if (todo == 0) return;
    const AnkiNoteQueue::Note base = ankivocab::toNote(record);
    for (size_t c = 0; c < columns.size() && ok; c++) {
      if (!(todo & (1u << c))) continue;
      if (used == pending) {
        ok = false;
        return;
      }
      AnkiNoteQueue::Note note = base;
      note.clientId = ankidevice::clientId(firstId + used++);
      note.deck = columns[c].deck;
      note.model = columns[c].model;
      ok = queues[c].append(note);
      if (ok) {
        queuedNow[row] = static_cast<uint8_t>(queuedNow[row] | (1u << c));
        added++;
      }
    }
  });
  return ok && whole;
}

// The marker moves only when every ticked cell is queued, so a failed send can
// be retried: cells queued by an earlier try are skipped, and a try that only
// failed to save the marker queues nothing and saves it again.
void AnkiVocabularyActivity::send() {
  std::vector<uint8_t> queuedNow = queued;  // this task is the only writer; render only reads
  size_t pending = 0;
  for (size_t row = 0; row < masks.size(); row++) {
    pending += static_cast<size_t>(__builtin_popcount(masks[row] & ~queuedNow[row]));
  }

  bool ok = true;
  int added = 0;
  if (pending > 0) {
    {
      RenderLock lock;
      snprintf(popupText, sizeof(popupText), "%s", tr(STR_LOADING_POPUP));
      popup = Popup::Busy;
    }
    requestUpdateAndWait();
    ok = queuePending(queuedNow, pending, added);
    if (!ok) LOG_ERR("ANKI", "Failed to queue looked-up words");
  }
  if (ok && window.last && !ANKI_STORE.setVocabDone(*window.last)) {
    LOG_ERR("ANKI", "Failed to save the sent-words marker");
    ok = false;
  }

  {
    RenderLock lock;
    queued = std::move(queuedNow);
    queuedTotal += added;
    if (ok) {
      snprintf(popupText, sizeof(popupText), tr(STR_ANKI_QUEUED_NOTES), queuedTotal);
    } else {
      snprintf(popupText, sizeof(popupText), "%s", tr(STR_ANKI_QUEUE_FAILED));
    }
    popup = ok ? Popup::Queued : Popup::Failed;
    popupTime = millis();
  }
  requestUpdate();
}

void AnkiVocabularyActivity::loop() {
  if (popup != Popup::None) {
    if (millis() - popupTime >= POPUP_DURATION_MS) {
      if (popup == Popup::Queued) {
        finish();
        return;
      }
      {
        RenderLock lock;
        popup = Popup::None;
      }
      requestUpdate();
    }
    return;
  }
  using Button = MappedInputManager::Button;
  if (mappedInput.wasReleased(Button::Back)) {
    finish();
    return;
  }
  if (empty != Empty::No) return;

  int tx = 0;
  int ty = 0;
  if (mappedInput.wasScreenTapped(tx, ty)) {
    handleTap(tx, ty);
    return;
  }
  switch (mappedInput.wasSwipe()) {
    case MappedInputManager::SwipeDir::Up:
      scrollRows(layout().visibleRows);
      return;
    case MappedInputManager::SwipeDir::Down:
      scrollRows(-layout().visibleRows);
      return;
    case MappedInputManager::SwipeDir::Left:
      scrollColumns(layout().visibleColumns);
      return;
    case MappedInputManager::SwipeDir::Right:
      scrollColumns(-layout().visibleColumns);
      return;
    case MappedInputManager::SwipeDir::None:
      break;
  }

  // Side buttons walk the rows, front Left/Right the accounts; the control
  // axis flips with the screen like NavNext/NavPrevious.
  const bool swapped = mappedInput.isNavDirectionSwapped();
  const Button rowNext = swapped ? Button::Up : Button::Down;
  const Button rowPrevious = swapped ? Button::Down : Button::Up;
  const int rows = cursorRows();
  ButtonNavigator::onPress({rowNext},
                           [this, rows] { moveCursor(ButtonNavigator::nextIndex(cursorRow, rows), cursorCol); });
  ButtonNavigator::onPress({rowPrevious},
                           [this, rows] { moveCursor(ButtonNavigator::previousIndex(cursorRow, rows), cursorCol); });
  buttonNavigator.onContinuous({rowNext}, [this, rows] {
    moveCursor(ButtonNavigator::nextPageIndex(cursorRow, rows, layout().visibleRows), cursorCol);
  });
  buttonNavigator.onContinuous({rowPrevious}, [this, rows] {
    moveCursor(ButtonNavigator::previousPageIndex(cursorRow, rows, layout().visibleRows), cursorCol);
  });

  const int columnCount = static_cast<int>(columns.size());
  if (cursorRow <= rowCount() && columnCount > 1) {
    const Button columnNext = swapped ? Button::Left : Button::Right;
    const Button columnPrevious = swapped ? Button::Right : Button::Left;
    if (mappedInput.wasPressed(columnNext)) {
      moveCursor(cursorRow, (cursorCol + 1) % columnCount);
    } else if (mappedInput.wasPressed(columnPrevious)) {
      moveCursor(cursorRow, (cursorCol + columnCount - 1) % columnCount);
    }
  }
  if (mappedInput.wasReleased(Button::Confirm)) activate();
}

void AnkiVocabularyActivity::drawCheckbox(const int x, const int y, const bool checked) const {
  renderer.drawRoundedRect(x, y, BOX, BOX, 2, 2, true);
  if (checked) {
    const int inset = BOX / 4;
    renderer.fillRoundedRect(x + inset, y + inset, BOX - 2 * inset, BOX - 2 * inset, 2, Color::Black);
  }
}

void AnkiVocabularyActivity::drawArrow(const int x, const int centerY, const bool pointRight) const {
  for (int i = 0; i <= ARROW; i++) {
    const int px = pointRight ? x + i : x + ARROW - i;
    renderer.drawLine(px, centerY - (ARROW - i), px, centerY + (ARROW - i));
  }
}

void AnkiVocabularyActivity::drawCentered(const Layout& l, const char* text) const {
  const int lineHeight = renderer.getLineHeight(UI_10_FONT_ID);
  const std::vector<std::string> lines = renderer.wrappedText(UI_10_FONT_ID, text, l.width, 6);
  const int block = lineHeight * static_cast<int>(lines.size());
  int y = l.headerTop + (l.rowsBottom - l.headerTop - block) / 2;
  for (const std::string& line : lines) {
    const int w = renderer.getTextWidth(UI_10_FONT_ID, line.c_str());
    renderer.drawText(UI_10_FONT_ID, l.x + (l.width - w) / 2, y, line.c_str());
    y += lineHeight;
  }
}

void AnkiVocabularyActivity::drawTable(const Layout& l) const {
  const int smallHeight = renderer.getLineHeight(SMALL_FONT_ID);
  const int lineHeight = renderer.getLineHeight(UI_10_FONT_ID);
  const int columnCount = static_cast<int>(columns.size());
  const int lastColumn = std::min(columnCount, firstCol + l.visibleColumns);

  // Header row: account names; the cursor's column inverted. Arrows in the
  // corner say that more accounts are off screen.
  for (int c = firstCol; c < lastColumn; c++) {
    const int cx = l.x + l.wordWidth + (c - firstCol) * l.columnWidth;
    const bool selected = cursorRow == 0 && cursorCol == c;
    if (selected)
      renderer.fillRoundedRect(cx + 2, l.headerTop + 2, l.columnWidth - 4, l.rowHeight - 4, 6, Color::Black);
    const std::string name = renderer.truncatedText(SMALL_FONT_ID, columns[static_cast<size_t>(c)].name.c_str(),
                                                    l.columnWidth - 2 * COLUMN_PADDING);
    const int w = renderer.getTextWidth(SMALL_FONT_ID, name.c_str());
    renderer.drawText(SMALL_FONT_ID, cx + (l.columnWidth - w) / 2, l.headerTop + (l.rowHeight - smallHeight) / 2,
                      name.c_str(), !selected);
  }
  const int arrowY = l.headerTop + l.rowHeight / 2;
  const int rightArrowX = l.x + l.wordWidth - COLUMN_PADDING - ARROW;
  if (firstCol > 0) drawArrow(rightArrowX - ARROW - 8, arrowY, false);
  if (lastColumn < columnCount) drawArrow(rightArrowX, arrowY, true);
  renderer.drawLine(l.x, l.rowsTop - 1, l.x + l.width, l.rowsTop - 1);

  const int entries = rowCount() + 1;
  for (int slot = 0; slot < l.visibleRows && top + slot < entries; slot++) {
    const int entry = top + slot;
    const int y = l.rowsTop + slot * l.rowHeight;
    const bool selectedRow = cursorRow == entry + 1;
    if (entry == rowCount()) {
      const Rect button = sendButton(l, slot);
      if (selectedRow) {
        renderer.fillRoundedRect(button.x, button.y, button.width, button.height, 6, Color::Black);
      } else {
        renderer.drawRoundedRect(button.x, button.y, button.width, button.height, 2, 6, true);
      }
      const char* label = tr(STR_ANKI_SEND);
      const int w = renderer.getTextWidth(UI_10_FONT_ID, label, EpdFontFamily::BOLD);
      renderer.drawText(UI_10_FONT_ID, l.x + (l.width - w) / 2, y + (l.rowHeight - lineHeight) / 2, label, !selectedRow,
                        EpdFontFamily::BOLD);
      continue;
    }

    if (selectedRow) renderer.fillRoundedRect(l.x, y + 2, l.wordWidth - 4, l.rowHeight - 4, 6, Color::LightGray);
    const std::string word = renderer.truncatedText(UI_10_FONT_ID, window.labels[static_cast<size_t>(entry)].c_str(),
                                                    l.wordWidth - 4 - 2 * COLUMN_PADDING);
    renderer.drawText(UI_10_FONT_ID, l.x + COLUMN_PADDING + 2, y + (l.rowHeight - lineHeight) / 2, word.c_str());
    for (int c = firstCol; c < lastColumn; c++) {
      const int cx = l.x + l.wordWidth + (c - firstCol) * l.columnWidth;
      if (selectedRow && cursorCol == c) {
        renderer.drawRoundedRect(cx + 2, y + 2, l.columnWidth - 4, l.rowHeight - 4, 2, 6, true);
      }
      drawCheckbox(cx + (l.columnWidth - BOX) / 2, y + (l.rowHeight - BOX) / 2,
                   (masks[static_cast<size_t>(entry)] & (1u << c)) != 0);
    }
    for (int x = l.x; x < l.x + l.width; x += 3) renderer.drawPixel(x, y + l.rowHeight - 1);
  }

  if (entries > l.visibleRows) {
    const int track = l.visibleRows * l.rowHeight;
    const int thumb = std::max(8, track * l.visibleRows / entries);
    renderer.fillRect(l.scrollBarX, l.rowsTop + (track - thumb) * top / (entries - l.visibleRows), SCROLL_BAR_WIDTH,
                      thumb);
  }
}

void AnkiVocabularyActivity::render(RenderLock&&) {
  renderer.clearScreen();
  const auto& metrics = UITheme::getInstance().getMetrics();
  const Rect safe = UITheme::getInstance().getScreenSafeArea(renderer, !mappedInput.hasTouch(), false);
  GUI.drawHeader(renderer, Rect{safe.x, safe.y + metrics.topPadding, safe.width, metrics.headerHeight},
                 tr(STR_ANKI_SEND_WORDS));

  const Layout l = layout();
  MappedInputManager::Labels labels = mappedInput.mapLabels(tr(STR_BACK), "", "", "");
  switch (empty) {
    case Empty::NoAccounts:
      drawCentered(l, tr(STR_ANKI_NO_ENABLED_ACCOUNTS));
      break;
    case Empty::SettingOff:
      drawCentered(l, tr(STR_ANKI_VOCAB_HINT));
      break;
    case Empty::NothingNew:
      drawCentered(l, tr(STR_ANKI_NO_NEW_WORDS));
      break;
    case Empty::No: {
      drawTable(l);
      const bool onSend = cursorRow > rowCount();
      const bool several = columns.size() > 1 && !onSend;
      labels = mappedInput.mapLabels(tr(STR_BACK), onSend ? tr(STR_ANKI_SEND) : tr(STR_TOGGLE),
                                     several ? tr(STR_DIR_LEFT) : "", several ? tr(STR_DIR_RIGHT) : "");
      break;
    }
  }
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  // drawPopup overlays the framebuffer and refreshes the display itself.
  if (popup != Popup::None) {
    GUI.drawPopup(renderer, popupText);
    return;
  }
  renderer.displayBuffer();
}
