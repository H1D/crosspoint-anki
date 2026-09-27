#include "AnkiDeckSelectActivity.h"

#include <AnkiSyncEngine.h>
#include <GfxRenderer.h>
#include <I18n.h>
#include <Logging.h>

#include <algorithm>

#include "MappedInputManager.h"
#include "anki/AnkiAccountStore.h"
#include "anki/AnkiSecureHttp.h"
#include "anki/AnkiStorageFs.h"
#include "components/UITheme.h"

namespace fui = freeink::ui;

AnkiDeckSelectActivity::AnkiDeckSelectActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                               const int accountIndex)
    : UiListActivity("AnkiDeckSelect", renderer, mappedInput), accountIndex(accountIndex) {}

void AnkiDeckSelectActivity::onEnter() {
  UiListActivity::onEnter();
  nav.selected = 0;
  dirty = false;
  names_.clear();
  selected_.clear();
  rowItems_.clear();

  const AnkiAccount* account = accountIndex < 0 ? nullptr : ANKI_STORE.getAccount(static_cast<size_t>(accountIndex));
  if (!account) {
    LOG_ERR("ANKI", "Deck picker: no account at index %d", accountIndex);
    return;
  }
  selected_ = account->decks;

  // Cached deck list from the last sync (no network here; the clock is unused).
  AnkiSyncEngine engine(AnkiStorageFs::instance(), AnkiSecureHttp::instance(), nullptr);
  std::vector<AnkiDeck> decks = engine.loadDecks(account->id);
  names_.reserve(decks.size() + selected_.size());
  for (AnkiDeck& d : decks) {
    if (!d.name.empty()) names_.push_back(std::move(d.name));
  }
  // Selected decks the cache does not know keep a row so they can be unticked.
  for (const std::string& s : selected_) {
    if (std::find(names_.begin(), names_.end(), s) == names_.end()) names_.push_back(s);
  }
  if (names_.empty()) return;  // empty state drawn by buildScreen()

  rowItems_.reserve(names_.size() + 1);
  fui::ListItem all;
  all.label = tr(STR_ANKI_ALL_DECKS);
  all.toggle = true;
  all.actionValue = 0;
  rowItems_.push_back(all);
  for (size_t i = 0; i < names_.size(); i++) {
    fui::ListItem item;
    item.label = names_[i].c_str();
    item.toggle = true;
    item.actionValue = static_cast<int16_t>(i + 1);
    rowItems_.push_back(item);
  }
  refreshChecks();
}

bool AnkiDeckSelectActivity::isSelected(const std::string& name) const {
  return std::find(selected_.begin(), selected_.end(), name) != selected_.end();
}

void AnkiDeckSelectActivity::refreshChecks() {
  if (rowItems_.empty()) return;
  rowItems_[0].toggleChecked = selected_.empty();
  for (size_t i = 0; i < names_.size(); i++) {
    rowItems_[i + 1].toggleChecked = isSelected(names_[i]);
  }
}

const char* AnkiDeckSelectActivity::headerTitle() const { return tr(STR_ANKI_DECKS); }

void AnkiDeckSelectActivity::activateIndex(const int index) {
  nav.selected = index;
  if (index < 0 || index >= static_cast<int>(rowItems_.size())) return;

  if (index == 0) {
    selected_.clear();
  } else {
    const std::string& name = names_[static_cast<size_t>(index - 1)];
    const auto it = std::find(selected_.begin(), selected_.end(), name);
    if (it != selected_.end()) {
      selected_.erase(it);
    } else {
      selected_.push_back(name);
    }
  }
  dirty = true;
  refreshChecks();
  requestUpdate();
}

void AnkiDeckSelectActivity::onBackButton() {
  if (dirty && accountIndex >= 0) {
    const AnkiAccount* stored = ANKI_STORE.getAccount(static_cast<size_t>(accountIndex));
    if (stored) {
      AnkiAccount account = *stored;
      account.decks = selected_;
      if (!ANKI_STORE.updateAccount(static_cast<size_t>(accountIndex), account)) {
        LOG_ERR("ANKI", "Failed to save deck selection for account %d", accountIndex);
      }
    }
  }
  // Leaving the screen; a lingering flash would gray a row on the editor.
  app.clearTapFlash();
  finish();
}

void AnkiDeckSelectActivity::buildScreen(UiScreen& screen) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  // Content below the GUI.drawHeader band, above the button hints; derived
  // from the safe area so board bezel insets apply (same as OpdsServerList).
  const Rect safe = UITheme::getInstance().getScreenSafeArea(renderer, true, false);
  screen.setContentMarginFromScreen(
      fui::Insets{static_cast<int16_t>(safe.y + metrics.topPadding + metrics.headerHeight),
                  static_cast<int16_t>(renderer.getScreenWidth() - (safe.x + safe.width)),
                  static_cast<int16_t>(renderer.getScreenHeight() - (safe.y + safe.height) + metrics.buttonHintsHeight),
                  static_cast<int16_t>(safe.x)});
  screen.spacer(static_cast<int16_t>(metrics.verticalSpacing));

  if (rowItems_.empty()) {
    screen.centeredText(tr(STR_ANKI_NO_DECKS_YET), screen.theme().bodyText);
    return;
  }

  fui::ListProps props;
  props.items = rowItems_.data();
  props.count = static_cast<uint16_t>(rowItems_.size());
  props.action = ACTION_ROW;
  props.inputMask = fui::InputTouch;  // physical buttons stay in loop()
  syncListViewport(screen, props);
  screen.list(props);
}
