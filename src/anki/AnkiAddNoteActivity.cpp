#include "AnkiAddNoteActivity.h"

#include <AnkiNoteQueue.h>
#include <AnkiPaths.h>
#include <AnkiSyncEngine.h>
#include <GfxRenderer.h>
#include <I18n.h>
#include <Logging.h>
#include <Memory.h>

#include <algorithm>
#include <cstdio>

#include "AnkiAccountStore.h"
#include "AnkiDevice.h"
#include "AnkiSecureHttp.h"
#include "AnkiStorageFs.h"
#include "MappedInputManager.h"
#include "activities/ActivityResult.h"
#include "activities/util/KeyboardEntryActivity.h"
#include "components/UITheme.h"
#include "settings/AnkiDeckSelectActivity.h"

namespace fui = freeink::ui;

namespace {

constexpr unsigned long POPUP_DURATION_MS = 1200;
constexpr const char* DEFAULT_DECK = "Default";
// Confirm hold on an account row that opens its deck list.
constexpr unsigned long DECK_HOLD_MS = 500;

// Accounts checked on the last add, kept across modals (ids, so store edits
// in between cannot re-target them). Empty until the first open.
std::vector<uint32_t> checkedAccountIds;
bool checkedInitialised = false;

bool isChecked(const uint32_t id) {
  return std::find(checkedAccountIds.begin(), checkedAccountIds.end(), id) != checkedAccountIds.end();
}

void setChecked(const uint32_t id, const bool checked) {
  const auto it = std::find(checkedAccountIds.begin(), checkedAccountIds.end(), id);
  if (checked && it == checkedAccountIds.end()) {
    checkedAccountIds.push_back(id);
  } else if (!checked && it != checkedAccountIds.end()) {
    checkedAccountIds.erase(it);
  }
}

}  // namespace

void AnkiAddNoteActivity::onEnter() {
  UiListActivity::onEnter();
  nav.selected = 0;
  popup = Popup::None;
  // Every exit but a successful Add reports cancelled (the caller stays open).
  ActivityResult cancelled;
  cancelled.isCancelled = true;
  setResult(std::move(cancelled));
  ANKI_STORE.loadFromFile();
  buildTargets();
  refreshRows();
  requestUpdate();
}

// Deck names for the row: the account's "deck for new words" first, then the
// list cached by the last sync, then the configured review decks, else Default.
std::vector<std::string> AnkiAddNoteActivity::deckChoices(const AnkiAccount& account) {
  std::vector<std::string> decks;
  {
    AnkiSyncEngine engine(AnkiStorageFs::instance(), AnkiSecureHttp::instance(), nullptr);
    const std::vector<AnkiDeck> cached = engine.loadDecks(account.id);
    decks.reserve(cached.size() + 1);
    for (const AnkiDeck& d : cached) {
      if (!d.name.empty()) decks.push_back(d.name);
    }
  }
  if (decks.empty()) decks = account.decks;
  if (!account.lastDeck.empty()) {
    const auto it = std::find(decks.begin(), decks.end(), account.lastDeck);
    if (it != decks.end()) decks.erase(it);
    decks.insert(decks.begin(), account.lastDeck);
  }
  if (decks.empty()) decks.emplace_back(DEFAULT_DECK);
  return decks;
}

void AnkiAddNoteActivity::buildTargets() {
  targets.clear();
  const auto& accounts = ANKI_STORE.getAccounts();
  targets.reserve(accounts.size());
  for (size_t i = 0; i < accounts.size(); i++) {
    const AnkiAccount& a = accounts[i];
    if (!a.enabled) continue;
    Target t;
    t.storeIndex = i;
    t.id = a.id;
    t.label = a.name.empty() ? a.url : a.name;
    t.decks = deckChoices(a);
    t.deckPos = 0;
    t.checked = false;
    targets.push_back(std::move(t));
  }
  if (targets.empty()) return;

  // First open: the review account, else the first enabled one. Later opens
  // reuse the remembered set; when none of it is still enabled, fall back the
  // same way so Add never silently does nothing.
  if (checkedInitialised) {
    for (Target& t : targets) t.checked = isChecked(t.id);
  }
  if (!anyChecked()) {
    const int reviewIdx = ANKI_STORE.getReviewAccountIndex();
    Target* pick = &targets.front();
    for (Target& t : targets) {
      if (static_cast<int>(t.storeIndex) == reviewIdx) pick = &t;
    }
    pick->checked = true;
    checkedAccountIds.clear();
    checkedAccountIds.push_back(pick->id);
  }
  checkedInitialised = true;
}

bool AnkiAddNoteActivity::anyChecked() const {
  return std::any_of(targets.begin(), targets.end(), [](const Target& t) { return t.checked; });
}

void AnkiAddNoteActivity::refreshRows() {
  rowItems.clear();
  if (targets.empty()) return;
  rowItems.reserve(targets.size() + 1);
  for (size_t i = 0; i < targets.size(); i++) {
    const Target& t = targets[i];
    fui::ListItem account;
    account.label = t.label.c_str();
    account.subtitle = t.decks[static_cast<size_t>(t.deckPos)].c_str();
    account.toggle = true;
    account.toggleChecked = t.checked;
    account.actionValue = static_cast<int16_t>(i);
    rowItems.push_back(account);
  }
  fui::ListItem add;
  add.label = tr(STR_ANKI_ADD);
  add.enabled = anyChecked();
  add.actionValue = static_cast<int16_t>(targets.size());
  rowItems.push_back(add);
}

const char* AnkiAddNoteActivity::headerTitle() const { return tr(STR_ANKI_ADD_TO_ANKI); }

void AnkiAddNoteActivity::drawFooter() {
  if (targets.empty()) {
    const auto labels = mappedInput.mapLabels(tr(STR_BACK), "", "", "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
    return;
  }
  // Front Left/Right cycle the highlighted account's deck; the side buttons move the selection.
  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_ANKI_DECK), tr(STR_ANKI_DECK));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
}

void AnkiAddNoteActivity::cycleDeck(const int direction) {
  const int index = nav.selected;
  if (index < 0 || index >= static_cast<int>(targets.size())) return;
  Target& t = targets[static_cast<size_t>(index)];
  const int count = static_cast<int>(t.decks.size());
  if (count < 2) return;
  {
    RenderLock lock;  // buildScreen reads the rows on the render task
    t.deckPos = (t.deckPos + direction + count) % count;
    refreshRows();
  }
  requestUpdate();
}

bool AnkiAddNoteActivity::handleCustomInput() {
  if (popup != Popup::None) {
    if (millis() - popupTime >= POPUP_DURATION_MS) {
      if (popup == Popup::Queued) {
        setResult(ActivityResult{});
        finish();
        return true;
      }
      popup = Popup::None;
      requestUpdate();
    }
    return true;
  }
  if (targets.empty()) return false;

  // Front Left/Right are also NavPrevious/NavNext for the list; claim them for
  // the whole press so the base navigator never sees them as row steps.
  using Button = MappedInputManager::Button;
  const bool left = mappedInput.wasReleased(Button::Left);
  const bool right = mappedInput.wasReleased(Button::Right);
  if (left || right || mappedInput.isPressed(Button::Left) || mappedInput.isPressed(Button::Right)) {
    if (left) cycleDeck(-1);
    if (right) cycleDeck(1);
    return true;
  }
  return false;
}

bool AnkiAddNoteActivity::handleButtons() {
  if (mappedInput.wasLongPressed(MappedInputManager::Button::Confirm, DECK_HOLD_MS)) {
    onRowLongPress(nav.selected);
    return true;
  }
  return UiListActivity::handleButtons();
}

void AnkiAddNoteActivity::onRowLongPress(const int index) {
  nav.selected = index;
  if (index < 0 || index >= static_cast<int>(rowItems.size())) return;
  // A hold on Add (the press swallows its release) still adds.
  if (index >= static_cast<int>(targets.size())) {
    activateIndex(index);
    return;
  }
  openDeckPicker(static_cast<size_t>(index));
}

void AnkiAddNoteActivity::activateIndex(const int index) {
  nav.selected = index;
  if (index < 0 || index >= static_cast<int>(rowItems.size())) return;
  const size_t targetIndex = static_cast<size_t>(index);
  if (targetIndex >= targets.size()) {
    queueNotes();
    return;
  }
  Target& t = targets[targetIndex];
  {
    RenderLock lock;
    t.checked = !t.checked;
    setChecked(t.id, t.checked);
    refreshRows();
  }
  requestUpdate();
}

// The account's synced deck list; before any sync, type the name instead.
void AnkiAddNoteActivity::openDeckPicker(const size_t targetIndex) {
  Target& t = targets[targetIndex];
  if (!t.checked) {
    RenderLock lock;
    t.checked = true;
    setChecked(t.id, true);
    refreshRows();
  }
  const std::string& current = t.decks[static_cast<size_t>(t.deckPos)];
  auto onResult = [this, targetIndex](const ActivityResult& result) {
    if (result.isCancelled || !std::holds_alternative<KeyboardResult>(result.data)) return;
    setDeck(targetIndex, std::get<KeyboardResult>(result.data).text);
  };
  app.clearTapFlash();
  if (!AnkiDeckSelectActivity::hasCachedDecks(t.id)) {
    auto keyboard = makeUniqueNoThrow<KeyboardEntryActivity>(renderer, mappedInput, tr(STR_ANKI_DECK), current, 100,
                                                             InputType::Text);
    if (!keyboard) {
      LOG_ERR("ANKI", "OOM: keyboard");
      return;
    }
    startActivityForResult(std::move(keyboard), onResult);
    return;
  }
  auto picker = makeUniqueNoThrow<AnkiDeckSelectActivity>(renderer, mappedInput, t.decks, current);
  if (!picker) {
    LOG_ERR("ANKI", "OOM: deck picker");
    return;
  }
  startActivityForResult(std::move(picker), onResult);
}

void AnkiAddNoteActivity::setDeck(const size_t targetIndex, const std::string& deck) {
  if (targetIndex >= targets.size() || deck.empty()) return;
  Target& t = targets[targetIndex];
  {
    RenderLock lock;
    const auto it = std::find(t.decks.begin(), t.decks.end(), deck);
    if (it != t.decks.end()) {
      t.deckPos = static_cast<int>(it - t.decks.begin());
    } else {
      t.decks.push_back(deck);
      t.deckPos = static_cast<int>(t.decks.size()) - 1;
    }
    refreshRows();  // row pointers may point into the old deck storage
  }
  requestUpdate();
}

// One note per checked account, each with its own client id and that row's
// deck. Nothing checked: the Add row is a no-op.
void AnkiAddNoteActivity::queueNotes() {
  if (!anyChecked()) return;
  // Leaving the screen after the popup; a lingering flash would gray a row underneath.
  app.clearTapFlash();

  const std::string front = ankinote::frontHtml(draft.word, draft.sentence, draft.headword);
  std::vector<std::string> tags;
  tags.reserve(2);
  tags.emplace_back("crosspoint");
  if (!draft.bookTitle.empty()) tags.push_back(ankinote::bookTag(draft.bookTitle));

  AnkiStorageFs& fs = AnkiStorageFs::instance();
  const auto& accounts = ANKI_STORE.getAccounts();
  size_t pending = 0;
  size_t queued = 0;
  size_t failed = 0;
  for (const Target& t : targets) {
    if (!t.checked || t.storeIndex >= accounts.size()) continue;
    const AnkiAccount& account = accounts[t.storeIndex];
    // An account that never synced has no data directory yet.
    fs.mkdirs(ankipaths::accountDir(t.id));
    AnkiNoteQueue queue(fs, ankipaths::notesFile(t.id));
    AnkiNoteQueue::Note note;
    note.clientId = ankidevice::nextClientId();
    note.deck = t.decks[static_cast<size_t>(t.deckPos)];
    note.model = account.model;
    note.front = front;
    note.back = draft.translation;
    note.tags = tags;
    if (queue.append(note)) {
      pending = queue.count();
      queued++;
      // The next add starts on this deck.
      ANKI_STORE.setLastDeck(t.storeIndex, note.deck);
    } else {
      LOG_ERR("ANKI", "Failed to queue note for account %u", static_cast<unsigned>(t.id));
      failed++;
    }
  }

  if (failed == 0) {
    // One account: its pending count; several: how many accounts got the note.
    if (queued > 1) {
      snprintf(popupText, sizeof(popupText), tr(STR_ANKI_QUEUED_ACCOUNTS), static_cast<int>(queued));
    } else {
      snprintf(popupText, sizeof(popupText), tr(STR_ANKI_QUEUED), static_cast<int>(pending));
    }
    popup = Popup::Queued;
  } else {
    snprintf(popupText, sizeof(popupText), "%s", tr(STR_ANKI_QUEUE_FAILED));
    popup = Popup::Failed;
  }
  popupTime = millis();
  requestUpdate();
}

void AnkiAddNoteActivity::buildScreen(UiScreen& screen) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  // Content below the GUI.drawHeader band, above the button hints; derived
  // from the safe area so board bezel insets apply (same as AnkiDeckSelect).
  const Rect safe = UITheme::getInstance().getScreenSafeArea(renderer, true, false);
  screen.setContentMarginFromScreen(
      fui::Insets{static_cast<int16_t>(safe.y + metrics.topPadding + metrics.headerHeight),
                  static_cast<int16_t>(renderer.getScreenWidth() - (safe.x + safe.width)),
                  static_cast<int16_t>(renderer.getScreenHeight() - (safe.y + safe.height) + metrics.buttonHintsHeight),
                  static_cast<int16_t>(safe.x)});
  screen.spacer(static_cast<int16_t>(metrics.verticalSpacing));

  const auto& theme = screen.theme();
  if (targets.empty()) {
    screen.centeredText(tr(STR_ANKI_NO_ENABLED_ACCOUNTS), theme.bodyText);
    return;
  }

  buildPreview(screen);

  fui::ListProps props;
  props.items = rowItems.data();
  props.count = static_cast<uint16_t>(rowItems.size());
  props.action = ACTION_ROW;
  props.inputMask = fui::InputTouch | fui::InputLongPress;  // physical buttons stay in loop()
  syncListViewport(screen, props);
  const fui::Rect listArea = screen.body();
  screen.list(props);
  drawDeckHints(screen, props, listArea);
}

// The note as it will read in Anki: front word, the example sentence, and
// the back, in a bordered card inset to the list's text edge.
void AnkiAddNoteActivity::buildPreview(UiScreen& screen) {
  const auto& theme = screen.theme();
  auto& target = screen.target();
  const auto inset = static_cast<int16_t>(theme.listInset);
  const auto pad = static_cast<int16_t>(theme.listSidePadding);
  const int16_t innerWidth = static_cast<int16_t>(screen.body().width - 2 * (inset + pad));
  if (innerWidth <= 0) return;

  const fui::TextStyle caption = theme.smallText;
  fui::TextStyle front = theme.bodyText;
  front.bold = true;
  fui::TextStyle example = theme.smallText;
  example.maxLines = 3;
  fui::TextStyle back = theme.bodyText;
  back.maxLines = 2;
  const bool hasBack = !draft.translation.empty();

  const char* frontText = draft.headword.empty() ? draft.word.c_str() : draft.headword.c_str();
  const char* backText = hasBack ? draft.translation.c_str() : tr(STR_ANKI_NO_TRANSLATION);
  const bool hasExample = !draft.sentence.empty();

  // Caption + value blocks, each as tall as its wrapped text.
  struct Block {
    const char* caption;
    const char* text;
    const fui::TextStyle* style;
    int16_t height;
  };
  Block blocks[3];
  int blockCount = 0;
  blocks[blockCount++] = {tr(STR_ANKI_FRONT), frontText, &front, 0};
  if (hasExample) blocks[blockCount++] = {tr(STR_ANKI_EXAMPLE), draft.sentence.c_str(), &example, 0};
  blocks[blockCount++] = {tr(STR_ANKI_CARD_BACK), backText, &back, 0};

  const int16_t captionH = target.lineHeight(caption.font);
  const auto gap = static_cast<int16_t>(theme.spaceSm);
  // The card keeps at most half the screen body for the account rows below;
  // on short (landscape) screens the example shrinks to one line first.
  const auto maxCardH = static_cast<int16_t>(screen.body().height / 2);
  int16_t cardH = 0;
  for (int pass = 0; pass < 2; pass++) {
    cardH = static_cast<int16_t>(2 * gap);
    for (int i = 0; i < blockCount; i++) {
      blocks[i].height = fui::measureWrappedText(target, blocks[i].text, *blocks[i].style, innerWidth).height;
      cardH = static_cast<int16_t>(cardH + captionH + blocks[i].height + (i > 0 ? gap : 0));
    }
    if (cardH <= maxCardH || !hasExample || example.maxLines == 1) break;
    example.maxLines = 1;
  }

  const fui::Rect band = screen.takeTop(cardH, theme.spaceMd);
  const fui::Rect card{static_cast<int16_t>(band.x + inset), band.y, static_cast<int16_t>(band.width - 2 * inset),
                       band.height};
  target.stroke(card, fui::Paint::solid(fui::Color::Black), 1, theme.listRowRadius);
  int16_t y = static_cast<int16_t>(card.y + gap);
  const auto x = static_cast<int16_t>(card.x + pad);
  for (int i = 0; i < blockCount; i++) {
    if (i > 0) y = static_cast<int16_t>(y + gap);
    if (y + captionH + blocks[i].height > card.bottom()) break;  // clamped band: drop what does not fit
    drawDimmedText(screen, fui::Rect{x, y, innerWidth, captionH}, blocks[i].caption, caption);
    y = static_cast<int16_t>(y + captionH);
    const fui::Rect textRect{x, y, innerWidth, blocks[i].height};
    if (blocks[i].text == backText && !hasBack) {
      drawDimmedText(screen, textRect, backText, back);
    } else {
      target.text(textRect, blocks[i].text, *blocks[i].style);
    }
    y = static_cast<int16_t>(y + blocks[i].height);
  }
}

// "hold to change", dimmed, after each account row's deck subtitle. The list
// draws the subtitle as one run, so the hint is placed by the same row
// geometry list() uses (rows stacked from the top of `area`, each sized by
// measureListRow, the subtitle under a vertically centered label band).
void AnkiAddNoteActivity::drawDeckHints(UiScreen& screen, const fui::ListProps& props, const fui::Rect area) {
  auto& target = screen.target();
  const char* hint = tr(STR_ANKI_HOLD_TO_CHANGE);
  const int16_t rowInset = props.rowInset < 0 ? 0 : props.rowInset;
  const int16_t sidePad = props.sidePadding < 0 ? 0 : props.sidePadding;
  const int16_t rowGap = props.rowGap < 0 ? 0 : props.rowGap;
  auto rowX = static_cast<int16_t>(area.x + rowInset);
  auto rowWidth = static_cast<int16_t>(area.width - 2 * rowInset);
  // list() narrows the rows for the scroll strip (syncListViewport sets props.nav, so it always reserves it).
  const int16_t scrollWidth = props.scrollIndicatorWidth < 0 ? 3 : props.scrollIndicatorWidth;
  const int16_t stripNeeded =
      static_cast<int16_t>(scrollWidth + (props.scrollIndicatorInset < 0 ? 0 : props.scrollIndicatorInset) + 2);
  if (props.scrollIndicator && scrollWidth > 0 && rowInset < stripNeeded) {
    const auto cut = static_cast<int16_t>(stripNeeded - rowInset);
    rowWidth = static_cast<int16_t>(rowWidth - cut);
    if (props.scrollIndicatorSide == 1) rowX = static_cast<int16_t>(rowX + cut);
  }
  const auto textWidth = static_cast<int16_t>(rowWidth - 2 * sidePad);
  const int16_t gap = target.measureText(props.subtitleText.font, "  ", props.subtitleText).width;
  const int16_t hintWidth = target.measureText(props.subtitleText.font, hint, props.subtitleText).width;

  int16_t y = area.y;
  for (int i = props.topIndex; i < props.count && y < area.bottom(); i++) {
    const fui::ListItem& item = props.items[i];
    const fui::ListRowLayout layout = fui::measureListRow(target, screen.frame().assets(), rowWidth, props, item);
    if (static_cast<size_t>(i) < targets.size() && item.subtitle && y + layout.height <= area.bottom()) {
      const int16_t subtitleWidth =
          target.measureText(props.subtitleText.font, item.subtitle, props.subtitleText).width;
      if (subtitleWidth + gap + hintWidth <= textWidth) {
        int16_t bandTop = static_cast<int16_t>(y + (layout.height - layout.labelHeight - layout.subtitleHeight) / 2);
        if (bandTop < y) bandTop = y;
        const fui::Rect hintRect{static_cast<int16_t>(rowX + sidePad + subtitleWidth + gap),
                                 static_cast<int16_t>(bandTop + layout.labelHeight), hintWidth, layout.subtitleHeight};
        // Dimmed on paper only: checkerboard ink is unreadable over the
        // selection's gray pill, and a filled selection draws paper-colored text.
        if (props.selectedIndex == i) {
          const fui::Paint fg =
              props.rowStyles.resolve(static_cast<fui::State>(item.state | fui::StateSelected)).foreground;
          target.text(hintRect, hint, fui::textStyleWithForeground(props.subtitleText, fg));
        } else {
          drawDimmedText(screen, hintRect, hint, props.subtitleText);
        }
      }
    }
    y = static_cast<int16_t>(y + layout.height + rowGap);
  }
}

void AnkiAddNoteActivity::render(RenderLock&& lock) {
  UiListActivity::render(std::move(lock));
  // drawPopup overlays the framebuffer and refreshes the display itself.
  if (popup != Popup::None) GUI.drawPopup(renderer, popupText);
}

// Clearing the odd (x + y) pixels leaves black ink on the even ones, the
// DarkGray dither pattern. Paper stays white and the LightGray selection pill
// (black only at even x and even y) keeps every dot.
void AnkiAddNoteActivity::drawDimmedText(UiScreen& screen, const fui::Rect rect, const char* text,
                                         const fui::TextStyle& style) {
  screen.target().text(rect, text, style);
  for (int y = rect.y; y < rect.bottom(); y++) {
    for (int x = rect.x + ((rect.x + y + 1) & 1); x < rect.right(); x += 2) renderer.drawPixel(x, y, false);
  }
}
