#include "AnkiVocabulary.h"

#include <algorithm>

#include "AnkiJson.h"

namespace ankivocab {

namespace {

// A record with a 2 KiB definition whose newlines are \u000a escapes stays
// well under this; a longer line is not one of ours and is skipped.
constexpr size_t MAX_LINE_BYTES = 16 * 1024;
// Same cap as the Back field of the single-word screen.
constexpr size_t MAX_BACK_BYTES = 600;

bool isSpace(const char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r'; }

// ASCII, Latin-1 and basic Cyrillic: the scripts of the books this is read in.
std::string lower(const std::string& s) {
  std::string out;
  out.reserve(s.size());
  for (size_t i = 0; i < s.size(); i++) {
    const auto c = static_cast<unsigned char>(s[i]);
    const auto next = i + 1 < s.size() ? static_cast<unsigned char>(s[i + 1]) : 0;
    if (c >= 'A' && c <= 'Z') {
      out.push_back(static_cast<char>(c + 0x20));
    } else if (c == 0xC3 && next >= 0x80 && next <= 0x9E && next != 0x97) {
      out.push_back(static_cast<char>(c));
      out.push_back(static_cast<char>(next + 0x20));
      i++;
    } else if (c == 0xD0 && next >= 0x90 && next <= 0x9F) {
      out.push_back(static_cast<char>(c));
      out.push_back(static_cast<char>(next + 0x20));
      i++;
    } else if (c == 0xD0 && next >= 0xA0 && next <= 0xAF) {
      out.push_back(static_cast<char>(0xD1));
      out.push_back(static_cast<char>(next - 0x20));
      i++;
    } else if (c == 0xD0 && next >= 0x80 && next <= 0x8F) {
      out.push_back(static_cast<char>(0xD1));
      out.push_back(static_cast<char>(next + 0x10));
      i++;
    } else {
      out.push_back(static_cast<char>(c));
    }
  }
  return out;
}

// The dictionary form when it differs from the word the way the single-word
// screen decides it: Dictionary::cleanWord, which lowercases ASCII only, so
// "Walk" for "walk" is not a different word.
std::string differingHeadword(const Record& r) {
  const auto comparable = [](const std::string& text) {
    std::string out = ankinote::cleanWord(text);
    std::transform(out.begin(), out.end(), out.begin(),
                   [](const char c) { return c >= 'A' && c <= 'Z' ? static_cast<char>(c + 0x20) : c; });
    return out;
  };
  const std::string headword = comparable(r.headword);
  return !headword.empty() && headword != comparable(r.word) ? r.headword : std::string();
}

// Whitespace runs become one space (the card breaks lines on spaces only),
// cut at a UTF-8 boundary.
std::string collapse(const std::string& text) {
  std::string out;
  out.reserve(std::min(text.size(), MAX_BACK_BYTES));
  bool pendingSpace = false;
  for (const char c : text) {
    if (isSpace(c)) {
      pendingSpace = !out.empty();
      continue;
    }
    if (pendingSpace) {
      out.push_back(' ');
      pendingSpace = false;
    }
    out.push_back(c);
  }
  if (out.size() > MAX_BACK_BYTES) {
    size_t cut = MAX_BACK_BYTES;
    while (cut > 0 && (static_cast<uint8_t>(out[cut]) & 0xC0) == 0x80) cut--;
    out.resize(cut);
  }
  return out;
}

// '\n'-terminated lines of a Source with the offset each one starts at.
class Lines {
 public:
  Lines(Source& source, const size_t from) : source(source), offset(from), seeked(source.seek(from)) {}

  // Next line that is non-empty and not over the cap; false at the end.
  bool next(std::string& line, size_t& start) {
    for (;;) {
      line.clear();
      start = offset;
      bool tooLong = false;
      bool ended = false;
      bool terminated = false;
      while (!terminated) {
        if (used == filled && !refill()) {
          ended = true;
          break;
        }
        const char c = buf[used++];
        offset++;
        if (c == '\n') {
          terminated = true;
        } else if (line.size() >= MAX_LINE_BYTES) {
          tooLong = true;
        } else if (c != '\r') {
          line.push_back(c);
        }
      }
      if (!tooLong && !line.empty()) return true;
      if (ended) return false;
    }
  }

  // Offset of the next unread byte.
  size_t position() const { return offset; }

  // True when the seek or a read failed: the lines seen so far are not the
  // whole file, whatever next() said.
  bool failed() const { return !seeked || readFailed; }

 private:
  bool refill() {
    const int n = seeked ? source.read(buf, sizeof(buf)) : 0;
    if (n < 0) readFailed = true;
    if (n <= 0) return false;
    filled = static_cast<size_t>(n);
    used = 0;
    return true;
  }

  Source& source;
  size_t offset;
  bool seeked;
  bool readFailed = false;
  char buf[256] = {};
  size_t used = 0;
  size_t filled = 0;
};

// Where the window starts: right after the marker's record if the file still
// has it there, else 0 (the file was replaced or deleted).
size_t startAfter(Source& source, const std::optional<Marker>& done) {
  if (!done) return 0;
  Lines lines(source, done->pos);
  std::string line;
  size_t start = 0;
  Record r;
  if (lines.next(line, start) && start == done->pos && parseRecord(line, r) && r.id == done->id) {
    return lines.position();
  }
  return 0;
}

}  // namespace

bool parseRecord(const std::string& line, Record& out) {
  out = Record{};
  std::string key;
  int depth = 0;
  bool closed = false;
  bool versionOk = false;
  ankijson::Reader::Callbacks cb;
  cb.onKey = [&](const std::string& k) { key = k; };
  cb.onObjectStart = [&]() { depth++; };
  cb.onArrayStart = [&]() { depth++; };
  cb.onArrayEnd = [&]() { depth--; };
  cb.onObjectEnd = [&]() { closed = --depth == 0; };
  cb.onNumber = [&](const std::string& text) {
    if (depth == 1 && key == "v") versionOk = text == "1";
  };
  cb.onString = [&](const std::string& v) {
    if (depth != 1) return;
    if (key == "id") {
      out.id = v;
    } else if (key == "word") {
      out.word = v;
    } else if (key == "headword") {
      out.headword = v;
    } else if (key == "definition") {
      out.definition = v;
    } else if (key == "title") {
      out.title = v;
    } else if (key == "sentence") {
      out.sentence = v;
    } else if (key == "back") {
      out.back = v;
    }
  };
  ankijson::Reader reader(std::move(cb));
  reader.feed(line);
  // A word that is all punctuation has no card to make.
  return closed && !reader.hasError() && versionOk && !out.id.empty() && !ankinote::cleanWord(out.word).empty();
}

std::string label(const Record& r) {
  const std::string headword = differingHeadword(r);
  return headword.empty() ? ankinote::cleanWord(r.word) : headword;
}

std::string rowKey(const Record& r) { return lower(ankinote::cleanWord(label(r))); }

AnkiNoteQueue::Note toNote(const Record& r) {
  AnkiNoteQueue::Note note;
  note.front = ankinote::frontHtml(ankinote::cleanWord(r.word), r.sentence, differingHeadword(r));
  note.back = r.back.empty() ? collapse(r.definition) : r.back;
  note.tags.emplace_back("crosspoint");
  if (!r.title.empty()) note.tags.push_back(ankinote::bookTag(r.title));
  return note;
}

Window scan(Source& source, const std::optional<Marker>& done, const size_t maxRows) {
  Window window;
  window.from = startAfter(source, done);
  std::vector<std::string> keys;
  Lines lines(source, window.from);
  std::string line;
  size_t start = 0;
  Record r;
  while (lines.next(line, start)) {
    if (!parseRecord(line, r)) continue;
    const std::string key = rowKey(r);
    if (std::find(keys.begin(), keys.end(), key) == keys.end()) {
      if (keys.size() == maxRows) break;
      keys.push_back(key);
      window.labels.push_back(label(r));
    }
    window.last = Marker{start, r.id};
  }
  return window;
}

bool forEachRow(Source& source, const Window& window, const std::function<void(size_t, const Record&)>& onRow) {
  if (!window.last) return window.labels.empty();
  std::vector<std::string> keys;
  Lines lines(source, window.from);
  std::string line;
  size_t start = 0;
  Record r;
  Marker seen;
  bool any = false;
  while (lines.next(line, start) && start <= window.last->pos) {
    if (!parseRecord(line, r)) continue;
    seen = Marker{start, r.id};
    any = true;
    const std::string key = rowKey(r);
    if (std::find(keys.begin(), keys.end(), key) == keys.end()) {
      keys.push_back(key);
      onRow(keys.size() - 1, r);
    }
  }
  return !lines.failed() && any && keys.size() == window.labels.size() && seen.pos == window.last->pos &&
         seen.id == window.last->id;
}

}  // namespace ankivocab
