#include "AnkiDashboardActivity.h"

#include <AnkiSyncEngine.h>
#include <GfxRenderer.h>
#include <I18n.h>
#include <Logging.h>
#include <Memory.h>

#include <algorithm>
#include <climits>
#include <cstdio>
#include <cstring>
#include <vector>

#include "AnkiAccountPickerActivity.h"
#include "AnkiReviewActivity.h"
#include "AnkiSyncActivity.h"
#include "anki/AnkiAccountStore.h"
#include "anki/AnkiDevice.h"
#include "anki/AnkiSecureHttp.h"
#include "anki/AnkiStorageFs.h"
#include "fontIds.h"

namespace {

// Heatmap cells: 14 px squares on a 16 px pitch in every orientation; the
// number of weeks follows the width.
constexpr int CELL = 14;
constexpr int PITCH = 16;
constexpr int MAX_WEEKS = 53;
constexpr int SIDE_PADDING = 20;
constexpr int LABEL_GAP = 6;
constexpr int BUTTON_GAP = 8;

// The n-th space-separated word of a translated list ("Jan Feb ..."). False
// when the list is shorter.
bool nthWord(const char* list, int n, char* out, const size_t outSize) {
  const char* p = list;
  for (;;) {
    while (*p == ' ') p++;
    if (*p == '\0') return false;
    const char* end = p;
    while (*end != '\0' && *end != ' ') end++;
    if (n == 0) {
      const size_t len = std::min(static_cast<size_t>(end - p), outSize - 1);
      memcpy(out, p, len);
      out[len] = '\0';
      return true;
    }
    n--;
    p = end;
  }
}

int monthOf(const AnkiDay d) {
  int y = 0;
  int m = 0;
  int day = 0;
  ankistats::toCivil(d, y, m, day);
  return m;
}

void monthLabel(const int month, char* out, const size_t outSize) {
  if (!nthWord(tr(STR_ANKI_MONTHS), month - 1, out, outSize)) snprintf(out, outSize, "%d", month);
}

}  // namespace

// Opening from Home with several enabled accounts asks which one to review
// (the answer becomes the review account, which exit and sleep syncs
// refresh). One enabled account, or a restart after a sync, opens directly.
void AnkiDashboardActivity::onEnter() {
  Activity::onEnter();
  ANKI_STORE.loadFromFile();
  const auto& accounts = ANKI_STORE.getAccounts();
  const auto enabled = std::count_if(accounts.begin(), accounts.end(), [](const AnkiAccount& a) { return a.enabled; });
  severalAccounts = enabled > 1;
  const AnkiAccount* review = ANKI_STORE.getReviewAccount();
  const bool reviewEnabled = review != nullptr && review->enabled;
  if (severalAccounts && (askAccount || !reviewEnabled)) {
    pickAccount();
    return;
  }
  if (enabled == 1 && !reviewEnabled) {
    const auto only = std::find_if(accounts.begin(), accounts.end(), [](const AnkiAccount& a) { return a.enabled; });
    ANKI_STORE.setReviewAccount(static_cast<size_t>(only - accounts.begin()));
  }
  refresh();
  requestUpdate();
}

void AnkiDashboardActivity::pickAccount() {
  auto picker = makeUniqueNoThrow<AnkiAccountPickerActivity>(renderer, mappedInput);
  if (!picker) {
    LOG_ERR("ANKI", "OOM: account picker");
    refresh();
    return;
  }
  startActivityForResult(std::move(picker), [this](const ActivityResult& result) {
    // Backing out of the first question leaves the app; of a switch, keeps this account.
    if (result.isCancelled && !hasAccount) {
      activityManager.goHome(HomeMenuItem::ANKI);
      return;
    }
    refresh();
  });
}

// Loads the review account and its numbers. Loop task; takes the render lock.
void AnkiDashboardActivity::refresh() {
  RenderLock lock;
  const AnkiAccount* account = ANKI_STORE.getReviewAccount();
  hasAccount = account != nullptr && account->enabled;
  if (!hasAccount) return;
  accountId = account->id;
  accountName = account->name;
  ankistats::loadStudy(AnkiStorageFs::instance(), accountId, ankidevice::wallEpoch(), study);
  LOG_DBG("ANKI", "Dashboard: %u to review, %u today, streak %u, %u pending", study.toReview, study.reviewedToday,
          study.streak, study.pending);
}

void AnkiDashboardActivity::run(const Action action) {
  switch (action) {
    case Action::Review:
      startReview();
      break;
    case Action::Sync:
      startSync();
      break;
    case Action::Account:
      pickAccount();
      break;
    case Action::None:
      break;
  }
}

void AnkiDashboardActivity::startReview() {
  auto review = makeUniqueNoThrow<AnkiReviewActivity>(renderer, mappedInput, accountId);
  if (!review) {
    LOG_ERR("ANKI", "OOM: review activity");
    return;
  }
  startActivityForResult(std::move(review), [this](const ActivityResult&) { refresh(); });
}

void AnkiDashboardActivity::startSync() {
  auto sync = makeUniqueNoThrow<AnkiSyncActivity>(renderer, mappedInput, AnkiSyncActivity::ReturnTo::ReviewApp);
  if (!sync) {
    LOG_ERR("ANKI", "OOM: sync activity");
    return;
  }
  // On the device the sync screen restarts back onto this dashboard; the
  // callback only runs where that restart is a no-op (simulator).
  startActivityForResult(std::move(sync), [this](const ActivityResult&) { refresh(); });
}

// Back leaves the app. Pending reviews or notes go out first through the
// sync screen, which restarts to Home afterwards.
void AnkiDashboardActivity::leave() {
  bool pending = false;
  {
    AnkiSyncEngine engine(AnkiStorageFs::instance(), AnkiSecureHttp::instance(), ankidevice::wallEpoch);
    const auto& accounts = ANKI_STORE.getAccounts();
    pending = std::any_of(accounts.begin(), accounts.end(), [&engine](const AnkiAccount& account) {
      return account.enabled && engine.hasPending(account);
    });
  }
  if (pending) {
    auto sync = makeUniqueNoThrow<AnkiSyncActivity>(renderer, mappedInput, AnkiSyncActivity::ReturnTo::Home);
    if (sync) {
      activityManager.replaceActivity(std::move(sync));
      return;
    }
    LOG_ERR("ANKI", "OOM: sync activity");
  }
  activityManager.goHome(HomeMenuItem::ANKI);
}

void AnkiDashboardActivity::loop() {
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    leave();
    return;
  }
  if (!hasAccount) return;

  int tx = 0;
  int ty = 0;
  if (mappedInput.wasScreenTapped(tx, ty)) {
    const ActionBar bar = actionBar();
    for (int i = 0; i < bar.count; i++) {
      const Rect& r = bar.rects[i];
      if (tx >= r.x && tx < r.x + r.width && ty >= r.y && ty < r.y + r.height) {
        run(bar.actions[i]);
        return;
      }
    }
    return;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    run(study.toReview > 0 ? Action::Review : Action::Sync);
  } else if (mappedInput.wasReleased(MappedInputManager::Button::Right)) {
    run(Action::Sync);
  } else if (severalAccounts && mappedInput.wasReleased(MappedInputManager::Button::Left)) {
    run(Action::Account);
  }
}

Rect AnkiDashboardActivity::safeArea() const {
  return UITheme::getInstance().getScreenSafeArea(renderer, !mappedInput.hasTouch(), false);
}

// Touch boards only: the primary action takes half the width, the others
// share the rest. With nothing left to review the primary action syncs.
AnkiDashboardActivity::ActionBar AnkiDashboardActivity::actionBar() const {
  ActionBar bar;
  if (!mappedInput.hasTouch() || !hasAccount) return bar;
  bar.actions[bar.count++] = study.toReview > 0 ? Action::Review : Action::Sync;
  if (study.toReview > 0) bar.actions[bar.count++] = Action::Sync;
  if (severalAccounts) bar.actions[bar.count++] = Action::Account;

  const Rect safe = safeArea();
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int height = renderer.getLineHeight(UI_10_FONT_ID) + 24;
  const int y = safe.y + safe.height - metrics.verticalSpacing - height;
  const int left = safe.x + SIDE_PADDING;
  const int width = safe.width - 2 * SIDE_PADDING;
  const int firstWidth = bar.count == 1 ? width : width / 2;
  const int restWidth = bar.count == 1 ? 0 : (width - firstWidth - (bar.count - 1) * BUTTON_GAP) / (bar.count - 1);
  int x = left;
  for (int i = 0; i < bar.count; i++) {
    const int w = i == 0 ? firstWidth : restWidth;
    bar.rects[i] = Rect{x, y, w, height};
    x += w + BUTTON_GAP;
  }
  return bar;
}

// Three numbers over their labels; only "to review" while no day is known.
void AnkiDashboardActivity::drawTiles(const int x, const int y, const int width) const {
  const int count = study.days == ankistats::Study::Days::None ? 1 : 3;
  const int colWidth = width / count;
  const int valueHeight = renderer.getLineHeight(UI_12_FONT_ID);
  for (int i = 0; i < count; i++) {
    char value[16];
    const char* label = nullptr;
    if (i == 0) {
      snprintf(value, sizeof(value), "%u", static_cast<unsigned>(study.toReview));
      label = tr(STR_ANKI_TO_REVIEW);
    } else if (i == 1) {
      snprintf(value, sizeof(value), "%u", static_cast<unsigned>(study.reviewedToday));
      label = tr(STR_ANKI_TODAY);
    } else {
      snprintf(value, sizeof(value), study.streakCapped ? "%u+" : "%u", static_cast<unsigned>(study.streak));
      label = tr(STR_ANKI_DAY_STREAK);
    }
    const int colX = x + i * colWidth;
    const int valueWidth = renderer.getTextWidth(UI_12_FONT_ID, value, EpdFontFamily::BOLD);
    renderer.drawText(UI_12_FONT_ID, colX + (colWidth - valueWidth) / 2, y, value, true, EpdFontFamily::BOLD);
    const std::string fitted = renderer.truncatedText(SMALL_FONT_ID, label, colWidth - 4);
    const int labelWidth = renderer.getTextWidth(SMALL_FONT_ID, fitted.c_str());
    renderer.drawText(SMALL_FONT_ID, colX + (colWidth - labelWidth) / 2, y + valueHeight + 2, fitted.c_str());
  }
}

// Month row + 7 rows of cells, then the caption while the server's history
// is missing. Without a known day there is only the caption.
int AnkiDashboardActivity::heatmapHeight() const {
  const int small = renderer.getLineHeight(SMALL_FONT_ID);
  const int caption = 2 * small;
  if (study.days == ankistats::Study::Days::None) return caption;
  const int grid = small + 2 + 7 * PITCH - (PITCH - CELL);
  return study.days == ankistats::Study::Days::Server ? grid : grid + LABEL_GAP + caption;
}

// GitHub-style: one column per week, Monday on top, today's column rightmost,
// days after today not drawn. Levels: a dot for none, 25 % and 50 % dither,
// solid black for a typical day or more. An empty today is outlined.
void AnkiDashboardActivity::drawHeatmap(const int x, const int y, const int width) const {
  const int small = renderer.getLineHeight(SMALL_FONT_ID);
  const auto drawCaption = [&](const int top) {
    const std::vector<std::string> lines =
        renderer.wrappedText(SMALL_FONT_ID, tr(STR_ANKI_HISTORY_AFTER_SYNC), width, 2);
    int lineY = top;
    for (const std::string& line : lines) {
      const int w = renderer.getTextWidth(SMALL_FONT_ID, line.c_str());
      renderer.drawText(SMALL_FONT_ID, x + (width - w) / 2, lineY, line.c_str());
      lineY += small;
    }
  };
  if (study.days == ankistats::Study::Days::None) {
    drawCaption(y);
    return;
  }

  static constexpr int LABELED_ROWS[] = {0, 2, 4};
  char word[24];
  int labelWidth = 0;
  for (const int row : LABELED_ROWS) {
    if (nthWord(tr(STR_ANKI_WEEKDAYS), row, word, sizeof(word))) {
      labelWidth = std::max(labelWidth, renderer.getTextWidth(SMALL_FONT_ID, word));
    }
  }
  const int weeks = std::clamp((width - labelWidth - LABEL_GAP + (PITCH - CELL)) / PITCH, 1, MAX_WEEKS);
  const int gridWidth = weeks * PITCH - (PITCH - CELL);
  const int left = x + (width - labelWidth - LABEL_GAP - gridWidth) / 2;
  const int gridX = left + labelWidth + LABEL_GAP;
  const int gridY = y + small + 2;
  const AnkiDay today = study.today;
  const AnkiDay firstMonday = today - ankistats::weekday(today) - 7 * (weeks - 1);

  for (const int row : LABELED_ROWS) {
    if (nthWord(tr(STR_ANKI_WEEKDAYS), row, word, sizeof(word))) {
      renderer.drawText(SMALL_FONT_ID, left, gridY + row * PITCH + (CELL - small) / 2, word);
    }
  }

  // A month's name sits over the week its first Monday is in. A label that
  // would run into the previous one is skipped, and so is the first
  // column's when the next month starts right after it.
  int labelEnd = INT_MIN;
  for (int c = 0; c < weeks; c++) {
    const int month = monthOf(firstMonday + 7 * c);
    if (c > 0 && month == monthOf(firstMonday + 7 * (c - 1))) continue;
    monthLabel(month, word, sizeof(word));
    const int labelX = gridX + c * PITCH;
    const int end = labelX + renderer.getTextWidth(SMALL_FONT_ID, word);
    if (labelX < labelEnd + LABEL_GAP) continue;
    if (c == 0) {
      int next = 1;
      while (next < weeks && monthOf(firstMonday + 7 * next) == month) next++;
      if (next < weeks && gridX + next * PITCH < end + LABEL_GAP) continue;
    }
    renderer.drawText(SMALL_FONT_ID, labelX, y, word);
    labelEnd = end;
  }

  for (int c = 0; c < weeks; c++) {
    for (int r = 0; r < 7; r++) {
      const AnkiDay d = firstMonday + 7 * c + r;
      if (d > today) break;
      const int cx = gridX + c * PITCH;
      const int cy = gridY + r * PITCH;
      switch (study.levelOn(d)) {
        case 0:
          if (d == today) {
            renderer.drawRect(cx, cy, CELL, CELL);
          } else {
            renderer.fillRect(cx + CELL / 2 - 1, cy + CELL / 2 - 1, 2, 2);
          }
          break;
        case 1:
          renderer.fillRectDither(cx, cy, CELL, CELL, Color::LightGray);
          break;
        case 2:
          renderer.fillRectDither(cx, cy, CELL, CELL, Color::DarkGray);
          break;
        default:
          renderer.fillRect(cx, cy, CELL, CELL);
          break;
      }
    }
  }

  if (study.days == ankistats::Study::Days::ThisDeviceOnly) drawCaption(gridY + 7 * PITCH + LABEL_GAP);
}

// "Synced N days ago" left (only when the clock is known), "N pending" right.
void AnkiDashboardActivity::drawStatus(const int x, const int y, const int width) const {
  char text[48];
  if (study.syncedDaysAgo == 0) {
    snprintf(text, sizeof(text), "%s", tr(STR_ANKI_SYNCED_TODAY));
  } else if (study.syncedDaysAgo == 1) {
    snprintf(text, sizeof(text), "%s", tr(STR_ANKI_SYNCED_YESTERDAY));
  } else if (study.syncedDaysAgo > 1) {
    snprintf(text, sizeof(text), tr(STR_ANKI_SYNCED_DAYS_AGO), static_cast<int>(study.syncedDaysAgo));
  } else {
    text[0] = '\0';
  }
  if (text[0] != '\0') renderer.drawText(SMALL_FONT_ID, x, y, text);
  if (study.pending > 0) {
    snprintf(text, sizeof(text), tr(STR_ANKI_PENDING), static_cast<int>(study.pending));
    renderer.drawText(SMALL_FONT_ID, x + width - renderer.getTextWidth(SMALL_FONT_ID, text), y, text);
  }
}

void AnkiDashboardActivity::drawActionBar(const ActionBar& bar) const {
  for (int i = 0; i < bar.count; i++) {
    const Rect& r = bar.rects[i];
    const char* label = bar.actions[i] == Action::Review    ? tr(STR_ANKI_REVIEW)
                        : bar.actions[i] == Action::Account ? tr(STR_ANKI_ACCOUNT)
                                                            : tr(STR_ANKI_SYNC);
    const bool primary = i == 0;
    if (primary) {
      renderer.fillRect(r.x, r.y, r.width, r.height);
    } else {
      renderer.drawRect(r.x, r.y, r.width, r.height, 2, true);
    }
    const std::string fitted = renderer.truncatedText(UI_10_FONT_ID, label, r.width - 8, EpdFontFamily::BOLD);
    const int w = renderer.getTextWidth(UI_10_FONT_ID, fitted.c_str(), EpdFontFamily::BOLD);
    const int textY = r.y + (r.height - renderer.getLineHeight(UI_10_FONT_ID)) / 2;
    renderer.drawText(UI_10_FONT_ID, r.x + (r.width - w) / 2, textY, fitted.c_str(), !primary, EpdFontFamily::BOLD);
  }
}

void AnkiDashboardActivity::render(RenderLock&&) {
  renderer.clearScreen();
  const auto& metrics = UITheme::getInstance().getMetrics();
  const Rect safe = safeArea();
  const int headerY = safe.y + metrics.topPadding;
  GUI.drawHeader(renderer, Rect{safe.x, headerY, safe.width, metrics.headerHeight}, tr(STR_ANKI),
                 hasAccount && !accountName.empty() ? accountName.c_str() : nullptr);

  if (!hasAccount) {
    const int lineHeight = renderer.getLineHeight(UI_10_FONT_ID);
    const std::vector<std::string> lines = renderer.wrappedText(UI_10_FONT_ID, tr(STR_ANKI_NO_ENABLED_ACCOUNTS),
                                                                safe.width - 2 * metrics.contentSidePadding, 4);
    int y = safe.y + (safe.height - lineHeight * static_cast<int>(lines.size())) / 2;
    for (const std::string& line : lines) {
      const int w = renderer.getTextWidth(UI_10_FONT_ID, line.c_str());
      renderer.drawText(UI_10_FONT_ID, safe.x + (safe.width - w) / 2, y, line.c_str());
      y += lineHeight;
    }
    const auto labels = mappedInput.mapLabels(tr(STR_BACK), "", "", "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
    renderer.displayBuffer();
    return;
  }

  // Tiles, heatmap and status row, centred as one block between the header
  // and the action bar (or the bottom of the safe area).
  const ActionBar bar = actionBar();
  const int top = headerY + metrics.headerHeight;
  const int bottom = bar.count > 0 ? bar.rects[0].y : safe.y + safe.height;
  const int x = safe.x + SIDE_PADDING;
  const int width = safe.width - 2 * SIDE_PADDING;
  const int tilesHeight = renderer.getLineHeight(UI_12_FONT_ID) + 2 + renderer.getLineHeight(SMALL_FONT_ID);
  const int mapHeight = heatmapHeight();
  const int statusHeight = renderer.getLineHeight(SMALL_FONT_ID);
  const int spare = std::max(0, bottom - top - tilesHeight - mapHeight - statusHeight);
  const int gap = std::min(spare / 2, std::clamp(spare / 4, metrics.verticalSpacing, 3 * metrics.verticalSpacing));
  int y = top + (spare - 2 * gap) / 2;
  drawTiles(x, y, width);
  y += tilesHeight + gap;
  drawHeatmap(x, y, width);
  y += mapHeight + gap;
  drawStatus(x, y, width);
  drawActionBar(bar);

  // Physical Left/Right are fixed; mapLabels swaps previous/next where the
  // nav direction is swapped, so hand it the labels pre-swapped.
  const char* primary = study.toReview > 0 ? tr(STR_ANKI_REVIEW) : tr(STR_ANKI_SYNC);
  const char* leftLabel = severalAccounts ? tr(STR_ANKI_ACCOUNT) : "";
  const char* rightLabel = tr(STR_ANKI_SYNC);
  const bool swapped = mappedInput.isNavDirectionSwapped();
  const auto labels =
      mappedInput.mapLabels(tr(STR_BACK), primary, swapped ? rightLabel : leftLabel, swapped ? leftLabel : rightLabel);
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer();
}
