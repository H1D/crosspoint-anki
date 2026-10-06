#include "AnkiReviewActivity.h"

#include <AnkiJournal.h>
#include <AnkiPaths.h>
#include <AnkiSyncEngine.h>
#include <AnkiTypeAnswer.h>
#include <FontCacheManager.h>
#include <GfxRenderer.h>
#include <HalGPIO.h>
#include <I18n.h>
#include <Logging.h>
#include <Memory.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <iterator>

#include "AnkiAccountPickerActivity.h"
#include "AnkiSyncActivity.h"
#include "CrossPointSettings.h"
#include "activities/util/KeyboardEntryActivity.h"
#include "anki/AnkiAccountStore.h"
#include "anki/AnkiDevice.h"
#include "anki/AnkiSecureHttp.h"
#include "anki/AnkiStorageFs.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace {

// Longest measurable/drawable span; longer tokens are split at this cap.
constexpr size_t MAX_SPAN_BYTES = 191;
constexpr int SIDE_PADDING = 20;
constexpr int GRADE_COUNT = 4;
constexpr int TYPE_BOX_PADDING = 8;  // text inset inside the type-in box
constexpr size_t MAX_TYPED_BYTES = 200;

const char* gradeLabel(const int index) {
  switch (index) {
    case 0:
      return tr(STR_ANKI_AGAIN);
    case 1:
      return tr(STR_ANKI_HARD);
    case 2:
      return tr(STR_ANKI_GOOD);
    default:
      return tr(STR_ANKI_EASY);
  }
}

}  // namespace

EpdFontFamily::Style AnkiReviewActivity::styleOf(const ankimarkup::Run& run) {
  if (run.bold && run.italic) return EpdFontFamily::BOLD_ITALIC;
  if (run.bold) return EpdFontFamily::BOLD;
  if (run.italic) return EpdFontFamily::ITALIC;
  return EpdFontFamily::REGULAR;
}

// One enabled account reviews directly; several ask which one every time the
// app opens (the answer becomes the review account, which exit and sleep
// syncs refresh).
void AnkiReviewActivity::onEnter() {
  Activity::onEnter();
  ANKI_STORE.loadFromFile();
  int enabledCount = 0;
  int onlyEnabled = -1;
  const auto& accounts = ANKI_STORE.getAccounts();
  for (size_t i = 0; i < accounts.size(); i++) {
    if (!accounts[i].enabled) continue;
    enabledCount++;
    onlyEnabled = static_cast<int>(i);
  }
  if (enabledCount > 1) {
    auto picker = makeUniqueNoThrow<AnkiAccountPickerActivity>(renderer, mappedInput);
    if (picker) {
      startActivityForResult(std::move(picker), [this](const ActivityResult& result) {
        if (result.isCancelled) {
          activityManager.goHome(HomeMenuItem::ANKI);
          return;
        }
        reload();
      });
      return;
    }
    LOG_ERR("ANKI", "OOM: account picker");
  } else if (enabledCount == 1 && ANKI_STORE.getReviewAccountIndex() != onlyEnabled) {
    ANKI_STORE.setReviewAccount(static_cast<size_t>(onlyEnabled));
  }
  reload();
  requestUpdate();
}

void AnkiReviewActivity::onExit() {
  Activity::onExit();
  if (auto* fcm = renderer.getFontCacheManager()) {
    fcm->releaseSdFontCaches();
  }
}

void AnkiReviewActivity::reload() {
  RenderLock lock;
  mode = Mode::NoAccount;
  cache.reset();
  cacheCount = 0;
  queue.clear();
  runs.clear();
  segments.clear();
  cursor = 0;

  const AnkiAccount* account = ANKI_STORE.getReviewAccount();
  if (!account || !account->enabled) return;
  accountId = account->id;

  cache = makeUniqueNoThrow<AnkiCardCache>(AnkiStorageFs::instance(), ankipaths::cardsFile(accountId),
                                           ankipaths::cacheMetaFile(accountId));
  if (!cache) {
    LOG_ERR("ANKI", "OOM: card cache");
    return;
  }
  const std::vector<int64_t> ids = cache->cardIds();
  cacheCount = ids.size();
  AnkiJournal journal(AnkiStorageFs::instance(), ankipaths::reviewsFile(accountId));
  const std::vector<int64_t> graded = journal.gradedCardIds();
  queue.reserve(cacheCount);
  for (size_t i = 0; i < cacheCount && i < UINT16_MAX; i++) {
    if (ids[i] == 0) continue;
    if (std::find(graded.begin(), graded.end(), ids[i]) != graded.end()) continue;
    queue.push_back(static_cast<uint16_t>(i));
  }
  LOG_DBG("ANKI", "Review queue: %u of %u cards", static_cast<unsigned>(queue.size()),
          static_cast<unsigned>(cacheCount));
  loadCurrent();
}

// Loads queue[cursor] into `card` and lays out its front. Bad cache lines are
// skipped. Caller holds the RenderLock.
void AnkiReviewActivity::loadCurrent() {
  runs.clear();
  segments.clear();
  ruleLine = -1;
  currentPage = 0;
  totalPages = 1;
  while (cursor < queue.size()) {
    if (cache->load(queue[cursor], card)) break;
    LOG_ERR("ANKI", "Bad cache line %u, skipping", static_cast<unsigned>(queue[cursor]));
    cursor++;
  }
  if (cursor >= queue.size()) {
    mode = Mode::Empty;
    card = AnkiCard();
    return;
  }
  runs = ankimarkup::parse(card.q);
  answerRunStart = runs.size();
  typed.clear();
  typeRun = -1;
  // The first marker becomes the box to type in; any further ones are dropped.
  for (size_t i = 0; i < runs.size(); i++) {
    if (!runs[i].typeIn) continue;
    if (typeRun < 0) {
      typeRun = static_cast<int>(i);
      runs[i].text = tr(STR_ANKI_TYPE_ANSWER);
      runs[i].italic = true;
    } else {
      runs[i].text.clear();
    }
  }
  mode = Mode::Front;
  layout();
}

// The answer's "[[type:...]]" marker becomes Anki's comparison of the typed
// answer with the expected one, on lines of its own; with no marker in the
// answer the comparison leads it. Other cards just lose stray markers.
std::vector<ankimarkup::Run> AnkiReviewActivity::withTypeComparison(std::vector<ankimarkup::Run> answer) const {
  std::vector<ankimarkup::Run> comparison;
  if (typeRun >= 0) {
    if (!card.typeAnswer.empty()) {
      comparison = ankitype::compare(typed, card.typeAnswer, card.typeIgnoreAccents);
    } else if (!typed.empty()) {
      // Expected text unknown (e.g. an older AnkiDo): show what was typed.
      ankimarkup::Run run;
      run.text = typed;
      comparison.push_back(std::move(run));
    }
  }
  const auto marker = std::find_if(answer.begin(), answer.end(), [](const ankimarkup::Run& r) { return r.typeIn; });
  const size_t at = marker == answer.end() ? 0 : static_cast<size_t>(marker - answer.begin());
  const size_t resume = marker == answer.end() ? 0 : at + 1;

  std::vector<ankimarkup::Run> out;
  out.reserve(answer.size() + comparison.size() + 2);
  const auto appendNewline = [&out] {
    ankimarkup::Run nl;
    nl.newline = true;
    out.push_back(nl);
  };
  for (size_t i = 0; i < at; i++) {
    if (!answer[i].typeIn) out.push_back(std::move(answer[i]));
  }
  if (!comparison.empty()) {
    if (!out.empty() && !out.back().newline) appendNewline();
    std::move(comparison.begin(), comparison.end(), std::back_inserter(out));
    if (resume < answer.size() && !answer[resume].newline) appendNewline();
  }
  for (size_t i = resume; i < answer.size(); i++) {
    if (!answer[i].typeIn) out.push_back(std::move(answer[i]));
  }
  return out;
}

void AnkiReviewActivity::flip() {
  RenderLock lock;
  // The question's type box gives way to the comparison in the answer.
  for (size_t i = 0; i < answerRunStart; i++) {
    if (runs[i].typeIn) runs[i].text.clear();
  }
  std::vector<ankimarkup::Run> answer = withTypeComparison(ankimarkup::parse(card.a));
  runs.reserve(runs.size() + answer.size());
  runs.insert(runs.end(), std::make_move_iterator(answer.begin()), std::make_move_iterator(answer.end()));
  mode = Mode::Back;
  layout();
  // Open on the page that shows the answer.
  currentPage = ruleLine >= 0 ? std::min(ruleLine / linesPerPage, totalPages - 1) : 0;
}

void AnkiReviewActivity::openTypeKeyboard() {
  auto keyboard = makeUniqueNoThrow<KeyboardEntryActivity>(renderer, mappedInput, tr(STR_ANKI_TYPE_ANSWER), typed,
                                                           MAX_TYPED_BYTES, InputType::Text);
  if (!keyboard) {
    LOG_ERR("ANKI", "OOM: keyboard");
    return;
  }
  keyboard->preferScriptOf(card.typeAnswer);
  // OK shows the answer, as Enter does in Anki; Back returns to the question.
  startActivityForResult(std::move(keyboard), [this](const ActivityResult& result) {
    if (!result.isCancelled && std::holds_alternative<KeyboardResult>(result.data)) {
      {
        RenderLock lock;
        typed = std::get<KeyboardResult>(result.data).text;
      }
      flip();
    }
    requestUpdate();
  });
}

bool AnkiReviewActivity::typeBoxContains(const int y) const {
  if (typeRun < 0 || typeLineFirst < 0) return false;
  const int lineHeight = renderer.getLineHeight(SETTINGS.getReaderFontId());
  const BodyArea body = bodyArea();
  if (y < body.y || y >= body.y + linesPerPage * lineHeight) return false;
  const int line = currentPage * linesPerPage + (y - body.y) / lineHeight;
  return line >= typeLineFirst && line <= typeLineLast;
}

void AnkiReviewActivity::grade(const uint8_t ease) {
  AnkiJournal journal(AnkiStorageFs::instance(), ankipaths::reviewsFile(accountId));
  if (!journal.append({ankidevice::nextClientId(), card.cardId, ease, ankidevice::nowEpoch()})) {
    LOG_ERR("ANKI", "Cannot journal review for card %lld", static_cast<long long>(card.cardId));
  }
  RenderLock lock;
  cursor++;
  loadCurrent();
}

// Back leaves the app. Pending reviews or notes go out first through the
// sync screen, which restarts to Home afterwards.
void AnkiReviewActivity::onBack() {
  bool pending = false;
  {
    AnkiSyncEngine engine(AnkiStorageFs::instance(), AnkiSecureHttp::instance(), ankidevice::nowEpoch);
    const auto& accounts = ANKI_STORE.getAccounts();
    pending = std::any_of(accounts.begin(), accounts.end(), [&engine](const AnkiAccount& account) {
      return account.enabled && engine.hasPending(account);
    });
  }
  if (pending) {
    auto sync = makeUniqueNoThrow<AnkiSyncActivity>(renderer, mappedInput, AnkiSyncActivity::ReturnTo::Home);
    if (sync) {
      activityManager.replaceActivity(std::move(sync));
      return;
    }
    LOG_ERR("ANKI", "OOM: sync activity");
  }
  activityManager.goHome(HomeMenuItem::ANKI);
}

void AnkiReviewActivity::startSync() {
  auto sync = makeUniqueNoThrow<AnkiSyncActivity>(renderer, mappedInput, AnkiSyncActivity::ReturnTo::ReviewApp);
  if (!sync) {
    LOG_ERR("ANKI", "OOM: sync activity");
    return;
  }
  // On the device the sync screen restarts back into this app on exit; the
  // callback only runs where that restart is a no-op (simulator).
  startActivityForResult(std::move(sync), [this](const ActivityResult&) { reload(); });
}

// One body rectangle for both card sides so nothing moves on flip: the grade
// strip is reserved on the front too, and the side-button hint boxes (drawn
// on the back) get a gutter where the board draws them: both edges on
// edge-button boards, otherwise the right edge (left in LandscapeClockwise,
// mirroring DictionaryDefinitionActivity's hint gutter).
AnkiReviewActivity::BodyArea AnkiReviewActivity::bodyArea() const {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto orientation = renderer.getOrientation();
  const bool isLandscapeCw = orientation == GfxRenderer::Orientation::LandscapeClockwise;
  const bool isInverted = orientation == GfxRenderer::Orientation::PortraitInverted;
  int leftInset = SIDE_PADDING;
  int rightInset = SIDE_PADDING;
  if (!mappedInput.hasTouch()) {
    const int hintInset = metrics.sideButtonHintsWidth + 8;
    if (gpio.hasEdgeSideButtons()) {
      leftInset += hintInset;
      rightInset += hintInset;
    } else if (isLandscapeCw) {
      leftInset += hintInset;
    } else {
      rightInset += hintInset;
    }
  }
  const int contentY = isInverted ? metrics.buttonHintsHeight : 0;
  const int topArea = metrics.topPadding + metrics.headerHeight;
  // Inverted: the button hints sit at the top (contentY), not the bottom.
  const int bottomArea =
      (isInverted ? 0 : metrics.buttonHintsHeight) + 2 * metrics.verticalSpacing + gradeStripHeight();
  return {leftInset, contentY + topArea, renderer.getScreenWidth() - leftInset - rightInset,
          renderer.getScreenHeight() - contentY - topArea - bottomArea};
}

int AnkiReviewActivity::gradeStripHeight() const {
  return renderer.getLineHeight(UI_10_FONT_ID) + renderer.getLineHeight(SMALL_FONT_ID) + 12;
}

int AnkiReviewActivity::gradeStripTop() const {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const bool isInverted = renderer.getOrientation() == GfxRenderer::Orientation::PortraitInverted;
  return renderer.getScreenHeight() - (isInverted ? 0 : metrics.buttonHintsHeight) - metrics.verticalSpacing -
         gradeStripHeight();
}

int AnkiReviewActivity::measure(const int fontId, const char* text, size_t len,
                                const EpdFontFamily::Style style) const {
  char buf[MAX_SPAN_BYTES + 1];
  len = std::min(len, MAX_SPAN_BYTES);
  memcpy(buf, text, len);
  buf[len] = '\0';
  return renderer.getTextAdvanceX(fontId, buf, style);
}

// Greedy word-wrap of `runs` into segments (one per run per line). Newline
// runs break lines; the answer runs start on a fresh line after a blank
// "rule" line. Whitespace at a line start is dropped.
void AnkiReviewActivity::layout() {
  segments.clear();
  segments.reserve(runs.size() + 16);
  ruleLine = -1;
  typeLineFirst = -1;
  typeLineLast = -1;

  const int fontId = SETTINGS.getReaderFontId();
  const BodyArea body = bodyArea();
  const int maxWidth = body.width;
  const int lineHeight = renderer.getLineHeight(fontId);
  linesPerPage = std::max(1, body.height / lineHeight);

  int x = 0;
  int line = 0;
  int lineStart = 0;  // TYPE_BOX_PADDING inside the type-in box
  const auto newLine = [&] {
    line++;
    x = lineStart;
  };

  for (size_t ri = 0; ri < runs.size(); ri++) {
    if (ri == answerRunStart && answerRunStart < runs.size()) {
      if (x > 0) newLine();
      ruleLine = line;
      newLine();
    }
    const ankimarkup::Run& run = runs[ri];
    if (run.newline) {
      newLine();
      continue;
    }
    // The type-in box takes whole lines, its text inset by the padding.
    if (run.typeIn) {
      if (run.text.empty()) continue;
      if (x > 0) newLine();
      typeLineFirst = line;
      lineStart = TYPE_BOX_PADDING;
      x = lineStart;
    }
    const int lineEnd = run.typeIn ? maxWidth - TYPE_BOX_PADDING : maxWidth;
    const EpdFontFamily::Style style = styleOf(run);
    renderer.ensureSdCardFontReady(fontId, run.text.c_str(), static_cast<uint8_t>(1u << style));
    const int spaceWidth = renderer.getSpaceWidth(fontId, style);
    const char* text = run.text.c_str();
    const size_t n = std::min(run.text.size(), static_cast<size_t>(UINT16_MAX - 1));

    size_t pos = 0;
    size_t segStart = 0;
    int segX = x;
    int segEndX = x;
    const auto flush = [&](size_t end) {
      while (end > segStart && text[end - 1] == ' ') end--;
      if (end > segStart) {
        segments.push_back({static_cast<uint16_t>(ri), static_cast<uint16_t>(segStart),
                            static_cast<uint16_t>(end - segStart), static_cast<uint16_t>(segEndX - segX),
                            static_cast<int16_t>(segX), static_cast<uint16_t>(line)});
      }
    };

    while (pos < n) {
      const char c = text[pos];
      if (c == ' ' || c == '\t' || c == '\r') {
        if (x > lineStart) {
          x += spaceWidth;
        } else {
          segStart = pos + 1;
        }
        pos++;
        continue;
      }
      const size_t tokenStart = pos;
      while (pos < n && text[pos] != ' ' && text[pos] != '\t' && text[pos] != '\r' &&
             pos - tokenStart < MAX_SPAN_BYTES) {
        pos++;
      }
      // Never cut a UTF-8 sequence at the byte cap.
      while (pos - tokenStart > 1 && pos < n && (text[pos] & 0xC0) == 0x80) pos--;
      const int width = measure(fontId, text + tokenStart, pos - tokenStart, style);
      if (x > lineStart && x + width > lineEnd) {
        flush(tokenStart);
        newLine();
        segStart = tokenStart;
        segX = lineStart;
      }
      x += width;
      segEndX = x;
    }
    flush(n);
    if (run.typeIn) {
      typeLineLast = line;
      lineStart = 0;
      newLine();
    }
  }

  lineCount = std::max(1, line + (x > 0 ? 1 : 0));
  totalPages = std::max(1, (lineCount + linesPerPage - 1) / linesPerPage);
  currentPage = 0;
}

void AnkiReviewActivity::loop() {
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    onBack();
    return;
  }

  int tx = 0;
  int ty = 0;
  const bool tapped = mappedInput.wasScreenTapped(tx, ty);

  switch (mode) {
    case Mode::NoAccount:
      return;

    case Mode::Empty:
      if (mappedInput.wasReleased(MappedInputManager::Button::Confirm) || tapped) startSync();
      return;

    case Mode::Front: {
      // Type-in cards: Confirm or a tap on the box opens the keyboard.
      if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
        if (typeRun >= 0) {
          openTypeKeyboard();
          return;
        }
        flip();
        requestUpdate();
        return;
      }
      // Reader-style tap zones: left third = previous page, the rest = next
      // page, and the last page flips.
      if (tapped) {
        if (typeBoxContains(ty)) {
          openTypeKeyboard();
          return;
        }
        if (tx < renderer.getScreenWidth() / 3 && currentPage > 0) {
          currentPage--;
        } else if (currentPage + 1 < totalPages) {
          currentPage++;
        } else {
          flip();
        }
        requestUpdate();
        return;
      }
      buttonNavigator.onRelease({MappedInputManager::Button::NavNext}, [this] {
        if (currentPage + 1 < totalPages) {
          currentPage++;
          requestUpdate();
        }
      });
      buttonNavigator.onPreviousRelease([this] {
        if (currentPage > 0) {
          currentPage--;
          requestUpdate();
        }
      });
      return;
    }

    case Mode::Back: {
      uint8_t ease = 0;
      if (tapped) {
        const int top = gradeStripTop();
        const int width = renderer.getScreenWidth();
        if (ty >= top && ty < top + gradeStripHeight()) {
          ease = static_cast<uint8_t>(1 + std::min(GRADE_COUNT - 1, tx * GRADE_COUNT / std::max(1, width)));
        } else if (totalPages > 1) {
          currentPage = (currentPage + 1) % totalPages;
          requestUpdate();
          return;
        }
      } else if (mappedInput.wasReleased(MappedInputManager::Button::ScreenLeft)) {
        ease = static_cast<uint8_t>(AnkiEase::Again);
      } else if (mappedInput.wasReleased(MappedInputManager::Button::ScreenRight)) {
        ease = static_cast<uint8_t>(AnkiEase::Good);
      } else if (mappedInput.wasReleased(MappedInputManager::Button::ScreenDown)) {
        ease = static_cast<uint8_t>(AnkiEase::Hard);
      } else if (mappedInput.wasReleased(MappedInputManager::Button::ScreenUp)) {
        ease = static_cast<uint8_t>(AnkiEase::Easy);
      } else if (mappedInput.wasReleased(MappedInputManager::Button::Confirm) && totalPages > 1) {
        currentPage = (currentPage + 1) % totalPages;
        requestUpdate();
        return;
      }
      if (ease != 0) {
        grade(ease);
        requestUpdate();
      }
      return;
    }
  }
}

const char* AnkiReviewActivity::kindLabel() const {
  if (card.kind == "new") return tr(STR_ANKI_KIND_NEW);
  if (card.kind == "learning") return tr(STR_ANKI_KIND_LEARNING);
  return tr(STR_ANKI_KIND_DUE);
}

// Deck name left, "<kind>  N left" right; the deck name is trimmed to the
// space the right part leaves.
void AnkiReviewActivity::drawHeader(const int contentX, const int contentWidth, const int headerY) const {
  char remaining[24];
  snprintf(remaining, sizeof(remaining), tr(STR_ANKI_REMAINING), static_cast<int>(queue.size() - cursor));
  char right[64];
  snprintf(right, sizeof(right), "%s  %s", kindLabel(), remaining);
  const int rightWidth = renderer.getTextWidth(UI_10_FONT_ID, right);
  const int rightX = contentX + contentWidth - SIDE_PADDING - rightWidth;
  renderer.drawText(UI_10_FONT_ID, rightX, headerY, right);

  const int leftX = contentX + SIDE_PADDING;
  const int available = rightX - leftX - 12;
  std::string deck = card.deck;
  if (renderer.getTextWidth(UI_10_FONT_ID, deck.c_str(), EpdFontFamily::BOLD) > available) {
    while (!deck.empty() &&
           renderer.getTextWidth(UI_10_FONT_ID, (deck + "…").c_str(), EpdFontFamily::BOLD) > available) {
      deck.pop_back();
      while (!deck.empty() && (static_cast<uint8_t>(deck.back()) & 0xC0) == 0x80) deck.pop_back();
    }
    deck += "…";
  }
  renderer.drawText(UI_10_FONT_ID, leftX, headerY, deck.c_str(), true, EpdFontFamily::BOLD);
}

// Draws the current page's segments (copied into a stack buffer for NUL
// termination). Called twice per render: font-cache scan pass, then paint.
void AnkiReviewActivity::drawBody(const int fontId, const BodyArea& body) const {
  const int lineHeight = renderer.getLineHeight(fontId);
  const int firstLine = currentPage * linesPerPage;
  const int lastLine = firstLine + linesPerPage;
  char buf[MAX_SPAN_BYTES + 1];
  for (const Segment& seg : segments) {
    if (seg.line < firstLine || seg.line >= lastLine) continue;
    const ankimarkup::Run& run = runs[seg.run];
    const size_t len = std::min(static_cast<size_t>(seg.len), MAX_SPAN_BYTES);
    memcpy(buf, run.text.c_str() + seg.start, len);
    buf[len] = '\0';
    const int x = body.x + seg.x;
    const int y = body.y + (seg.line - firstLine) * lineHeight;
    if (run.mark) {
      // Inverted, like Anki's red/grey comparison highlights.
      renderer.fillRect(x - 1, y, seg.width + 2, lineHeight);
      renderer.drawText(fontId, x, y, buf, false, styleOf(run));
      continue;
    }
    renderer.drawText(fontId, x, y, buf, true, styleOf(run));
    if (run.cloze) renderer.drawRect(x - 3, y, seg.width + 6, lineHeight);
    if (run.typeIn) renderer.drawRect(body.x, y, body.width, lineHeight);
  }
  if (ruleLine >= firstLine && ruleLine < lastLine) {
    const int y = body.y + (ruleLine - firstLine) * lineHeight + lineHeight / 2;
    renderer.drawLine(body.x, y, body.x + body.width - 1, y);
  }
}

// Four equal columns above the button hints: grade label over AnkiDo's next
// interval. Tap targets on touch boards, a legend elsewhere.
void AnkiReviewActivity::drawGradeStrips() const {
  const int top = gradeStripTop();
  const int width = renderer.getScreenWidth();
  const int colWidth = width / GRADE_COUNT;
  const int labelHeight = renderer.getLineHeight(UI_10_FONT_ID);
  renderer.drawLine(0, top, width - 1, top);
  for (int i = 0; i < GRADE_COUNT; i++) {
    const int colX = i * colWidth;
    if (i > 0) renderer.drawLine(colX, top, colX, top + gradeStripHeight() - 1);
    const char* label = gradeLabel(i);
    const int labelWidth = renderer.getTextWidth(UI_10_FONT_ID, label, EpdFontFamily::BOLD);
    renderer.drawText(UI_10_FONT_ID, colX + (colWidth - labelWidth) / 2, top + 4, label, true, EpdFontFamily::BOLD);
    const std::string next = ankimarkup::stripBidiControls(card.next[i]);
    const int nextWidth = renderer.getTextWidth(SMALL_FONT_ID, next.c_str());
    renderer.drawText(SMALL_FONT_ID, colX + (colWidth - nextWidth) / 2, top + 6 + labelHeight, next.c_str());
  }
}

void AnkiReviewActivity::drawHints() const {
  MappedInputManager::Labels labels{};
  switch (mode) {
    case Mode::NoAccount:
      labels = mappedInput.mapLabels(tr(STR_BACK), "", "", "");
      break;
    case Mode::Empty:
      labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_ANKI_SYNC), "", "");
      break;
    case Mode::Front:
      labels = mappedInput.mapLabels(tr(STR_BACK), typeRun >= 0 ? tr(STR_ANKI_TYPE) : tr(STR_ANKI_SHOW_ANSWER),
                                     currentPage > 0 ? "<" : "", currentPage + 1 < totalPages ? ">" : "");
      break;
    case Mode::Back: {
      labels = mappedInput.mapDirectionalLabels(tr(STR_BACK), totalPages > 1 ? ">" : "", tr(STR_ANKI_AGAIN),
                                                tr(STR_ANKI_GOOD), tr(STR_ANKI_EASY), tr(STR_ANKI_HARD));
      // Side buttons are physical Up/Down; which screen direction (and so
      // which grade) they carry follows the orientation transform.
      static constexpr StrId SIDE_LABELS[][2] = {
          {StrId::STR_ANKI_EASY, StrId::STR_ANKI_HARD},   // Portrait
          {StrId::STR_ANKI_GOOD, StrId::STR_ANKI_AGAIN},  // LandscapeClockwise
          {StrId::STR_ANKI_HARD, StrId::STR_ANKI_EASY},   // PortraitInverted
          {StrId::STR_ANKI_AGAIN, StrId::STR_ANKI_GOOD},  // LandscapeCounterClockwise
      };
      const auto& side = SIDE_LABELS[static_cast<int>(renderer.getOrientation()) & 3];
      GUI.drawSideButtonHints(renderer, I18n::getInstance().get(side[0]), I18n::getInstance().get(side[1]));
      break;
    }
  }
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
}

void AnkiReviewActivity::render(RenderLock&&) {
  renderer.clearScreen();

  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto orientation = renderer.getOrientation();
  const bool isLandscapeCw = orientation == GfxRenderer::Orientation::LandscapeClockwise;
  const bool isLandscapeCcw = orientation == GfxRenderer::Orientation::LandscapeCounterClockwise;
  const bool isInverted = orientation == GfxRenderer::Orientation::PortraitInverted;
  const int hintGutterWidth = (isLandscapeCw || isLandscapeCcw) ? metrics.sideButtonHintsWidth : 0;
  const int contentX = isLandscapeCw ? hintGutterWidth : 0;
  const int contentWidth = renderer.getScreenWidth() - hintGutterWidth;
  const int contentY = isInverted ? metrics.buttonHintsHeight : 0;
  const int pageHeight = renderer.getScreenHeight();

  if (mode == Mode::NoAccount || mode == Mode::Empty) {
    GUI.drawHeader(renderer, Rect{contentX, contentY + metrics.topPadding, contentWidth, metrics.headerHeight},
                   tr(STR_ANKI));
    const char* message = mode == Mode::NoAccount ? tr(STR_ANKI_NO_REVIEW_ACCOUNT)
                          : cacheCount > 0        ? tr(STR_ANKI_ALL_DONE)
                                                  : tr(STR_ANKI_NO_CARDS);
    const int lineHeight = renderer.getLineHeight(UI_10_FONT_ID);
    const std::vector<std::string> lines =
        renderer.wrappedText(UI_10_FONT_ID, message, contentWidth - 2 * metrics.contentSidePadding, 4);
    int y = (pageHeight - lineHeight * static_cast<int>(lines.size())) / 2;
    for (const std::string& line : lines) {
      renderer.drawCenteredText(UI_10_FONT_ID, y, line.c_str());
      y += lineHeight;
    }
  } else {
    drawHeader(contentX, contentWidth, contentY + metrics.topPadding + 10);
    // Two-pass draw inside a prewarm scope so SD-card font glyphs load in one
    // batch instead of one on-demand read per character.
    const int fontId = SETTINGS.getReaderFontId();
    const BodyArea body = bodyArea();
    auto* fcm = renderer.getFontCacheManager();
    auto scope = fcm->createPrewarmScope();
    drawBody(fontId, body);
    scope.endScanAndPrewarm();
    drawBody(fontId, body);
    if (mode == Mode::Back) drawGradeStrips();
  }

  drawHints();
  renderer.displayBuffer();
}
