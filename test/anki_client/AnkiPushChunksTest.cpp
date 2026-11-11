#include <gtest/gtest.h>

#include <algorithm>
#include <set>
#include <string>
#include <vector>

#include "AnkiFs.h"
#include "AnkiHttp.h"
#include "AnkiNoteQueue.h"
#include "AnkiPaths.h"
#include "AnkiSyncEngine.h"
#include "MemFs.h"

namespace {

std::vector<std::string> clientIdsIn(const std::string& body) {
  std::vector<std::string> ids;
  const std::string key = "\"client_id\":\"";
  for (size_t at = body.find(key); at != std::string::npos; at = body.find(key, at + 1)) {
    const size_t from = at + key.size();
    ids.push_back(body.substr(from, body.find('"', from) - from));
  }
  return ids;
}

// AnkiDo's POST /notes: answers "added" for every note except the refused ones.
class NotesServer final : public AnkiHttp {
 public:
  std::set<std::string> refuse;
  size_t failRequest = 0;  // 1-based request that gets no answer
  std::vector<size_t> sizes;

  int request(const char*, const std::string&, const std::vector<Header>&, const std::string& body,
              const DataCallback& onData) override {
    const std::vector<std::string> ids = clientIdsIn(body);
    sizes.push_back(ids.size());
    if (sizes.size() == failRequest) return -1;
    std::string resp = "{\"results\":[";
    for (size_t i = 0; i < ids.size(); i++) {
      if (i) resp += ",";
      resp += refuse.count(ids[i]) ? "{\"status\":\"error\",\"error\":{\"message\":\"unknown field\"},\"client_id\":\""
                                   : "{\"status\":\"added\",\"client_id\":\"";
      resp += ids[i] + "\"}";
    }
    resp += "]}";
    onData(200, reinterpret_cast<const uint8_t*>(resp.data()), resp.size());
    return 200;
  }
};

// AnkiConnect: addNote succeeds unless the note's deck is "Missing".
class ConnectServer final : public AnkiHttp {
 public:
  size_t failAdd = 0;  // 1-based addNote that gets no answer
  size_t adds = 0;

  int request(const char*, const std::string&, const std::vector<Header>&, const std::string& body,
              const DataCallback& onData) override {
    std::string resp = "{\"result\":[\"Front\",\"Back\"],\"error\":null}";
    if (body.find("\"action\":\"addNote\"") != std::string::npos) {
      adds++;
      if (adds == failAdd) return -1;
      resp = body.find("\"deckName\":\"Missing\"") != std::string::npos
                 ? "{\"result\":null,\"error\":\"deck was not found: Missing\"}"
                 : "{\"result\":1,\"error\":null}";
    }
    onData(200, reinterpret_cast<const uint8_t*>(resp.data()), resp.size());
    return 200;
  }
};

AnkiAccount ankidoAccount() {
  AnkiAccount a;
  a.id = 3;
  a.url = "http://anki.local:8766";
  a.profile = "alice";
  a.token = "akd_x";
  return a;
}

AnkiAccount connectAccount() {
  AnkiAccount a;
  a.id = 5;
  a.backend = AnkiBackend::AnkiConnect;
  a.url = "http://desk.local:8765";
  return a;
}

void fill(AnkiNoteQueue& queue, size_t count, const std::string& deck = "D") {
  for (size_t i = 1; i <= count; i++) queue.append({"n" + std::to_string(i), deck, "Basic", "front", "back", {"t"}});
}

std::vector<std::string> ids(const std::vector<AnkiNoteQueue::Note>& notes) {
  std::vector<std::string> out;
  for (const auto& n : notes) out.push_back(n.clientId);
  return out;
}

// Wraps MemFs and fails every read of one path once `okBytes` were served.
class FlakyFs final : public AnkiFs {
 public:
  explicit FlakyFs(MemFs& inner) : inner(inner) {}
  std::string flakyPath;
  size_t okBytes = 0;

  std::unique_ptr<AnkiFile> open(const std::string& path, Mode mode) override {
    auto file = inner.open(path, mode);
    if (!file || mode != Mode::Read || path != flakyPath) return file;
    return std::make_unique<Flaky>(std::move(file), okBytes);
  }
  bool exists(const std::string& p) override { return inner.exists(p); }
  bool remove(const std::string& p) override { return inner.remove(p); }
  bool rename(const std::string& a, const std::string& b) override { return inner.rename(a, b); }
  bool mkdirs(const std::string& p) override { return inner.mkdirs(p); }
  bool removeDir(const std::string& p) override { return inner.removeDir(p); }

 private:
  class Flaky final : public AnkiFile {
   public:
    Flaky(std::unique_ptr<AnkiFile> file, size_t okBytes) : file(std::move(file)), left(okBytes) {}
    int read(void* buf, size_t len) override {
      if (left == 0) return -1;
      const int n = file->read(buf, std::min(len, left));
      if (n > 0) left -= static_cast<size_t>(n);
      return n;
    }
    size_t write(const void* buf, size_t len) override { return file->write(buf, len); }
    bool close() override { return file->close(); }

   private:
    std::unique_ptr<AnkiFile> file;
    size_t left;
  };
  MemFs& inner;
};

}  // namespace

TEST(AnkiNoteQueueChunks, LoadIsBoundedAndCanSkip) {
  MemFs fs;
  AnkiNoteQueue queue(fs, "/q.jsonl");
  fill(queue, 25);
  EXPECT_EQ(queue.load(10).size(), 10u);
  EXPECT_EQ(queue.load(10)[0].clientId, "n1");
  EXPECT_EQ(ids(queue.load(10, 22)), (std::vector<std::string>{"n23", "n24", "n25"}));
  EXPECT_TRUE(queue.load(0).empty());
  EXPECT_EQ(queue.load().size(), 25u);
  EXPECT_EQ(queue.count(), 25u);
}

TEST(AnkiNoteQueueChunks, RemoveAckedStreamsAndKeepsTheRestInOrder) {
  MemFs fs;
  AnkiNoteQueue queue(fs, "/q.jsonl");
  fill(queue, 5);
  ASSERT_TRUE(queue.removeAcked({"n2", "n4", "n9"}));
  EXPECT_EQ(ids(queue.load()), (std::vector<std::string>{"n1", "n3", "n5"}));
  EXPECT_FALSE(fs.exists("/q.jsonl.tmp"));
  ASSERT_TRUE(queue.removeAcked({"n1", "n3", "n5"}));
  EXPECT_FALSE(fs.exists("/q.jsonl"));
  EXPECT_FALSE(fs.exists("/q.jsonl.tmp"));
}

TEST(AnkiNoteQueueChunks, RemoveAckedLeavesTheQueueWhenAReadFails) {
  MemFs mem;
  FlakyFs fs(mem);
  AnkiNoteQueue queue(fs, "/q.jsonl");
  fill(queue, 12);
  const std::string before = mem.files["/q.jsonl"];
  fs.flakyPath = "/q.jsonl";
  fs.okBytes = before.size() / 2;  // the read error looks like an early end of file
  EXPECT_FALSE(queue.removeAcked({"n1"}));
  EXPECT_EQ(mem.files["/q.jsonl"], before);
  EXPECT_FALSE(mem.exists("/q.jsonl.tmp"));
}

TEST(AnkiSyncEngineChunks, TwentyFiveNotesGoInThreeRequests) {
  MemFs fs;
  NotesServer http;
  AnkiSyncEngine engine(fs, http, nullptr);
  const AnkiAccount a = ankidoAccount();
  AnkiNoteQueue queue(fs, ankipaths::notesFile(a.id));
  fill(queue, 25);

  const auto r = engine.pushNotes(a);
  EXPECT_TRUE(r.ok) << r.error;
  EXPECT_EQ(http.sizes, (std::vector<size_t>{10, 10, 5}));
  EXPECT_EQ(r.notesSent, 25u);
  EXPECT_EQ(r.notesAcked, 25u);
  EXPECT_TRUE(queue.empty());
  EXPECT_FALSE(fs.exists(ankipaths::notesFile(a.id)));
}

TEST(AnkiSyncEngineChunks, ARefusedNoteStaysAloneAndIsNotResent) {
  MemFs fs;
  NotesServer http;
  http.refuse = {"n13"};
  AnkiSyncEngine engine(fs, http, nullptr);
  const AnkiAccount a = ankidoAccount();
  AnkiNoteQueue queue(fs, ankipaths::notesFile(a.id));
  fill(queue, 25);

  const auto r = engine.pushNotes(a);
  EXPECT_TRUE(r.ok);
  EXPECT_EQ(http.sizes, (std::vector<size_t>{10, 10, 5}));
  EXPECT_EQ(r.notesSent, 25u);
  EXPECT_EQ(r.notesAcked, 24u);
  EXPECT_EQ(r.error, "unknown field");
  EXPECT_EQ(ids(queue.load()), (std::vector<std::string>{"n13"}));
}

TEST(AnkiSyncEngineChunks, AFailingSecondChunkKeepsTheUnsentNotes) {
  MemFs fs;
  NotesServer http;
  http.failRequest = 2;
  AnkiSyncEngine engine(fs, http, nullptr);
  const AnkiAccount a = ankidoAccount();
  AnkiNoteQueue queue(fs, ankipaths::notesFile(a.id));
  fill(queue, 25);

  const auto r = engine.pushNotes(a);
  EXPECT_FALSE(r.ok);
  EXPECT_EQ(http.sizes.size(), 2u);
  EXPECT_EQ(r.notesSent, 20u);
  EXPECT_EQ(r.notesAcked, 10u);
  const auto left = queue.load();
  ASSERT_EQ(left.size(), 15u);
  EXPECT_EQ(left.front().clientId, "n11");
  EXPECT_EQ(left.back().clientId, "n25");
}

TEST(AnkiSyncEngineChunks, StopsAfterAChunkThatAckedNothing) {
  MemFs fs;
  NotesServer http;
  for (size_t i = 1; i <= 25; i++) http.refuse.insert("n" + std::to_string(i));
  AnkiSyncEngine engine(fs, http, nullptr);
  const AnkiAccount a = ankidoAccount();
  AnkiNoteQueue queue(fs, ankipaths::notesFile(a.id));
  fill(queue, 25);

  const auto r = engine.pushNotes(a);
  EXPECT_TRUE(r.ok);
  EXPECT_EQ(http.sizes, (std::vector<size_t>{10}));
  EXPECT_EQ(r.notesSent, 10u);
  EXPECT_EQ(r.notesAcked, 0u);
  EXPECT_EQ(queue.count(), 25u);
}

TEST(AnkiSyncEngineChunks, AsksTheBudgetBeforeEveryChunk) {
  MemFs fs;
  NotesServer http;
  AnkiSyncEngine engine(fs, http, nullptr);
  const AnkiAccount a = ankidoAccount();
  AnkiNoteQueue queue(fs, ankipaths::notesFile(a.id));
  fill(queue, 25);

  size_t asked = 0;
  engine.shouldStop = [&] { return ++asked == 3; };
  const auto r = engine.pushNotes(a);
  EXPECT_TRUE(r.ok);
  EXPECT_EQ(asked, 3u);
  EXPECT_EQ(http.sizes, (std::vector<size_t>{10, 10}));
  EXPECT_EQ(r.notesAcked, 20u);
  EXPECT_EQ(queue.count(), 5u);

  asked = 0;
  engine.shouldStop = [&] {
    asked++;
    return false;
  };
  EXPECT_TRUE(engine.pushNotes(a).ok);
  EXPECT_EQ(asked, 1u);  // not asked once the queue is empty
  EXPECT_TRUE(queue.empty());
}

TEST(AnkiSyncEngineChunks, AQueueThatCannotBeRewrittenIsAFailureAndNotResent) {
  MemFs mem;
  FlakyFs fs(mem);
  NotesServer http;
  AnkiSyncEngine engine(fs, http, nullptr);
  const AnkiAccount a = ankidoAccount();
  AnkiNoteQueue queue(fs, ankipaths::notesFile(a.id));
  fill(queue, 25);
  const std::string before = mem.files[ankipaths::notesFile(a.id)];
  fs.flakyPath = ankipaths::notesFile(a.id);
  fs.okBytes = 1100;  // enough to load the first chunk, short of the whole file the rewrite reads

  const auto r = engine.pushNotes(a);
  EXPECT_FALSE(r.ok);
  EXPECT_EQ(r.error, "SD write failed");
  EXPECT_EQ(http.sizes.size(), 1u);
  EXPECT_EQ(mem.files[ankipaths::notesFile(a.id)], before);
}

TEST(AnkiConnectChunks, TwentyFiveNotesAreAllAdded) {
  MemFs fs;
  ConnectServer http;
  AnkiSyncEngine engine(fs, http, nullptr);
  const AnkiAccount a = connectAccount();
  AnkiNoteQueue queue(fs, ankipaths::notesFile(a.id));
  fill(queue, 25);

  size_t asked = 0;
  engine.shouldStop = [&] {
    asked++;
    return false;
  };
  const auto r = engine.pushNotes(a);
  EXPECT_TRUE(r.ok) << r.error;
  EXPECT_EQ(http.adds, 25u);
  EXPECT_EQ(asked, 3u);
  EXPECT_EQ(r.notesSent, 25u);
  EXPECT_EQ(r.notesAcked, 25u);
  EXPECT_FALSE(fs.exists(ankipaths::notesFile(a.id)));
}

TEST(AnkiConnectChunks, ARefusedNoteStaysAlone) {
  MemFs fs;
  ConnectServer http;
  AnkiSyncEngine engine(fs, http, nullptr);
  const AnkiAccount a = connectAccount();
  AnkiNoteQueue queue(fs, ankipaths::notesFile(a.id));
  fill(queue, 12);
  queue.append({"bad", "Missing", "Basic", "f", "b", {}});

  const auto r = engine.pushNotes(a);
  EXPECT_TRUE(r.ok);
  EXPECT_EQ(r.notesSent, 13u);
  EXPECT_EQ(r.notesAcked, 12u);
  EXPECT_EQ(r.error, "deck was not found: Missing");
  EXPECT_EQ(ids(queue.load()), (std::vector<std::string>{"bad"}));
}

TEST(AnkiConnectChunks, AFailingRequestKeepsTheUnsentNotes) {
  MemFs fs;
  ConnectServer http;
  http.failAdd = 12;
  AnkiSyncEngine engine(fs, http, nullptr);
  const AnkiAccount a = connectAccount();
  AnkiNoteQueue queue(fs, ankipaths::notesFile(a.id));
  fill(queue, 25);

  const auto r = engine.pushNotes(a);
  EXPECT_FALSE(r.ok);
  EXPECT_EQ(r.notesAcked, 11u);  // the notes added before the failure are not offered again
  const auto left = queue.load();
  ASSERT_EQ(left.size(), 14u);
  EXPECT_EQ(left.front().clientId, "n12");
}

TEST(AnkiConnectChunks, StopsWhenTheBudgetIsSpent) {
  MemFs fs;
  ConnectServer http;
  AnkiSyncEngine engine(fs, http, nullptr);
  const AnkiAccount a = connectAccount();
  AnkiNoteQueue queue(fs, ankipaths::notesFile(a.id));
  fill(queue, 25);

  size_t asked = 0;
  engine.shouldStop = [&] { return ++asked == 2; };
  const auto r = engine.pushNotes(a);
  EXPECT_TRUE(r.ok);
  EXPECT_EQ(r.notesAcked, 10u);
  EXPECT_EQ(queue.count(), 15u);
}
