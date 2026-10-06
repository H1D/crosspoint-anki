#pragma once

#include <cstddef>
#include <cstdint>
#include <ctime>

// Daily local refresh times for plugins ("wake": ["07:55", "19:05"] under a
// sleep.enter handler). Pure functions over the C time API so the host tests
// can drive them with any TZ.
namespace wakeschedule {

// "HH:MM" (24-hour) to minutes after local midnight, or -1 when malformed.
inline int parseClock(const char* s) {
  if (!s) return -1;
  int h = 0;
  int m = 0;
  int i = 0;
  for (int digits = 0; digits < 2; digits++, i++) {
    if (s[i] < '0' || s[i] > '9') return -1;
    h = h * 10 + (s[i] - '0');
  }
  if (s[i++] != ':') return -1;
  for (int digits = 0; digits < 2; digits++, i++) {
    if (s[i] < '0' || s[i] > '9') return -1;
    m = m * 10 + (s[i] - '0');
  }
  if (s[i] != '\0' || h > 23 || m > 59) return -1;
  return h * 60 + m;
}

// Epoch seconds of the earliest listed local time that is at least
// `minLeadSec` after `now`, or 0 when the list is empty. Built with mktime()
// per candidate day, so the result is right across DST changes.
inline int64_t nextWake(const int64_t now, const int16_t* minutes, const size_t count, const int minLeadSec = 60) {
  int64_t best = 0;
  const time_t t = static_cast<time_t>(now);
  struct tm today{};
  localtime_r(&t, &today);
  for (size_t i = 0; i < count; i++) {
    if (minutes[i] < 0) continue;
    for (int dayOffset = 0; dayOffset < 2; dayOffset++) {
      struct tm cand = today;
      cand.tm_mday += dayOffset;
      cand.tm_hour = minutes[i] / 60;
      cand.tm_min = minutes[i] % 60;
      cand.tm_sec = 0;
      cand.tm_isdst = -1;
      const int64_t at = static_cast<int64_t>(mktime(&cand));
      if (at >= now + minLeadSec) {
        if (best == 0 || at < best) best = at;
        break;
      }
    }
  }
  return best;
}

}  // namespace wakeschedule
