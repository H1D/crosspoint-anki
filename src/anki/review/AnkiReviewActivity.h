#pragma once

#include <AnkiCardCache.h>
#include <AnkiMarkup.h>
#include <AnkiTypes.h>

#include <cstdint>
#include <memory>
#include <vector>

#include "activities/Activity.h"
#include "util/ButtonNavigator.h"

// Flashcard review for the review account: shows the cached queue one card
// at a time (front, flip, grade) and journals each grade for the next sync.
// Only the current card and its styled runs live in RAM; the cache is read
// one line at a time.
class AnkiReviewActivity final : public Activity {
 public:
  explicit AnkiReviewActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("AnkiReview", renderer, mappedInput) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  enum class Mode { NoAccount, Empty, Front, Back };

  // One run's portion on one wrapped line: byte span of runs[run].text,
  // drawn at x within the body. Width is kept so cloze boxes need no re-measure.
  struct Segment {
    uint16_t run;
    uint16_t start;
    uint16_t len;
    uint16_t width;
    int16_t x;
    uint16_t line;
  };

  struct BodyArea {
    int x;
    int y;
    int width;
    int height;
  };

  BodyArea bodyArea() const;
  int gradeStripHeight() const;
  int gradeStripTop() const;
  void reload();
  void loadCurrent();
  void flip();
  void grade(uint8_t ease);
  void onBack();
  void startSync();
  void layout();
  int measure(int fontId, const char* text, size_t len, EpdFontFamily::Style style) const;
  void drawBody(int fontId, const BodyArea& body) const;
  void drawHeader(int contentX, int contentWidth, int headerY) const;
  void drawGradeStrips() const;
  void drawHints() const;
  const char* kindLabel() const;
  static EpdFontFamily::Style styleOf(const ankimarkup::Run& run);

  Mode mode = Mode::NoAccount;
  uint32_t accountId = 0;
  std::unique_ptr<AnkiCardCache> cache;
  size_t cacheCount = 0;
  std::vector<uint16_t> queue;  // cache indices still to review, in cache order
  size_t cursor = 0;
  AnkiCard card;
  std::vector<ankimarkup::Run> runs;  // question runs, answer runs appended on flip
  size_t answerRunStart = 0;          // runs.size() while the front is shown
  std::vector<Segment> segments;
  int lineCount = 0;
  int ruleLine = -1;  // line index of the question/answer separator, -1 on the front
  int currentPage = 0;
  int totalPages = 1;
  int linesPerPage = 1;
  ButtonNavigator buttonNavigator;
};
