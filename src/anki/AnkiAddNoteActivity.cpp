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
  summary = noteWord;
  if (!headword.empty()) summary += " (" + headword + ")";
  summary += " \xE2\x80\x94 ";
  summary += translation.empty() ? tr(STR_ANKI_NO_TRANSLATION) : translation;
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
  t.deckPos = (t.deckPos + direction + count) % count;
  refreshRows();
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
  t.checked = !t.checked;
  setChecked(t.id, t.checked);
  refreshRows();
  requestUpdate();
}

// The account's synced deck list; before any sync, type the name instead.
void AnkiAddNoteActivity::openDeckPicker(const size_t targetIndex) {
  Target& t = targets[targetIndex];
  if (!t.checked) {
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
  const auto it = std::find(t.decks.begin(), t.decks.end(), deck);
  if (it != t.decks.end()) {
    t.deckPos = static_cast<int>(it - t.decks.begin());
  } else {
    t.decks.push_back(deck);
    t.deckPos = static_cast<int>(t.decks.size()) - 1;
  }
  refreshRows();  // row pointers may point into the old deck storage
  requestUpdate();
}

// One note per checked account, each with its own client id and that row's
// deck. Nothing checked: the Add row is a no-op.
void AnkiAddNoteActivity::queueNotes() {
  if (!anyChecked()) return;
  // Leaving the screen after the popup; a lingering flash would gray a row underneath.
  app.clearTapFlash();

  const std::string front = ankinote::frontHtml(noteWord, sentence, headword);
  std::vector<std::string> tags;
  tags.reserve(2);
  tags.emplace_back("crosspoint");
  if (!bookTitle.empty()) tags.push_back(ankinote::bookTag(bookTitle));

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
    note.back = translation;
    note.tags = tags;
    if (queue.append(note)) {
      pending = queue.count();
      queued++;
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

  // One line, "word — translation", truncated; inset to the list's text edge.
  fui::TextStyle summaryStyle = theme.bodyText;
  summaryStyle.maxLines = 1;
  const auto textInset = static_cast<int16_t>(theme.listInset + theme.listSidePadding);
  const fui::Insets sideInset{0, textInset, 0, textInset};
  screen.target().text(screen.takeTop(screen.target().lineHeight(summaryStyle.font), theme.spaceMd).inset(sideInset),
                       summary.c_str(), summaryStyle);

  fui::ListProps props;
  props.items = rowItems.data();
  props.count = static_cast<uint16_t>(rowItems.size());
  props.action = ACTION_ROW;
  props.inputMask = fui::InputTouch | fui::InputLongPress;  // physical buttons stay in loop()
  syncListViewport(screen, props);
  screen.list(props);
}

void AnkiAddNoteActivity::render(RenderLock&& lock) {
  UiListActivity::render(std::move(lock));
  // drawPopup overlays the framebuffer and refreshes the display itself.
  if (popup != Popup::None) GUI.drawPopup(renderer, popupText);
}
