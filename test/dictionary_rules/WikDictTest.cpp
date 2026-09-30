#include <gtest/gtest.h>

#include <string>

#include "WikDict.h"

using WikDict::shortTranslation;

// WikDict's part-of-speech line; parse() requires one.
#define POS(p) "<div><font class=\"grammar\">" p "</font></div>"

// Entries from WikDict's en-ru StarDict dictionary (2026-06), trimmed.
constexpr const char* kBankNoun =
    "<div>/<font color=\"gray\">\xCB\x88"
    "ba\xC5\x8Bk</font>/<br>\n"
    "<div><font class=\"grammar\" color=\"green\">noun</font></div><ol>"
    "<li><ol><li>institution</li><li>branch office</li></ol><div>\xD0\xB1\xD0\xB0\xD0\xBD\xD0\xBA</div></li>"
    "<li>controller of a card game<ol><li><div>\xD0\xB1\xD0\xB0\xD0\xBD\xD0\xBA</div></li>"
    "<li><div>\xD0\xB1\xD0\xB0\xD0\xBD\xD0\xBA\xD0\xBE\xD0\xBC\xD1\x91\xD1\x82</div></li></ol></li>"
    "<li>edge of river or lake<ol><li><div>\xD0\xB1\xD0\xB5\xCC\x81\xD1\x80\xD0\xB5\xD0\xB3</div></li>"
    "<li><div>\xD0\xB1\xD1\x80\xD0\xB5\xD0\xB3</div></li></ol></li>"
    "<li>incline of an aircraft<div>\xD0\xB2\xD0\xB8\xD1\x80\xD0\xB0\xCC\x81\xD0\xB6</div></li></ol></div>";

TEST(WikDict, KeepsOnlyTranslationsFirstPerSenseFirst) {
  // банк, берег, вираж (first of each sense; the second sense's банк is a
  // duplicate), then the rest: банкомёт, брег. No IPA, POS or English glosses.
  EXPECT_EQ(shortTranslation(kBankNoun),
            "\xD0\xB1\xD0\xB0\xD0\xBD\xD0\xBA, \xD0\xB1\xD0\xB5\xD1\x80\xD0\xB5\xD0\xB3, "
            "\xD0\xB2\xD0\xB8\xD1\x80\xD0\xB0\xD0\xB6, "
            "\xD0\xB1\xD0\xB0\xD0\xBD\xD0\xBA\xD0\xBE\xD0\xBC\xD1\x91\xD1\x82, \xD0\xB1\xD1\x80\xD0\xB5\xD0\xB3");
}

TEST(WikDict, CapsTermCount) {
  EXPECT_EQ(shortTranslation(kBankNoun, 2),
            "\xD0\xB1\xD0\xB0\xD0\xBD\xD0\xBA, \xD0\xB1\xD0\xB5\xD1\x80\xD0\xB5\xD0\xB3");
}

TEST(WikDict, StressVariantsAreOneTerm) {
  // гото́вый and готовый in two senses: one term.
  const std::string html =
      POS("adjective") "<ol><li>ready<ol><li><div>\xD0\xB3\xD0\xBE\xD1\x82\xD0\xBE\xCC\x81\xD0\xB2\xD1\x8B\xD0\xB9</div></li></ol></li>"
      "<li>intent<ol><li><div>\xD0\xB3\xD0\xBE\xD1\x82\xD0\xBE\xD0\xB2\xD1\x8B\xD0\xB9</div></li></ol></li></ol>";
  EXPECT_EQ(shortTranslation(html), "\xD0\xB3\xD0\xBE\xD1\x82\xD0\xBE\xD0\xB2\xD1\x8B\xD0\xB9");
}

TEST(WikDict, UnwrapsWikiLinksAndEntities) {
  const std::string html = POS("noun") "<ol><li>gloss<div>[[saw|saws]] &amp; [[teeth]]</div></li></ol>";
  EXPECT_EQ(shortTranslation(html), "saws & teeth");
}

TEST(WikDict, SensesOfSeveralEntriesTakeTurns) {
  // Two entries (noun, verb) appended by the lookup: n1, v1, n2, v2.
  const std::string html =
      "<div>" POS("noun") "<ol><li>a<div>n1</div></li><li>b<div>n2</div></li><li>c<div>n3</div></li></ol></div>"
      "<div>" POS("verb") "<ol><li>d<div>v1</div></li><li>e<div>v2</div></li></ol></div>";
  EXPECT_EQ(shortTranslation(html, 4), "n1, v1, n2, v2");
}

TEST(WikDict, OtherLayoutsGiveEmpty) {
  // Plain text, and HTML without leaf divs inside a sense list.
  EXPECT_EQ(shortTranslation("house: a building"), "");
  EXPECT_EQ(shortTranslation("<b>house</b><br><i>n.</i> a building"), "");
  EXPECT_EQ(shortTranslation("<div>top level only</div>"), "");
  // Sense lists with <div>s but no part-of-speech marker: another layout.
  EXPECT_EQ(WikDict::compactHtml("<ol><li><div>sense</div><p>example</p></li></ol>"), "");
  EXPECT_EQ(shortTranslation(""), "");
}

TEST(WikDict, ParseKeepsPartOfSpeechGlossesAndTerms) {
  const auto entries = WikDict::parse(kBankNoun);
  ASSERT_EQ(entries.size(), 1u);
  EXPECT_EQ(entries[0].partOfSpeech, "noun");
  ASSERT_EQ(entries[0].senses.size(), 4u);
  EXPECT_EQ(entries[0].senses[0].gloss, "institution; branch office");
  EXPECT_EQ(entries[0].senses[2].gloss, "edge of river or lake");
  ASSERT_EQ(entries[0].senses[2].terms.size(), 2u);
  // Stress accents stay for display.
  EXPECT_EQ(entries[0].senses[2].terms[0], "\xD0\xB1\xD0\xB5\xCC\x81\xD1\x80\xD0\xB5\xD0\xB3");
}

TEST(WikDict, CompactHtmlDropsPronunciation) {
  const std::string html =
      "<div>/<font color=\"gray\">k&lt;l</font>/<br>\n<div><font class=\"grammar\">verb</font></div>"
      "<ol><li>to shut<div>a</div><div>b</div></li><li><div>c &amp; d</div></li></ol></div>";
  EXPECT_EQ(WikDict::compactHtml(html),
            "<p><b>verb</b></p><p>1. <i>to shut</i> \xE2\x80\x94 a, b</p><p>2. c &amp; d</p>");
  EXPECT_EQ(WikDict::compactHtml("<b>plain</b> text"), "");
}

TEST(WikDict, SingleSenseShapes) {
  // Gloss then one translation, no list (numbered inline).
  const std::string huis =
      "<div>/<font color=\"gray\">h</font>/<br>\n<div><font class=\"grammar\">noun</font></div>"
      "1. gebouw om in te wonen<div>dom</div></div>";
  EXPECT_EQ(WikDict::compactHtml(huis), "<p><b>noun</b></p><p><i>gebouw om in te wonen</i> \xE2\x80\x94 dom</p>");
  // Gloss then a plain list of synonyms.
  const std::string close =
      "<div>/x/<br>\n<div><font class=\"grammar\">noun</font></div>end or conclusion"
      "<ol><li><div>a</div></li><li><div>b</div></li></ol></div>";
  EXPECT_EQ(WikDict::compactHtml(close), "<p><b>noun</b></p><p><i>end or conclusion</i> \xE2\x80\x94 a, b</p>");
  EXPECT_EQ(WikDict::shortTranslation(huis + close), "dom, a, b");
  EXPECT_EQ(WikDict::compactHtml(POS("x") "<ol><li><div>a\xCC\x81"
                                          "b</div><div>ab</div></li></ol>"),
            "<p><b>x</b></p><p>a\xCC\x81"
            "b</p>");
}

TEST(WikDict, GlossNumbersDropped) {
  const std::string html =
      "<div><div><font class=\"grammar\">noun</font></div><ol><li>2. greeting<div>bye</div></li>"
      "<li>1.<div>hi</div></li></ol></div>";
  EXPECT_EQ(WikDict::compactHtml(html), "<p><b>noun</b></p><p>1. <i>greeting</i> \xE2\x80\x94 bye</p><p>2. hi</p>");
}

TEST(WikDict, PartOfSpeechWordsSplit) {
  const auto entries = WikDict::parse(POS("possessivePronoun") "<div>his</div>");
  ASSERT_EQ(entries.size(), 1u);
  EXPECT_EQ(entries[0].partOfSpeech, "possessive pronoun");
}
