#include "AnkiSyncEngine.h"

#include "AnkiCardCache.h"
#include "AnkiJson.h"
#include "AnkiNoteQueue.h"
#include "AnkiPaths.h"

namespace {

// Pulls the message out of AnkiDo's {"error":{"code","message",...}} body.
struct ErrorCollector {
  std::string key;
  std::string code;
  std::string message;
  ankijson::Reader reader;
  ErrorCollector() : reader(makeCallbacks()) {}
  ankijson::Reader::Callbacks makeCallbacks() {
    ankijson::Reader::Callbacks cb;
    cb.onKey = [this](const std::string& k) { key = k; };
    cb.onString = [this](const std::string& v) {
      if (key == "code" && code.empty()) code = v;
      if (key == "message" && message.empty()) message = v;
    };
    return cb;
  }
  std::string text() const {
    if (!message.empty()) return message;
    return code;
  }
};

std::string statusText(int status) {
  if (status <= 0) return "No connection";
  return "HTTP " + std::to_string(status);
}

}  // namespace

std::vector<AnkiHttp::Header> AnkiSyncEngine::authHeaders(const AnkiAccount& account) const {
  return {{"Authorization", "Bearer " + account.token}, {"Content-Type", "application/json"}, {"Accept", "application/json"}};
}

void AnkiSyncEngine::classify(Result& r, int status) {
  r.httpStatus = status;
  r.ok = status >= 200 && status < 300;
  r.authFailed = status == 401 || status == 403;
}

std::string AnkiSyncEngine::buildExchangeBody(const AnkiAccount& account,
                                              const std::vector<AnkiJournal::Entry>& reviews, bool wantCards,
                                              int syncTimeoutSeconds) {
  std::string body = "{\"reviews\":" + AnkiJournal::toReviewsJson(reviews);
  if (wantCards) {
    body += ",\"want\":{";
    if (!account.decks.empty()) {
      body += "\"decks\":[";
      for (size_t i = 0; i < account.decks.size(); i++) {
        if (i) body.push_back(',');
        ankijson::appendQuoted(body, account.decks[i]);
      }
      body += "],";
    }
    body += "\"kinds\":[\"due\",\"learning\",\"new\"]";
    body += ",\"limit\":" + std::to_string(account.cacheSize == 0 ? 60 : account.cacheSize);
    if (account.maxNewPerDay > 0) body += ",\"max_new_per_day\":" + std::to_string(account.maxNewPerDay);
    body += ",\"fields\":\"compact\",\"render\":\"text\"}";
  } else {
    // Nothing wanted back: ask for an empty queue rather than the default 60.
    body += ",\"want\":{\"limit\":1,\"kinds\":[\"due\"],\"fields\":\"compact\",\"render\":\"text\"}";
  }
  body += ",\"sync\":\"auto\",\"sync_timeout_seconds\":" + std::to_string(syncTimeoutSeconds) + "}";
  return body;
}

AnkiSyncEngine::Result AnkiSyncEngine::exchange(const AnkiAccount& account, bool wantCards) {
  Result r;
  AnkiJournal journal(fs, ankipaths::reviewsFile(account.id));
  const std::vector<AnkiJournal::Entry> pending = journal.load();
  r.reviewsSent = pending.size();
  if (pending.empty() && !wantCards) {
    r.ok = true;  // nothing to do for a non-review account
    return r;
  }

  fs.mkdirs(ankipaths::accountDir(account.id));
  const std::string body = buildExchangeBody(account, pending, wantCards, syncTimeoutSeconds);

  // Streaming parse state. Cards go straight to a temp file; acked ids,
  // counts and the sync error are the only things kept in memory.
  const std::string cardsTmp = ankipaths::cardsFile(account.id) + ".tmp";
  std::unique_ptr<AnkiFile> cardsOut;
  if (wantCards) cardsOut = fs.open(cardsTmp, AnkiFs::Mode::Write);
  std::vector<std::string> acked;
  AnkiCounts counts;
  std::string syncError;
  bool writeFailed = false;

  enum class Section { None, Reviews, Cards, Counts, Sync, Other };
  Section section = Section::None;
  std::string key;
  std::string topKey;
  AnkiCard card;
  int nextIndex = -1;
  bool inMediaArray = false;
  int cardDepth = 0;  // reader depth at which a card object starts
  std::string reviewClientId;
  // Inside "sync": {"error": {"code":..,"message":..}}
  std::string syncErrCode;
  std::string syncErrMessage;

  ankijson::Reader::Callbacks cb;
  int depth = 0;
  cb.onKey = [&](const std::string& k) {
    key = k;
    if (depth == 1) {
      topKey = k;
      if (k == "reviews") section = Section::Reviews;
      else if (k == "cards") section = Section::Cards;
      else if (k == "counts") section = Section::Counts;
      else if (k == "sync") section = Section::Sync;
      else section = Section::Other;
    }
  };
  cb.onObjectStart = [&]() {
    depth++;
    if (section == Section::Cards && depth == 2) {
      card = AnkiCard();
      cardDepth = depth;
    }
    if (section == Section::Reviews && depth == 2) reviewClientId.clear();
  };
  cb.onObjectEnd = [&]() {
    if (section == Section::Cards && depth == cardDepth && cardsOut) {
      if (card.cardId != 0) {
        const std::string line = AnkiCardCache::toLine(card);
        if (cardsOut->write(line) != line.size()) writeFailed = true;
        r.cardsFetched++;
      }
      cardDepth = 0;
    }
    if (section == Section::Reviews && depth == 2 && !reviewClientId.empty()) acked.push_back(reviewClientId);
    depth--;
    // The section stays active until the next top-level key replaces it
    // (onKey at depth 1); an array section holds many objects.
  };
  cb.onArrayStart = [&]() {
    if (section == Section::Cards && depth == cardDepth) {
      if (key == "next") nextIndex = 0;
      else if (key == "media") inMediaArray = true;
    }
  };
  cb.onArrayEnd = [&]() {
    nextIndex = -1;
    inMediaArray = false;
  };
  cb.onString = [&](const std::string& v) {
    if (section == Section::Cards && cardDepth != 0) {
      if (nextIndex >= 0) {
        if (nextIndex < 4) card.next[nextIndex] = v;
        nextIndex++;
      } else if (inMediaArray) {
        card.mediaCount++;
      } else if (depth == cardDepth) {
        if (key == "deck") card.deck = v;
        else if (key == "kind") card.kind = v;
        else if (key == "q") card.q = v;
        else if (key == "a") card.a = v;
      }
    } else if (section == Section::Reviews && depth == 2) {
      if (key == "client_id") reviewClientId = v;
    } else if (section == Section::Sync) {
      if (key == "code" && syncErrCode.empty()) syncErrCode = v;
      if (key == "message" && syncErrMessage.empty()) syncErrMessage = v;
    }
  };
  cb.onNumber = [&](const std::string& v) {
    if (section == Section::Cards && cardDepth != 0 && depth == cardDepth && key == "card_id") {
      card.cardId = ankijson::toInt64(v);
    } else if (section == Section::Counts && depth == 2) {
      const int64_t n = ankijson::toInt64(v);
      if (key == "new") counts.newCount = static_cast<uint16_t>(n);
      else if (key == "learning") counts.learning = static_cast<uint16_t>(n);
      else if (key == "due") counts.due = static_cast<uint16_t>(n);
      else if (key == "returned") counts.returned = static_cast<uint16_t>(n);
    }
  };
  ankijson::Reader reader(std::move(cb));

  std::string errorBody;  // only kept for non-2xx responses (small)
  const int status = http.request("POST", account.apiBase() + "/exchange", authHeaders(account), body,
                                  [&](int httpStatus, const uint8_t* data, size_t len) {
                                    if (httpStatus >= 200 && httpStatus < 300) {
                                      reader.feed(reinterpret_cast<const char*>(data), len);
                                      return !reader.hasError() && !writeFailed;
                                    }
                                    if (errorBody.size() < 1024)
                                      errorBody.append(reinterpret_cast<const char*>(data), len);
                                    return true;
                                  });
  classify(r, status);

  if (cardsOut) cardsOut->close();

  if (!r.ok) {
    if (cardsOut) fs.remove(cardsTmp);
    ErrorCollector ec;
    ec.reader.feed(errorBody);
    r.error = ec.text().empty() ? statusText(status) : ec.text();
    return r;
  }
  if (reader.hasError() || writeFailed) {
    if (cardsOut) fs.remove(cardsTmp);
    r.ok = false;
    r.error = writeFailed ? "SD write failed" : "Bad response";
    return r;
  }

  if (!acked.empty()) journal.removeAcked(acked);
  r.reviewsAcked = acked.size();
  r.counts = counts;
  if (!syncErrCode.empty() || !syncErrMessage.empty()) {
    syncError = syncErrMessage.empty() ? syncErrCode : syncErrMessage;
  }
  r.syncError = syncError;

  if (wantCards) {
    AnkiCardCache cache(fs, ankipaths::cardsFile(account.id), ankipaths::cacheMetaFile(account.id));
    if (fs.exists(cache.path())) fs.remove(cache.path());
    fs.rename(cardsTmp, cache.path());
    AnkiCardCache::Meta meta;
    meta.counts = counts;
    meta.fetchedAt = clock ? clock() : 0;
    meta.syncError = syncError;
    cache.writeMeta(meta);
  }
  return r;
}

AnkiSyncEngine::Result AnkiSyncEngine::pushNotes(const AnkiAccount& account) {
  Result r;
  AnkiNoteQueue queue(fs, ankipaths::notesFile(account.id));
  const std::vector<AnkiNoteQueue::Note> notes = queue.load();
  r.notesSent = notes.size();
  if (notes.empty()) {
    r.ok = true;
    return r;
  }
  const std::string body = AnkiNoteQueue::toNotesRequestJson(notes);

  // Response: {"results":[{"status":..,"client_id":..,...}]}. Every result
  // with a client_id and a status other than "error" is done for good.
  std::vector<std::string> acked;
  std::string key;
  std::string clientId;
  std::string status;
  std::string firstError;
  int depth = 0;
  ankijson::Reader::Callbacks cb;
  cb.onKey = [&](const std::string& k) { key = k; };
  cb.onObjectStart = [&]() {
    depth++;
    if (depth == 2) {
      clientId.clear();
      status.clear();
    }
  };
  cb.onObjectEnd = [&]() {
    if (depth == 2 && !clientId.empty() && !status.empty() && status != "error") acked.push_back(clientId);
    depth--;
  };
  cb.onString = [&](const std::string& v) {
    if (depth == 2 && key == "client_id") clientId = v;
    else if (depth == 2 && key == "status") status = v;
    else if (depth == 3 && key == "message" && firstError.empty()) firstError = v;
  };
  ankijson::Reader reader(std::move(cb));

  std::string errorBody;
  const int httpStatus = http.request("POST", account.apiBase() + "/notes", authHeaders(account), body,
                                      [&](int st, const uint8_t* data, size_t len) {
                                        if (st >= 200 && st < 300) {
                                          reader.feed(reinterpret_cast<const char*>(data), len);
                                        } else if (errorBody.size() < 1024) {
                                          errorBody.append(reinterpret_cast<const char*>(data), len);
                                        }
                                        return true;
                                      });
  classify(r, httpStatus);
  if (!r.ok) {
    ErrorCollector ec;
    ec.reader.feed(errorBody);
    r.error = ec.text().empty() ? statusText(httpStatus) : ec.text();
    return r;
  }
  if (reader.hasError()) {
    r.ok = false;
    r.error = "Bad response";
    return r;
  }
  if (!acked.empty()) queue.removeAcked(acked);
  r.notesAcked = acked.size();
  if (acked.size() < notes.size() && !firstError.empty()) r.error = firstError;  // informational
  return r;
}

AnkiSyncEngine::Result AnkiSyncEngine::fetchDecks(const AnkiAccount& account) {
  Result r;
  fs.mkdirs(ankipaths::accountDir(account.id));
  // Re-serialize the deck list compactly while it streams:
  // {"decks":[{"name":..,"new":..,"learning":..,"due":..},...]}
  std::string out = "{\"decks\":[";
  bool first = true;
  std::string key;
  AnkiDeck deck;
  int depth = 0;
  ankijson::Reader::Callbacks cb;
  cb.onKey = [&](const std::string& k) { key = k; };
  cb.onObjectStart = [&]() {
    depth++;
    if (depth == 2) deck = AnkiDeck();
  };
  cb.onObjectEnd = [&]() {
    if (depth == 2 && !deck.name.empty()) {
      if (!first) out.push_back(',');
      first = false;
      out += "{\"name\":";
      ankijson::appendQuoted(out, deck.name);
      out += ",\"new\":" + std::to_string(deck.newCount) + ",\"learning\":" + std::to_string(deck.learning) +
             ",\"due\":" + std::to_string(deck.due) + "}";
    }
    depth--;
  };
  cb.onString = [&](const std::string& v) {
    if (depth == 2 && key == "name") deck.name = v;
  };
  cb.onNumber = [&](const std::string& v) {
    if (depth != 2) return;
    const int64_t n = ankijson::toInt64(v);
    if (key == "new") deck.newCount = static_cast<uint16_t>(n);
    else if (key == "learning") deck.learning = static_cast<uint16_t>(n);
    else if (key == "due") deck.due = static_cast<uint16_t>(n);
  };
  ankijson::Reader reader(std::move(cb));

  std::string errorBody;
  const int status = http.request("GET", account.apiBase() + "/decks", authHeaders(account), "",
                                  [&](int st, const uint8_t* data, size_t len) {
                                    if (st >= 200 && st < 300) {
                                      reader.feed(reinterpret_cast<const char*>(data), len);
                                    } else if (errorBody.size() < 1024) {
                                      errorBody.append(reinterpret_cast<const char*>(data), len);
                                    }
                                    return true;
                                  });
  classify(r, status);
  if (!r.ok) {
    ErrorCollector ec;
    ec.reader.feed(errorBody);
    r.error = ec.text().empty() ? statusText(status) : ec.text();
    return r;
  }
  if (reader.hasError()) {
    r.ok = false;
    r.error = "Bad response";
    return r;
  }
  out += "]}";
  if (!fs.writeAllAtomic(ankipaths::decksFile(account.id), out)) {
    r.ok = false;
    r.error = "SD write failed";
  }
  return r;
}

std::vector<AnkiDeck> AnkiSyncEngine::loadDecks(uint32_t accountId) {
  std::vector<AnkiDeck> decks;
  std::string text;
  if (!fs.readAll(ankipaths::decksFile(accountId), text)) return decks;
  std::string key;
  AnkiDeck deck;
  int depth = 0;
  ankijson::Reader::Callbacks cb;
  cb.onKey = [&](const std::string& k) { key = k; };
  cb.onObjectStart = [&]() {
    depth++;
    if (depth == 2) deck = AnkiDeck();
  };
  cb.onObjectEnd = [&]() {
    if (depth == 2 && !deck.name.empty()) decks.push_back(deck);
    depth--;
  };
  cb.onString = [&](const std::string& v) {
    if (depth == 2 && key == "name") deck.name = v;
  };
  cb.onNumber = [&](const std::string& v) {
    if (depth != 2) return;
    const int64_t n = ankijson::toInt64(v);
    if (key == "new") deck.newCount = static_cast<uint16_t>(n);
    else if (key == "learning") deck.learning = static_cast<uint16_t>(n);
    else if (key == "due") deck.due = static_cast<uint16_t>(n);
  };
  ankijson::Reader reader(std::move(cb));
  reader.feed(text);
  return decks;
}

bool AnkiSyncEngine::hasPending(const AnkiAccount& account) {
  AnkiJournal journal(fs, ankipaths::reviewsFile(account.id));
  if (!journal.empty()) return true;
  AnkiNoteQueue queue(fs, ankipaths::notesFile(account.id));
  return !queue.empty();
}

AnkiSyncEngine::Result AnkiSyncEngine::syncAccount(const AnkiAccount& account, bool wantCards) {
  Result total = exchange(account, wantCards);
  if (!total.ok) return total;

  const Result notes = pushNotes(account);
  total.notesSent = notes.notesSent;
  total.notesAcked = notes.notesAcked;
  if (!notes.ok) {
    total.ok = false;
    total.httpStatus = notes.httpStatus;
    total.error = notes.error;
    total.authFailed = notes.authFailed;
    return total;
  }
  if (total.error.empty()) total.error = notes.error;  // per-note failure text, informational

  const Result decks = fetchDecks(account);
  if (!decks.ok) {
    // Deck list is a convenience; a failure here does not fail the session,
    // but surface it when nothing else went wrong.
    if (total.error.empty()) total.error = decks.error;
  }
  return total;
}
