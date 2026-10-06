#include <gtest/gtest.h>

#include <cstdlib>
#include <ctime>

#include "WakeSchedule.h"

namespace {

using wakeschedule::nextWake;
using wakeschedule::parseClock;

// Local wall time in Europe/Amsterdam to epoch seconds.
int64_t ams(int year, int month, int day, int hour, int minute) {
  setenv("TZ", "CET-1CEST,M3.5.0,M10.5.0/3", 1);
  tzset();
  struct tm t{};
  t.tm_year = year - 1900;
  t.tm_mon = month - 1;
  t.tm_mday = day;
  t.tm_hour = hour;
  t.tm_min = minute;
  t.tm_isdst = -1;
  return static_cast<int64_t>(mktime(&t));
}

constexpr int16_t SCHOOL[] = {7 * 60 + 55, 19 * 60 + 5};

}  // namespace

TEST(WakeSchedule, ParsesClockTimes) {
  EXPECT_EQ(parseClock("07:55"), 7 * 60 + 55);
  EXPECT_EQ(parseClock("00:00"), 0);
  EXPECT_EQ(parseClock("23:59"), 23 * 60 + 59);
  EXPECT_EQ(parseClock("24:00"), -1);
  EXPECT_EQ(parseClock("7:55"), -1);
  EXPECT_EQ(parseClock("07:60"), -1);
  EXPECT_EQ(parseClock("07:555"), -1);
  EXPECT_EQ(parseClock(""), -1);
  EXPECT_EQ(parseClock(nullptr), -1);
}

TEST(WakeSchedule, PicksTheNextTimeToday) {
  EXPECT_EQ(nextWake(ams(2026, 10, 6, 12, 0), SCHOOL, 2), ams(2026, 10, 6, 19, 5));
  EXPECT_EQ(nextWake(ams(2026, 10, 6, 6, 0), SCHOOL, 2), ams(2026, 10, 6, 7, 55));
}

TEST(WakeSchedule, RollsOverToTomorrow) {
  EXPECT_EQ(nextWake(ams(2026, 10, 6, 22, 0), SCHOOL, 2), ams(2026, 10, 7, 7, 55));
}

TEST(WakeSchedule, SkipsTimesInsideTheLead) {
  // Just woken at 19:05: the same slot must not be picked again.
  EXPECT_EQ(nextWake(ams(2026, 10, 6, 19, 5) - 30, SCHOOL, 2), ams(2026, 10, 7, 7, 55));
}

TEST(WakeSchedule, HandlesDstEnd) {
  // 25 October 2026 is 25 hours long in Amsterdam; 07:55 must stay 07:55 local.
  const int64_t at = nextWake(ams(2026, 10, 24, 22, 0), SCHOOL, 2);
  EXPECT_EQ(at, ams(2026, 10, 25, 7, 55));
  EXPECT_EQ(at - ams(2026, 10, 24, 22, 0), (9 * 60 + 55 + 60) * 60);
}

TEST(WakeSchedule, EmptyListMeansNoWake) { EXPECT_EQ(nextWake(ams(2026, 10, 6, 12, 0), SCHOOL, 0), 0); }
