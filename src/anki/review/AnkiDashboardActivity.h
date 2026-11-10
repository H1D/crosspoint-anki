#pragma once

#include <AnkiStats.h>

#include <cstdint>
#include <string>

#include "activities/Activity.h"
#include "components/UITheme.h"

// Start screen of the Anki app for one account: cards to review, today's
// reviews, the day streak and a heatmap of reviews per day. Confirm reviews,
// Right syncs, Left switches account, Back leaves after the exit sync.
// Reads only (ankistats::loadStudy); every SD write happens in the screens it
// opens. Stays alive under the review screen, so Back from review redraws it
// at once with the new grades counted.
class AnkiDashboardActivity final : public Activity {
 public:
  // askAccount: with several enabled accounts, ask which one first (opening
  // from Home). False after a sync's restart: stay on the review account.
  AnkiDashboardActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, bool askAccount)
      : Activity("AnkiDashboard", renderer, mappedInput), askAccount(askAccount) {}

  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  enum class Action : uint8_t { None, Review, Sync, Account };
  static constexpr int MAX_ACTIONS = 3;

  // Touch-board buttons along the bottom, left to right.
  struct ActionBar {
    Action actions[MAX_ACTIONS] = {};
    Rect rects[MAX_ACTIONS];
    int count = 0;
  };

  void pickAccount();
  void refresh();
  void run(Action action);
  void startReview();
  void startSync();
  void leave();

  Rect safeArea() const;
  ActionBar actionBar() const;
  void drawTiles(int x, int y, int width) const;
  int heatmapHeight() const;
  void drawHeatmap(int x, int y, int width) const;
  void drawStatus(int x, int y, int width) const;
  void drawActionBar(const ActionBar& bar) const;

  const bool askAccount;
  bool hasAccount = false;
  bool severalAccounts = false;
  uint32_t accountId = 0;
  std::string accountName;
  ankistats::Study study;  // about 400 B, part of this heap-allocated activity
};
