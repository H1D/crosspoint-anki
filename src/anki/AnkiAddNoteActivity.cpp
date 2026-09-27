#include "AnkiAddNoteActivity.h"

#include <AnkiNoteQueue.h>
#include <AnkiPaths.h>
#include <AnkiSyncEngine.h>
#include <FontCacheManager.h>
#include <GfxRenderer.h>
#include <I18n.h>
#include <Logging.h>

#include <algorithm>
#include <cstdio>

#include "AnkiAccountStore.h"
#include "AnkiDevice.h"
#include "AnkiSecureHttp.h"
#include "AnkiStorageFs.h"
#include "CrossPointSettings.h"
#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace {

constexpr unsigned long POPUP_DURATION_MS = 1200;
constexpr int BODY_GAP = 6;  // vertical gap between the word, sentence and translation blocks
constexpr const char* DEFAULT_DECK = "Default";

// Account preselected on the next open, across modals; the deck preselect is
// the account's "Deck for new words" setting (AnkiAccount::lastDeck).
uint32_t lastAccountId = 0;

}  // namespace

void AnkiAddNoteActivity::onEnter() {
  Activity::onEnter();
  ANKI_STORE.loadFromFile();
  loadAccounts();
  loadDecks();

  // Merge the note text into the SD font's advance table up front so the
  // wrapping in render() measures from RAM instead of loading glyphs one
  // overflow slot at a time (same as DictionaryWordSelectActivity).
  const int fontId = SETTINGS.getReaderFontId();
  std::string text;
  text.reserve(noteWord.size() + sentence.size() + translation.size() + 2);
  text.append(noteWord).push_back(' ');
  text.append(sentence).push_back(' ');
  text.append(translation);
  constexpr uint8_t styleMask =
      (1u << EpdFontFamily::REGULAR) | (1u << EpdFontFamily::BOLD) | (1u << EpdFontFamily::ITALIC);
  renderer.ensureSdCardFontReady(fontId, text.c_str(), styleMask);
  requestUpdate();
}

void AnkiAddNoteActivity::loadAccounts() {
  const auto& accounts = ANKI_STORE.getAccounts();
  accountIndices.clear();
  accountIndices.reserve(accounts.size());
  for (size_t i = 0; i < accounts.size(); i++) {
    if (accounts[i].enabled) accountIndices.push_back(i);
  }
  accountPos = accountIndices.empty() ? -1 : 0;
  if (accountIndices.empty()) return;

  // Last used, else the review account, else the first enabled one.
  const int reviewIdx = ANKI_STORE.getReviewAccountIndex();
  int reviewPos = -1;
  for (size_t p = 0; p < accountIndices.size(); p++) {
    const AnkiAccount& a = accounts[accountIndices[p]];
    if (lastAccountId != 0 && a.id == lastAccountId) {
      accountPos = static_cast<int>(p);
      return;
    }
    if (static_cast<int>(accountIndices[p]) == reviewIdx) reviewPos = static_cast<int>(p);
  }
  if (reviewPos >= 0) accountPos = reviewPos;
}

const AnkiAccount* AnkiAddNoteActivity::currentAccount() const {
  if (accountPos < 0 || accountPos >= static_cast<int>(accountIndices.size())) return nullptr;
  const auto& accounts = ANKI_STORE.getAccounts();
  const size_t idx = accountIndices[static_cast<size_t>(accountPos)];
  return idx < accounts.size() ? &accounts[idx] : nullptr;
}

void AnkiAddNoteActivity::loadDecks() {
  deckNames.clear();
  deckPos = 0;
  decksFromSync = false;
  const AnkiAccount* account = currentAccount();
  if (!account) return;

  {
    AnkiSyncEngine engine(AnkiStorageFs::instance(), AnkiSecureHttp::instance(), nullptr);
    const std::vector<AnkiDeck> decks = engine.loadDecks(account->id);
    deckNames.reserve(decks.size() + 1);
    for (const AnkiDeck& d : decks) deckNames.push_back(d.name);
    decksFromSync = !deckNames.empty();
  }
  if (deckNames.empty()) deckNames = account->decks;
  // The remembered deck stays selectable even when the current list lacks it.
  if (!account->lastDeck.empty() &&
      std::find(deckNames.begin(), deckNames.end(), account->lastDeck) == deckNames.end()) {
    deckNames.insert(deckNames.begin(), account->lastDeck);
  }
  if (deckNames.empty()) deckNames.emplace_back(DEFAULT_DECK);

  if (!account->lastDeck.empty()) {
    const auto it = std::find(deckNames.begin(), deckNames.end(), account->lastDeck);
    if (it != deckNames.end()) deckPos = static_cast<int>(it - deckNames.begin());
  }
}

void AnkiAddNoteActivity::cycleAccount(const int direction) {
  const int count = static_cast<int>(accountIndices.size());
  if (count < 2) return;
  accountPos = (accountPos + direction + count) % count;
  loadDecks();
  requestUpdate();
}

void AnkiAddNoteActivity::cycleDeck(const int direction) {
  const int count = static_cast<int>(deckNames.size());
  if (count < 2) return;
  deckPos = (deckPos + direction + count) % count;
  requestUpdate();
}

void AnkiAddNoteActivity::addNote() {
  const AnkiAccount* account = currentAccount();
  if (!account || deckNames.empty()) return;
  const std::string& deck = deckNames[static_cast<size_t>(deckPos)];
  const uint32_t accountId = account->id;

  AnkiStorageFs& fs = AnkiStorageFs::instance();
  // An account that never synced has no data directory yet.
  fs.mkdirs(ankipaths::accountDir(accountId));
  AnkiNoteQueue queue(fs, ankipaths::notesFile(accountId));

  AnkiNoteQueue::Note note;
  note.clientId = ankidevice::nextClientId();
  note.deck = deck;
  note.model = account->model;
  note.front = ankinote::frontHtml(noteWord, sentence);
  note.back = translation;
  note.tags.reserve(2);
  note.tags.emplace_back("crosspoint");
  if (!bookTitle.empty()) note.tags.push_back(ankinote::bookTag(bookTitle));

  if (queue.append(note)) {
    // A deck picked here is a one-off; account.lastDeck is the user's setting
    // ("Deck for new words") and is never written from the modal.
    lastAccountId = accountId;
    snprintf(popupText, sizeof(popupText), tr(STR_ANKI_QUEUED), static_cast<int>(queue.count()));
    popup = Popup::Queued;
  } else {
    LOG_ERR("ANKI", "Failed to queue note for account %u", static_cast<unsigned>(accountId));
    snprintf(popupText, sizeof(popupText), "%s", tr(STR_ANKI_QUEUE_FAILED));
    popup = Popup::Failed;
  }
  popupTime = millis();
  requestUpdate();
}

void AnkiAddNoteActivity::loop() {
  if (popup != Popup::None) {
    if (millis() - popupTime >= POPUP_DURATION_MS) {
      if (popup == Popup::Queued) {
        finish();
        return;
      }
      popup = Popup::None;
      requestUpdate();
    }
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    finish();
    return;
  }
  if (accountIndices.empty()) return;

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    addNote();
  } else if (mappedInput.wasPressed(MappedInputManager::Button::Up)) {
    cycleAccount(-1);
  } else if (mappedInput.wasPressed(MappedInputManager::Button::Down)) {
    cycleAccount(1);
  } else if (mappedInput.wasPressed(MappedInputManager::Button::Left)) {
    cycleDeck(-1);
  } else if (mappedInput.wasPressed(MappedInputManager::Button::Right)) {
    cycleDeck(1);
  }
}

// Word (bold), sentence, translation (italic) in the reader font between
// `top` and `bottom`. Called twice per render inside a prewarm scope, so it
// must draw the same text both times.
void AnkiAddNoteActivity::drawBody(const int contentX, const int contentWidth, const int top, const int bottom) const {
  const int fontId = SETTINGS.getReaderFontId();
  const int lineHeight = renderer.getLineHeight(fontId);
  if (lineHeight <= 0) return;

  int y = top;
  renderer.drawText(fontId, contentX, y, noteWord.c_str(), true, EpdFontFamily::BOLD);
  y += lineHeight + BODY_GAP;

  // Sentence gets up to two thirds of the remaining lines, the translation the rest (at least one).
  const int remaining = std::max(2, (bottom - y - BODY_GAP) / lineHeight);
  const int sentenceMax = std::max(1, remaining * 2 / 3);
  const auto sentenceLines = renderer.wrappedText(fontId, sentence.c_str(), contentWidth, sentenceMax);
  for (const auto& line : sentenceLines) {
    renderer.drawText(fontId, contentX, y, line.c_str());
    y += lineHeight;
  }
  y += BODY_GAP;

  const int translationMax = std::max(1, remaining - static_cast<int>(sentenceLines.size()));
  const char* translationText = translation.empty() ? tr(STR_ANKI_NO_TRANSLATION) : translation.c_str();
  const auto translationLines =
      renderer.wrappedText(fontId, translationText, contentWidth, translationMax, EpdFontFamily::ITALIC);
  for (const auto& line : translationLines) {
    renderer.drawText(fontId, contentX, y, line.c_str(), true, EpdFontFamily::ITALIC);
    y += lineHeight;
  }
}

bool AnkiAddNoteActivity::sideHintsShown() const {
  const auto orientation = renderer.getOrientation();
  const bool portrait =
      orientation == GfxRenderer::Orientation::Portrait || orientation == GfxRenderer::Orientation::PortraitInverted;
  // Side hint boxes sit at fixed portrait y positions; in landscape they would
  // land on the body, so Up/Down go unhinted there.
  return portrait && !mappedInput.hasTouch() && accountIndices.size() > 1;
}

void AnkiAddNoteActivity::drawHints() const {
  if (accountIndices.empty()) {
    const auto labels = mappedInput.mapLabels(tr(STR_BACK), "", "", "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
    return;
  }
  const bool manyDecks = deckNames.size() > 1;
  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_ANKI_ADD), manyDecks ? "<" : "", manyDecks ? ">" : "");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  if (sideHintsShown()) {
    GUI.drawSideButtonHints(renderer, tr(STR_ANKI_ACCOUNT), tr(STR_ANKI_ACCOUNT));
  }
}

void AnkiAddNoteActivity::render(RenderLock&&) {
  renderer.clearScreen();

  const auto& metrics = UITheme::getInstance().getMetrics();
  const int pageWidth = renderer.getScreenWidth();
  const int pageHeight = renderer.getScreenHeight();

  // The hint bar is drawn at the physical bottom: a side gutter in landscape,
  // the top band when inverted (same scheme as DictionaryDefinitionActivity).
  const auto orientation = renderer.getOrientation();
  const bool isLandscapeCw = orientation == GfxRenderer::Orientation::LandscapeClockwise;
  const bool isLandscapeCcw = orientation == GfxRenderer::Orientation::LandscapeCounterClockwise;
  const bool isInverted = orientation == GfxRenderer::Orientation::PortraitInverted;
  const int hintGutterWidth = (isLandscapeCw || isLandscapeCcw) ? metrics.sideButtonHintsWidth : 0;
  const int areaX = isLandscapeCw ? hintGutterWidth : 0;
  const int areaWidth = pageWidth - hintGutterWidth;
  const int areaY = isInverted ? metrics.buttonHintsHeight : 0;
  const int areaBottom = pageHeight - (isInverted ? 0 : metrics.buttonHintsHeight);

  GUI.drawHeader(renderer, Rect{areaX, areaY + metrics.topPadding, areaWidth, metrics.headerHeight},
                 tr(STR_ANKI_ADD_TO_ANKI));

  // Keep the text clear of the side-button hint boxes when they are drawn.
  const int sidePad = sideHintsShown() ? std::max(metrics.contentSidePadding, metrics.sideButtonHintsWidth + 10)
                                       : metrics.contentSidePadding;
  const int contentX = areaX + sidePad;
  const int contentWidth = areaWidth - 2 * sidePad;
  const int top = areaY + metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int bottom = areaBottom - metrics.verticalSpacing;
  const int uiLineHeight = renderer.getLineHeight(UI_10_FONT_ID);

  if (accountIndices.empty()) {
    const auto lines = renderer.wrappedText(UI_10_FONT_ID, tr(STR_ANKI_NO_ENABLED_ACCOUNTS), contentWidth, 3);
    int y = (top + bottom - static_cast<int>(lines.size()) * uiLineHeight) / 2;
    for (const auto& line : lines) {
      const int x = contentX + (contentWidth - renderer.getTextWidth(UI_10_FONT_ID, line.c_str())) / 2;
      renderer.drawText(UI_10_FONT_ID, x, y, line.c_str());
      y += uiLineHeight;
    }
    drawHints();
    renderer.displayBuffer();
    return;
  }

  // Selector rows sit above the hint bar: account, deck, deck subtitle.
  const int smallLineHeight = renderer.getLineHeight(SMALL_FONT_ID);
  const int rowGap = 4;
  const int selectorHeight = 2 * uiLineHeight + smallLineHeight + 2 * rowGap;
  const int selectorTop = bottom - selectorHeight;
  renderer.drawLine(contentX, selectorTop - metrics.verticalSpacing, contentX + contentWidth - 1,
                    selectorTop - metrics.verticalSpacing);

  const AnkiAccount* account = currentAccount();
  std::string row;
  row.reserve(64);
  row.assign(tr(STR_ANKI_ACCOUNT)).append(": ").append(account ? account->name : "");
  int y = selectorTop;
  renderer.drawText(UI_10_FONT_ID, contentX, y,
                    renderer.truncatedText(UI_10_FONT_ID, row.c_str(), contentWidth).c_str());
  y += uiLineHeight + rowGap;

  row.assign(tr(STR_ANKI_DECK)).append(": ").append(deckNames[static_cast<size_t>(deckPos)]);
  renderer.drawText(UI_10_FONT_ID, contentX, y,
                    renderer.truncatedText(UI_10_FONT_ID, row.c_str(), contentWidth, EpdFontFamily::BOLD).c_str(), true,
                    EpdFontFamily::BOLD);
  y += uiLineHeight + rowGap;

  if (!decksFromSync) {
    renderer.drawText(SMALL_FONT_ID, contentX, y,
                      renderer.truncatedText(SMALL_FONT_ID, tr(STR_ANKI_NO_DECKS_YET), contentWidth).c_str());
  }

  // Two-pass draw inside a prewarm scope so SD-card font glyphs load in one
  // batch (same pattern as the reader and DictionaryDefinitionActivity).
  const int bodyBottom = selectorTop - 2 * metrics.verticalSpacing;
  auto* fcm = renderer.getFontCacheManager();
  auto scope = fcm->createPrewarmScope();
  drawBody(contentX, contentWidth, top, bodyBottom);  // scan pass: records codepoints only
  scope.endScanAndPrewarm();
  drawBody(contentX, contentWidth, top, bodyBottom);

  drawHints();

  if (popup != Popup::None) {
    // drawPopup overlays the framebuffer and refreshes the display itself.
    GUI.drawPopup(renderer, popupText);
    return;
  }
  renderer.displayBuffer();
}
