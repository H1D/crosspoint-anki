#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "AnkiFs.h"
#include "AnkiHttp.h"
#include "AnkiJson.h"
#include "AnkiTypes.h"

// The AnkiConnect side of the sync engine: same steps and same SD files as the
// AnkiDo path, spoken as AnkiConnect actions against Anki desktop
// (POST <url> with {"action","version":6,"params","key"}):
//   exchange   answerCards for the journal, then findCards + cardsInfo for the
//              queue (review account only). Card HTML is turned into AnkiDo's
//              text markup on the device (AnkiHtml).
//   pushNotes  addNote per queued note; field names come from modelFieldNames.
//   fetchDecks deckNames + getDeckStats -> decks.json.
// AnkiConnect has no client ids: every review in a request that the server
// answered is treated as delivered, so a lost response can grade a card twice.
class AnkiConnectClient {
 public:
  using Clock = std::function<int64_t()>;

  AnkiConnectClient(AnkiFs& fs, AnkiHttp& http, Clock clock) : fs(fs), http(http), clock(std::move(clock)) {}

  AnkiSyncResult exchange(const AnkiAccount& account, bool wantCards);
  AnkiSyncResult pushNotes(const AnkiAccount& account);
  AnkiSyncResult fetchDecks(const AnkiAccount& account);

  // Request body for one action (public for tests). `params` is a JSON object
  // literal or empty.
  static std::string requestBody(const AnkiAccount& account, const char* action, const std::string& params);
  // Anki search: the account's deck filter plus a state clause such as "is:due".
  static std::string buildSearch(const AnkiAccount& account, const char* state);

  // Cards per cardsInfo request. Each card's HTML (with the note type's CSS
  // in front) streams through the JSON reader one string at a time; the batch
  // bounds how long one response holds the radio.
  size_t cardsPerRequest = 8;

 private:
  struct Call {
    int status = 0;     // HTTP status, <= 0 on transport failure
    std::string error;  // AnkiConnect "error" string, or transport/HTTP text
    bool ok = false;    // 2xx, error null, well-formed
  };

  // Performs one action. Events inside "result" are forwarded to `onResult`;
  // depth counting there starts at the result value itself.
  Call call(const AnkiAccount& account, const char* action, const std::string& params,
            ankijson::Reader::Callbacks onResult);
  static void fail(AnkiSyncResult& r, const Call& c);

  bool ensureProfile(const AnkiAccount& account, AnkiSyncResult& r);
  bool fetchDeckList(const AnkiAccount& account, std::vector<AnkiDeck>& decks, AnkiSyncResult& r);
  bool findCards(const AnkiAccount& account, const char* state, size_t limit, std::vector<int64_t>& ids,
                 AnkiSyncResult& r);
  bool fetchQueue(const AnkiAccount& account, AnkiSyncResult& r);
  // First two field names of a note type (cached per session); Front/Back when unknown.
  std::pair<std::string, std::string> fieldNames(const AnkiAccount& account, const std::string& model);

  AnkiFs& fs;
  AnkiHttp& http;
  Clock clock;
  std::string loadedProfile;
  std::map<std::string, std::pair<std::string, std::string>> modelFields;
};
