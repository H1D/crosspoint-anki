#pragma once

#include <cstdint>
#include <string>
#include <vector>

// Plain data shared between the Anki client library (host-testable) and the
// firmware activities. No Arduino types here.

// Which server an account talks to. AnkiDo is the fork's own REST bridge to
// AnkiWeb; AnkiConnect is the add-on inside Anki desktop (port 8765).
enum class AnkiBackend : uint8_t { AnkiDo = 0, AnkiConnect = 1 };

struct AnkiAccount {
  uint32_t id = 0;  // stable, assigned at creation; names the SD directory
  std::string name;
  AnkiBackend backend = AnkiBackend::AnkiDo;
  std::string url;      // base URL, no trailing slash (AnkiDo server or AnkiConnect endpoint)
  std::string profile;  // AnkiDo profile name; AnkiConnect: Anki profile to load (optional)
  std::string token;    // AnkiDo bearer token; AnkiConnect: api key (optional). Plaintext in memory
  bool enabled = true;
  std::string model = "Basic";     // note type used by add-word
  std::vector<std::string> decks;  // decks to review; empty = all
  uint8_t cacheSize = 60;          // cards per exchange, 1..200
  uint8_t maxNewPerDay = 0;        // per-fetch cap; 0 = none (deck options apply server-side)
  std::string lastDeck;            // default deck for add-word (a user setting; JSON key "lastDeck")

  bool isAnkiConnect() const { return backend == AnkiBackend::AnkiConnect; }
  // AnkiDo only: the per-profile API root.
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

// Outcome of one sync step or session, shared by both backends.
struct AnkiSyncResult {
  bool ok = false;          // the step reached the server and was accepted
  int httpStatus = 0;       // last HTTP status (<= 0 on transport failure)
  std::string error;        // human-readable failure, empty when ok
  bool authFailed = false;  // token/key rejected: stop retrying, tell the user
  size_t reviewsSent = 0;
  size_t reviewsAcked = 0;
  size_t notesSent = 0;
  size_t notesAcked = 0;
  size_t cardsFetched = 0;
  size_t decksFetched = 0;  // > 0 when this step already refreshed the deck cache
  AnkiCounts counts;
  std::string syncError;  // AnkiDo's inline-sync error (exchange only), informational
};
