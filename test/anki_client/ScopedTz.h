#pragma once

#include <cstdlib>
#include <ctime>
#include <string>

// Sets the process TZ for one test and restores it afterwards.
class ScopedTz {
 public:
  explicit ScopedTz(const char* tz) {
    if (const char* old = getenv("TZ")) {
      saved = old;
      hadTz = true;
    }
    setenv("TZ", tz, 1);
    tzset();
  }
  ~ScopedTz() {
    if (hadTz) {
      setenv("TZ", saved.c_str(), 1);
    } else {
      unsetenv("TZ");
    }
    tzset();
  }
  ScopedTz(const ScopedTz&) = delete;
  ScopedTz& operator=(const ScopedTz&) = delete;

 private:
  std::string saved;
  bool hadTz = false;
};
