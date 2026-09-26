#include "AnkiMarkup.h"

namespace ankimarkup {

std::string stripBidiControls(const std::string& s) {
  std::string out;
  out.reserve(s.size());
  for (size_t i = 0; i < s.size(); i++) {
    const unsigned char c = static_cast<unsigned char>(s[i]);
    // U+2066..U+2069 = E2 81 A6..A9 ; U+202A..U+202E = E2 80 AA..AE
    if (c == 0xE2 && i + 2 < s.size()) {
      const unsigned char c1 = static_cast<unsigned char>(s[i + 1]);
      const unsigned char c2 = static_cast<unsigned char>(s[i + 2]);
      if ((c1 == 0x81 && c2 >= 0xA6 && c2 <= 0xA9) || (c1 == 0x80 && c2 >= 0xAA && c2 <= 0xAE)) {
        i += 2;
        continue;
      }
    }
    out.push_back(static_cast<char>(c));
  }
  return out;
}

namespace {
void flush(std::vector<Run>& runs, std::string& buf, bool bold, bool italic) {
  if (buf.empty()) return;
  Run r;
  r.text = buf;
  r.bold = bold;
  r.italic = italic;
  runs.push_back(std::move(r));
  buf.clear();
}

bool isWordChar(char c) {
  const unsigned char u = static_cast<unsigned char>(c);
  return u >= 0x80 || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9');
}
}  // namespace

std::vector<Run> parse(const std::string& input) {
  const std::string text = stripBidiControls(input);
  std::vector<Run> runs;
  std::string buf;
  bool bold = false;
  bool italic = false;
  const size_t n = text.size();

  for (size_t i = 0; i < n; i++) {
    const char c = text[i];
    if (c == '\n') {
      flush(runs, buf, bold, italic);
      Run nl;
      nl.newline = true;
      runs.push_back(nl);
      continue;
    }
    if (c == '*' && i + 1 < n && text[i + 1] == '*') {
      // Only toggle when a matching "**" exists later; otherwise literal.
      if (bold || text.find("**", i + 2) != std::string::npos) {
        flush(runs, buf, bold, italic);
        bold = !bold;
        i++;
        continue;
      }
    }
    if (c == '_') {
      // Italic markers sit at word edges: "_word_" but not "snake_case".
      const bool prevWord = i > 0 && isWordChar(text[i - 1]);
      const bool nextWord = i + 1 < n && isWordChar(text[i + 1]);
      const bool opener = !italic && !prevWord && nextWord && text.find('_', i + 1) != std::string::npos;
      const bool closer = italic && !nextWord;
      if (opener || closer) {
        flush(runs, buf, bold, italic);
        italic = !italic;
        continue;
      }
    }
    if (c == '[') {
      const size_t close = text.find(']', i + 1);
      if (close != std::string::npos) {
        const std::string inner = text.substr(i + 1, close - i - 1);
        flush(runs, buf, bold, italic);
        Run r;
        r.bold = bold;
        r.italic = italic;
        if (inner.rfind("img:", 0) == 0) {
          r.text = "[image]";
        } else {
          r.text = inner;
          r.cloze = true;
        }
        runs.push_back(std::move(r));
        i = close;
        continue;
      }
    }
    buf.push_back(c);
  }
  flush(runs, buf, bold, italic);
  return runs;
}

std::string toPlain(const std::string& text) {
  std::string out;
  for (const Run& r : parse(text)) {
    if (r.newline) {
      out.push_back('\n');
    } else if (r.cloze) {
      out += "[" + r.text + "]";
    } else {
      out += r.text;
    }
  }
  return out;
}

std::string htmlEscape(const std::string& s) {
  std::string out;
  out.reserve(s.size() + 8);
  for (const char c : s) {
    switch (c) {
      case '&':
        out += "&amp;";
        break;
      case '<':
        out += "&lt;";
        break;
      case '>':
        out += "&gt;";
        break;
      case '"':
        out += "&quot;";
        break;
      default:
        out.push_back(c);
    }
  }
  return out;
}

}  // namespace ankimarkup
