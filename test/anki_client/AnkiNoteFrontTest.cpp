#include <gtest/gtest.h>

#include "AnkiNoteQueue.h"

TEST(AnkiNoteFront, HeadwordOnTopWordAsWrittenInSentence) {
  EXPECT_EQ(ankinote::frontHtml("kwam", "Ze kwam binnen.", "komen"), "<b>komen</b><br><br>Ze <b>kwam</b> binnen.");
  EXPECT_EQ(ankinote::frontHtml("kwam", "Ze kwam.", ""), "<b>kwam</b><br><br>Ze <b>kwam</b>.");
  EXPECT_EQ(ankinote::frontHtml("kwam", "", "komen"), "<b>komen</b><br><br><b>kwam</b>");
}
