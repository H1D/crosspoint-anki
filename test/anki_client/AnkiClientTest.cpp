#include <gtest/gtest.h>

#include <map>
#include <memory>
#include <string>
#include <vector>

#include "AnkiCardCache.h"
#include "AnkiFs.h"
#include "AnkiHttp.h"
#include "AnkiJournal.h"
#include "AnkiJson.h"
#include "AnkiMarkup.h"
#include "AnkiNoteQueue.h"
#include "AnkiPaths.h"
#include "AnkiSyncEngine.h"

namespace {

// ---------------------------------------------------------------------------
// In-memory filesystem
class MemFs final : public AnkiFs {
 public:
  std::map<std::string, std::string> files;

  class File final : public AnkiFile {
   public:
    File(std::string* data, bool writable, size_t pos) : data(data), writable(writable), pos(pos) {}
    int read(void* buf, size_t len) override {
      if (writable || pos >= data->size()) return 0;
      const size_t n = std::min(len, data->size() - pos);
      memcpy(buf, data->data() + pos, n);
      pos += n;
      return static_cast<int>(n);
    }
    size_t write(const void* buf, size_t len) override {
      if (!writable) return 0;
      data->append(static_cast<const char*>(buf), len);
      return len;
    }
    bool close() override { return true; }

   private:
    std::string* data;
    bool writable;
    size_t pos;
  };

  std::unique_ptr<AnkiFile> open(const std::string& path, Mode mode) override {
    if (mode == Mode::Read) {
      auto it = files.find(path);
      if (it == files.end()) return nullptr;
      return std::make_unique<File>(&it->second, false, 0);
    }
    if (mode == Mode::Write) files[path].clear();
    return std::make_unique<File>(&files[path], true, 0);
  }
  bool exists(const std::string& path) override { return files.count(path) > 0; }
  bool remove(const std::string& path) override { return files.erase(path) > 0; }
  bool rename(const std::string& from, const std::string& to) override {
    auto it = files.find(from);
    if (it == files.end()) return false;
    files[to] = it->second;
    files.erase(it);
    return true;
  }
  bool mkdirs(const std::string&) override { return true; }
  bool removeDir(const std::string& path) override {
    for (auto it = files.begin(); it != files.end();) {
      if (it->first.rfind(path + "/", 0) == 0)
        it = files.erase(it);
      else
        ++it;
    }
    return true;
  }
};

// Fake transport: canned status/body per URL suffix, records requests.
class FakeHttp final : public AnkiHttp {
 public:
  struct Canned {
    int status;
    std::string body;
  };
  std::map<std::string, Canned> responses;  // key: method + " " + url suffix after apiBase
  struct Seen {
    std::string method;
    std::string url;
    std::vector<Header> headers;
    std::string body;
  };
  std::vector<Seen> seen;
  size_t chunk = 7;  // feed bodies in odd-sized chunks to exercise streaming

  int request(const char* method, const std::string& url, const std::vector<Header>& headers, const std::string& body,
              const DataCallback& onData) override {
    seen.push_back({method, url, headers, body});
    for (const auto& [key, canned] : responses) {
      const std::string m = key.substr(0, key.find(' '));
      const std::string suffix = key.substr(key.find(' ') + 1);
      if (m == method && url.size() >= suffix.size() &&
          url.compare(url.size() - suffix.size(), suffix.size(), suffix) == 0) {
        for (size_t i = 0; i < canned.body.size(); i += chunk) {
          const size_t n = std::min(chunk, canned.body.size() - i);
          if (!onData(canned.status, reinterpret_cast<const uint8_t*>(canned.body.data() + i), n)) break;
        }
        return canned.status;
      }
    }
    return -1;
  }
};

AnkiAccount testAccount() {
  AnkiAccount a;
  a.id = 3;
  a.name = "Test";
  a.url = "http://anki.local:8766";
  a.profile = "alice";
  a.token = "akd_x";
  a.decks = {"Dutch::Common"};
  a.cacheSize = 40;
  a.maxNewPerDay = 5;
  return a;
}

const char* kExchangeResponse = R"({
  "reviews": [
    {"status": "applied", "card_id": 111, "client_id": "dev-1"},
    {"status": "duplicate", "card_id": 112, "client_id": "dev-2"},
    {"status": "rejected", "reason": "stale", "card_id": 113, "client_id": "dev-3"}
  ],
  "sync": {"error": {"code": "profile_busy", "message": "sync timed out", "retryable": true}},
  "cards": [
    {"card_id": 1790433421131, "note_id": 5, "deck": "Ankido::Test", "q": "e2e **front** é", "a": "back \"one\"",
     "media": ["a.wav", "b.png"], "interval_days": 0, "due": 1790434053, "queue": "learning", "type": "learning",
     "deck_rank": 0, "kind": "learning", "next": ["<⁨1⁩m", "<⁨10⁩m", "⁨1⁩d", "⁨4⁩d"]},
    {"card_id": 22, "deck": "D2", "q": "line1\nline2", "a": "a2", "media": [], "kind": "new",
     "next": ["1m", "6m", "10m", "3d"], "nested": {"deep": {"x": [1, 2, {"y": "z"}]}}}
  ],
  "counts": {"new": 7, "learning": 1, "due": 0, "returned": 2},
  "next_cursor": null,
  "decks": ["Ankido"]
})";

}  // namespace

// ---------------------------------------------------------------------------
TEST(AnkiJson, QuotingRoundTrip) {
  std::string out;
  ankijson::appendQuoted(out, std::string("a\"b\\c\nd\te\x01"));
  EXPECT_EQ(out, "\"a\\\"b\\\\c\\nd\\te\\u0001\"");

  std::string got;
  ankijson::Reader::Callbacks cb;
  cb.onString = [&](const std::string& v) { got = v; };
  ankijson::Reader r(std::move(cb));
  r.feed("[" + out + "]");
  EXPECT_FALSE(r.hasError());
  EXPECT_EQ(got, "a\"b\\c\nd\te\x01");
}

TEST(AnkiJson, UnicodeEscapesAndSurrogates) {
  std::string got;
  ankijson::Reader::Callbacks cb;
  cb.onString = [&](const std::string& v) { got = v; };
  ankijson::Reader r(std::move(cb));
  r.feed(R"({"k":"é⁨x😀"})");
  EXPECT_FALSE(r.hasError());
  EXPECT_EQ(got, "\xC3\xA9\xE2\x81\xA8x\xF0\x9F\x98\x80");
}

TEST(AnkiJson, KeysValuesAndNestingAcrossChunks) {
  std::vector<std::string> events;
  ankijson::Reader::Callbacks cb;
  cb.onKey = [&](const std::string& k) { events.push_back("K:" + k); };
  cb.onString = [&](const std::string& v) { events.push_back("S:" + v); };
  cb.onNumber = [&](const std::string& v) { events.push_back("N:" + v); };
  cb.onBool = [&](bool b) { events.push_back(b ? "T" : "F"); };
  cb.onNull = [&]() { events.push_back("null"); };
  cb.onObjectStart = [&]() { events.push_back("{"); };
  cb.onObjectEnd = [&]() { events.push_back("}"); };
  cb.onArrayStart = [&]() { events.push_back("["); };
  cb.onArrayEnd = [&]() { events.push_back("]"); };
  ankijson::Reader r(std::move(cb));
  const std::string doc = R"({"a":[1,-2.5e3,"s",true,null,{"b":false}],"c":"d"})";
  for (size_t i = 0; i < doc.size(); i++) r.feed(doc.data() + i, 1);
  EXPECT_FALSE(r.hasError());
  const std::vector<std::string> want = {"{", "K:a", "[", "N:1", "N:-2.5e3", "S:s", "T",   "null",
                                         "{", "K:b", "F", "}",   "]",        "K:c", "S:d", "}"};
  EXPECT_EQ(events, want);
}

TEST(AnkiJson, ToInt64) {
  EXPECT_EQ(ankijson::toInt64("1790433421131"), 1790433421131LL);
  EXPECT_EQ(ankijson::toInt64("-4"), -4);
  EXPECT_EQ(ankijson::toInt64("x"), 0);
}

// ---------------------------------------------------------------------------
TEST(AnkiMarkup, StripsBidiIsolates) {
  EXPECT_EQ(ankimarkup::stripBidiControls("<\xE2\x81\xA8"
                                          "10\xE2\x81\xA9m"),
            "<10m");
  EXPECT_EQ(ankimarkup::stripBidiControls("plain"), "plain");
}

TEST(AnkiMarkup, ParsesRuns) {
  const auto runs = ankimarkup::parse("a **b** _c_ [d] [img:x.png]\ne snake_case");
  ASSERT_EQ(runs.size(), 10u);
  EXPECT_EQ(runs[0].text, "a ");
  EXPECT_TRUE(runs[1].bold);
  EXPECT_EQ(runs[1].text, "b");
  EXPECT_TRUE(runs[3].italic);
  EXPECT_EQ(runs[3].text, "c");
  EXPECT_TRUE(runs[5].cloze);
  EXPECT_EQ(runs[5].text, "d");
  EXPECT_EQ(runs[7].text, "[image]");
  EXPECT_TRUE(runs[8].newline);
  EXPECT_EQ(runs[9].text, "e snake_case");
  EXPECT_EQ(ankimarkup::toPlain("**x** [y]"), "x [y]");
}

TEST(AnkiMarkup, UnbalancedMarkersStayLiteral) {
  const auto runs = ankimarkup::parse("2 ** 3 and a_b [open");
  ASSERT_EQ(runs.size(), 1u);
  EXPECT_EQ(runs[0].text, "2 ** 3 and a_b [open");
}

// ---------------------------------------------------------------------------
TEST(AnkiJournal, AppendLoadAck) {
  MemFs fs;
  AnkiJournal j(fs, "/j.jsonl");
  EXPECT_TRUE(j.empty());
  EXPECT_TRUE(j.append({"dev-1", 111, 3, 1758880123}));
  EXPECT_TRUE(j.append({"dev-2", 112, 1, 0}));
  EXPECT_FALSE(j.empty());
  auto e = j.load();
  ASSERT_EQ(e.size(), 2u);
  EXPECT_EQ(e[0].clientId, "dev-1");
  EXPECT_EQ(e[0].cardId, 111);
  EXPECT_EQ(e[0].ease, 3);
  EXPECT_EQ(e[0].answeredAt, 1758880123);
  EXPECT_EQ(e[1].answeredAt, 0);
  EXPECT_EQ(
      AnkiJournal::toReviewsJson(e),
      R"([{"client_id":"dev-1","card_id":111,"ease":3,"answered_at":1758880123},{"client_id":"dev-2","card_id":112,"ease":1}])");
  EXPECT_TRUE(j.removeAcked({"dev-1"}));
  e = j.load();
  ASSERT_EQ(e.size(), 1u);
  EXPECT_EQ(e[0].clientId, "dev-2");
  EXPECT_TRUE(j.removeAcked({"dev-2"}));
  EXPECT_TRUE(j.empty());
  EXPECT_FALSE(fs.exists("/j.jsonl"));
}

TEST(AnkiJournal, SkipsCorruptLines) {
  MemFs fs;
  fs.files["/j.jsonl"] = "{\"client_id\":\"a\",\"card_id\":1,\"ease\":2}\ngarbage\n{\"client_id\":\"b\"}\n";
  AnkiJournal j(fs, "/j.jsonl");
  const auto e = j.load();
  ASSERT_EQ(e.size(), 1u);
  EXPECT_EQ(e[0].clientId, "a");
}

// ---------------------------------------------------------------------------
TEST(AnkiNoteQueue, RoundTripAndRequestBody) {
  MemFs fs;
  AnkiNoteQueue q(fs, "/n.jsonl");
  AnkiNoteQueue::Note n;
  n.clientId = "dev-n1";
  n.deck = "Dutch";
  n.model = "Basic";
  n.front = "<b>huis</b><br><br>Het <b>huis</b> is groot.";
  n.back = "house";
  n.tags = {"crosspoint", "book:De_Avonden"};
  EXPECT_TRUE(q.append(n));
  const auto notes = q.load();
  ASSERT_EQ(notes.size(), 1u);
  EXPECT_EQ(notes[0].front, n.front);
  ASSERT_EQ(notes[0].tags.size(), 2u);
  EXPECT_EQ(notes[0].tags[1], "book:De_Avonden");
  const std::string body = AnkiNoteQueue::toNotesRequestJson(notes);
  EXPECT_NE(body.find("\"dedupe\":\"update\""), std::string::npos);
  EXPECT_NE(body.find("\"fields\":{\"Front\":\"<b>huis</b><br><br>Het <b>huis</b> is groot.\",\"Back\":\"house\"}"),
            std::string::npos);
  EXPECT_TRUE(q.removeAcked({"dev-n1"}));
  EXPECT_TRUE(q.empty());
}

TEST(AnkiNote, SentenceAndFront) {
  const std::vector<std::string> words = {"First", "one.",    "Het", "huis,", "is", "\xE2\x80\x9Cgroot.\xE2\x80\x9D",
                                          "Next",  "sentence"};
  EXPECT_EQ(ankinote::sentenceAround(words, 3), "Het huis, is \xE2\x80\x9Cgroot.\xE2\x80\x9D");
  EXPECT_EQ(ankinote::sentenceAround(words, 0), "First one.");
  EXPECT_EQ(ankinote::sentenceAround(words, 7), "Next sentence");
  EXPECT_EQ(ankinote::cleanWord("huis,"), "huis");
  EXPECT_EQ(ankinote::cleanWord("\xE2\x80\x9Cgroot.\xE2\x80\x9D"), "groot");
  EXPECT_EQ(ankinote::cleanWord("(don't)"), "don't");
  EXPECT_EQ(ankinote::frontHtml("huis", "Het huis is <groot>."),
            "<b>huis</b><br><br>Het <b>huis</b> is &lt;groot&gt;.");
  EXPECT_EQ(ankinote::frontHtml("huis", ""), "<b>huis</b>");
  EXPECT_EQ(ankinote::bookTag("De Avonden \"x\""), "book:De_Avonden_x");
}

// ---------------------------------------------------------------------------
TEST(AnkiCardCache, LineRoundTrip) {
  AnkiCard c;
  c.cardId = 42;
  c.deck = "D";
  c.kind = "due";
  c.q = "q \"x\"\nline";
  c.a = "a";
  c.next[0] = "1m";
  c.next[3] = "4d";
  c.mediaCount = 2;
  AnkiCard back;
  ASSERT_TRUE(AnkiCardCache::parseLine(AnkiCardCache::toLine(c), back));
  EXPECT_EQ(back.cardId, 42);
  EXPECT_EQ(back.q, c.q);
  EXPECT_EQ(back.next[3], "4d");
  EXPECT_EQ(back.next[1], "");
  EXPECT_EQ(back.mediaCount, 2);
}

TEST(AnkiCardCache, CardIdsSinglePass) {
  EXPECT_EQ(AnkiCardCache::lineCardId("{\"card_id\":1790433421131,\"deck\":\"D\"}"), 1790433421131);
  EXPECT_EQ(AnkiCardCache::lineCardId("{\"deck\":\"card_id\", \"card_id\": -7}"), -7);
  EXPECT_EQ(AnkiCardCache::lineCardId("{\"deck\":\"D\"}"), 0);
  EXPECT_EQ(AnkiCardCache::lineCardId("{\"card_id\":\"x\"}"), 0);

  MemFs fs;
  AnkiCardCache cache(fs, "/c/cards.jsonl", "/c/cache.json");
  EXPECT_TRUE(cache.cardIds().empty());
  AnkiCard c;
  c.deck = "D";
  c.q = std::string(3000, 'q');  // longer than the id scan cap
  std::string text;
  for (int64_t id : {int64_t{11}, int64_t{22}, int64_t{33}}) {
    c.cardId = id;
    text += AnkiCardCache::toLine(c);
  }
  text += "\n{\"deck\":\"no id\"}\n";
  ASSERT_TRUE(fs.writeAllAtomic("/c/cards.jsonl", text));
  const std::vector<int64_t> ids = cache.cardIds();
  ASSERT_EQ(ids.size(), 4u);
  EXPECT_EQ(ids[0], 11);
  EXPECT_EQ(ids[2], 33);
  EXPECT_EQ(ids[3], 0);
  EXPECT_EQ(ids.size(), cache.count());
  AnkiCard back;
  ASSERT_TRUE(cache.load(2, back));
  EXPECT_EQ(back.cardId, ids[2]);
}

// ---------------------------------------------------------------------------
TEST(AnkiSyncEngine, ExchangeBody) {
  AnkiAccount a = testAccount();
  std::vector<AnkiJournal::Entry> reviews = {{"dev-1", 111, 3, 1758880123}};
  const std::string body = AnkiSyncEngine::buildExchangeBody(a, reviews, true, 15);
  EXPECT_EQ(
      body,
      R"({"reviews":[{"client_id":"dev-1","card_id":111,"ease":3,"answered_at":1758880123}],"want":{"decks":["Dutch::Common"],"kinds":["due","learning","new"],"limit":40,"max_new_per_day":5,"fields":"compact","render":"text"},"sync":"auto","sync_timeout_seconds":15})");
  a.decks.clear();
  a.maxNewPerDay = 0;
  const std::string all = AnkiSyncEngine::buildExchangeBody(a, {}, true, 15);
  EXPECT_EQ(all.find("\"decks\""), std::string::npos);
  EXPECT_EQ(all.find("max_new_per_day"), std::string::npos);
}

TEST(AnkiSyncEngine, ExchangeStreamsCardsAndAcksReviews) {
  MemFs fs;
  FakeHttp http;
  AnkiSyncEngine engine(fs, http, [] { return int64_t(1700000000); });
  AnkiAccount a = testAccount();
  AnkiJournal journal(fs, ankipaths::reviewsFile(a.id));
  journal.append({"dev-1", 111, 3, 0});
  journal.append({"dev-2", 112, 1, 0});
  journal.append({"dev-3", 113, 4, 0});
  journal.append({"dev-4", 114, 2, 0});  // not echoed by the server: stays queued
  http.responses["POST /v1/p/alice/exchange"] = {200, kExchangeResponse};

  const auto r = engine.exchange(a, true);
  EXPECT_TRUE(r.ok) << r.error;
  EXPECT_EQ(r.httpStatus, 200);
  EXPECT_EQ(r.reviewsSent, 4u);
  EXPECT_EQ(r.reviewsAcked, 3u);
  EXPECT_EQ(r.cardsFetched, 2u);
  EXPECT_EQ(r.counts.newCount, 7);
  EXPECT_EQ(r.counts.returned, 2);
  EXPECT_EQ(r.syncError, "sync timed out");

  ASSERT_EQ(http.seen.size(), 1u);
  EXPECT_EQ(http.seen[0].url, "http://anki.local:8766/v1/p/alice/exchange");
  EXPECT_EQ(http.seen[0].headers[0].second, "Bearer akd_x");

  const auto left = journal.load();
  ASSERT_EQ(left.size(), 1u);
  EXPECT_EQ(left[0].clientId, "dev-4");

  AnkiCardCache cache(fs, ankipaths::cardsFile(a.id), ankipaths::cacheMetaFile(a.id));
  EXPECT_EQ(cache.count(), 2u);
  AnkiCard c;
  ASSERT_TRUE(cache.load(0, c));
  EXPECT_EQ(c.cardId, 1790433421131LL);
  EXPECT_EQ(c.deck, "Ankido::Test");
  EXPECT_EQ(c.q, "e2e **front** \xC3\xA9");
  EXPECT_EQ(c.a, "back \"one\"");
  EXPECT_EQ(c.kind, "learning");
  EXPECT_EQ(c.mediaCount, 2);
  EXPECT_EQ(ankimarkup::stripBidiControls(c.next[1]), "<10m");
  ASSERT_TRUE(cache.load(1, c));
  EXPECT_EQ(c.cardId, 22);
  EXPECT_EQ(c.q, "line1\nline2");
  EXPECT_EQ(c.next[3], "3d");
  EXPECT_FALSE(cache.load(2, c));
  EXPECT_FALSE(fs.exists(ankipaths::cardsFile(a.id) + ".tmp"));

  AnkiCardCache::Meta meta;
  ASSERT_TRUE(cache.readMeta(meta));
  EXPECT_EQ(meta.counts.newCount, 7);
  EXPECT_EQ(meta.fetchedAt, 1700000000);
  EXPECT_EQ(meta.syncError, "sync timed out");
}

TEST(AnkiSyncEngine, ExchangeErrorKeepsJournalAndCache) {
  MemFs fs;
  FakeHttp http;
  AnkiSyncEngine engine(fs, http, nullptr);
  AnkiAccount a = testAccount();
  fs.files[ankipaths::cardsFile(a.id)] = "old\n";
  AnkiJournal journal(fs, ankipaths::reviewsFile(a.id));
  journal.append({"dev-1", 111, 3, 0});
  http.responses["POST /v1/p/alice/exchange"] = {
      401, R"({"error":{"code":"unauthorized","message":"token revoked","retryable":false}})"};

  const auto r = engine.exchange(a, true);
  EXPECT_FALSE(r.ok);
  EXPECT_TRUE(r.authFailed);
  EXPECT_EQ(r.error, "token revoked");
  EXPECT_EQ(journal.load().size(), 1u);
  EXPECT_EQ(fs.files[ankipaths::cardsFile(a.id)], "old\n");
  EXPECT_FALSE(fs.exists(ankipaths::cardsFile(a.id) + ".tmp"));

  http.responses.clear();  // transport failure
  const auto r2 = engine.exchange(a, true);
  EXPECT_FALSE(r2.ok);
  EXPECT_EQ(r2.error, "No connection");
  EXPECT_EQ(fs.files[ankipaths::cardsFile(a.id)], "old\n");
}

TEST(AnkiSyncEngine, NonReviewAccountOnlyPushes) {
  MemFs fs;
  FakeHttp http;
  AnkiSyncEngine engine(fs, http, nullptr);
  AnkiAccount a = testAccount();
  // Nothing pending: no request at all.
  EXPECT_TRUE(engine.exchange(a, false).ok);
  EXPECT_TRUE(http.seen.empty());
  AnkiJournal journal(fs, ankipaths::reviewsFile(a.id));
  journal.append({"dev-1", 111, 3, 0});
  http.responses["POST /v1/p/alice/exchange"] = {
      200,
      R"({"reviews":[{"status":"applied","client_id":"dev-1"}],"cards":[{"card_id":9,"q":"x","a":"y"}],"counts":{}})"};
  const auto r = engine.exchange(a, false);
  EXPECT_TRUE(r.ok);
  EXPECT_EQ(r.reviewsAcked, 1u);
  EXPECT_EQ(r.cardsFetched, 0u);  // cards ignored, cache untouched
  EXPECT_FALSE(fs.exists(ankipaths::cardsFile(a.id)));
  ASSERT_EQ(http.seen.size(), 1u);
  EXPECT_NE(http.seen[0].body.find("\"limit\":1"), std::string::npos);
}

TEST(AnkiSyncEngine, PushNotesAcksAllButErrors) {
  MemFs fs;
  FakeHttp http;
  AnkiSyncEngine engine(fs, http, nullptr);
  AnkiAccount a = testAccount();
  AnkiNoteQueue queue(fs, ankipaths::notesFile(a.id));
  queue.append({"n1", "D", "Basic", "f1", "b1", {"crosspoint"}});
  queue.append({"n2", "D", "Basic", "f2", "", {}});
  queue.append({"n3", "D", "Basic", "f3", "", {}});
  http.responses["POST /v1/p/alice/notes"] = {200, R"({"results":[
    {"status":"added","note_id":1,"card_ids":[2],"media":[],"client_id":"n1"},
    {"status":"skipped_duplicate","note_id":3,"client_id":"n2"},
    {"status":"error","error":{"code":"unknown_field","message":"unknown field(s) for model 'Basic': Frnt","retryable":false},"client_id":"n3"}
  ]})"};
  const auto r = engine.pushNotes(a);
  EXPECT_TRUE(r.ok);
  EXPECT_EQ(r.notesSent, 3u);
  EXPECT_EQ(r.notesAcked, 2u);
  EXPECT_EQ(r.error, "unknown field(s) for model 'Basic': Frnt");
  const auto left = queue.load();
  ASSERT_EQ(left.size(), 1u);
  EXPECT_EQ(left[0].clientId, "n3");
}

TEST(AnkiSyncEngine, FetchAndLoadDecks) {
  MemFs fs;
  FakeHttp http;
  AnkiSyncEngine engine(fs, http, nullptr);
  AnkiAccount a = testAccount();
  http.responses["GET /v1/p/alice/decks"] = {200, R"({"decks":[
    {"id":1,"name":"Dutch","parent":null,"new":20,"learning":3,"due":41,"total":812},
    {"id":2,"name":"Dutch::Common","parent":"Dutch","new":12,"learning":3,"due":41,"total":640}]})"};
  const auto r = engine.fetchDecks(a);
  EXPECT_TRUE(r.ok) << r.error;
  const auto decks = engine.loadDecks(a.id);
  ASSERT_EQ(decks.size(), 2u);
  EXPECT_EQ(decks[1].name, "Dutch::Common");
  EXPECT_EQ(decks[1].due, 41);
  EXPECT_EQ(decks[0].newCount, 20);
}

TEST(AnkiSyncEngine, SyncAccountRunsAllSteps) {
  MemFs fs;
  FakeHttp http;
  AnkiSyncEngine engine(fs, http, nullptr);
  AnkiAccount a = testAccount();
  AnkiNoteQueue queue(fs, ankipaths::notesFile(a.id));
  queue.append({"n1", "D", "Basic", "f1", "b1", {}});
  EXPECT_TRUE(engine.hasPending(a));
  http.responses["POST /v1/p/alice/exchange"] = {200, kExchangeResponse};
  http.responses["POST /v1/p/alice/notes"] = {200, R"({"results":[{"status":"added","client_id":"n1"}]})"};
  http.responses["GET /v1/p/alice/decks"] = {200, R"({"decks":[{"name":"D","new":1,"learning":0,"due":2}]})"};
  const auto r = engine.syncAccount(a, true);
  EXPECT_TRUE(r.ok) << r.error;
  EXPECT_EQ(r.cardsFetched, 2u);
  EXPECT_EQ(r.notesAcked, 1u);
  EXPECT_EQ(http.seen.size(), 3u);
  EXPECT_FALSE(engine.hasPending(a));
  EXPECT_EQ(engine.loadDecks(a.id).size(), 1u);
}

// ---------------------------------------------------------------------------
// AnkiConnect backend
#include "AnkiConnectClient.h"
#include "AnkiHtml.h"

namespace {

AnkiAccount connectAccount() {
  AnkiAccount a;
  a.id = 5;
  a.name = "Desktop";
  a.backend = AnkiBackend::AnkiConnect;
  a.url = "http://desk.local:8765";
  a.profile = "";
  a.token = "";
  a.decks = {"Dutch::Common"};
  a.cacheSize = 10;
  return a;
}

// Routes AnkiConnect actions by the "action" in the request body.
class FakeConnect final : public AnkiHttp {
 public:
  std::map<std::string, std::string> responses;  // action -> body
  std::vector<std::string> bodies;
  int status = 200;

  int request(const char*, const std::string&, const std::vector<Header>&, const std::string& body,
              const DataCallback& onData) override {
    bodies.push_back(body);
    const size_t p = body.find("\"action\":\"") + 10;
    const std::string action = body.substr(p, body.find('"', p) - p);
    auto it = responses.find(action);
    const std::string resp = it == responses.end() ? "{\"result\":null,\"error\":\"unsupported action\"}" : it->second;
    for (size_t i = 0; i < resp.size(); i += 5) {
      const size_t n = std::min<size_t>(5, resp.size() - i);
      if (!onData(status, reinterpret_cast<const uint8_t*>(resp.data() + i), n)) break;
    }
    return status;
  }
  size_t count(const char* action) const {
    size_t n = 0;
    for (const auto& b : bodies)
      if (b.find(std::string("\"action\":\"") + action + "\"") != std::string::npos) n++;
    return n;
  }
  const std::string& last(const char* action) const {
    for (auto it = bodies.rbegin(); it != bodies.rend(); ++it)
      if (it->find(std::string("\"action\":\"") + action + "\"") != std::string::npos) return *it;
    static const std::string none;
    return none;
  }
};

}  // namespace

TEST(AnkiHtml, MirrorsAnkiDoTextRendering) {
  const std::string q =
      "<style>.card { font: 12px; }</style><div class=\"front\">huis &amp; <b>tuin</b><br>\n"
      "  <i>zin</i> [sound:a.mp3]<img src=\"pic.png\">&nbsp;<!-- c --> end</div>[anki:play:q:0]";
  unsigned images = 0;
  EXPECT_EQ(ankihtml::toMarkup(q, &images), "huis & **tuin**\n_zin_ [img:pic.png] end");
  EXPECT_EQ(images, 1u);
  // Cloze: question keeps Anki's [...] / [hint], answer side brackets the text.
  EXPECT_EQ(ankihtml::toMarkup("De <span class=\"cloze\" data-ordinal=\"1\">[...]</span> is groot"),
            "De [...] is groot");
  EXPECT_EQ(
      ankihtml::toMarkup("De <span class=\"cloze\"><b>hond</b></span> is <span class=\"cloze-inactive\">groot</span>"),
      "De [**hond**] is groot");
  // Blank-line runs squeeze to one; entities decode.
  EXPECT_EQ(ankihtml::toMarkup("<p>a</p><p></p><p></p><p>b &#233; &#x263A;</p>"), "a\n\nb é ☺");
  EXPECT_EQ(ankihtml::toMarkup("x < y &unknown; z"), "x < y &unknown; z");
}

TEST(AnkiHtml, AnswerPart) {
  EXPECT_EQ(ankihtml::answerPart("front<hr id=answer>back"), "back");
  EXPECT_EQ(ankihtml::answerPart("front<HR id=\"answer\" />back"), "back");
  EXPECT_EQ(ankihtml::answerPart("<hr>no marker"), "<hr>no marker");
}

TEST(AnkiConnectClient, RequestBodyAndSearch) {
  AnkiAccount a = connectAccount();
  EXPECT_EQ(AnkiConnectClient::requestBody(a, "deckNames", ""), "{\"action\":\"deckNames\",\"version\":6}");
  a.token = "s3cret";
  EXPECT_EQ(AnkiConnectClient::requestBody(a, "findCards", "{\"query\":\"is:due\"}"),
            "{\"action\":\"findCards\",\"version\":6,\"key\":\"s3cret\",\"params\":{\"query\":\"is:due\"}}");
  a.decks = {"Dutch::Common", "A \"b\" c_d*"};
  EXPECT_EQ(AnkiConnectClient::buildSearch(a, "is:new"),
            "(\"deck:Dutch::Common\" OR \"deck:A \\\"b\\\" c\\_d\\*\") is:new");
  a.decks.clear();
  EXPECT_EQ(AnkiConnectClient::buildSearch(a, "is:new"), "is:new");
}

TEST(AnkiConnectClient, ExchangeAnswersCardsAndStreamsQueue) {
  MemFs fs;
  FakeConnect http;
  AnkiSyncEngine engine(fs, http, [] { return int64_t{1700000000}; });
  const AnkiAccount a = connectAccount();
  AnkiJournal journal(fs, ankipaths::reviewsFile(a.id));
  journal.append({"dev-1", 111, 3, 0});
  journal.append({"dev-2", 112, 1, 0});

  http.responses["answerCards"] = "{\"result\":[true,false],\"error\":null}";
  http.responses["deckNames"] = "{\"result\":[\"Default\",\"Dutch\",\"Dutch::Common\"],\"error\":null}";
  http.responses["getDeckStats"] =
      "{\"result\":{\"1\":{\"deck_id\":1,\"name\":\"Dutch::Common\",\"new_count\":1,\"learn_count\":0,"
      "\"review_count\":2,\"total_in_deck\":9},\"2\":{\"name\":\"Dutch\",\"new_count\":3,\"learn_count\":0,"
      "\"review_count\":2}},\"error\":null}";
  http.responses["findCards"] = "{\"result\":[1001,1002,1003],\"error\":null}";
  http.responses["cardsInfo"] =
      "{\"result\":[{\"cardId\":1001,\"fields\":{\"Front\":{\"value\":\"x\",\"order\":0}},"
      "\"question\":\"<style>.x{}</style>Q <b>one</b>\",\"answer\":\"Q <b>one</b><hr id=answer>A one\","
      "\"deckName\":\"Dutch::Common\",\"queue\":2,\"type\":2},{\"cardId\":1002,\"question\":\"Q2\","
      "\"answer\":\"A2\",\"deckName\":\"Dutch::Common\",\"queue\":0}],\"error\":null}";

  const AnkiSyncEngine::Result r = engine.syncAccount(a, /*wantCards=*/true);
  EXPECT_TRUE(r.ok) << r.error;
  EXPECT_EQ(r.reviewsSent, 2u);
  EXPECT_EQ(r.reviewsAcked, 2u);
  EXPECT_TRUE(journal.empty());
  EXPECT_EQ(http.last("answerCards"),
            "{\"action\":\"answerCards\",\"version\":6,\"params\":{\"answers\":[{\"cardId\":111,\"ease\":3},"
            "{\"cardId\":112,\"ease\":1}]}}");
  // learning, due, new: three searches, the new one capped by new_count (1).
  EXPECT_EQ(http.count("findCards"), 3u);
  EXPECT_NE(http.bodies[3].find("is:learn -is:suspended -is:buried"), std::string::npos);
  EXPECT_NE(http.bodies[4].find("is:due -is:learn"), std::string::npos);
  EXPECT_NE(http.bodies[5].find("is:new"), std::string::npos);
  // 3 + 3 + 1 ids, 10 per request -> one cardsInfo call; deck list fetched once (not again by syncAccount).
  EXPECT_EQ(http.count("cardsInfo"), 1u);
  EXPECT_EQ(http.count("deckNames"), 1u);
  EXPECT_EQ(r.cardsFetched, 2u);
  EXPECT_EQ(r.counts.newCount, 1);
  EXPECT_EQ(r.counts.due, 2);

  AnkiCardCache cache(fs, ankipaths::cardsFile(a.id), ankipaths::cacheMetaFile(a.id));
  ASSERT_EQ(cache.count(), 2u);
  AnkiCard c;
  ASSERT_TRUE(cache.load(0, c));
  EXPECT_EQ(c.cardId, 1001);
  EXPECT_EQ(c.q, "Q **one**");
  EXPECT_EQ(c.a, "A one");
  EXPECT_EQ(c.kind, "due");
  EXPECT_EQ(c.deck, "Dutch::Common");
  ASSERT_TRUE(cache.load(1, c));
  EXPECT_EQ(c.kind, "new");
  AnkiCardCache::Meta meta;
  ASSERT_TRUE(cache.readMeta(meta));
  EXPECT_EQ(meta.counts.returned, 2);
  EXPECT_EQ(meta.fetchedAt, 1700000000);

  const std::vector<AnkiDeck> decks = engine.loadDecks(a.id);
  ASSERT_EQ(decks.size(), 3u);
  EXPECT_EQ(decks[2].name, "Dutch::Common");
  EXPECT_EQ(decks[2].due, 2);
  EXPECT_EQ(decks[2].newCount, 1);
}

TEST(AnkiConnectClient, ErrorsKeepJournalAndFlagBadKey) {
  MemFs fs;
  FakeConnect http;
  AnkiSyncEngine engine(fs, http, nullptr);
  const AnkiAccount a = connectAccount();
  AnkiJournal journal(fs, ankipaths::reviewsFile(a.id));
  journal.append({"dev-1", 111, 3, 0});

  http.responses["answerCards"] = "{\"result\":null,\"error\":\"valid api key must be provided\"}";
  AnkiSyncEngine::Result r = engine.exchange(a, false);
  EXPECT_FALSE(r.ok);
  EXPECT_TRUE(r.authFailed);
  EXPECT_EQ(r.error, "valid api key must be provided");
  EXPECT_EQ(journal.count(), 1u);

  http.status = 500;
  r = engine.exchange(a, false);
  EXPECT_FALSE(r.ok);
  EXPECT_EQ(r.error, "HTTP 500");
  EXPECT_EQ(journal.count(), 1u);
}

TEST(AnkiConnectClient, ProfileIsLoadedOncePerSession) {
  MemFs fs;
  FakeConnect http;
  AnkiSyncEngine engine(fs, http, nullptr);
  AnkiAccount a = connectAccount();
  a.profile = "Alice";
  http.responses["loadProfile"] = "{\"result\":true,\"error\":null}";
  http.responses["deckNames"] = "{\"result\":[\"Default\"],\"error\":null}";
  http.responses["getDeckStats"] = "{\"result\":{},\"error\":null}";
  EXPECT_TRUE(engine.fetchDecks(a).ok);
  EXPECT_TRUE(engine.fetchDecks(a).ok);
  EXPECT_EQ(http.count("loadProfile"), 1u);
  EXPECT_EQ(http.bodies[0], "{\"action\":\"loadProfile\",\"version\":6,\"params\":{\"name\":\"Alice\"}}");

  http.responses["loadProfile"] = "{\"result\":false,\"error\":null}";
  a.profile = "Nobody";
  const AnkiSyncEngine::Result r = engine.fetchDecks(a);
  EXPECT_FALSE(r.ok);
  EXPECT_EQ(r.error, "Profile not found: Nobody");
}

TEST(AnkiConnectClient, PushNotesUsesModelFieldsAndDropsDuplicates) {
  MemFs fs;
  FakeConnect http;
  AnkiSyncEngine engine(fs, http, nullptr);
  const AnkiAccount a = connectAccount();
  AnkiNoteQueue queue(fs, ankipaths::notesFile(a.id));
  queue.append({"n1", "Dutch::Common", "Basic", "<b>huis</b>", "house", {"crosspoint", "book:X"}});
  queue.append({"n2", "Dutch::Common", "Basic", "<b>huis</b>", "house", {}});
  queue.append({"n3", "Missing", "Basic", "<b>boom</b>", "tree", {}});

  http.responses["modelFieldNames"] = "{\"result\":[\"Vorderseite\",\"Rückseite\",\"Extra\"],\"error\":null}";
  // One canned answer per addNote call, in order.
  std::vector<std::string> addResults = {
      "{\"result\":1496198395707,\"error\":null}",
      "{\"result\":null,\"error\":\"cannot create note because it is a duplicate\"}",
      "{\"result\":null,\"error\":\"deck was not found: Missing\"}",
  };
  size_t adds = 0;
  struct Seq final : public AnkiHttp {
    FakeConnect& inner;
    std::vector<std::string>& results;
    size_t& adds;
    Seq(FakeConnect& i, std::vector<std::string>& r, size_t& n) : inner(i), results(r), adds(n) {}
    int request(const char* m, const std::string& u, const std::vector<Header>& h, const std::string& body,
                const DataCallback& onData) override {
      if (body.find("\"action\":\"addNote\"") != std::string::npos) inner.responses["addNote"] = results[adds++];
      return inner.request(m, u, h, body, onData);
    }
  } seq(http, addResults, adds);
  AnkiSyncEngine engine2(fs, seq, nullptr);

  const AnkiSyncEngine::Result r = engine2.pushNotes(a);
  EXPECT_TRUE(r.ok);
  EXPECT_EQ(r.notesSent, 3u);
  EXPECT_EQ(r.notesAcked, 2u);  // added + duplicate; the missing deck stays queued
  EXPECT_EQ(r.error, "deck was not found: Missing");
  EXPECT_EQ(queue.count(), 1u);
  EXPECT_EQ(queue.load()[0].clientId, "n3");
  EXPECT_EQ(http.count("modelFieldNames"), 1u);  // cached per session
  EXPECT_EQ(
      http.bodies[1],
      "{\"action\":\"addNote\",\"version\":6,\"params\":{\"note\":{\"deckName\":\"Dutch::Common\","
      "\"modelName\":\"Basic\",\"fields\":{\"Vorderseite\":\"<b>huis</b>\",\"Rückseite\":\"house\"},"
      "\"tags\":[\"crosspoint\",\"book:X\"],\"options\":{\"allowDuplicate\":false,\"duplicateScope\":\"deck\"}}}}");
}

TEST(AnkiConnectClient, NonReviewAccountOnlyPushes) {
  MemFs fs;
  FakeConnect http;
  AnkiSyncEngine engine(fs, http, nullptr);
  const AnkiAccount a = connectAccount();
  const AnkiSyncEngine::Result r = engine.exchange(a, false);
  EXPECT_TRUE(r.ok);
  EXPECT_TRUE(http.bodies.empty());
}
