#include "AnkiAccountActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>
#include <Logging.h>
#include <Memory.h>

#include <cstdio>

#include "AnkiDeckSelectActivity.h"
#include "MappedInputManager.h"
#include "activities/util/KeyboardEntryActivity.h"
#include "anki/AnkiAccountStore.h"
#include "components/UITheme.h"

namespace fui = freeink::ui;

namespace {
enum Row : int {
  ROW_NAME = 0,
  ROW_URL,
  ROW_PROFILE,
  ROW_TOKEN,
  ROW_ENABLED,
  ROW_DECKS,
  ROW_DEFAULT_DECK,
  ROW_MODEL,
  ROW_CACHE_SIZE,
  ROW_DELETE,
};

// OptionPopup choices for the cache size (labels and values in step).
constexpr const char* CACHE_SIZE_LABELS[] = {"20", "40", "60", "100"};
constexpr uint8_t CACHE_SIZE_VALUES[] = {20, 40, 60, 100};

int indexOfValue(const uint8_t* values, const int count, const uint8_t value) {
  for (int i = 0; i < count; i++) {
    if (values[i] == value) return i;
  }
  return 0;
}
}  // namespace

AnkiAccountActivity::AnkiAccountActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, const int accountIndex)
    : UiListActivity("AnkiAccount", renderer, mappedInput), accountIndex(accountIndex) {
  // Labels never change (the values track editAccount live), so they're set
  // once here rather than every buildScreen() call.
  static constexpr StrId fieldNames[BASE_ITEMS] = {
      StrId::STR_ANKI_ACCOUNT_NAME, StrId::STR_ANKI_SERVER_URL, StrId::STR_ANKI_PROFILE,
      StrId::STR_ANKI_TOKEN,        StrId::STR_ANKI_ENABLED,    StrId::STR_ANKI_DECKS,
      StrId::STR_ANKI_DEFAULT_DECK, StrId::STR_ANKI_NOTE_TYPE,  StrId::STR_ANKI_CACHE_SIZE};
  for (int i = 0; i < BASE_ITEMS; i++) {
    fieldRowItems[i].label = I18N.get(fieldNames[i]);
    fieldRowItems[i].actionValue = static_cast<int16_t>(i);
  }
  fieldRowItems[ROW_ENABLED].toggle = true;
  fieldRowItems[ROW_DELETE].label = tr(STR_ANKI_DELETE_ACCOUNT);
  fieldRowItems[ROW_DELETE].actionValue = static_cast<int16_t>(ROW_DELETE);
}

int AnkiAccountActivity::getMenuItemCount() const {
  return isNewAccount ? BASE_ITEMS : BASE_ITEMS + 1;  // +1 for Delete
}

void AnkiAccountActivity::onEnter() {
  UiListActivity::onEnter();

  isNewAccount = (accountIndex < 0);
  showSaveError = false;

  if (!isNewAccount) {
    // Edit flow: copy the selected account into local editable state.
    // Changes are persisted field-by-field through saveAccount().
    const auto* account = ANKI_STORE.getAccount(static_cast<size_t>(accountIndex));
    if (account) {
      editAccount = *account;
    } else {
      // Account was deleted between navigation and entering this screen — treat as new
      isNewAccount = true;
      accountIndex = -1;
    }
  }
}

void AnkiAccountActivity::reloadFromStore() {
  if (accountIndex < 0) return;
  const auto* account = ANKI_STORE.getAccount(static_cast<size_t>(accountIndex));
  if (account) editAccount = *account;
}

bool AnkiAccountActivity::handleCustomInput() {
  return optionPopup.handleInput(mappedInput, [this] { requestUpdate(); });
}

void AnkiAccountActivity::activateIndex(const int index) {
  nav.selected = index;
  // Activation opens a keyboard/picker or leaves the screen; a lingering
  // flash would gray an unrelated row.
  app.clearTapFlash();
  handleSelection();
}

bool AnkiAccountActivity::saveAccount() {
  bool success = false;

  if (isNewAccount) {
    // Create flow: the first save inserts a new record (and assigns its id).
    success = ANKI_STORE.addAccount(editAccount);
    if (success) {
      // Subsequent field edits update in place rather than creating duplicates.
      isNewAccount = false;
      accountIndex = static_cast<int>(ANKI_STORE.getCount()) - 1;
    } else {
      LOG_ERR("ANKI", "Failed to add account");
    }
  } else {
    success = ANKI_STORE.updateAccount(static_cast<size_t>(accountIndex), editAccount);
    if (!success) {
      LOG_ERR("ANKI", "Failed to update account at index %d", accountIndex);
    }
  }
  // The store normalises the URL and defaults the model on save.
  if (success) reloadFromStore();

  showSaveError = !success;
  if (showSaveError) {
    requestUpdate();
  }

  return success;
}

void AnkiAccountActivity::editText(const StrId titleId, std::string AnkiAccount::* field, const size_t maxLength,
                                   const InputType type) {
  const std::string& current = editAccount.*field;
  const std::string prefill = (type == InputType::Url && current.empty()) ? "https://" : current;
  auto handler = [this, field, type](const ActivityResult& result) {
    if (result.isCancelled) return;
    const auto& kb = std::get<KeyboardResult>(result.data);
    const bool bareScheme = type == InputType::Url && (kb.text == "https://" || kb.text == "http://");
    editAccount.*field = bareScheme ? "" : kb.text;
    saveAccount();
    requestUpdate();
  };
  auto keyboard =
      makeUniqueNoThrow<KeyboardEntryActivity>(renderer, mappedInput, I18N.get(titleId), prefill, maxLength, type);
  if (!keyboard) {
    LOG_ERR("ANKI", "OOM: keyboard");
    return;
  }
  startActivityForResult(std::move(keyboard), handler);
}

void AnkiAccountActivity::openDeckPicker() {
  // The picker reads and writes the stored account (it needs the id for the
  // deck cache), so a new account is saved first.
  if (isNewAccount && !saveAccount()) return;
  auto picker = makeUniqueNoThrow<AnkiDeckSelectActivity>(renderer, mappedInput, accountIndex);
  if (!picker) {
    LOG_ERR("ANKI", "OOM: deck picker");
    return;
  }
  startActivityForResult(std::move(picker), [this](const ActivityResult&) {
    reloadFromStore();
    requestUpdate();
  });
}

void AnkiAccountActivity::saveDefaultDeck(const std::string& deck) {
  if (!ANKI_STORE.setLastDeck(static_cast<size_t>(accountIndex), deck)) {
    LOG_ERR("ANKI", "Failed to save default deck for account %d", accountIndex);
    showSaveError = true;
  }
  reloadFromStore();
  requestUpdate();
}

void AnkiAccountActivity::openDefaultDeckPicker() {
  // setLastDeck() addresses the stored account, so a new one is saved first.
  if (isNewAccount && !saveAccount()) return;
  if (!AnkiDeckSelectActivity::hasCachedDecks(editAccount.id)) {
    // Nothing cached yet (no sync so far): let the user type the deck name.
    auto keyboard = makeUniqueNoThrow<KeyboardEntryActivity>(renderer, mappedInput, tr(STR_ANKI_DEFAULT_DECK),
                                                             editAccount.lastDeck, 100, InputType::Text);
    if (!keyboard) {
      LOG_ERR("ANKI", "OOM: keyboard");
      return;
    }
    startActivityForResult(std::move(keyboard), [this](const ActivityResult& result) {
      if (result.isCancelled) return;
      saveDefaultDeck(std::get<KeyboardResult>(result.data).text);
    });
    return;
  }
  auto picker = makeUniqueNoThrow<AnkiDeckSelectActivity>(renderer, mappedInput, accountIndex,
                                                          AnkiDeckSelectActivity::Mode::Single);
  if (!picker) {
    LOG_ERR("ANKI", "OOM: deck picker");
    return;
  }
  startActivityForResult(std::move(picker), [this](const ActivityResult& result) {
    if (result.isCancelled || !std::holds_alternative<KeyboardResult>(result.data)) return;
    saveDefaultDeck(std::get<KeyboardResult>(result.data).text);
  });
}

void AnkiAccountActivity::handleSelection() {
  // Each field edit is saved immediately so partially configured accounts
  // survive navigation and power loss.
  switch (nav.selected) {
    case ROW_NAME:
      editText(StrId::STR_ANKI_ACCOUNT_NAME, &AnkiAccount::name, 63, InputType::Text);
      break;
    case ROW_URL:
      editText(StrId::STR_ANKI_SERVER_URL, &AnkiAccount::url, 127, InputType::Url);
      break;
    case ROW_PROFILE:
      editText(StrId::STR_ANKI_PROFILE, &AnkiAccount::profile, 63, InputType::Text);
      break;
    case ROW_TOKEN:
      // Fallback path; the web Settings page is the intended way to paste it.
      editText(StrId::STR_ANKI_TOKEN, &AnkiAccount::token, 200, InputType::Text);
      break;
    case ROW_ENABLED:
      editAccount.enabled = !editAccount.enabled;
      saveAccount();
      requestUpdate();
      break;
    case ROW_DECKS:
      openDeckPicker();
      break;
    case ROW_DEFAULT_DECK:
      openDefaultDeckPicker();
      break;
    case ROW_MODEL:
      editText(StrId::STR_ANKI_NOTE_TYPE, &AnkiAccount::model, 63, InputType::Text);
      break;
    case ROW_CACHE_SIZE: {
      constexpr int count = static_cast<int>(sizeof(CACHE_SIZE_VALUES) / sizeof(CACHE_SIZE_VALUES[0]));
      optionPopup.show(tr(STR_ANKI_CACHE_SIZE), CACHE_SIZE_LABELS, count,
                       indexOfValue(CACHE_SIZE_VALUES, count, editAccount.cacheSize), [this](int idx) {
                         editAccount.cacheSize = CACHE_SIZE_VALUES[idx];
                         saveAccount();
                       });
      requestUpdate();
      break;
    }
    case ROW_DELETE:
      // Delete is only offered for existing accounts.
      if (isNewAccount) break;
      if (!ANKI_STORE.removeAccount(static_cast<size_t>(accountIndex))) {
        LOG_ERR("ANKI", "Failed to remove account at index %d", accountIndex);
        showSaveError = true;
        requestUpdate();
        return;
      }
      finish();
      break;
    default:
      break;
  }
}

void AnkiAccountActivity::buildScreen(UiScreen& screen) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  // Content below the GUI.drawHeader band, above the button hints; derived
  // from the safe area so board bezel insets apply (same as OpdsSettings).
  const Rect safe = UITheme::getInstance().getScreenSafeArea(renderer, true, false);
  screen.setContentMarginFromScreen(
      fui::Insets{static_cast<int16_t>(safe.y + metrics.topPadding + metrics.headerHeight),
                  static_cast<int16_t>(renderer.getScreenWidth() - (safe.x + safe.width)),
                  static_cast<int16_t>(renderer.getScreenHeight() - (safe.y + safe.height) + metrics.buttonHintsHeight),
                  static_cast<int16_t>(safe.x)});

  // Token hint where the old sub-header band sat. Two lines: the sentence
  // does not fit one row on a 480 px panel in most languages.
  fui::TextStyle hintStyle = screen.theme().smallText;
  hintStyle.maxLines = 2;
  const fui::Rect band = screen.takeTop(static_cast<int16_t>(metrics.tabBarHeight * 2));
  const int16_t pad = screen.theme().headerSidePadding;
  screen.target().text(band.inset(fui::Insets{0, pad, 0, pad}), tr(STR_ANKI_WEB_HINT), hintStyle);
  screen.spacer(static_cast<int16_t>(metrics.verticalSpacing));

  // Labels/actionValue were set once in the constructor; only the live
  // values (pointers into editAccount or the fixed number buffers) refresh.
  fieldRowItems[ROW_NAME].value = editAccount.name.empty() ? tr(STR_NOT_SET) : editAccount.name.c_str();
  fieldRowItems[ROW_URL].value = editAccount.url.empty() ? tr(STR_NOT_SET) : editAccount.url.c_str();
  fieldRowItems[ROW_PROFILE].value = editAccount.profile.empty() ? tr(STR_NOT_SET) : editAccount.profile.c_str();
  fieldRowItems[ROW_TOKEN].value = editAccount.token.empty() ? tr(STR_NOT_SET) : "******";
  fieldRowItems[ROW_ENABLED].toggleChecked = editAccount.enabled;
  if (editAccount.decks.empty()) {
    fieldRowItems[ROW_DECKS].value = tr(STR_ANKI_ALL_DECKS);
  } else {
    snprintf(deckCountBuf, sizeof(deckCountBuf), "%u", static_cast<unsigned>(editAccount.decks.size()));
    fieldRowItems[ROW_DECKS].value = deckCountBuf;
  }
  fieldRowItems[ROW_DEFAULT_DECK].value = editAccount.lastDeck.empty() ? tr(STR_NOT_SET) : editAccount.lastDeck.c_str();
  fieldRowItems[ROW_MODEL].value = editAccount.model.empty() ? tr(STR_NOT_SET) : editAccount.model.c_str();
  snprintf(cacheSizeBuf, sizeof(cacheSizeBuf), "%u", static_cast<unsigned>(editAccount.cacheSize));
  fieldRowItems[ROW_CACHE_SIZE].value = cacheSizeBuf;

  fui::ListProps props;
  props.items = fieldRowItems;
  props.count = static_cast<uint16_t>(getMenuItemCount());
  props.action = ACTION_ROW;
  props.inputMask = fui::InputTouch;  // physical buttons stay in loop()
  props.valueInset = 8;               // air between the value and the row edge
  // Label at the value's font size: both sides of the row read as one unit.
  // maxLines=2 also marks the style caller-owned (see textStyleUnset).
  props.labelText = screen.theme().smallText;
  props.labelText.maxLines = 2;
  syncListViewport(screen, props);
  screen.list(props);
}

const char* AnkiAccountActivity::headerTitle() const {
  return isNewAccount ? tr(STR_ANKI_ADD_ACCOUNT) : tr(STR_ANKI_ACCOUNT);
}

void AnkiAccountActivity::render(RenderLock&& lock) {
  if (optionPopup.processRender(renderer, mappedInput)) return;
  UiListActivity::render(std::move(lock));
}

void AnkiAccountActivity::drawFooter() {
  UiListActivity::drawFooter();
  if (showSaveError) {
    GUI.drawPopup(renderer, tr(STR_ERROR_GENERAL_FAILURE));
  }
}
