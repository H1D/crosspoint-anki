#include "AnkiNoteQueue.h"

#include <algorithm>

#include "AnkiJson.h"
#include "AnkiMarkup.h"

namespace {
bool parseLine(const std::string& line, AnkiNoteQueue::Note& out) {
  std::string key;
  bool inTags = false;
  bool ok = false;
  ankijson::Reader::Callbacks cb;
  cb.onKey = [&](const std::string& k) { key = k; };
  cb.onArrayStart = [&]() { inTags = key == "tags"; };
  cb.onArrayEnd = [&]() { inTags = false; };
  cb.onString = [&](const std::string& v) {
    if (inTags) {
      out.tags.push_back(v);
      return;
    }
    if (key == "client_id") {
      out.clientId = v;
      ok = true;
    } else if (key == "deck") {
      out.deck = v;
    } else if (key == "model") {
      out.model = v;
    } else if (key == "front") {
      out.front = v;
    } else if (key == "back") {
      out.back = v;
    }
  };
  ankijson::Reader reader(std::move(cb));
  reader.feed(line);
  return ok && !reader.hasError();
}
}  // namespace

std::string AnkiNoteQueue::toLine(const Note& n) {
  std::string line = "{\"client_id\":";
  ankijson::appendQuoted(line, n.clientId);
  line += ",\"deck\":";
  ankijson::appendQuoted(line, n.deck);
  line += ",\"model\":";
  ankijson::appendQuoted(line, n.model);
  line += ",\"front\":";
  ankijson::appendQuoted(line, n.front);
  line += ",\"back\":";
  ankijson::appendQuoted(line, n.back);
  line += ",\"tags\":[";
  for (size_t i = 0; i < n.tags.size(); i++) {
    if (i) line.push_back(',');
    ankijson::appendQuoted(line, n.tags[i]);
  }
  line += "]}\n";
  return line;
}

bool AnkiNoteQueue::append(const Note& n) {
  const std::string line = toLine(n);
  auto f = fs.open(path, AnkiFs::Mode::Append);
  if (!f) return false;
  const bool ok = f->write(line) == line.size();
  f->close();
  return ok;
}

std::vector<AnkiNoteQueue::Note> AnkiNoteQueue::load() const {
  std::vector<Note> notes;
  auto f = fs.open(path, AnkiFs::Mode::Read);
  if (!f) return notes;
  AnkiLineReader lines(*f);
  std::string line;
  while (lines.next(line)) {
    if (line.empty()) continue;
    Note n;
    if (parseLine(line, n)) notes.push_back(std::move(n));
  }
  f->close();
  return notes;
}

bool AnkiNoteQueue::empty() const {
  auto f = fs.open(path, AnkiFs::Mode::Read);
  if (!f) return true;
  AnkiLineReader lines(*f);
  std::string line;
  bool any = false;
  while (lines.next(line)) {
    if (!line.empty()) {
      any = true;
      break;
    }
  }
  f->close();
  return !any;
}

bool AnkiNoteQueue::removeAcked(const std::vector<std::string>& ackedIds) {
  if (ackedIds.empty()) return true;
  std::string remaining;
  size_t kept = 0;
  for (const Note& n : load()) {
    if (std::find(ackedIds.begin(), ackedIds.end(), n.clientId) != ackedIds.end()) continue;
    kept++;
    remaining += toLine(n);
  }
  if (kept == 0) return clear();
  return fs.writeAllAtomic(path, remaining);
}

std::string AnkiNoteQueue::toNotesRequestJson(const std::vector<Note>& notes) {
  // Request-level deck/model are required by the API; the per-note overrides
  // carry the real values, so the first note's values serve as the defaults.
  std::string out = "{\"deck\":";
  ankijson::appendQuoted(out, notes.empty() ? "Default" : notes[0].deck);
  out += ",\"model\":";
  ankijson::appendQuoted(out, notes.empty() ? "Basic" : notes[0].model);
  out += ",\"dedupe\":\"update\",\"notes\":[";
  for (size_t i = 0; i < notes.size(); i++) {
    const Note& n = notes[i];
    if (i) out.push_back(',');
    out += "{\"client_id\":";
    ankijson::appendQuoted(out, n.clientId);
    out += ",\"deck\":";
    ankijson::appendQuoted(out, n.deck);
    out += ",\"model\":";
    ankijson::appendQuoted(out, n.model);
    out += ",\"fields\":{\"Front\":";
    ankijson::appendQuoted(out, n.front);
    out += ",\"Back\":";
    ankijson::appendQuoted(out, n.back);
    out += "},\"tags\":[";
    for (size_t t = 0; t < n.tags.size(); t++) {
      if (t) out.push_back(',');
      ankijson::appendQuoted(out, n.tags[t]);
    }
    out += "]}";
  }
  out += "]}";
  return out;
}

namespace ankinote {

namespace {
bool endsSentence(const std::string& w) {
  if (w.empty()) return false;
  // Walk back over closing quotes/brackets to the last real character.
  size_t i = w.size();
  while (i > 0) {
    const unsigned char c = static_cast<unsigned char>(w[i - 1]);
    if (c == '"' || c == '\'' || c == ')' || c == ']') {
      i--;
      continue;
    }
    // U+201D right double quote (E2 80 9D), U+2019 right single (E2 80 99)
    if (i >= 3 && static_cast<unsigned char>(w[i - 3]) == 0xE2 && static_cast<unsigned char>(w[i - 2]) == 0x80 &&
        (c == 0x9D || c == 0x99)) {
      i -= 3;
      continue;
    }
    break;
  }
  if (i == 0) return false;
  const char c = w[i - 1];
  if (c == '.' || c == '!' || c == '?') return true;
  // U+2026 ellipsis (E2 80 A6)
  return i >= 3 && static_cast<unsigned char>(w[i - 3]) == 0xE2 && static_cast<unsigned char>(w[i - 2]) == 0x80 &&
         static_cast<unsigned char>(w[i - 1]) == 0xA6;
}
}  // namespace

std::string sentenceAround(const std::vector<std::string>& words, size_t index) {
  if (words.empty() || index >= words.size()) return "";
  size_t start = index;
  while (start > 0 && !endsSentence(words[start - 1])) start--;
  size_t end = index;
  while (end + 1 < words.size() && !endsSentence(words[end])) end++;
  std::string out;
  for (size_t i = start; i <= end; i++) {
    if (!out.empty()) out.push_back(' ');
    out += words[i];
  }
  return out;
}

std::string cleanWord(const std::string& token) {
  size_t b = 0;
  size_t e = token.size();
  auto isPunct = [](unsigned char c) {
    return c < 0x80 && !((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9'));
  };
  // Strip ASCII punctuation and typographic quotes/dashes/ellipsis (3-byte
  // sequences E2 80 xx) from both ends until nothing changes.
  for (;;) {
    const size_t b0 = b;
    const size_t e0 = e;
    while (b < e && isPunct(static_cast<unsigned char>(token[b]))) b++;
    while (e > b && isPunct(static_cast<unsigned char>(token[e - 1]))) e--;
    while (e - b >= 3 && static_cast<unsigned char>(token[e - 3]) == 0xE2 &&
           static_cast<unsigned char>(token[e - 2]) == 0x80) {
      e -= 3;
    }
    while (e - b >= 3 && static_cast<unsigned char>(token[b]) == 0xE2 &&
           static_cast<unsigned char>(token[b + 1]) == 0x80) {
      b += 3;
    }
    if (b == b0 && e == e0) break;
  }
  return token.substr(b, e - b);
}

std::string frontHtml(const std::string& word, const std::string& sentence) {
  const std::string w = ankimarkup::htmlEscape(word);
  std::string html = "<b>" + w + "</b>";
  if (sentence.empty()) return html;
  std::string s = ankimarkup::htmlEscape(sentence);
  // Bold the first occurrence of the word inside the sentence.
  const size_t at = s.find(w);
  if (at != std::string::npos) s.replace(at, w.size(), "<b>" + w + "</b>");
  html += "<br><br>" + s;
  return html;
}

std::string bookTag(const std::string& title) {
  std::string tag = "book:";
  for (const char c : title) {
    if (c == ' ' || c == '\t' || c == '\n') {
      tag.push_back('_');
    } else if (c != '"') {
      tag.push_back(c);
    }
  }
  if (tag.size() > 60) tag.resize(60);
  return tag;
}

}  // namespace ankinote
