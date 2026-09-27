#pragma once

#include <AnkiTypes.h>

#include "activities/UiListActivity.h"
#include "components/OptionPopup.h"

enum class InputType;  // activities/util/KeyboardEntryActivity.h

/**
 * Edit screen for a single AnkiDo account. Every field edit is persisted
 * immediately through ANKI_STORE (add on the first save of a new account,
 * update in place afterwards), like OpdsSettingsActivity.
 */
class AnkiAccountActivity final : public UiListActivity {
 public:
  /**
   * @param accountIndex Index into AnkiAccountStore, or -1 for a new account
   */
  explicit AnkiAccountActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, int accountIndex = -1);

  void onEnter() override;
  void render(RenderLock&&) override;

 private:
  int accountIndex;
  AnkiAccount editAccount;
  bool isNewAccount = false;
  bool showSaveError = false;
  OptionPopup optionPopup;

  int listCount() const override { return getMenuItemCount(); }
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  // Popup input goes first; while it is open it consumes the pass.
  bool handleCustomInput() override;
  const char* headerTitle() const override;
  void drawFooter() override;

  int getMenuItemCount() const;
  void handleSelection();
  bool saveAccount();
  void reloadFromStore();
  void editText(StrId titleId, std::string AnkiAccount::* field, size_t maxLength, InputType type);
  void openDeckPicker();
  void openDefaultDeckPicker();
  void saveDefaultDeck(const std::string& deck);

  // Row storage: Name, URL, Profile, Token, Enabled, Decks, Deck for new words, Note type,
  // Cards per sync + Delete (existing accounts only). Labels are
  // set once in the constructor; buildScreen() only refreshes the values.
  static constexpr int BASE_ITEMS = 9;
  static constexpr int MAX_MENU_ITEMS = BASE_ITEMS + 1;
  freeink::ui::ListItem fieldRowItems[MAX_MENU_ITEMS]{};
  // Numeric values rendered into fixed buffers (no per-render allocation).
  char deckCountBuf[8]{};
  char cacheSizeBuf[8]{};
};
