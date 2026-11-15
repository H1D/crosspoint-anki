#pragma once

#include <climits>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "AnkiFs.h"
#include "AnkiJournal.h"
#include "AnkiJson.h"

// Reviews per Anki day for one account, behind the Anki dashboard.
//
// Two sources that never hold the same review, added together only when read:
//   stats.json     the server's per-day counts at the last stats fetch
//   reviews.jsonl  grades the server has not acknowledged yet (AnkiJournal)
// The sync engine fetches stats after every exchange that acknowledged a
// review, so an acknowledged review is counted by the server, never by both.
// A failed fetch can only undercount until the next one.

// A calendar date as days since 1970-01-01 (proleptic Gregorian), the unit
// every per-day count is keyed by. 2026-11-10 is 20767.
using AnkiDay = int32_t;
constexpr AnkiDay ANKI_NO_DAY = INT32_MIN;

namespace ankistats {

constexpr uint16_t DAYS = 371;    // 53 weeks: the most any orientation draws
constexpr int ROLLOVER_HOUR = 4;  // Anki's default "next day starts at", for servers that do not name one

AnkiDay fromCivil(int year, int month, int day);
void toCivil(AnkiDay d, int& year, int& month, int& day);
// "YYYY-MM-DD" -> day; ANKI_NO_DAY for anything else.
AnkiDay parseIso(const std::string& text);
int weekday(AnkiDay d);  // 0 = Monday ... 6 = Sunday
// Calendar date of an instant under the process TZ (HalClock::setTimezone on
// the device, setenv("TZ") in tests).
AnkiDay localDate(int64_t epoch);

// Where one Anki day ends. Other instants are placed in whole 86400 s steps
// from it, so once a server has named the boundary no time zone is involved.
struct Anchor {
  AnkiDay day = ANKI_NO_DAY;  // the Anki day that ends at endsAt
  int64_t endsAt = 0;         // epoch seconds
  bool valid() const { return day != ANKI_NO_DAY && endsAt > 0; }
  AnkiDay dayOf(int64_t epoch) const;
};

// AnkiDo names the boundary (next_day_at). The label is the newest key when
// something was reviewed today (keys never pass today), else the device date
// the day started on, raised past the newest key.
Anchor fromServer(int64_t nextDayAt, AnkiDay newestKey, bool reviewedToday);
// No boundary from a server (AnkiConnect, or nothing fetched yet): the device
// clock, its TZ and ROLLOVER_HOUR. A newest key past the device's today wins.
// Invalid when now is 0.
Anchor fromClock(int64_t now, AnkiDay newestKey);

// stats.json in memory, about 760 B: keep it off the stack.
struct History {
  Anchor anchor;               // the server's today at the fetch
  uint16_t counts[DAYS] = {};  // counts[i]: reviews on anchor.day - i
};
bool loadHistory(AnkiFs& fs, uint32_t accountId, History& out);  // false: no file or a bad one
bool saveHistory(AnkiFs& fs, uint32_t accountId, const History& history);

// One stats answer, parsed as it streams:
//   AnkiDo       GET /stats body {"reviewed_by_day":{"YYYY-MM-DD":n},"reviewed_today":n,"next_day_at":t}
//   AnkiConnect  getNumCardsReviewedByDay result [["YYYY-MM-DD",n]]
// Keeps the newest DAYS + 8 days, whatever order they arrive in.
class StatsParser {
 public:
  enum class Format : uint8_t { AnkiDo, AnkiConnect };

  explicit StatsParser(Format format);
  // Reader callbacks bound to this parser, for the whole body (AnkiDo) or the
  // result value (AnkiConnect).
  ankijson::Reader::Callbacks callbacks();
  // False when the answer names no day boundary (AnkiDo without next_day_at,
  // AnkiConnect with now == 0).
  bool toHistory(int64_t now, History& out) const;

 private:
  void add(AnkiDay day, int64_t count);

  Format format;
  int depth = 0;
  std::string topKey;
  AnkiDay pairDay = ANKI_NO_DAY;
  int64_t pairCount = -1;
  int64_t nextDayAt = 0;
  int64_t reviewedToday = 0;
  std::vector<std::pair<AnkiDay, uint16_t>> days;
};

// Writes what `parser` read as the account's stats.json. False without a day
// boundary, out of memory, or on SD failure.
bool saveAnswer(AnkiFs& fs, uint32_t accountId, const StatsParser& parser, int64_t now);

// Everything the dashboard draws for one account, about 400 B: filled in place.
struct Study {
  enum class Days : uint8_t {
    None,            // nothing fetched and the clock is unknown
    ThisDeviceOnly,  // nothing fetched: this device's pending grades only
    Server,          // the server's counts plus pending grades
  };
  Days days = Days::None;
  AnkiDay today = ANKI_NO_DAY;  // rightmost heatmap day
  uint16_t toReview = 0;        // cards left today: the server's counts, at least `reviewable`
  uint16_t reviewable = 0;      // cached cards not graded on this device
  uint16_t pending = 0;         // grades waiting for upload
  uint16_t reviewedToday = 0;
  uint16_t streak = 0;         // days with reviews up to today; an empty today does not break it yet
  bool streakCapped = false;   // the streak fills the whole window
  int16_t syncedDaysAgo = -1;  // -1: never fetched, or the clock is unknown
  uint8_t level[DAYS] = {};    // 0..3; level[i] is day today - i

  uint8_t levelOn(AnkiDay d) const;  // 0 outside the window
};

// Reads stats.json, reviews.jsonl and the card cache of one account. `now` is
// the device wall clock, 0 when unknown.
void loadStudy(AnkiFs& fs, uint32_t accountId, int64_t now, Study& out);
// The day part of loadStudy (everything but toReview and pending). history may be null.
void merge(const History* history, const std::vector<AnkiJournal::Entry>& pending, int64_t now, Study& out);

}  // namespace ankistats
