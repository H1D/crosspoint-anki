#include "AnkiAccountPickerActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include "MappedInputManager.h"
#include "anki/AnkiAccountStore.h"
#include "components/UITheme.h"

namespace fui = freeink::ui;

void AnkiAccountPickerActivity::onEnter() {
  UiListActivity::onEnter();
  nav.selected = 0;
  rebuildRowItems();
}

void AnkiAccountPickerActivity::rebuildRowItems() {
  rowItems_.clear();
  subtitles_.clear();

  const auto& accounts = ANKI_STORE.getAccounts();
  rowItems_.reserve(accounts.size());
  subtitles_.reserve(accounts.size());
  const int reviewIdx = ANKI_STORE.getReviewAccountIndex();
  for (size_t i = 0; i < accounts.size(); i++) {
    const AnkiAccount& a = accounts[i];
    if (!a.enabled) continue;
    subtitles_.push_back(a.profile.empty() ? a.url : a.profile + "@" + a.url);
    fui::ListItem item;
    item.label = !a.name.empty() ? a.name.c_str() : !a.url.empty() ? a.url.c_str() : tr(STR_NOT_SET);
    if (!subtitles_.back().empty()) item.subtitle = subtitles_.back().c_str();
    if (static_cast<int>(i) == reviewIdx) {
      item.value = tr(STR_ANKI_REVIEW_MARK);
      nav.selected = static_cast<int>(rowItems_.size());  // open on the last reviewed account
    }
    item.actionValue = static_cast<int16_t>(i);  // store index, not row index
    rowItems_.push_back(item);
  }
}

const char* AnkiAccountPickerActivity::headerTitle() const { return tr(STR_ANKI_PICK_ACCOUNT); }

void AnkiAccountPickerActivity::activateIndex(const int index) {
  if (index < 0 || index >= listCount()) return;
  app.clearTapFlash();
  const int storeIndex = rowItems_[static_cast<size_t>(index)].actionValue;
  ANKI_STORE.setReviewAccount(static_cast<size_t>(storeIndex));
  setResult(MenuResult{storeIndex});
  finish();
}

void AnkiAccountPickerActivity::onBackButton() {
  ActivityResult cancelled;
  cancelled.isCancelled = true;
  setResult(std::move(cancelled));
  finish();
}

void AnkiAccountPickerActivity::buildScreen(UiScreen& screen) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const Rect safe = UITheme::getInstance().getScreenSafeArea(renderer, true, false);
  screen.setContentMarginFromScreen(
      fui::Insets{static_cast<int16_t>(safe.y + metrics.topPadding + metrics.headerHeight),
                  static_cast<int16_t>(renderer.getScreenWidth() - (safe.x + safe.width)),
                  static_cast<int16_t>(renderer.getScreenHeight() - (safe.y + safe.height) + metrics.buttonHintsHeight),
                  static_cast<int16_t>(safe.x)});

  // Two lines, like the account editor's hint: one row truncates in most languages.
  fui::TextStyle hintStyle = screen.theme().smallText;
  hintStyle.maxLines = 2;
  const fui::Rect band = screen.takeTop(static_cast<int16_t>(metrics.tabBarHeight * 2));
  const int16_t pad = screen.theme().headerSidePadding;
  screen.target().text(band.inset(fui::Insets{0, pad, 0, pad}), tr(STR_ANKI_PICKER_HINT), hintStyle);
  screen.spacer(static_cast<int16_t>(metrics.verticalSpacing));

  fui::ListProps props;
  props.items = rowItems_.data();
  props.count = static_cast<uint16_t>(rowItems_.size());
  props.action = ACTION_ROW;
  props.inputMask = fui::InputTouch;  // physical buttons stay in loop()
  syncListViewport(screen, props);
  screen.list(props);
}
