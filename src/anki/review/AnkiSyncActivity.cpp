#include "AnkiSyncActivity.h"

#include <AnkiSyncEngine.h>
#include <GfxRenderer.h>
#include <I18n.h>
#include <Logging.h>
#include <Memory.h>
#include <WiFi.h>

#include <cstdio>

#include "SilentRestart.h"
#include "activities/network/WifiSelectionActivity.h"
#include "anki/AnkiAccountStore.h"
#include "anki/AnkiDevice.h"
#include "anki/AnkiSecureHttp.h"
#include "anki/AnkiStorageFs.h"
#include "components/UITheme.h"
#include "fontIds.h"

void AnkiSyncActivity::addDetail(const std::string& line) {
  if (detailCount < MAX_DETAIL_LINES) details[detailCount++] = line;
}

void AnkiSyncActivity::onEnter() {
  Activity::onEnter();
  wifiActivated = true;
  if (WiFi.status() == WL_CONNECTED) {
    onWifiSelectionComplete(true);
    return;
  }
  auto wifi = makeUniqueNoThrow<WifiSelectionActivity>(renderer, mappedInput);
  if (!wifi) {
    LOG_ERR("ANKI", "OOM: wifi selection activity");
    onWifiSelectionComplete(false);
    return;
  }
  startActivityForResult(std::move(wifi),
                         [this](const ActivityResult& result) { onWifiSelectionComplete(!result.isCancelled); });
}

void AnkiSyncActivity::onExit() {
  Activity::onExit();
  if (!wifiActivated) return;
  WiFi.disconnect(false);
  delay(30);
  if (returnTo == ReturnTo::ReviewApp) {
    silentRestartToAnki();
  } else {
    silentRestart();
  }
}

void AnkiSyncActivity::onWifiSelectionComplete(const bool success) {
  if (!success) {
    {
      RenderLock lock;
      state = FAILED;
      title = tr(STR_ANKI_SYNC_FAILED);
      addDetail(tr(STR_WIFI_CONN_FAILED));
    }
    requestUpdate();
    return;
  }

  WiFi.setSleep(false);
  {
    RenderLock lock;
    state = SYNCING;
  }
  // Paint the "Syncing" screen before the blocking exchange starts.
  requestUpdateAndWait();
  performSync();
}

void AnkiSyncActivity::performSync() {
  AnkiSyncEngine engine(AnkiStorageFs::instance(), AnkiSecureHttp::instance(), ankidevice::nowEpoch);
  const auto& accounts = ANKI_STORE.getAccounts();
  const int reviewIdx = ANKI_STORE.getReviewAccountIndex();

  size_t reviews = 0;
  size_t notes = 0;
  size_t cards = 0;
  size_t synced = 0;
  bool failed = false;
  bool authFailed = false;
  std::string firstError;
  std::string syncError;

  for (size_t i = 0; i < accounts.size(); i++) {
    const AnkiAccount& account = accounts[i];
    if (!account.enabled) continue;
    const bool wantCards = static_cast<int>(i) == reviewIdx;
    if (!wantCards && !engine.hasPending(account)) continue;

    LOG_DBG("ANKI", "Syncing account %s (cards=%d)", account.name.c_str(), wantCards ? 1 : 0);
    const AnkiSyncEngine::Result r = engine.syncAccount(account, wantCards);
    synced++;
    reviews += r.reviewsAcked;
    notes += r.notesAcked;
    cards += r.cardsFetched;
    if (!r.ok) {
      failed = true;
      authFailed = authFailed || r.authFailed;
      if (firstError.empty()) firstError = account.name + ": " + r.error;
      LOG_ERR("ANKI", "Sync failed for %s: %s (HTTP %d)", account.name.c_str(), r.error.c_str(), r.httpStatus);
    } else if (syncError.empty() && !r.syncError.empty()) {
      syncError = r.syncError;
    }
  }

  {
    RenderLock lock;
    detailCount = 0;
    if (synced == 0) {
      state = FAILED;
      title = tr(STR_ANKI_SYNC_FAILED);
      addDetail(tr(STR_ANKI_NO_ENABLED_ACCOUNTS));
    } else {
      state = failed ? FAILED : DONE;
      title = failed ? tr(STR_ANKI_SYNC_FAILED) : tr(STR_ANKI_SYNC_DONE);
      if (authFailed) {
        addDetail(tr(STR_ANKI_AUTH_FAILED));
      } else if (failed) {
        addDetail(firstError);
      }
      char buf[64];
      snprintf(buf, sizeof(buf), tr(STR_ANKI_REVIEWS_SENT), static_cast<int>(reviews));
      addDetail(buf);
      snprintf(buf, sizeof(buf), tr(STR_ANKI_NOTES_SENT), static_cast<int>(notes));
      addDetail(buf);
      snprintf(buf, sizeof(buf), tr(STR_ANKI_CARDS_FETCHED), static_cast<int>(cards));
      addDetail(buf);
      // AnkiDo's inline AnkiWeb sync failing is informational: the queue still came back.
      if (!failed && !syncError.empty()) addDetail(syncError);
    }
  }
  requestUpdate();
}

void AnkiSyncActivity::loop() {
  if (state != DONE && state != FAILED) return;
  int x = 0;
  int y = 0;
  // On release, not press: the review app underneath acts on Back releases.
  if (mappedInput.wasReleased(MappedInputManager::Button::Back) ||
      mappedInput.wasReleased(MappedInputManager::Button::Confirm) || mappedInput.wasScreenTapped(x, y)) {
    finish();
  }
}

void AnkiSyncActivity::render(RenderLock&&) {
  renderer.clearScreen();

  const auto& metrics = UITheme::getInstance().getMetrics();
  const int pageWidth = renderer.getScreenWidth();
  const int pageHeight = renderer.getScreenHeight();
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, tr(STR_ANKI_SYNC));

  const int lineHeight = renderer.getLineHeight(UI_10_FONT_ID);
  const int lineStep = lineHeight + 6;
  if (state == CONNECTING || state == SYNCING) {
    renderer.drawCenteredText(UI_10_FONT_ID, (pageHeight - lineHeight) / 2, tr(STR_ANKI_SYNCING));
  } else {
    const int blockHeight = lineStep * static_cast<int>(1 + detailCount);
    int y = (pageHeight - blockHeight) / 2;
    renderer.drawCenteredText(UI_10_FONT_ID, y, title.c_str(), true, EpdFontFamily::BOLD);
    for (size_t i = 0; i < detailCount; i++) {
      y += lineStep;
      renderer.drawCenteredText(UI_10_FONT_ID, y, details[i].c_str());
    }
  }

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), "", "", "");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer();
}
