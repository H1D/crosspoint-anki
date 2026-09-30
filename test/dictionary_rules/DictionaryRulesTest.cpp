#include <gtest/gtest.h>

#include <algorithm>
#include <set>
#include <string>
#include <vector>

#include "DictLanguage.h"
#include "DictStemmer.h"

namespace {

// Mirrors Dictionary::lookup's order against a headword set: irregular
// lemma, exact word, then the stem variants in order.
std::string firstHit(const std::set<std::string>& headwords, const std::string& word, const std::string& lang) {
  if (const char* lemma = DictStemmer::irregularLemma(word, lang)) {
    if (headwords.count(lemma)) return lemma;
  }
  if (headwords.count(word)) return word;
  std::vector<std::string> variants;
  DictStemmer::variants(word, lang, variants);
  for (const std::string& v : variants) {
    if (headwords.count(v)) return v;
  }
  return "";
}

// Includes decoys the rules must not prefer: bom (bomb) for bomen, bos
// (forest) for boze, zien (see) for zit, zegen (blessing) for zegt, was (wax)
// for was.
const std::set<std::string> DUTCH = {
    "boom",  "bom",    "huis",  "kat",   "brief",  "tafel", "auto",   "bal",    "groot",  "dik",
    "boos",  "bos",    "lief",  "lopen", "werken", "wonen", "spelen", "zitten", "zien",   "lezen",
    "geven", "zeggen", "zegen", "zijn",  "was",    "gaan",  "man",    "mooi",   "kijken", "praten",
};

}  // namespace

TEST(DictLanguage, PrimarySubtag) {
  EXPECT_EQ(DictLanguage::primary("nl-NL"), "nl");
  EXPECT_EQ(DictLanguage::primary("EN_us"), "en");
  EXPECT_EQ(DictLanguage::primary("deu"), "de");
  EXPECT_EQ(DictLanguage::primary("dut"), "nl");
  EXPECT_EQ(DictLanguage::primary("haw"), "haw");
  EXPECT_EQ(DictLanguage::primary(""), "");
  EXPECT_EQ(DictLanguage::primary("english"), "");
  EXPECT_EQ(DictLanguage::primary("n1"), "");
}

TEST(DictLanguage, SourceFromLangLine) {
  EXPECT_EQ(DictLanguage::sourceOf("StarDict's dict ifo file\nversion=3.0.0\nlang=nl-ru\r\n", "x"), "nl");
  EXPECT_EQ(DictLanguage::sourceOf("bookname=A (en-ru)\nlang=de\n", "wikdict-en-ru"), "de");
}

TEST(DictLanguage, SourceFromWikDictBookname) {
  const std::string ifo =
      "StarDict's dict ifo file\nversion=3.0.0\n"
      "bookname=Nederlands-\xD0\xA0\xD1\x83\xD1\x81\xD1\x81\xD0\xBA\xD0\xB8\xD0\xB9 FreeDict+WikDict dictionary "
      "(nl-ru)\nwordcount=13892\n";
  EXPECT_EQ(DictLanguage::sourceOf(ifo, "my-dict"), "nl");
}

TEST(DictLanguage, SourceFromFolderName) {
  EXPECT_EQ(DictLanguage::sourceOf("bookname=Webster\n", "wikdict-en-ru"), "en");
  EXPECT_EQ(DictLanguage::sourceOf("", "nl_en"), "nl");
  EXPECT_EQ(DictLanguage::sourceOf("", "freedict-nld-eng"), "nl");
  EXPECT_EQ(DictLanguage::sourceOf("", "webster"), "");
  EXPECT_EQ(DictLanguage::sourceOf("bookname=Oxford (2nd edition)\n", "oxford"), "");
}

TEST(DictStemmer, DutchNounsAndAdjectives) {
  EXPECT_EQ(firstHit(DUTCH, "bomen", "nl"), "boom");
  EXPECT_EQ(firstHit(DUTCH, "huizen", "nl"), "huis");
  EXPECT_EQ(firstHit(DUTCH, "katten", "nl"), "kat");
  EXPECT_EQ(firstHit(DUTCH, "brieven", "nl"), "brief");
  EXPECT_EQ(firstHit(DUTCH, "tafels", "nl"), "tafel");
  EXPECT_EQ(firstHit(DUTCH, "auto's", "nl"), "auto");
  EXPECT_EQ(firstHit(DUTCH, "huisje", "nl"), "huis");
  EXPECT_EQ(firstHit(DUTCH, "balletje", "nl"), "bal");
  EXPECT_EQ(firstHit(DUTCH, "boompje", "nl"), "boom");
  EXPECT_EQ(firstHit(DUTCH, "mannetjes", "nl"), "man");
  EXPECT_EQ(firstHit(DUTCH, "grote", "nl"), "groot");
  EXPECT_EQ(firstHit(DUTCH, "groter", "nl"), "groot");
  EXPECT_EQ(firstHit(DUTCH, "grootste", "nl"), "groot");
  EXPECT_EQ(firstHit(DUTCH, "dikke", "nl"), "dik");
  EXPECT_EQ(firstHit(DUTCH, "boze", "nl"), "boos");
  EXPECT_EQ(firstHit(DUTCH, "lieve", "nl"), "lief");
  EXPECT_EQ(firstHit(DUTCH, "mooie", "nl"), "mooi");
}

TEST(DictStemmer, DutchVerbs) {
  EXPECT_EQ(firstHit(DUTCH, "loopt", "nl"), "lopen");
  EXPECT_EQ(firstHit(DUTCH, "loop", "nl"), "lopen");
  EXPECT_EQ(firstHit(DUTCH, "werkte", "nl"), "werken");
  EXPECT_EQ(firstHit(DUTCH, "woonden", "nl"), "wonen");
  EXPECT_EQ(firstHit(DUTCH, "gewerkt", "nl"), "werken");
  EXPECT_EQ(firstHit(DUTCH, "gespeeld", "nl"), "spelen");
  EXPECT_EQ(firstHit(DUTCH, "gelopen", "nl"), "lopen");
  EXPECT_EQ(firstHit(DUTCH, "zit", "nl"), "zitten");
  EXPECT_EQ(firstHit(DUTCH, "leest", "nl"), "lezen");
  EXPECT_EQ(firstHit(DUTCH, "geeft", "nl"), "geven");
  EXPECT_EQ(firstHit(DUTCH, "zegt", "nl"), "zeggen");
  EXPECT_EQ(firstHit(DUTCH, "gezegd", "nl"), "zeggen");
  EXPECT_EQ(firstHit(DUTCH, "keek", "nl"), "kijken");
  EXPECT_EQ(firstHit(DUTCH, "praatte", "nl"), "praten");
  EXPECT_EQ(firstHit(DUTCH, "lopend", "nl"), "lopen");
  EXPECT_EQ(firstHit({"deen", "toen"}, "de", "nl"), "");
  EXPECT_EQ(firstHit({"deen", "toen"}, "tot", "nl"), "");
}

TEST(DictStemmer, DutchIrregularBeatsExactNoun) {
  EXPECT_EQ(firstHit(DUTCH, "liep", "nl"), "lopen");
  EXPECT_EQ(firstHit(DUTCH, "ging", "nl"), "gaan");
  EXPECT_EQ(firstHit(DUTCH, "was", "nl"), "zijn");
  EXPECT_EQ(DictStemmer::irregularLemma("was", "en"), nullptr);
}

TEST(DictStemmer, EnglishRulesForOtherLanguages) {
  const std::set<std::string> english = {"walk", "story", "run", "make", "dog", "stop"};
  EXPECT_EQ(firstHit(english, "walked", "en"), "walk");
  EXPECT_EQ(firstHit(english, "stories", ""), "story");
  EXPECT_EQ(firstHit(english, "running", "en"), "run");
  EXPECT_EQ(firstHit(english, "making", "es"), "make");
  EXPECT_EQ(firstHit(english, "dog's", "en"), "dog");
  EXPECT_EQ(firstHit(english, "stopped", "en"), "stop");
}

TEST(DictStemmer, VariantsExcludeWordAndDuplicates) {
  std::vector<std::string> v;
  DictStemmer::variants("gewerkten", "nl", v);
  EXPECT_EQ(std::find(v.begin(), v.end(), "gewerkten"), v.end());
  std::set<std::string> unique(v.begin(), v.end());
  EXPECT_EQ(unique.size(), v.size());
  EXPECT_LE(v.size(), 32u);
}
