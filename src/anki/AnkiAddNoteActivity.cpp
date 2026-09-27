#include "AnkiAddNoteActivity.h"

#include <AnkiNoteQueue.h>
#include <AnkiPaths.h>
#include <AnkiSyncEngine.h>
#include <GfxRenderer.h>
#include <I18n.h>
#include <Logging.h>

#include <algorithm>
#include <cstdio>

#include "AnkiAccountStore.h"
#include "AnkiDevice.h"
#include "AnkiSecureHttp.h"
#include "AnkiStorageFs.h"
#include "MappedInputManager.h"
#include "components/UITheme.h"

namespace fui = freeink::ui;

namespace {

constexpr unsigned long POPUP_DURATION_MS = 1200;
constexpr const char* DEFAULT_DECK = "Default";

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
    fui::ListItem item;
    item.label = t.label.c_str();
    item.subtitle = t.decks[static_cast<size_t>(t.deckPos)].c_str();
    item.toggle = true;
    item.toggleChecked = t.checked;
    item.actionValue = static_cast<int16_t>(i);
    rowItems.push_back(item);
  }
  fui::ListItem add;
  add.label = tr(STR_ANKI_ADD);
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
  // Front Left/Right cycle the highlighted row's deck; the side buttons move the selection.
  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_ANKI_DECK), tr(STR_ANKI_DECK));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
}

void AnkiAddNoteActivity::cycleDeck(const int direction) {
  const int index = nav.selected;
  if (index < 0 || index >= static_cast<int>(targets.size())) return;
  Target& t = targets[static_cast<size_t>(index)];
  const int count = static_cast<int>(t.decks.size());
  if (count < 2) return;
  t.deckPos = (t.deckPos + direction + count) % count;
  refreshRows();
  requestUpdate();
}

bool AnkiAddNoteActivity::handleCustomInput() {
  if (popup != Popup::None) {
    if (millis() - popupTime >= POPUP_DURATION_MS) {
      if (popup == Popup::Queued) {
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

void AnkiAddNoteActivity::activateIndex(const int index) {
  nav.selected = index;
  if (index < 0 || index >= static_cast<int>(rowItems.size())) return;
  if (index < static_cast<int>(targets.size())) {
    Target& t = targets[static_cast<size_t>(index)];
    t.checked = !t.checked;
    setChecked(t.id, t.checked);
    refreshRows();
    requestUpdate();
    return;
  }
  queueNotes();
}

// One note per checked account, each with its own client id and that row's
// deck. Nothing checked: the Add row is a no-op.
void AnkiAddNoteActivity::queueNotes() {
  if (!anyChecked()) return;
  // Leaving the screen after the popup; a lingering flash would gray a row underneath.
  app.clearTapFlash();

  const std::string front = ankinote::frontHtml(noteWord, sentence);
  std::vector<std::string> tags;
  tags.reserve(2);
  tags.emplace_back("crosspoint");
  if (!bookTitle.empty()) tags.push_back(ankinote::bookTag(bookTitle));

  AnkiStorageFs& fs = AnkiStorageFs::instance();
  const auto& accounts = ANKI_STORE.getAccounts();
  size_t pending = 0;
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
    note.back = translation;
    note.tags = tags;
    if (queue.append(note)) {
      pending += queue.count();
    } else {
      LOG_ERR("ANKI", "Failed to queue note for account %u", static_cast<unsigned>(t.id));
      failed++;
    }
  }

  if (failed == 0) {
    snprintf(popupText, sizeof(popupText), tr(STR_ANKI_QUEUED), static_cast<int>(pending));
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

  // Note band: word (bold, one line), sentence (two lines), translation (two
  // small lines); longer text truncates. Inset to the list's text edge.
  fui::TextStyle wordStyle = theme.bodyText;
  wordStyle.bold = true;
  wordStyle.maxLines = 1;
  fui::TextStyle sentenceStyle = theme.bodyText;
  sentenceStyle.maxLines = 2;
  fui::TextStyle translationStyle = theme.smallText;
  translationStyle.maxLines = 2;
  const int16_t bodyLine = screen.target().lineHeight(sentenceStyle.font);
  const int16_t smallLine = screen.target().lineHeight(translationStyle.font);
  const fui::Insets sideInset{0, theme.listSidePadding, 0, theme.listSidePadding};
  screen.target().text(screen.takeTop(bodyLine, theme.spaceXs).inset(sideInset), noteWord.c_str(), wordStyle);
  screen.target().text(screen.takeTop(static_cast<int16_t>(bodyLine * 2), theme.spaceXs).inset(sideInset),
                       sentence.c_str(), sentenceStyle);
  screen.target().text(screen.takeTop(static_cast<int16_t>(smallLine * 2), theme.spaceMd).inset(sideInset),
                       translation.empty() ? tr(STR_ANKI_NO_TRANSLATION) : translation.c_str(), translationStyle);

  fui::ListProps props;
  props.items = rowItems.data();
  props.count = static_cast<uint16_t>(rowItems.size());
  props.action = ACTION_ROW;
  props.inputMask = fui::InputTouch;  // physical buttons stay in loop()
  syncListViewport(screen, props);
  screen.list(props);
}

void AnkiAddNoteActivity::render(RenderLock&& lock) {
  UiListActivity::render(std::move(lock));
  // drawPopup overlays the framebuffer and refreshes the display itself.
  if (popup != Popup::None) GUI.drawPopup(renderer, popupText);
}
