#include <gtest/gtest.h>

#include <algorithm>
#include <cstring>

#include "AnkiVocabulary.h"

using ankivocab::Marker;
using ankivocab::Record;
using ankivocab::Window;

namespace {

// What VocabularyStore::append writes, field order included.
const char* UPSTREAM_LINE =
    R"({"v":1,"id":"0123456789abcdef0123456789abcdef","timestamp":1762800000,"word":"walked,","headword":"walk",)"
    R"("definition":"walk\nto move on foot","definition_truncated":false,"title":"My Book","author":"A. Writer",)"
    R"("series":"","series_index":null,"chapter":"Chapter 1","page":12,"spine":3,"start_offset":4321})";

std::string record(const std::string& id, const std::string& word, const std::string& headword = "",
                   const std::string& forkKeys = "") {
  return R"({"v":1,"id":")" + id + R"(","timestamp":null,"word":")" + word + R"(","headword":")" + headword +
         R"(","definition":"d","definition_truncated":false,"title":"T","start_offset":null)" + forkKeys + "}\n";
}

class StringSource final : public ankivocab::Source {
 public:
  explicit StringSource(std::string data) : data(std::move(data)) {}
  bool seek(size_t at) override {
    if (at > data.size()) return false;
    pos = at;
    return true;
  }
  int read(void* buf, size_t len) override {
    const size_t n = std::min(len, data.size() - pos);
    memcpy(buf, data.data() + pos, n);
    pos += n;
    return static_cast<int>(n);
  }
  std::string data;

 private:
  size_t pos = 0;
};

std::vector<std::string> rowWords(StringSource& source, const Window& window) {
  std::vector<std::string> words;
  ankivocab::forEachRow(source, window, [&](size_t row, const Record& r) {
    EXPECT_EQ(row, words.size());
    words.push_back(r.word);
  });
  return words;
}

}  // namespace

TEST(AnkiVocabulary, ParsesAnUpstreamLine) {
  Record r;
  ASSERT_TRUE(ankivocab::parseRecord(UPSTREAM_LINE, r));
  EXPECT_EQ(r.id, "0123456789abcdef0123456789abcdef");
  EXPECT_EQ(r.word, "walked,");
  EXPECT_EQ(r.headword, "walk");
  EXPECT_EQ(r.definition, "walk\nto move on foot");
  EXPECT_EQ(r.title, "My Book");
  EXPECT_TRUE(r.sentence.empty());
  EXPECT_TRUE(r.back.empty());
}

TEST(AnkiVocabulary, ParsesTheForkKeys) {
  Record r;
  ASSERT_TRUE(ankivocab::parseRecord(
      record("a1", "kwam", "komen", R"(,"sentence":"Ze kwam binnen.","back":"to come, \"arrive\"")"), r));
  EXPECT_EQ(r.sentence, "Ze kwam binnen.");
  EXPECT_EQ(r.back, "to come, \"arrive\"");
}

TEST(AnkiVocabulary, RejectsPartialAndGarbageLines) {
  Record r;
  const std::string whole = UPSTREAM_LINE;
  EXPECT_FALSE(ankivocab::parseRecord("", r));
  EXPECT_FALSE(ankivocab::parseRecord("not json at all", r));
  EXPECT_FALSE(ankivocab::parseRecord(whole.substr(0, whole.size() / 2), r));
  EXPECT_FALSE(ankivocab::parseRecord(whole.substr(0, whole.size() - 1), r));
  EXPECT_FALSE(ankivocab::parseRecord(R"({"v":2,"id":"x","word":"walk"})", r));
  EXPECT_FALSE(ankivocab::parseRecord(R"({"id":"x","word":"walk"})", r));
  EXPECT_FALSE(ankivocab::parseRecord(R"({"v":1,"word":"walk"})", r));
  EXPECT_FALSE(ankivocab::parseRecord(R"({"v":1,"id":"x","word":""})", r));
  EXPECT_FALSE(ankivocab::parseRecord(R"({"v":1,"id":"x","word":"..."})", r));
  EXPECT_TRUE(ankivocab::parseRecord(R"({"v":1,"id":"x","word":"walk"})", r));
}

TEST(AnkiVocabulary, OneRowPerWordIgnoringCaseAndPunctuation) {
  Record walked, lower, form, other;
  ASSERT_TRUE(ankivocab::parseRecord(record("1", "Walked"), walked));
  ASSERT_TRUE(ankivocab::parseRecord(record("2", "walked,"), lower));
  EXPECT_EQ(ankivocab::rowKey(walked), ankivocab::rowKey(lower));
  // The dictionary form decides: "walked" and "walks" both land on "walk".
  ASSERT_TRUE(ankivocab::parseRecord(record("3", "walks", "walk"), form));
  ASSERT_TRUE(ankivocab::parseRecord(record("4", "walk"), other));
  EXPECT_EQ(ankivocab::rowKey(form), ankivocab::rowKey(other));
  EXPECT_NE(ankivocab::rowKey(walked), ankivocab::rowKey(other));
  ASSERT_TRUE(ankivocab::parseRecord(record("5", "\xD0\x9F\xD1\x80\xD0\xB8\xD0\xB2\xD0\xB5\xD1\x82"), form));  // Привет
  ASSERT_TRUE(
      ankivocab::parseRecord(record("6", "\xD0\xBF\xD1\x80\xD0\xB8\xD0\xB2\xD0\xB5\xD1\x82"), other));  // привет
  EXPECT_EQ(ankivocab::rowKey(form), ankivocab::rowKey(other));
}

TEST(AnkiVocabulary, FrontIsTheSingleWordCard) {
  Record r;
  ASSERT_TRUE(ankivocab::parseRecord(record("1", "kwam,", "komen", R"(,"sentence":"Ze kwam binnen.")"), r));
  EXPECT_EQ(ankivocab::label(r), "komen");
  EXPECT_EQ(ankivocab::toNote(r).front, "<b>komen</b><br><br>Ze <b>kwam</b> binnen.");

  ASSERT_TRUE(ankivocab::parseRecord(record("2", "walk.", "walk"), r));
  EXPECT_EQ(ankivocab::label(r), "walk");
  EXPECT_EQ(ankivocab::toNote(r).front, "<b>walk</b>");
}

TEST(AnkiVocabulary, BackIsTheForkKeyElseTheCollapsedDefinition) {
  Record r;
  ASSERT_TRUE(ankivocab::parseRecord(UPSTREAM_LINE, r));
  EXPECT_EQ(ankivocab::toNote(r).back, "walk to move on foot");

  r.definition = "  a \t b\n\n\nc  ";
  EXPECT_EQ(ankivocab::toNote(r).back, "a b c");

  r.back = "to come";
  EXPECT_EQ(ankivocab::toNote(r).back, "to come");
}

TEST(AnkiVocabulary, BackFallbackIsCutOnACharacterBoundary) {
  Record r;
  r.word = "w";
  r.definition = "x";
  for (int i = 0; i < 300; i++) r.definition += "\xE2\x82\xAC";  // 3-byte euro signs
  const std::string back = ankivocab::toNote(r).back;
  ASSERT_LE(back.size(), 600u);
  EXPECT_EQ(back.size(), 598u);
  EXPECT_EQ(back.substr(back.size() - 3), "\xE2\x82\xAC");
}

TEST(AnkiVocabulary, TagsNameTheBook) {
  Record r;
  ASSERT_TRUE(ankivocab::parseRecord(UPSTREAM_LINE, r));
  EXPECT_EQ(ankivocab::toNote(r).tags, (std::vector<std::string>{"crosspoint", "book:My_Book"}));
  r.title.clear();
  EXPECT_EQ(ankivocab::toNote(r).tags, (std::vector<std::string>{"crosspoint"}));
}

TEST(AnkiVocabularyScan, ListsDistinctWordsKeepingTheFirstRecord) {
  StringSource source(record("1", "Walked") + record("2", "house") + "garbage\n\n" + record("3", "walked") +
                      record("4", "tree"));
  const Window window = ankivocab::scan(source, std::nullopt, 100);
  EXPECT_EQ(window.labels, (std::vector<std::string>{"Walked", "house", "tree"}));
  ASSERT_TRUE(window.last);
  EXPECT_EQ(window.last->id, "4");
  EXPECT_EQ(rowWords(source, window), (std::vector<std::string>{"Walked", "house", "tree"}));
}

TEST(AnkiVocabularyScan, StopsAtTheRowLimitAndTheRestIsTheNextWindow) {
  StringSource source(record("1", "a") + record("2", "b") + record("3", "a") + record("4", "c") + record("5", "d"));
  const Window first = ankivocab::scan(source, std::nullopt, 2);
  EXPECT_EQ(first.labels, (std::vector<std::string>{"a", "b"}));
  ASSERT_TRUE(first.last);
  EXPECT_EQ(first.last->id, "3");  // the duplicate of "a" is part of the window
  EXPECT_EQ(rowWords(source, first), (std::vector<std::string>{"a", "b"}));

  const Window second = ankivocab::scan(source, first.last, 2);
  EXPECT_EQ(second.labels, (std::vector<std::string>{"c", "d"}));
  EXPECT_EQ(rowWords(source, second), (std::vector<std::string>{"c", "d"}));
}

TEST(AnkiVocabularyScan, ResumesAfterTheMarkerAndNewLookupsAppear) {
  StringSource source(record("1", "a") + record("2", "b"));
  const Window done = ankivocab::scan(source, std::nullopt, 100);
  EXPECT_TRUE(ankivocab::scan(source, done.last, 100).labels.empty());
  EXPECT_FALSE(ankivocab::scan(source, done.last, 100).last);

  source.data += record("3", "c") + record("4", "a");  // "a" again: a new row, it was dismissed before
  const Window next = ankivocab::scan(source, done.last, 100);
  EXPECT_EQ(next.labels, (std::vector<std::string>{"c", "a"}));
  EXPECT_EQ(next.last->id, "4");
}

TEST(AnkiVocabularyScan, ARewrittenFileStartsOver) {
  StringSource source(record("1", "a") + record("2", "b"));
  const Window done = ankivocab::scan(source, std::nullopt, 100);

  source.data = record("7", "x") + record("8", "y") + record("9", "z");
  EXPECT_EQ(ankivocab::scan(source, done.last, 100).labels, (std::vector<std::string>{"x", "y", "z"}));
  source.data.clear();
  EXPECT_TRUE(ankivocab::scan(source, done.last, 100).labels.empty());
  Marker beyondTheEnd{9999, "1"};
  source.data = record("1", "a");
  EXPECT_EQ(ankivocab::scan(source, beyondTheEnd, 100).labels, (std::vector<std::string>{"a"}));
}

TEST(AnkiVocabularyScan, SkipsOverlongAndUnfinishedLines) {
  StringSource source(record("1", "a") + std::string(20000, 'x') + "\n" + record("2", "b"));
  const std::string whole = record("3", "c");
  source.data += whole.substr(0, whole.size() - 5);  // a lookup cut off mid-write
  const Window window = ankivocab::scan(source, std::nullopt, 100);
  EXPECT_EQ(window.labels, (std::vector<std::string>{"a", "b"}));
  EXPECT_EQ(window.last->id, "2");
}

TEST(AnkiVocabularyScan, ReadsAFinalRecordWithoutANewline) {
  std::string last = record("2", "b");
  last.pop_back();
  StringSource source(record("1", "a") + last);
  const Window window = ankivocab::scan(source, std::nullopt, 100);
  EXPECT_EQ(window.labels, (std::vector<std::string>{"a", "b"}));
  EXPECT_TRUE(ankivocab::scan(source, window.last, 100).labels.empty());
}
