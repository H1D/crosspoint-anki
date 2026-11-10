#include <gtest/gtest.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <memory>
#include <random>
#include <string>
#include <vector>

#include "AnkiCardCache.h"
#include "AnkiJournal.h"
#include "AnkiPaths.h"
#include "AnkiStats.h"
#include "MemFs.h"
#include "ScopedTz.h"

using ankistats::Anchor;
using ankistats::History;
using ankistats::StatsParser;
using ankistats::Study;

namespace {

AnkiDay day(const char* iso) { return ankistats::parseIso(iso); }

std::string iso(const AnkiDay d) {
  int y = 0;
  int m = 0;
  int dd = 0;
  ankistats::toCivil(d, y, m, dd);
  char buf[16];
  snprintf(buf, sizeof(buf), "%04d-%02d-%02d", y, m, dd);
  return buf;
}

constexpr int64_t NEXT_DAY_AT = 1794369600;        // 2026-11-11 04:00 UTC
constexpr int64_t NOON = NEXT_DAY_AT - 16 * 3600;  // 2026-11-10 12:00 UTC
constexpr int64_t SECONDS_PER_DAY = 86400;

// A stats fetch on 2026-11-10 (UTC, rollover 04:00); counts[i] is today - i.
std::unique_ptr<History> historyWith(std::initializer_list<uint16_t> counts) {
  auto h = std::make_unique<History>();
  h->anchor = {day("2026-11-10"), NEXT_DAY_AT};
  std::copy(counts.begin(), counts.end(), h->counts);
  return h;
}

AnkiJournal::Entry graded(const int64_t cardId, const int64_t answeredAt) {
  return {"dev-" + std::to_string(cardId), cardId, 3, answeredAt};
}

}  // namespace

TEST(AnkiStats, CivilDays) {
  EXPECT_EQ(day("2026-11-10"), 20767);
  EXPECT_EQ(day("1970-01-01"), 0);
  EXPECT_EQ(ankistats::weekday(day("2026-11-10")), 1);  // Tuesday
  EXPECT_EQ(ankistats::weekday(0), 3);                  // Thursday
  EXPECT_EQ(ankistats::weekday(-1), 2);
  EXPECT_EQ(day("2000-03-01") - day("2000-02-28"), 2);
  EXPECT_EQ(day("2100-03-01") - day("2100-02-28"), 1);
  for (const char* bad :
       {"2100-02-29", "2026-02-30", "2026-13-01", "2026-00-10", "2026-11-1", "2026/11/10", "2026-11-10x", ""}) {
    EXPECT_EQ(day(bad), ANKI_NO_DAY) << bad;
  }
  for (AnkiDay d = day("1999-12-01"); d <= day("2101-03-01"); d++) {
    ASSERT_EQ(day(iso(d).c_str()), d) << iso(d);
  }
}

TEST(AnkiStats, AnchorPlacesInstantsInWholeDays) {
  const Anchor a{day("2026-11-10"), NEXT_DAY_AT};
  EXPECT_EQ(a.dayOf(NEXT_DAY_AT - 1), day("2026-11-10"));
  EXPECT_EQ(a.dayOf(NEXT_DAY_AT), day("2026-11-11"));
  EXPECT_EQ(a.dayOf(NEXT_DAY_AT - SECONDS_PER_DAY), day("2026-11-10"));
  EXPECT_EQ(a.dayOf(NEXT_DAY_AT - SECONDS_PER_DAY - 1), day("2026-11-09"));
  EXPECT_EQ(a.dayOf(NEXT_DAY_AT - 10 * SECONDS_PER_DAY - 5), day("2026-10-31"));
}

TEST(AnkiStats, ServerAnchorLabels) {
  const ScopedTz utc("UTC0");
  // Something reviewed today: the newest key is today's label.
  EXPECT_EQ(ankistats::fromServer(NEXT_DAY_AT, day("2026-11-10"), true).day, day("2026-11-10"));
  EXPECT_EQ(ankistats::fromServer(NEXT_DAY_AT, day("2026-11-10"), true).endsAt, NEXT_DAY_AT);
  // Nothing yet today: the device date the day started on, past the newest key.
  EXPECT_EQ(ankistats::fromServer(NEXT_DAY_AT, day("2026-11-08"), false).day, day("2026-11-10"));
  EXPECT_EQ(ankistats::fromServer(NEXT_DAY_AT, ANKI_NO_DAY, false).day, day("2026-11-10"));
  EXPECT_EQ(ankistats::fromServer(NEXT_DAY_AT, day("2026-11-10"), false).day, day("2026-11-11"));
  EXPECT_FALSE(ankistats::fromServer(0, day("2026-11-10"), true).valid());

  // A Tokyo collection on a UTC server keys the day that started 11-10 04:00
  // JST as 11-09: the key wins whenever there is one for today.
  const ScopedTz tokyo("JST-9");
  constexpr int64_t TOKYO_NEXT_DAY_AT = 1794337200;  // 2026-11-11 04:00 JST
  EXPECT_EQ(ankistats::fromServer(TOKYO_NEXT_DAY_AT, day("2026-11-09"), true).day, day("2026-11-09"));
  EXPECT_EQ(ankistats::fromServer(TOKYO_NEXT_DAY_AT, ANKI_NO_DAY, false).day, day("2026-11-10"));
}

TEST(AnkiStats, ClockAnchorRollsOverAtFourLocal) {
  const ScopedTz cet("CET-1CEST,M3.5.0,M10.5.0/3");
  Anchor a = ankistats::fromClock(1794279540, ANKI_NO_DAY);  // 2026-11-10 03:59 CET
  EXPECT_EQ(a.day, day("2026-11-09"));
  EXPECT_EQ(a.endsAt, 1794279600);
  a = ankistats::fromClock(1794279600, ANKI_NO_DAY);  // 04:00 CET
  EXPECT_EQ(a.day, day("2026-11-10"));
  EXPECT_EQ(a.endsAt, 1794366000);

  // Spring forward: Saturday's Anki day is 23 hours and ends at 04:00 CEST.
  a = ankistats::fromClock(1774695600, ANKI_NO_DAY);  // 2026-03-28 12:00 CET
  EXPECT_EQ(a.day, day("2026-03-28"));
  EXPECT_EQ(a.endsAt, 1774749600);
  a = ankistats::fromClock(1774747800, ANKI_NO_DAY);  // 2026-03-29 03:30 CEST
  EXPECT_EQ(a.day, day("2026-03-28"));
  EXPECT_EQ(a.endsAt, 1774749600);
  a = ankistats::fromClock(1774751400, ANKI_NO_DAY);  // 2026-03-29 04:30 CEST
  EXPECT_EQ(a.day, day("2026-03-29"));
  EXPECT_EQ(a.endsAt, 1774836000);
  // Fall back: a 25-hour day ending at 04:00 CET.
  a = ankistats::fromClock(1792843200, ANKI_NO_DAY);  // 2026-10-24 14:00 CEST
  EXPECT_EQ(a.day, day("2026-10-24"));
  EXPECT_EQ(a.endsAt, 1792897200);

  // A desktop clock ahead of the device: its newest key is today.
  a = ankistats::fromClock(1794279600, day("2026-11-11"));
  EXPECT_EQ(a.day, day("2026-11-11"));
  EXPECT_EQ(a.endsAt, 1794366000 + SECONDS_PER_DAY);
  EXPECT_FALSE(ankistats::fromClock(0, ANKI_NO_DAY).valid());
}

TEST(AnkiStats, HistoryFileRoundTrip) {
  MemFs fs;
  const auto h = historyWith({57, 120, 0, 33});
  ASSERT_TRUE(ankistats::saveHistory(fs, 7, *h));
  EXPECT_EQ(fs.files[ankipaths::statsFile(7)],
            R"({"v":1,"day":"2026-11-10","ends_at":1794369600,"counts":[57,120,0,33]})");

  auto back = std::make_unique<History>();
  back->counts[5] = 9;
  ASSERT_TRUE(ankistats::loadHistory(fs, 7, *back));
  EXPECT_EQ(back->anchor.day, day("2026-11-10"));
  EXPECT_EQ(back->anchor.endsAt, NEXT_DAY_AT);
  EXPECT_EQ(back->counts[0], 57);
  EXPECT_EQ(back->counts[1], 120);
  EXPECT_EQ(back->counts[3], 33);
  EXPECT_EQ(back->counts[5], 0);

  EXPECT_FALSE(ankistats::loadHistory(fs, 8, *back));
  fs.files[ankipaths::statsFile(7)] = R"({"v":2,"day":"2026-11-10","ends_at":1794369600,"counts":[1]})";
  EXPECT_FALSE(ankistats::loadHistory(fs, 7, *back));
  fs.files[ankipaths::statsFile(7)] = R"({"v":1,"day":"10 Nov","ends_at":1794369600,"counts":[1]})";
  EXPECT_FALSE(ankistats::loadHistory(fs, 7, *back));
}

TEST(AnkiStats, ParsesAnkiDoStats) {
  const ScopedTz utc("UTC0");
  const std::string body =
      R"({"reviewed_by_day":{"2026-11-08":12,"2026-11-10":5,"2025-01-01":99,"bad":3},"reviewed_today":5,)"
      R"("decks":[{"id":1,"name":"D","level":1,"new":3,"learning":0,"due":4}],"day_rollover_hour":4,)"
      R"("next_day_at":1794369600,"today":812,"collection_mod":1794300000000})";
  StatsParser parser(StatsParser::Format::AnkiDo);
  ankijson::Reader reader(parser.callbacks());
  for (const char c : body) reader.feed(&c, 1);
  ASSERT_FALSE(reader.hasError());
  auto h = std::make_unique<History>();
  ASSERT_TRUE(parser.toHistory(0, *h));
  EXPECT_EQ(h->anchor.day, day("2026-11-10"));
  EXPECT_EQ(h->anchor.endsAt, NEXT_DAY_AT);
  EXPECT_EQ(h->counts[0], 5);
  EXPECT_EQ(h->counts[2], 12);
  int total = 0;
  for (const uint16_t c : h->counts) total += c;
  EXPECT_EQ(total, 17) << "deck counts and out-of-window days stay out";

  StatsParser noBoundary(StatsParser::Format::AnkiDo);
  ankijson::Reader r2(noBoundary.callbacks());
  r2.feed(R"({"reviewed_by_day":{"2026-11-10":5},"reviewed_today":5})");
  EXPECT_FALSE(noBoundary.toHistory(NOON, *h));
}

TEST(AnkiStats, ParsesAnkiConnectDaysInAnyOrder) {
  const ScopedTz utc("UTC0");
  const AnkiDay today = day("2026-11-10");
  std::vector<AnkiDay> all;
  all.reserve(400);
  for (int i = 0; i < 400; i++) all.push_back(today - i);
  std::shuffle(all.begin(), all.end(), std::mt19937(7));
  std::string body = "[";
  for (const AnkiDay d : all) {
    if (body.size() > 1) body += ",";
    body += "[\"" + iso(d) + "\"," + std::to_string(1 + d % 50) + "]";
  }
  body += "]";
  StatsParser parser(StatsParser::Format::AnkiConnect);
  ankijson::Reader reader(parser.callbacks());
  reader.feed(body);
  auto h = std::make_unique<History>();
  ASSERT_TRUE(parser.toHistory(NOON, *h));
  EXPECT_EQ(h->anchor.day, today);
  EXPECT_EQ(h->anchor.endsAt, NEXT_DAY_AT);
  for (int i = 0; i < ankistats::DAYS; i++) ASSERT_EQ(h->counts[i], 1 + (today - i) % 50) << i;

  StatsParser countFirst(StatsParser::Format::AnkiConnect);
  ankijson::Reader r2(countFirst.callbacks());
  r2.feed(R"([[7,"2026-11-09"]])");
  ASSERT_TRUE(countFirst.toHistory(NOON, *h));
  EXPECT_EQ(h->counts[1], 7);
  EXPECT_FALSE(countFirst.toHistory(0, *h)) << "no clock, no day boundary";
}

TEST(AnkiStatsMerge, StreakIsAliveUntilTodayEnds) {
  Study s;
  ankistats::merge(historyWith({0, 3, 4, 2, 1}).get(), {}, NOON, s);
  EXPECT_EQ(s.days, Study::Days::Server);
  EXPECT_EQ(s.today, day("2026-11-10"));
  EXPECT_EQ(s.reviewedToday, 0);
  EXPECT_EQ(s.streak, 4);
  EXPECT_FALSE(s.streakCapped);
  EXPECT_EQ(s.syncedDaysAgo, 0);

  ankistats::merge(historyWith({1, 3, 0, 2}).get(), {}, NOON, s);
  EXPECT_EQ(s.reviewedToday, 1);
  EXPECT_EQ(s.streak, 2);
}

TEST(AnkiStatsMerge, UndatedGradesCountAsToday) {
  Study s;
  ankistats::merge(historyWith({0, 3, 4}).get(), {graded(11, 0), graded(12, 0)}, NOON, s);
  EXPECT_EQ(s.reviewedToday, 2);
  EXPECT_EQ(s.streak, 3);
}

TEST(AnkiStatsMerge, DatedGradesLandOnTheirDayClampedToToday) {
  Study s;
  ankistats::merge(historyWith({}).get(),
                   {graded(1, NOON - 2 * SECONDS_PER_DAY), graded(2, NOON + 5 * SECONDS_PER_DAY),
                    graded(3, NOON - 400 * SECONDS_PER_DAY)},
                   NOON, s);
  EXPECT_EQ(s.reviewedToday, 1);
  EXPECT_GT(s.levelOn(s.today - 2), 0);
  EXPECT_EQ(s.levelOn(s.today - 1), 0);
  EXPECT_EQ(s.streak, 1);
}

TEST(AnkiStatsMerge, LevelsFollowQuartiles) {
  Study s;
  ankistats::merge(historyWith({10, 20, 30, 40, 50, 60, 70, 80}).get(), {}, NOON, s);
  const uint8_t expected[] = {1, 1, 1, 1, 2, 2, 3, 3, 0};
  for (size_t i = 0; i < sizeof(expected); i++) EXPECT_EQ(s.level[i], expected[i]) << "day today - " << i;
  EXPECT_EQ(s.levelOn(s.today + 1), 0);
  EXPECT_EQ(s.levelOn(s.today - ankistats::DAYS), 0);
}

TEST(AnkiStatsMerge, WindowSlidesWithTheClock) {
  auto h = historyWith({});
  std::fill(std::begin(h->counts), std::end(h->counts), 2);
  Study s;
  ankistats::merge(h.get(), {}, NOON, s);
  EXPECT_EQ(s.streak, ankistats::DAYS);
  EXPECT_TRUE(s.streakCapped);

  ankistats::merge(h.get(), {graded(1, 0)}, NOON + 2 * SECONDS_PER_DAY, s);
  EXPECT_EQ(s.today, day("2026-11-12"));
  EXPECT_EQ(s.syncedDaysAgo, 2);
  EXPECT_EQ(s.reviewedToday, 1);
  EXPECT_EQ(s.streak, 1) << "yesterday had nothing";
  EXPECT_EQ(s.levelOn(day("2026-11-10")), 3);
}

TEST(AnkiStatsMerge, StatesWithoutStats) {
  const ScopedTz utc("UTC0");
  Study s;
  ankistats::merge(nullptr, {graded(1, 0)}, 0, s);
  EXPECT_EQ(s.days, Study::Days::None);
  EXPECT_EQ(s.today, ANKI_NO_DAY);

  ankistats::merge(nullptr, {graded(1, 0)}, NOON, s);
  EXPECT_EQ(s.days, Study::Days::ThisDeviceOnly);
  EXPECT_EQ(s.today, day("2026-11-10"));
  EXPECT_EQ(s.reviewedToday, 1);
  EXPECT_EQ(s.syncedDaysAgo, -1);

  ankistats::merge(historyWith({4}).get(), {}, 0, s);
  EXPECT_EQ(s.days, Study::Days::Server);
  EXPECT_EQ(s.today, day("2026-11-10"));
  EXPECT_EQ(s.reviewedToday, 4);
  EXPECT_EQ(s.syncedDaysAgo, -1) << "clock unknown";
}

TEST(AnkiCardCache, UnreviewedSkipsMissingAndGradedIds) {
  EXPECT_EQ(AnkiCardCache::unreviewed({5, 0, 7, 9, 7}, {7}), (std::vector<uint16_t>{0, 3}));
  EXPECT_TRUE(AnkiCardCache::unreviewed({}, {1}).empty());
}

TEST(AnkiStats, LoadStudyReadsStatsJournalAndCache) {
  const ScopedTz utc("UTC0");
  MemFs fs;
  constexpr uint32_t ID = 4;
  ASSERT_TRUE(ankistats::saveHistory(fs, ID, *historyWith({0, 2})));
  AnkiJournal journal(fs, ankipaths::reviewsFile(ID));
  journal.append(graded(101, 0));
  journal.append(graded(102, 0));
  std::string cards;
  for (const int64_t id : {101, 102, 103}) {
    AnkiCard c;
    c.cardId = id;
    c.q = "q";
    cards += AnkiCardCache::toLine(c);
  }
  fs.files[ankipaths::cardsFile(ID)] = cards;

  Study s;
  ankistats::loadStudy(fs, ID, NOON, s);
  EXPECT_EQ(s.days, Study::Days::Server);
  EXPECT_EQ(s.toReview, 1);
  EXPECT_EQ(s.pending, 2);
  EXPECT_EQ(s.reviewedToday, 2);
  EXPECT_EQ(s.streak, 2);
}
