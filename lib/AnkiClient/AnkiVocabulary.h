#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "AnkiNoteQueue.h"

// Words looked up while reading, from upstream's /Vocabulary.txt (JSON Lines,
// one record per lookup), turned into notes in one go. The firmware screen is
// src/anki/AnkiVocabularyActivity.*; this is the part that runs on the host.
namespace ankivocab {

struct Record {
  std::string id;
  std::string word;      // as picked on the page
  std::string headword;  // dictionary form, may equal `word`
  std::string definition;
  std::string title;
  // Written by the fork when a lookup could also have been an add-word:
  // the note's sentence and Back field exactly as the single-word screen
  // would have queued them.
  std::string sentence;
  std::string back;
};

// A complete JSON object with schema "v":1 and a non-empty id and word.
bool parseRecord(const std::string& line, Record& out);

// De-duplication key: the lowercased dictionary form, else the word.
std::string rowKey(const Record& r);
// The word line on top of the card: the dictionary form when it differs from
// the word, else the word.
std::string label(const Record& r);
// Front, Back and tags as the add-word screen would have queued them; the
// caller fills clientId, deck and model.
AnkiNoteQueue::Note toNote(const Record& r);

// Random-access bytes of the vocabulary file (HalFile on the device).
class Source {
 public:
  virtual ~Source() = default;
  virtual bool seek(size_t pos) = 0;
  // Returns bytes read, 0 at EOF, <0 on error.
  virtual int read(void* buf, size_t len) = 0;
};

// The last record a previous Send handled.
struct Marker {
  size_t pos = 0;  // byte offset where that record starts
  std::string id;
};

// The not-yet-handled records, up to a row limit, with one row per distinct
// word. Only labels stay in memory; Send walks the window again.
struct Window {
  size_t from = 0;                  // first byte after the marker's record, else 0
  std::vector<std::string> labels;  // one per row
  std::optional<Marker> last;       // the last record in the window: the next marker
};

Window scan(Source& source, const std::optional<Marker>& done, size_t maxRows);

// Calls `onRow(row, record)` for the first record of each row of `window`,
// in row order. Returns true only when the file still is the one the window
// was scanned from: no read error, exactly window.labels.size() rows, and the
// last record at window.last's offset with its id. On false some rows may
// not have been delivered.
bool forEachRow(Source& source, const Window& window, const std::function<void(size_t row, const Record&)>& onRow);

}  // namespace ankivocab
