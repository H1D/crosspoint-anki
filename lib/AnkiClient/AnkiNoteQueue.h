#pragma once

#include <cstddef>
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

  bool append(const Note& n);
  std::vector<Note> load() const;
  size_t count() const { return load().size(); }
  bool empty() const;
  // Rewrites the queue without the acknowledged client ids.
  bool removeAcked(const std::vector<std::string>& ackedIds);
  bool clear() { return !fs.exists(path) || fs.remove(path); }

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
std::string frontHtml(const std::string& word, const std::string& sentence);

// Sanitizes a book title into a tag: spaces -> '_', tags cannot contain spaces.
std::string bookTag(const std::string& title);

// Strips surrounding punctuation from a page token ("word," -> "word").
std::string cleanWord(const std::string& token);

}  // namespace ankinote
