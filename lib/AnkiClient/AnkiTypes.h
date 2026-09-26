#pragma once

#include <cstdint>
#include <string>
#include <vector>

// Plain data shared between the Anki client library (host-testable) and the
// firmware activities. No Arduino types here.

struct AnkiAccount {
  uint32_t id = 0;  // stable, assigned at creation; names the SD directory
  std::string name;
  std::string url;      // AnkiDo base URL, no trailing slash
  std::string profile;  // AnkiDo profile name
  std::string token;    // bearer token (plaintext in memory)
  bool enabled = true;
  std::string model = "Basic";     // note type used by add-word
  std::vector<std::string> decks;  // decks to review; empty = all
  uint8_t cacheSize = 60;          // cards per exchange, 1..200
  uint8_t maxNewPerDay = 10;       // 0 = no cap
  std::string lastDeck;            // last deck used by add-word

  std::string apiBase() const { return url + "/v1/p/" + profile; }
};

struct AnkiCard {
  int64_t cardId = 0;
  std::string deck;
  std::string kind;  // due | new | learning
  std::string q;
  std::string a;
  std::string next[4];  // again, hard, good, easy
  uint8_t mediaCount = 0;
};

struct AnkiDeck {
  std::string name;
  uint16_t newCount = 0;
  uint16_t learning = 0;
  uint16_t due = 0;
};

struct AnkiCounts {
  uint16_t newCount = 0;
  uint16_t learning = 0;
  uint16_t due = 0;
  uint16_t returned = 0;
};

// Anki ease values as sent to POST /reviews.
enum class AnkiEase : uint8_t { Again = 1, Hard = 2, Good = 3, Easy = 4 };
