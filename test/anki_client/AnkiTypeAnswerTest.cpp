#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "AnkiHtml.h"
#include "AnkiMarkup.h"
#include "AnkiTypeAnswer.h"

namespace {

// Runs flattened to text: marked stretches in <...>, bold in *...*, newlines as '|'.
std::string show(const std::vector<ankimarkup::Run>& runs) {
  std::string out;
  for (const ankimarkup::Run& r : runs) {
    if (r.newline) {
      out += "|";
    } else if (r.mark) {
      out += "<" + r.text + ">";
    } else if (r.bold) {
      out += "*" + r.text + "*";
    } else {
      out += r.text;
    }
  }
  return out;
}

}  // namespace

TEST(AnkiMarkup, TypeMarkerIsOneRunNotACloze) {
  const auto runs = ankimarkup::parse("front\n\n[[type:nc:Word]] after");
  ASSERT_EQ(runs.size(), 5u);
  EXPECT_TRUE(runs[3].typeIn);
  EXPECT_FALSE(runs[3].cloze);
  EXPECT_EQ(runs[3].text, "nc:Word");
  EXPECT_EQ(runs[4].text, " after");
  EXPECT_EQ(ankimarkup::toPlain("a [[type:Word]]b"), "a b");
  // A lone "[[type:" without its closing brackets stays text.
  EXPECT_FALSE(ankimarkup::parse("[[type:Word")[0].typeIn);
}

TEST(AnkiTypeAnswer, ParsesSpecs) {
  ankitype::Spec spec;
  ASSERT_TRUE(ankitype::findSpec("Q [[type:Word]] tail", spec));
  EXPECT_EQ(spec.field, "Word");
  EXPECT_FALSE(spec.cloze);
  EXPECT_FALSE(spec.ignoreAccents);
  ASSERT_TRUE(ankitype::findSpec("[[type:cloze:Text]]", spec));
  EXPECT_TRUE(spec.cloze);
  EXPECT_EQ(spec.field, "Text");
  spec = ankitype::parseSpec("nc:cloze:Back Side");
  EXPECT_TRUE(spec.cloze);
  EXPECT_TRUE(spec.ignoreAccents);
  EXPECT_EQ(spec.field, "Back Side");
  EXPECT_FALSE(ankitype::findSpec("no marker [[x]]", spec));
  EXPECT_EQ(ankitype::stripMarkers("a [[type:W]] b [[type:nc:W]]"), "a  b ");
}

TEST(AnkiTypeAnswer, ClozeForTyping) {
  const std::string text = "{{c1::Paris}} is in {{c2::France::country}}, {{c1::Paris}} again";
  EXPECT_EQ(ankitype::clozeForTyping(text, 1), "Paris");
  EXPECT_EQ(ankitype::clozeForTyping(text, 2), "France");
  EXPECT_EQ(ankitype::clozeForTyping(text, 3), "");
  EXPECT_EQ(ankitype::clozeForTyping("{{c1::one}} and {{c1::two}}", 1), "one, two");
  EXPECT_EQ(ankitype::clozeForTyping("{{c1::outer {{c2::inner}} end}}", 1), "outer inner end");
  EXPECT_EQ(ankitype::clozeForTyping("{{c1::outer {{c2::inner}} end}}", 2), "inner");
}

TEST(AnkiTypeAnswer, ExpectedTextFromFieldHtml) {
  ankitype::Spec plain;
  plain.field = "Word";
  EXPECT_EQ(ankitype::expectedFromField("<b>pro</b>mise&nbsp; [sound:p.mp3]<br>", plain, 0), "promise");
  EXPECT_EQ(ankitype::expectedFromField("<div>to go</div><div>gone</div>", plain, 0), "to go gone");
  ankitype::Spec cloze = plain;
  cloze.cloze = true;
  EXPECT_EQ(ankitype::expectedFromField("{{c1::a}} {{c2::<i>b</i>}}", cloze, 1), "b");
  EXPECT_EQ(ankihtml::toTextLine("<i>x</i> <img src=\"a.png\"> y"), "x y");
}

TEST(AnkiTypeAnswer, CompareShowsAnkiStyleDiff) {
  EXPECT_EQ(show(ankitype::compare("promise", "promise", false)), "*promise*");
  EXPECT_EQ(show(ankitype::compare("  promise ", "promise", false)), "*promise*");
  EXPECT_EQ(show(ankitype::compare("", "promise", false)), "*promise*");
  // One wrong letter: marked in the typed line, the right one marked below.
  EXPECT_EQ(show(ankitype::compare("promiss", "promise", false)), "promis<s>|*promis*<e>");
  // A missing letter is only marked in the expected line.
  EXPECT_EQ(show(ankitype::compare("pomise", "promise", false)), "pomise|*p*<r>*omise*");
  // A wrong space shows as a middle dot rather than as nothing.
  EXPECT_EQ(show(ankitype::compare("ice cream", "icecream", false)), "ice<\xC2\xB7>cream|*icecream*");
  EXPECT_EQ(show(ankitype::compare("icecream", "ice cream", false)), "icecream|*ice*<\xC2\xB7>*cream*");
  // Case counts, as in Anki.
  EXPECT_EQ(show(ankitype::compare("Promise", "promise", false)), "<P>romise|<p>*romise*");
}

TEST(AnkiTypeAnswer, IgnoreAccentsFoldsDiacritics) {
  // "café" typed as "cafe": wrong with accents counting, right with nc:.
  EXPECT_EQ(show(ankitype::compare("cafe", "caf\xC3\xA9", false)), "caf<e>|*caf*<\xC3\xA9>");
  EXPECT_EQ(show(ankitype::compare("cafe", "caf\xC3\xA9", true)), "*caf\xC3\xA9*");
  // Polish ż/ó and Russian ё fold too; ł has no decomposition and does not.
  EXPECT_EQ(show(ankitype::compare("zolw", "\xC5\xBC\xC3\xB3\xC5\x82w", true)),
            "zo<l>w|*\xC5\xBC\xC3\xB3*<\xC5\x82>*w*");
  EXPECT_EQ(show(ankitype::compare("\xD0\xB5\xD0\xB6", "\xD1\x91\xD0\xB6", true)), "*\xD1\x91\xD0\xB6*");
  // Combining stress marks are dropped with nc: (обеща́ть == обещать).
  EXPECT_EQ(show(ankitype::compare("\xD0\xB0\xD0\xB1", "\xD0\xB0\xCC\x81\xD0\xB1", true)), "*\xD0\xB0\xD0\xB1*");
}

TEST(AnkiTypeAnswer, StressMarkStaysWithItsLetter) {
  // "ба́" typed as "бо": the accent is marked together with the а it sits on.
  EXPECT_EQ(show(ankitype::compare("\xD0\xB1\xD0\xBE", "\xD0\xB1\xD0\xB0\xCC\x81", false)),
            "\xD0\xB1<\xD0\xBE>|*\xD0\xB1*<\xD0\xB0\xCC\x81>");
}

TEST(AnkiTypeAnswer, HugeInputsAreMarkedWithoutDiffing) {
  const std::string want(1000, 'a');
  EXPECT_EQ(show(ankitype::compare("b", want, false)), "<b>|<" + want + ">");
  EXPECT_EQ(show(ankitype::compare("", want, false)), "*" + want + "*");
}

TEST(AnkiTypeAnswer, LongAnswersAreMarkedWhole) {
  const std::string want(100, 'a');
  const std::string got = want.substr(0, 99) + "b";
  const std::string shown = show(ankitype::compare(got, want, false));
  EXPECT_EQ(shown, "<" + got + ">|<" + want + ">");
}
