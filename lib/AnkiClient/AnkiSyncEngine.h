#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "AnkiFs.h"
#include "AnkiHttp.h"
#include "AnkiJournal.h"
#include "AnkiTypes.h"

// One sync session for one account, without any knowledge of Wi-Fi or the UI:
//   1. POST /exchange  — pending reviews out, fresh card queue in (review account only)
//   2. POST /notes     — queued add-word notes
//   3. GET  /decks     — deck list for the pickers
// Everything is streamed to or from the SD card; no response is held whole.
class AnkiSyncEngine {
 public:
  struct Result {
    bool ok = false;         // the step reached the server and was accepted
    int httpStatus = 0;      // last HTTP status (<= 0 on transport failure)
    std::string error;       // human-readable failure, empty when ok
    bool authFailed = false;  // 401/403: stop retrying, tell the user
    size_t reviewsSent = 0;
    size_t reviewsAcked = 0;
    size_t notesSent = 0;
    size_t notesAcked = 0;
    size_t cardsFetched = 0;
    AnkiCounts counts;
    std::string syncError;  // AnkiDo's inline-sync error (exchange only), informational
  };

  using Clock = std::function<int64_t()>;  // epoch seconds, 0 when unknown

  AnkiSyncEngine(AnkiFs& fs, AnkiHttp& http, Clock clock) : fs(fs), http(http), clock(std::move(clock)) {}

  // Runs all three steps. `wantCards` selects the review account behaviour:
  // it asks for a queue and replaces the cache. Other accounts only push.
  // Stops at the first transport failure. Returns the combined result.
  Result syncAccount(const AnkiAccount& account, bool wantCards);

  Result exchange(const AnkiAccount& account, bool wantCards);
  Result pushNotes(const AnkiAccount& account);
  Result fetchDecks(const AnkiAccount& account);

  // True when the account has anything to upload.
  bool hasPending(const AnkiAccount& account);

  // Deck list cached by fetchDecks().
  std::vector<AnkiDeck> loadDecks(uint32_t accountId);

  // Request body for POST /exchange (public for tests).
  static std::string buildExchangeBody(const AnkiAccount& account, const std::vector<AnkiJournal::Entry>& reviews,
                                       bool wantCards, int syncTimeoutSeconds);

  // Seconds AnkiDo may spend on its inline AnkiWeb sync. Short: the device is
  // on battery and holds the radio up for the whole request.
  int syncTimeoutSeconds = 15;

 private:
  std::vector<AnkiHttp::Header> authHeaders(const AnkiAccount& account) const;
  static void classify(Result& r, int status);

  AnkiFs& fs;
  AnkiHttp& http;
  Clock clock;
};
