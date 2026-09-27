#include "AnkiConnectClient.h"

#include <algorithm>
#include <cctype>

#include "AnkiCardCache.h"
#include "AnkiHtml.h"
#include "AnkiJournal.h"
#include "AnkiNoteQueue.h"
#include "AnkiPaths.h"
#include "AnkiSyncEngine.h"

namespace {

constexpr size_t MAX_ERROR_BODY = 512;

bool containsNoCase(const std::string& hay, const char* needle) {
  const size_t n = std::char_traits<char>::length(needle);
  if (n == 0 || hay.size() < n) return false;
  for (size_t i = 0; i + n <= hay.size(); i++) {
    size_t k = 0;
    while (k < n &&
           std::tolower(static_cast<unsigned char>(hay[i + k])) == std::tolower(static_cast<unsigned char>(needle[k])))
      k++;
    if (k == n) return true;
  }
  return false;
}

std::string statusText(const int status) {
  if (status <= 0) return "No connection";
  return "HTTP " + std::to_string(status);
}

// One quoted deck term. Inside a quoted search term Anki treats a backslash
// as an escape and * and _ as wildcards.
void appendDeckTerm(std::string& out, const std::string& deck) {
  out += "\"deck:";
  for (const char c : deck) {
    if (c == '"' || c == '\\' || c == '*' || c == '_') out.push_back('\\');
    out.push_back(c);
  }
  out.push_back('"');
}

uint16_t saturate16(const int64_t v) { return static_cast<uint16_t>(std::clamp<int64_t>(v, 0, 65535)); }

}  // namespace

std::string AnkiConnectClient::requestBody(const AnkiAccount& account, const char* action, const std::string& params) {
  std::string body = "{\"action\":";
  ankijson::appendQuoted(body, action);
  body += ",\"version\":6";
  if (!account.token.empty()) {
    body += ",\"key\":";
    ankijson::appendQuoted(body, account.token);
  }
  if (!params.empty()) body += ",\"params\":" + params;
  body.push_back('}');
  return body;
}

std::string AnkiConnectClient::buildSearch(const AnkiAccount& account, const char* state) {
  std::string q;
  if (!account.decks.empty()) {
    q.push_back('(');
    for (size_t i = 0; i < account.decks.size(); i++) {
      if (i) q += " OR ";
      appendDeckTerm(q, account.decks[i]);
    }
    q += ") ";
  }
  q += state;
  return q;
}

AnkiConnectClient::Call AnkiConnectClient::call(const AnkiAccount& account, const char* action,
                                                const std::string& params, ankijson::Reader::Callbacks inner) {
  Call c;
  if (account.url.empty()) {
    c.error = "No URL";
    return c;
  }
  const std::string body = requestBody(account, action, params);

  // {"result": <value>, "error": null | "message"}: events under "result" go
  // to the caller, the error string is kept here.
  int depth = 0;
  std::string topKey;
  bool inResult = false;
  std::string errorText;
  ankijson::Reader::Callbacks cb;
  cb.onKey = [&](const std::string& k) {
    if (depth == 1) {
      topKey = k;
      inResult = k == "result";
    } else if (inResult && inner.onKey) {
      inner.onKey(k);
    }
  };
  cb.onObjectStart = [&]() {
    if (inResult && inner.onObjectStart) inner.onObjectStart();
    depth++;
  };
  cb.onObjectEnd = [&]() {
    depth--;
    if (depth >= 1 && inResult && inner.onObjectEnd) inner.onObjectEnd();
  };
  cb.onArrayStart = [&]() {
    if (inResult && inner.onArrayStart) inner.onArrayStart();
    depth++;
  };
  cb.onArrayEnd = [&]() {
    depth--;
    if (inResult && inner.onArrayEnd) inner.onArrayEnd();
  };
  cb.onString = [&](const std::string& v) {
    if (inResult) {
      if (inner.onString) inner.onString(v);
    } else if (depth == 1 && topKey == "error") {
      errorText = v;
    }
  };
  cb.onNumber = [&](const std::string& v) {
    if (inResult && inner.onNumber) inner.onNumber(v);
  };
  cb.onBool = [&](const bool v) {
    if (inResult && inner.onBool) inner.onBool(v);
  };
  cb.onNull = [&]() {
    if (inResult && inner.onNull) inner.onNull();
  };
  ankijson::Reader reader(std::move(cb));

  std::string errorBody;  // only kept for non-2xx responses (small)
  const std::vector<AnkiHttp::Header> headers = {{"Content-Type", "application/json"}, {"Accept", "application/json"}};
  c.status = http.request("POST", account.url, headers, body, [&](int st, const uint8_t* data, size_t len) {
    if (st >= 200 && st < 300) {
      reader.feed(reinterpret_cast<const char*>(data), len);
      return !reader.hasError();
    }
    if (errorBody.size() < MAX_ERROR_BODY) errorBody.append(reinterpret_cast<const char*>(data), len);
    return true;
  });
  if (c.status < 200 || c.status >= 300) {
    c.error = statusText(c.status);
    return c;
  }
  if (reader.hasError()) {
    c.error = "Bad response";
    return c;
  }
  if (!errorText.empty()) {
    c.error = errorText;
    return c;
  }
  c.ok = true;
  return c;
}

void AnkiConnectClient::fail(AnkiSyncResult& r, const Call& c) {
  r.ok = false;
  r.httpStatus = c.status;
  r.error = c.error;
  // AnkiConnect answers a wrong key with HTTP 200 and this message.
  r.authFailed = containsNoCase(c.error, "api key");
}

bool AnkiConnectClient::ensureProfile(const AnkiAccount& account, AnkiSyncResult& r) {
  if (account.profile.empty() || loadedProfile == account.profile) return true;
  std::string params = "{\"name\":";
  ankijson::appendQuoted(params, account.profile);
  params.push_back('}');
  bool loaded = false;
  ankijson::Reader::Callbacks cb;
  cb.onBool = [&](const bool v) { loaded = v; };
  const Call c = call(account, "loadProfile", params, std::move(cb));
  if (!c.ok) {
    fail(r, c);
    return false;
  }
  if (!loaded) {
    r.ok = false;
    r.httpStatus = c.status;
    r.error = "Profile not found: " + account.profile;
    return false;
  }
  loadedProfile = account.profile;
  return true;
}

bool AnkiConnectClient::fetchDeckList(const AnkiAccount& account, std::vector<AnkiDeck>& decks, AnkiSyncResult& r) {
  decks.clear();
  decks.reserve(16);
  {
    int depth = 0;
    ankijson::Reader::Callbacks cb;
    cb.onArrayStart = [&]() { depth++; };
    cb.onArrayEnd = [&]() { depth--; };
    cb.onString = [&](const std::string& v) {
      if (depth != 1) return;
      AnkiDeck d;
      d.name = v;
      decks.push_back(std::move(d));
    };
    const Call c = call(account, "deckNames", "", std::move(cb));
    if (!c.ok) {
      fail(r, c);
      return false;
    }
    r.httpStatus = c.status;
  }
  if (decks.empty()) return true;

  // getDeckStats: {"<deck id>": {"name":..,"new_count":..,"learn_count":..,"review_count":..}, ...}
  std::string params = "{\"decks\":[";
  for (size_t i = 0; i < decks.size(); i++) {
    if (i) params.push_back(',');
    ankijson::appendQuoted(params, decks[i].name);
  }
  params += "]}";
  std::string key;
  AnkiDeck cur;
  int depth = 0;
  ankijson::Reader::Callbacks cb;
  cb.onKey = [&](const std::string& k) { key = k; };
  cb.onObjectStart = [&]() {
    depth++;
    if (depth == 2) cur = AnkiDeck();
  };
  cb.onObjectEnd = [&]() {
    if (depth == 2) {
      for (AnkiDeck& d : decks) {
        if (d.name != cur.name) continue;
        d.newCount = cur.newCount;
        d.learning = cur.learning;
        d.due = cur.due;
        break;
      }
    }
    depth--;
  };
  cb.onString = [&](const std::string& v) {
    if (depth == 2 && key == "name") cur.name = v;
  };
  cb.onNumber = [&](const std::string& v) {
    if (depth != 2) return;
    const int64_t n = ankijson::toInt64(v);
    if (key == "new_count")
      cur.newCount = saturate16(n);
    else if (key == "learn_count")
      cur.learning = saturate16(n);
    else if (key == "review_count")
      cur.due = saturate16(n);
  };
  const Call c = call(account, "getDeckStats", params, std::move(cb));
  // Counts are a convenience: an AnkiConnect too old for getDeckStats still
  // yields the names. A rejected key is not.
  if (!c.ok && containsNoCase(c.error, "api key")) {
    fail(r, c);
    return false;
  }
  return true;
}

AnkiSyncResult AnkiConnectClient::fetchDecks(const AnkiAccount& account) {
  AnkiSyncResult r;
  fs.mkdirs(ankipaths::accountDir(account.id));
  if (!ensureProfile(account, r)) return r;
  std::vector<AnkiDeck> decks;
  if (!fetchDeckList(account, decks, r)) return r;
  if (!fs.writeAllAtomic(ankipaths::decksFile(account.id), ankidecks::toJson(decks))) {
    r.ok = false;
    r.error = "SD write failed";
    return r;
  }
  r.ok = true;
  r.decksFetched = decks.size();
  return r;
}

bool AnkiConnectClient::findCards(const AnkiAccount& account, const char* state, const size_t limit,
                                  std::vector<int64_t>& ids, AnkiSyncResult& r) {
  if (limit == 0) return true;
  std::string params = "{\"query\":";
  ankijson::appendQuoted(params, buildSearch(account, state));
  params.push_back('}');
  size_t taken = 0;
  int depth = 0;
  ankijson::Reader::Callbacks cb;
  cb.onArrayStart = [&]() { depth++; };
  cb.onArrayEnd = [&]() { depth--; };
  cb.onNumber = [&](const std::string& v) {
    // The whole id list streams through; only the first `limit` are kept.
    if (depth == 1 && taken < limit) {
      ids.push_back(ankijson::toInt64(v));
      taken++;
    }
  };
  const Call c = call(account, "findCards", params, std::move(cb));
  if (!c.ok) {
    fail(r, c);
    return false;
  }
  return true;
}

bool AnkiConnectClient::fetchQueue(const AnkiAccount& account, AnkiSyncResult& r) {
  std::vector<AnkiDeck> decks;
  if (!fetchDeckList(account, decks, r)) return false;
  if (fs.writeAllAtomic(ankipaths::decksFile(account.id), ankidecks::toJson(decks))) r.decksFetched = decks.size();

  // Today's counts for the reviewed decks (top-level ones when all decks are
  // reviewed: a parent's numbers include its children). new_count already
  // honours the deck's daily limit, so it caps the new cards fetched.
  int64_t newSum = 0;
  int64_t learnSum = 0;
  int64_t dueSum = 0;
  for (const AnkiDeck& d : decks) {
    const bool selected = account.decks.empty()
                              ? d.name.find("::") == std::string::npos
                              : std::find(account.decks.begin(), account.decks.end(), d.name) != account.decks.end();
    if (!selected) continue;
    newSum += d.newCount;
    learnSum += d.learning;
    dueSum += d.due;
  }
  AnkiCounts counts;
  counts.newCount = saturate16(newSum);
  counts.learning = saturate16(learnSum);
  counts.due = saturate16(dueSum);

  const size_t limit = account.cacheSize == 0 ? 60 : account.cacheSize;
  std::vector<int64_t> ids;
  ids.reserve(limit);
  // Learning cards first (they were failed recently), then reviews, then new.
  // The three searches are disjoint: is:learn is queue-based, is:new is type-based.
  if (!findCards(account, "is:learn -is:suspended -is:buried", limit, ids, r)) return false;
  if (!findCards(account, "is:due -is:learn", limit - ids.size(), ids, r)) return false;
  size_t newCap = std::min(limit - ids.size(), static_cast<size_t>(counts.newCount));
  if (account.maxNewPerDay > 0) newCap = std::min(newCap, static_cast<size_t>(account.maxNewPerDay));
  if (!findCards(account, "is:new -is:suspended -is:buried", newCap, ids, r)) return false;

  const std::string cardsTmp = ankipaths::cardsFile(account.id) + ".tmp";
  std::unique_ptr<AnkiFile> out = fs.open(cardsTmp, AnkiFs::Mode::Write);
  if (!out) {
    r.ok = false;
    r.error = "SD write failed";
    return false;
  }
  bool writeFailed = false;
  for (size_t start = 0; start < ids.size() && !writeFailed; start += cardsPerRequest) {
    const size_t end = std::min(ids.size(), start + cardsPerRequest);
    std::string params = "{\"cards\":[";
    for (size_t i = start; i < end; i++) {
      if (i > start) params.push_back(',');
      params += std::to_string(ids[i]);
    }
    params += "]}";

    // cardsInfo: [{"cardId":..,"question":<html>,"answer":<html>,"deckName":..,"queue":n,...}]
    // Each card is converted and written as soon as its object closes.
    AnkiCard card;
    std::string key;
    int depth = 0;
    int64_t queue = 0;
    ankijson::Reader::Callbacks cb;
    cb.onKey = [&](const std::string& k) { key = k; };
    cb.onArrayStart = [&]() { depth++; };
    cb.onArrayEnd = [&]() { depth--; };
    cb.onObjectStart = [&]() {
      depth++;
      if (depth == 2) {
        card = AnkiCard();
        queue = 0;
      }
    };
    cb.onObjectEnd = [&]() {
      if (depth == 2 && card.cardId != 0 && !writeFailed) {
        // Anki queues: 0 new, 1 learning, 2 review, 3 day-learning.
        card.kind = queue == 0 ? "new" : (queue == 1 || queue == 3) ? "learning" : "due";
        const std::string line = AnkiCardCache::toLine(card);
        if (out->write(line) != line.size())
          writeFailed = true;
        else
          r.cardsFetched++;
        card = AnkiCard();  // release the text before the next card streams in
      }
      depth--;
    };
    cb.onString = [&](const std::string& v) {
      if (depth != 2) return;
      unsigned images = 0;
      if (key == "question") {
        card.q = ankihtml::toMarkup(v, &images);
      } else if (key == "answer") {
        card.a = ankihtml::toMarkup(ankihtml::answerPart(v), &images);
      } else if (key == "deckName") {
        card.deck = v;
        return;
      } else {
        return;
      }
      card.mediaCount = static_cast<uint8_t>(std::min<unsigned>(255, card.mediaCount + images));
    };
    cb.onNumber = [&](const std::string& v) {
      if (depth != 2) return;
      if (key == "cardId")
        card.cardId = ankijson::toInt64(v);
      else if (key == "queue")
        queue = ankijson::toInt64(v);
    };
    const Call c = call(account, "cardsInfo", params, std::move(cb));
    if (!c.ok) {
      out->close();
      fs.remove(cardsTmp);
      fail(r, c);
      return false;
    }
  }
  out->close();
  if (writeFailed) {
    fs.remove(cardsTmp);
    r.ok = false;
    r.error = "SD write failed";
    return false;
  }

  AnkiCardCache cache(fs, ankipaths::cardsFile(account.id), ankipaths::cacheMetaFile(account.id));
  if (fs.exists(cache.path())) fs.remove(cache.path());
  fs.rename(cardsTmp, cache.path());
  AnkiCardCache::Meta meta;
  meta.counts = counts;
  meta.counts.returned = saturate16(static_cast<int64_t>(r.cardsFetched));
  meta.fetchedAt = clock ? clock() : 0;
  cache.writeMeta(meta);
  r.counts = meta.counts;
  return true;
}

AnkiSyncResult AnkiConnectClient::exchange(const AnkiAccount& account, const bool wantCards) {
  AnkiSyncResult r;
  AnkiJournal journal(fs, ankipaths::reviewsFile(account.id));
  const std::vector<AnkiJournal::Entry> pending = journal.load();
  r.reviewsSent = pending.size();
  if (pending.empty() && !wantCards) {
    r.ok = true;  // nothing to do for a non-review account
    return r;
  }
  fs.mkdirs(ankipaths::accountDir(account.id));
  if (!ensureProfile(account, r)) return r;

  if (!pending.empty()) {
    // answerCards grades each card as if answered now; the journal's
    // answered_at has no counterpart here.
    std::string params = "{\"answers\":[";
    for (size_t i = 0; i < pending.size(); i++) {
      if (i) params.push_back(',');
      params +=
          "{\"cardId\":" + std::to_string(pending[i].cardId) + ",\"ease\":" + std::to_string(pending[i].ease) + "}";
    }
    params += "]}";
    const Call c = call(account, "answerCards", params, ankijson::Reader::Callbacks{});
    if (!c.ok) {
      fail(r, c);
      return r;
    }
    // No per-review status comes back (false only means the card is gone), so
    // the whole batch is done once the request was processed.
    std::vector<std::string> acked;
    acked.reserve(pending.size());
    for (const AnkiJournal::Entry& e : pending) acked.push_back(e.clientId);
    journal.removeAcked(acked);
    r.reviewsAcked = acked.size();
    r.httpStatus = c.status;
  }
  r.ok = true;
  if (wantCards) fetchQueue(account, r);  // sets ok=false and error on failure
  return r;
}

std::pair<std::string, std::string> AnkiConnectClient::fieldNames(const AnkiAccount& account,
                                                                  const std::string& model) {
  const auto it = modelFields.find(model);
  if (it != modelFields.end()) return it->second;
  std::pair<std::string, std::string> names{"Front", "Back"};
  std::vector<std::string> found;
  found.reserve(2);
  std::string params = "{\"modelName\":";
  ankijson::appendQuoted(params, model);
  params.push_back('}');
  int depth = 0;
  ankijson::Reader::Callbacks cb;
  cb.onArrayStart = [&]() { depth++; };
  cb.onArrayEnd = [&]() { depth--; };
  cb.onString = [&](const std::string& v) {
    if (depth == 1 && found.size() < 2) found.push_back(v);
  };
  const Call c = call(account, "modelFieldNames", params, std::move(cb));
  if (c.ok && found.size() == 2) names = {found[0], found[1]};
  // Remember the answer for the session, including "model was not found":
  // addNote will report that itself, once per note, without another lookup.
  if (c.status >= 200 && c.status < 300) modelFields[model] = names;
  return names;
}

AnkiSyncResult AnkiConnectClient::pushNotes(const AnkiAccount& account) {
  AnkiSyncResult r;
  AnkiNoteQueue queue(fs, ankipaths::notesFile(account.id));
  const std::vector<AnkiNoteQueue::Note> notes = queue.load();
  r.notesSent = notes.size();
  if (notes.empty()) {
    r.ok = true;
    return r;
  }
  if (!ensureProfile(account, r)) return r;

  std::vector<std::string> acked;
  acked.reserve(notes.size());
  std::string firstError;
  bool failed = false;
  Call last;
  for (const AnkiNoteQueue::Note& n : notes) {
    const std::pair<std::string, std::string> fields = fieldNames(account, n.model);
    std::string params = "{\"note\":{\"deckName\":";
    ankijson::appendQuoted(params, n.deck);
    params += ",\"modelName\":";
    ankijson::appendQuoted(params, n.model);
    params += ",\"fields\":{";
    ankijson::appendQuoted(params, fields.first);
    params.push_back(':');
    ankijson::appendQuoted(params, n.front);
    params.push_back(',');
    ankijson::appendQuoted(params, fields.second);
    params.push_back(':');
    ankijson::appendQuoted(params, n.back);
    params += "},\"tags\":[";
    for (size_t t = 0; t < n.tags.size(); t++) {
      if (t) params.push_back(',');
      ankijson::appendQuoted(params, n.tags[t]);
    }
    params += "],\"options\":{\"allowDuplicate\":false,\"duplicateScope\":\"deck\"}}}";

    const Call c = call(account, "addNote", params, ankijson::Reader::Callbacks{});
    if (c.ok) {
      acked.push_back(n.clientId);
      continue;
    }
    if (c.status < 200 || c.status >= 300 || containsNoCase(c.error, "api key")) {
      failed = true;  // transport or auth: stop, keep the rest queued
      last = c;
      break;
    }
    // Duplicates and empty notes are refused for good; a missing deck or note
    // type waits for a settings fix (the note stays queued, the text is shown).
    if (containsNoCase(c.error, "duplicate") || containsNoCase(c.error, "empty")) {
      acked.push_back(n.clientId);
      continue;
    }
    if (firstError.empty()) firstError = c.error;
  }
  if (!acked.empty()) queue.removeAcked(acked);
  r.notesAcked = acked.size();
  if (failed) {
    fail(r, last);
    return r;
  }
  r.ok = true;
  r.error = firstError;  // informational
  return r;
}
