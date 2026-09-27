#pragma once

#include <string>

#include "activities/Activity.h"

// Connect-then-sync screen for the Anki accounts: joins Wi-Fi (auto-connect
// to the saved network, else the picker), runs one AnkiSyncEngine session per
// enabled account and shows a summary. Like every Wi-Fi session in
// CrossPoint it ends in a silent restart, back into the review app or home.
class AnkiSyncActivity final : public Activity {
 public:
  enum class ReturnTo { ReviewApp, Home };

  explicit AnkiSyncActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, ReturnTo returnTo)
      : Activity("AnkiSync", renderer, mappedInput), returnTo(returnTo) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
  bool preventAutoSleep() override { return state == CONNECTING || state == SYNCING; }

 private:
  enum State { CONNECTING, SYNCING, DONE, FAILED };
  static constexpr size_t MAX_DETAIL_LINES = 4;

  const ReturnTo returnTo;
  State state = CONNECTING;
  bool wifiActivated = false;
  // Result screen: a bold title plus up to four detail lines.
  std::string title;
  std::string details[MAX_DETAIL_LINES];
  size_t detailCount = 0;

  void addDetail(const std::string& line);
  void onWifiSelectionComplete(bool success);
  void performSync();
};
