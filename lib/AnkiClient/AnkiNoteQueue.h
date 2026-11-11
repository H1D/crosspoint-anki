#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <string>
#include <vector>

#include "AnkiFs.h"

// Queue of notes created on the device (add-word) and not yet accepted by
// AnkiDo. One JSON object per line:
//   {"client_id":"x4-n7","deck":"Dutch","model":"Basic","front":"<b>huis</b>...",
//    "back":"house","tags":["crosspoint","book:Title"]}
class AnkiNoteQueue {
 public:
  struct Note {
    std::string clientId;
    std::string deck;
    std::string model;
    std::string front;
    std::string back;
    std::vector<std::string> tags;
  };

  AnkiNoteQueue(AnkiFs& fs, std::string path) : fs(fs), path(std::move(path)) {}

  // Notes per request (and per rewrite of the file) when pushing: bounds the
  // RAM a push needs however long the queue is.
  static constexpr size_t PUSH_CHUNK = 10;

  bool append(const Note& n);
  // At most `maxCount` notes, after the first `skip` readable ones.
  std::vector<Note> load(size_t maxCount = std::numeric_limits<size_t>::max(), size_t skip = 0) const;
  size_t count() const;
  bool empty() const;
  // Rewrites the queue without the acknowledged client ids, line by line
  // through a temp file. The queue is left alone when the copy fails.
  bool removeAcked(const std::vector<std::string>& ackedIds);
  bool clear() { return !fs.exists(path) || fs.remove(path); }

  // Pushes the queue PUSH_CHUNK notes at a time. `pushChunk` sends one chunk,
  // adds the client ids the server is done with to `acked`, and returns false
  // when its request failed. After each chunk the acked notes leave the file.
  // Notes the server refused stay queued and are not offered again in this call.
  using PushChunk = std::function<bool(const std::vector<Note>& chunk, std::vector<std::string>& acked)>;
  struct Drain {
    enum class End : uint8_t {
      Empty,       // nothing left to offer
      Stopped,     // shouldStop() said the time is up; the rest stays queued
      NoProgress,  // a chunk was entirely refused
      Failed,      // pushChunk reported a failed request
      SaveFailed   // the queue file could not be rewritten
    };
    size_t sent = 0;   // notes offered to the server
    size_t acked = 0;  // notes the server finished with
    End end = End::Empty;
  };
  // `shouldStop` (may be empty) is asked before every chunk.
  Drain drain(const std::function<bool()>& shouldStop, const PushChunk& pushChunk);

  // Request body for POST /notes: notes grouped in one request, each with its
  // own deck/model override, "dedupe":"update" (see DECISIONS.md).
  static std::string toNotesRequestJson(const std::vector<Note>& notes);

 private:
  static std::string toLine(const Note& n);
  AnkiFs& fs;
  std::string path;
};

// Builds the note for a word picked in a book.
namespace ankinote {

// Returns the sentence around `index` within `words` (page words in reading
// order): from the word after the previous sentence terminator (. ! ? or a
// closing quote following one) to the next terminator inclusive.
std::string sentenceAround(const std::vector<std::string>& words, size_t index);

// Front field HTML: bold word, blank line, sentence with the word bolded.
// A non-empty `headword` (the dictionary form, "komen" for "kwam") replaces
// the word on top; the sentence still bolds the word as written.
std::string frontHtml(const std::string& word, const std::string& sentence, const std::string& headword = {});

// Sanitizes a book title into a tag: spaces -> '_', tags cannot contain spaces.
std::string bookTag(const std::string& title);

// Strips surrounding punctuation from a page token ("word," -> "word").
std::string cleanWord(const std::string& token);

}  // namespace ankinote
