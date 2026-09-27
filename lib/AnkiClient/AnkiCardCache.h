#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "AnkiFs.h"
#include "AnkiTypes.h"

// The cached review queue for one account: cards.jsonl (one card per line,
// written while the exchange response streams in) plus cache.json with the
// server counts and the fetch time. Cards are read one line at a time, so the
// cache never has to fit in RAM.
class AnkiCardCache {
 public:
  AnkiCardCache(AnkiFs& fs, std::string cardsPath, std::string metaPath)
      : fs(fs), cardsPath(std::move(cardsPath)), metaPath(std::move(metaPath)) {}

  // Scans the file once to learn how many cards it holds. Cheap enough for the
  // ~60 lines a cache holds; called when the review app opens.
  size_t count();
  // Loads the n-th card (0-based). Returns false past the end or on a bad line.
  bool load(size_t index, AnkiCard& out);
  // Card id of every line, index-aligned with load(), in one pass that reads
  // only the "card_id" number (0 for a line without one). Cheap enough to
  // run when the review app opens, unlike load() per index.
  std::vector<int64_t> cardIds();
  static int64_t lineCardId(const std::string& line);

  struct Meta {
    AnkiCounts counts;
    int64_t fetchedAt = 0;
    std::string syncError;  // last inline-sync error reported by AnkiDo, if any
  };
  bool readMeta(Meta& out);
  bool writeMeta(const Meta& meta);

  bool clear();

  // Line format helpers, shared with the exchange parser and tests.
  static std::string toLine(const AnkiCard& card);
  static bool parseLine(const std::string& line, AnkiCard& out);

  const std::string& path() const { return cardsPath; }

 private:
  AnkiFs& fs;
  std::string cardsPath;
  std::string metaPath;
};
