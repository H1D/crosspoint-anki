#include "AnkiSettingsActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>
#include <Logging.h>
#include <Memory.h>

#include "AnkiAccountActivity.h"
#include "MappedInputManager.h"
#include "anki/AnkiAccountStore.h"
#include "components/UITheme.h"

namespace fui = freeink::ui;

AnkiSettingsActivity::AnkiSettingsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
    : UiListActivity("AnkiSettings", renderer, mappedInput) {}

void AnkiSettingsActivity::onEnter() {
  UiListActivity::onEnter();

  // Reload from disk in case accounts were changed by the editor or the web UI
  ANKI_STORE.loadFromFile();
  nav.selected = 0;
  rebuildRowItems();
}

void AnkiSettingsActivity::rebuildRowItems() {
  rowItems_.clear();
  subtitles_.clear();

  const auto& accounts = ANKI_STORE.getAccounts();
  const auto accountCount = static_cast<int>(accounts.size());
  const int reviewIdx = ANKI_STORE.getReviewAccountIndex();
  rowItems_.reserve(accountCount + 1);
  subtitles_.reserve(accountCount);

  for (int i = 0; i < accountCount; i++) {
    const AnkiAccount& a = accounts[i];
    // Built once per reload; the ListItem points at the owned string.
    subtitles_.push_back(a.profile.empty() ? a.url : a.profile + "@" + a.url);

    fui::ListItem item;
    item.label = !a.name.empty() ? a.name.c_str() : !a.url.empty() ? a.url.c_str() : tr(STR_NOT_SET);
    if (!subtitles_.back().empty()) item.subtitle = subtitles_.back().c_str();
    if (i == reviewIdx) item.value = tr(STR_ANKI_REVIEW_MARK);
    item.actionValue = static_cast<int16_t>(i);
    rowItems_.push_back(item);
  }

  fui::ListItem addAccount;
  addAccount.label = tr(STR_ANKI_ADD_ACCOUNT);
  addAccount.actionValue = static_cast<int16_t>(accountCount);
  rowItems_.push_back(addAccount);
}

const char* AnkiSettingsActivity::headerTitle() const { return tr(STR_ANKI_ACCOUNTS); }

void AnkiSettingsActivity::activateIndex(const int index) {
  nav.selected = index;
  // Activation opens the editor; a lingering flash would gray an unrelated row.
  app.clearTapFlash();

  const auto accountCount = static_cast<int>(ANKI_STORE.getCount());
  openEditor(index < accountCount ? index : -1);
  requestUpdate();
}

void AnkiSettingsActivity::openEditor(const int accountIndex) {
  auto editor = makeUniqueNoThrow<AnkiAccountActivity>(renderer, mappedInput, accountIndex);
  if (!editor) {
    LOG_ERR("ANKI", "OOM: account editor");
    return;
  }
  startActivityForResult(std::move(editor), [this](const ActivityResult&) {
    // Reload the list when returning from the editor
    ANKI_STORE.loadFromFile();
    nav.selected = 0;
    rebuildRowItems();
  });
}

void AnkiSettingsActivity::buildScreen(UiScreen& screen) {
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

  // "Add account" is always present, so the list is never empty.
  fui::ListProps props;
  props.items = rowItems_.data();
  props.count = static_cast<uint16_t>(rowItems_.size());
  props.action = ACTION_ROW;
  props.inputMask = fui::InputTouch;  // physical buttons stay in loop()
  syncListViewport(screen, props);
  screen.list(props);
}
