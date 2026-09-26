#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "AnkiFs.h"
#include "AnkiTypes.h"

// Append-only journal of reviews graded on the device but not yet
// acknowledged by AnkiDo. One JSON object per line:
//   {"client_id":"x4-12","card_id":123,"ease":3,"answered_at":1758880123}
// answered_at is omitted when the device clock was not trustworthy.
class AnkiJournal {
 public:
  struct Entry {
    std::string clientId;
    int64_t cardId = 0;
    uint8_t ease = 3;
    int64_t answeredAt = 0;  // 0 = unknown
  };

  AnkiJournal(AnkiFs& fs, std::string path) : fs(fs), path(std::move(path)) {}

  bool append(const Entry& e);
  std::vector<Entry> load() const;
  size_t count() const { return load().size(); }
  bool empty() const;
  // Set of card ids already graded on this device (to skip them in the cache).
  std::vector<int64_t> gradedCardIds() const;

  // JSON array text for the "reviews" field of POST /exchange.
  static std::string toReviewsJson(const std::vector<Entry>& entries);

  // Rewrites the journal without the acknowledged client ids.
  bool removeAcked(const std::vector<std::string>& ackedIds);

  bool clear() { return !fs.exists(path) || fs.remove(path); }

 private:
  AnkiFs& fs;
  std::string path;
};
