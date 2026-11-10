#include "AnkiStats.h"

#include <algorithm>
#include <cstdio>
#include <ctime>
#include <iterator>
#include <memory>
#include <new>

#include "AnkiCardCache.h"
#include "AnkiPaths.h"

namespace ankistats {

namespace {

constexpr int64_t SECONDS_PER_DAY = 86400;
constexpr size_t PARSE_CAPACITY = DAYS + 8;

int64_t floorDiv(const int64_t a, const int64_t b) {
  const int64_t q = a / b;
  return (a % b != 0 && (a < 0) != (b < 0)) ? q - 1 : q;
}

uint16_t saturate16(const int64_t v) { return static_cast<uint16_t>(std::clamp<int64_t>(v, 0, UINT16_MAX)); }

std::string toIso(const AnkiDay d) {
  int y = 0;
  int m = 0;
  int day = 0;
  toCivil(d, y, m, day);
  char buf[16];
  snprintf(buf, sizeof(buf), "%04d-%02d-%02d", y, m, day);
  return buf;
}

}  // namespace

// Howard Hinnant's days_from_civil / civil_from_days.
AnkiDay fromCivil(int year, const int month, const int day) {
  year -= month <= 2 ? 1 : 0;
  const int era = (year >= 0 ? year : year - 399) / 400;
  const int yoe = year - era * 400;
  const int doy = (153 * (month + (month > 2 ? -3 : 9)) + 2) / 5 + day - 1;
  const int doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return era * 146097 + doe - 719468;
}

void toCivil(const AnkiDay d, int& year, int& month, int& day) {
  const int z = d + 719468;
  const int era = (z >= 0 ? z : z - 146096) / 146097;
  const int doe = z - era * 146097;
  const int yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
  const int doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
  const int mp = (5 * doy + 2) / 153;
  day = doy - (153 * mp + 2) / 5 + 1;
  month = mp < 10 ? mp + 3 : mp - 9;
  year = yoe + era * 400 + (month <= 2 ? 1 : 0);
}

AnkiDay parseIso(const std::string& text) {
  if (text.size() != 10 || text[4] != '-' || text[7] != '-') return ANKI_NO_DAY;
  int fields[3] = {};
  static constexpr int STARTS[3] = {0, 5, 8};
  static constexpr int LENGTHS[3] = {4, 2, 2};
  for (int f = 0; f < 3; f++) {
    for (int i = 0; i < LENGTHS[f]; i++) {
      const char c = text[STARTS[f] + i];
      if (c < '0' || c > '9') return ANKI_NO_DAY;
      fields[f] = fields[f] * 10 + (c - '0');
    }
  }
  if (fields[1] < 1 || fields[1] > 12 || fields[2] < 1 || fields[2] > 31) return ANKI_NO_DAY;
  const AnkiDay d = fromCivil(fields[0], fields[1], fields[2]);
  int y = 0;
  int m = 0;
  int day = 0;
  toCivil(d, y, m, day);
  return m == fields[1] && day == fields[2] ? d : ANKI_NO_DAY;  // rejects 2026-02-30
}

int weekday(const AnkiDay d) { return static_cast<int>(((static_cast<int64_t>(d) + 3) % 7 + 7) % 7); }

AnkiDay localDate(const int64_t epoch) {
  const time_t t = static_cast<time_t>(epoch);
  struct tm lt{};
  localtime_r(&t, &lt);
  return fromCivil(lt.tm_year + 1900, lt.tm_mon + 1, lt.tm_mday);
}

AnkiDay Anchor::dayOf(const int64_t epoch) const {
  return static_cast<AnkiDay>(day + floorDiv(epoch - (endsAt - SECONDS_PER_DAY), SECONDS_PER_DAY));
}

Anchor fromServer(const int64_t nextDayAt, const AnkiDay newestKey, const bool reviewedToday) {
  Anchor a;
  if (nextDayAt <= 0) return a;
  a.endsAt = nextDayAt;
  if (reviewedToday && newestKey != ANKI_NO_DAY) {
    a.day = newestKey;
    return a;
  }
  a.day = localDate(nextDayAt - SECONDS_PER_DAY + 1);
  if (newestKey != ANKI_NO_DAY && a.day <= newestKey) a.day = newestKey + 1;
  return a;
}

Anchor fromClock(const int64_t now, const AnkiDay newestKey) {
  Anchor a;
  if (now <= 0) return a;
  const time_t t = static_cast<time_t>(now);
  struct tm lt{};
  localtime_r(&t, &lt);
  AnkiDay label = fromCivil(lt.tm_year + 1900, lt.tm_mon + 1, lt.tm_mday);
  if (lt.tm_hour < ROLLOVER_HOUR) label--;
  // The day ends at the rollover hour on the next calendar date, local time
  // (mktime applies that date's DST offset).
  int y = 0;
  int m = 0;
  int d = 0;
  toCivil(label + 1, y, m, d);
  struct tm end{};
  end.tm_year = y - 1900;
  end.tm_mon = m - 1;
  end.tm_mday = d;
  end.tm_hour = ROLLOVER_HOUR;
  end.tm_isdst = -1;
  const time_t endsAt = mktime(&end);
  if (endsAt <= 0) return a;
  a.day = label;
  a.endsAt = static_cast<int64_t>(endsAt);
  if (newestKey != ANKI_NO_DAY && newestKey > label) {
    a.endsAt += static_cast<int64_t>(newestKey - label) * SECONDS_PER_DAY;
    a.day = newestKey;
  }
  return a;
}

bool loadHistory(AnkiFs& fs, const uint32_t accountId, History& out) {
  std::string text;
  if (!fs.readAll(ankipaths::statsFile(accountId), text)) return false;
  std::fill(std::begin(out.counts), std::end(out.counts), 0);
  int depth = 0;
  std::string key;
  int64_t version = 0;
  Anchor anchor;
  size_t n = 0;
  ankijson::Reader::Callbacks cb;
  cb.onKey = [&](const std::string& k) { key = k; };
  cb.onObjectStart = [&] { depth++; };
  cb.onObjectEnd = [&] { depth--; };
  cb.onArrayStart = [&] { depth++; };
  cb.onArrayEnd = [&] { depth--; };
  cb.onString = [&](const std::string& v) {
    if (depth == 1 && key == "day") anchor.day = parseIso(v);
  };
  cb.onNumber = [&](const std::string& v) {
    if (depth == 1 && key == "v") {
      version = ankijson::toInt64(v);
    } else if (depth == 1 && key == "ends_at") {
      anchor.endsAt = ankijson::toInt64(v);
    } else if (depth == 2 && key == "counts" && n < DAYS) {
      out.counts[n++] = saturate16(ankijson::toInt64(v));
    }
  };
  ankijson::Reader reader(std::move(cb));
  reader.feed(text);
  if (reader.hasError() || version != 1 || !anchor.valid()) return false;
  out.anchor = anchor;
  return true;
}

// {"v":1,"day":"2026-11-10","ends_at":1794366000,"counts":[57,120,0,33]}:
// counts[i] is day - i, trailing zeros dropped, about 1.6 KB at most.
bool saveHistory(AnkiFs& fs, const uint32_t accountId, const History& history) {
  const auto* last = std::find_if(std::rbegin(history.counts), std::rend(history.counts), [](const uint16_t c) {
                       return c != 0;
                     }).base();
  std::string text;
  text.reserve(64 + static_cast<size_t>(last - std::begin(history.counts)) * 4);
  text = "{\"v\":1,\"day\":\"" + toIso(history.anchor.day) + "\",\"ends_at\":" + std::to_string(history.anchor.endsAt) +
         ",\"counts\":[";
  for (const auto* c = std::begin(history.counts); c != last; ++c) {
    if (c != std::begin(history.counts)) text.push_back(',');
    text += std::to_string(*c);
  }
  text += "]}";
  fs.mkdirs(ankipaths::accountDir(accountId));
  return fs.writeAllAtomic(ankipaths::statsFile(accountId), text);
}

StatsParser::StatsParser(const Format format) : format(format) { days.reserve(PARSE_CAPACITY); }

void StatsParser::add(const AnkiDay day, const int64_t count) {
  if (day == ANKI_NO_DAY || count <= 0) return;
  if (days.size() < PARSE_CAPACITY) {
    days.emplace_back(day, saturate16(count));
    return;
  }
  const auto oldest = std::min_element(days.begin(), days.end());
  if (day > oldest->first) *oldest = {day, saturate16(count)};
}

ankijson::Reader::Callbacks StatsParser::callbacks() {
  ankijson::Reader::Callbacks cb;
  cb.onObjectStart = [this] { depth++; };
  cb.onObjectEnd = [this] { depth--; };
  cb.onArrayStart = [this] {
    depth++;
    pairDay = ANKI_NO_DAY;
    pairCount = -1;
  };
  cb.onArrayEnd = [this] {
    if (format == Format::AnkiConnect && depth == 2) add(pairDay, pairCount);
    depth--;
  };
  cb.onKey = [this](const std::string& k) {
    if (depth == 1) {
      topKey = k;
    } else if (depth == 2 && topKey == "reviewed_by_day") {
      pairDay = parseIso(k);
    }
  };
  cb.onString = [this](const std::string& v) {
    if (format == Format::AnkiConnect && depth == 2) pairDay = parseIso(v);
  };
  cb.onNumber = [this](const std::string& v) {
    const int64_t n = ankijson::toInt64(v);
    if (format == Format::AnkiConnect) {
      if (depth == 2) pairCount = n;
    } else if (depth == 2 && topKey == "reviewed_by_day") {
      add(pairDay, n);
    } else if (depth == 1 && topKey == "next_day_at") {
      nextDayAt = n;
    } else if (depth == 1 && topKey == "reviewed_today") {
      reviewedToday = n;
    }
  };
  return cb;
}

bool StatsParser::toHistory(const int64_t now, History& out) const {
  const auto newestIt = std::max_element(days.begin(), days.end());
  const AnkiDay newest = newestIt == days.end() ? ANKI_NO_DAY : newestIt->first;
  const Anchor anchor =
      format == Format::AnkiDo ? fromServer(nextDayAt, newest, reviewedToday > 0) : fromClock(now, newest);
  if (!anchor.valid()) return false;
  out.anchor = anchor;
  std::fill(std::begin(out.counts), std::end(out.counts), 0);
  for (const auto& [day, count] : days) {
    const int64_t i = static_cast<int64_t>(anchor.day) - day;
    if (i >= 0 && i < DAYS) out.counts[i] = count;
  }
  return true;
}

bool saveAnswer(AnkiFs& fs, const uint32_t accountId, const StatsParser& parser, const int64_t now) {
  const std::unique_ptr<History> history(new (std::nothrow) History());
  return history && parser.toHistory(now, *history) && saveHistory(fs, accountId, *history);
}

uint8_t Study::levelOn(const AnkiDay d) const {
  if (today == ANKI_NO_DAY) return 0;
  const int64_t i = static_cast<int64_t>(today) - d;
  return i >= 0 && i < DAYS ? level[i] : 0;
}

void merge(const History* history, const std::vector<AnkiJournal::Entry>& pending, const int64_t now, Study& out) {
  out.days = Study::Days::None;
  out.today = ANKI_NO_DAY;
  out.reviewedToday = 0;
  out.streak = 0;
  out.streakCapped = false;
  out.syncedDaysAgo = -1;
  std::fill(std::begin(out.level), std::end(out.level), 0);

  const Anchor anchor = history ? history->anchor : fromClock(now, ANKI_NO_DAY);
  if (!anchor.valid()) return;
  out.days = history ? Study::Days::Server : Study::Days::ThisDeviceOnly;
  // Never before the server's today, even when the device clock lags it.
  const AnkiDay today = now > 0 ? std::max(anchor.day, anchor.dayOf(now)) : anchor.day;
  out.today = today;

  std::vector<uint16_t> merged(DAYS, 0);
  if (history) {
    const int64_t shift = static_cast<int64_t>(today) - history->anchor.day;
    for (int64_t j = 0; j + shift < DAYS; j++) merged[j + shift] = history->counts[j];
  }
  // An undated grade (no RTC) is dated by the server at upload: today.
  for (const AnkiJournal::Entry& e : pending) {
    const AnkiDay d = e.answeredAt > 0 ? std::min(today, anchor.dayOf(e.answeredAt)) : today;
    const int64_t i = static_cast<int64_t>(today) - d;
    if (i < DAYS && merged[i] < UINT16_MAX) merged[i]++;
  }
  out.reviewedToday = merged[0];

  // Still alive while today is empty: count back from yesterday.
  const auto first = merged.begin() + (merged[0] > 0 ? 0 : 1);
  const auto gap = std::find(first, merged.end(), 0);
  out.streak = static_cast<uint16_t>(gap - first);
  out.streakCapped = gap == merged.end() && out.streak > 0;

  // Levels relative to a typical day: the median of the nonzero days.
  std::vector<uint16_t> nonzero;
  nonzero.reserve(DAYS);
  std::copy_if(merged.begin(), merged.end(), std::back_inserter(nonzero), [](const uint16_t n) { return n > 0; });
  uint32_t typical = 0;
  if (!nonzero.empty()) {
    const auto mid = nonzero.begin() + static_cast<std::ptrdiff_t>(nonzero.size() / 2);
    std::nth_element(nonzero.begin(), mid, nonzero.end());
    typical = *mid;
  }
  std::transform(merged.begin(), merged.end(), std::begin(out.level), [typical](const uint16_t n) -> uint8_t {
    if (n == 0) return 0;
    if (n >= typical) return 3;
    return 2u * n >= typical ? 2 : 1;
  });

  if (history && now > 0) {
    out.syncedDaysAgo = static_cast<int16_t>(std::min<int64_t>(INT16_MAX, today - history->anchor.day));
  }
}

void loadStudy(AnkiFs& fs, const uint32_t accountId, const int64_t now, Study& out) {
  const std::vector<AnkiJournal::Entry> pending = AnkiJournal(fs, ankipaths::reviewsFile(accountId)).load();
  {
    const std::unique_ptr<History> history(new (std::nothrow) History());
    const bool hasHistory = history && loadHistory(fs, accountId, *history);
    merge(hasHistory ? history.get() : nullptr, pending, now, out);
  }
  std::vector<int64_t> graded;
  graded.reserve(pending.size());
  std::transform(pending.begin(), pending.end(), std::back_inserter(graded),
                 [](const AnkiJournal::Entry& e) { return e.cardId; });
  AnkiCardCache cache(fs, ankipaths::cardsFile(accountId), ankipaths::cacheMetaFile(accountId));
  out.toReview = saturate16(static_cast<int64_t>(AnkiCardCache::unreviewed(cache.cardIds(), graded).size()));
  out.pending = saturate16(static_cast<int64_t>(pending.size()));
}

}  // namespace ankistats
