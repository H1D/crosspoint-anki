#include "AnkiHtml.h"

#include <cctype>
#include <cstdint>
#include <cstring>

namespace {

char lower(const char c) { return static_cast<char>(std::tolower(static_cast<unsigned char>(c))); }

bool isSpace(const char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\f' || c == '\v'; }

// Case-insensitive compare of `s` (already lowercased by the tag parser) with a literal.
bool is(const std::string& s, const char* lit) { return s == lit; }

bool isBlock(const std::string& t) {
  return is(t, "p") || is(t, "div") || is(t, "br") || is(t, "li") || is(t, "tr") || is(t, "hr") || is(t, "table") ||
         (t.size() == 2 && t[0] == 'h' && t[1] >= '1' && t[1] <= '6');
}

void appendUtf8(std::string& out, uint32_t cp) {
  if (cp < 0x80) {
    out.push_back(static_cast<char>(cp));
  } else if (cp < 0x800) {
    out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
    out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
  } else if (cp < 0x10000) {
    out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
    out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
    out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
  } else {
    out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
    out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
    out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
    out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
  }
}

struct NamedEntity {
  const char* name;
  const char* utf8;
};
// The entities Anki's editor and common templates actually emit. Anything
// else is left as typed; the device would not have the glyph anyway.
constexpr NamedEntity kEntities[] = {
    {"amp", "&"},    {"lt", "<"},    {"gt", ">"},    {"quot", "\""}, {"apos", "'"},  {"nbsp", " "},
    {"hellip", "…"}, {"mdash", "—"}, {"ndash", "–"}, {"lsquo", "‘"}, {"rsquo", "’"}, {"ldquo", "“"},
    {"rdquo", "”"},  {"laquo", "«"}, {"raquo", "»"}, {"copy", "©"},  {"deg", "°"},   {"times", "×"},
    {"middot", "·"}, {"bull", "•"},  {"euro", "€"},  {"shy", ""},    {"zwj", ""},    {"zwnj", ""},
};

// Decodes the entity that starts at s[i] == '&' into out. Returns the number
// of input characters consumed, 0 when it is not a recognised entity.
size_t decodeEntity(const std::string& s, const size_t i, std::string& out) {
  const size_t semi = s.find(';', i + 1);
  if (semi == std::string::npos || semi - i > 10) return 0;
  const std::string name = s.substr(i + 1, semi - i - 1);
  if (name.empty()) return 0;
  if (name[0] == '#') {
    const bool hex = name.size() > 1 && (name[1] == 'x' || name[1] == 'X');
    size_t p = hex ? 2 : 1;
    if (p >= name.size()) return 0;
    uint32_t cp = 0;
    for (; p < name.size(); p++) {
      const char c = name[p];
      int digit;
      if (c >= '0' && c <= '9')
        digit = c - '0';
      else if (hex && c >= 'a' && c <= 'f')
        digit = c - 'a' + 10;
      else if (hex && c >= 'A' && c <= 'F')
        digit = c - 'A' + 10;
      else
        return 0;
      cp = cp * (hex ? 16 : 10) + static_cast<uint32_t>(digit);
      if (cp > 0x10FFFF) return 0;
    }
    if (cp == 0) return 0;
    appendUtf8(out, cp);
    return semi - i + 1;
  }
  for (const NamedEntity& e : kEntities) {
    if (name == e.name) {
      out += e.utf8;
      return semi - i + 1;
    }
  }
  return 0;
}

// One start/end tag as scanned from the source.
struct Tag {
  std::string name;  // lowercase
  bool closing = false;
  std::string cls;  // class attribute
  std::string src;  // src attribute
};

// Parses the text between '<' and '>'.
void parseTag(const std::string& inner, Tag& tag) {
  size_t p = 0;
  if (p < inner.size() && inner[p] == '/') {
    tag.closing = true;
    p++;
  }
  while (p < inner.size() && !isSpace(inner[p]) && inner[p] != '/' && inner[p] != '>')
    tag.name.push_back(lower(inner[p++]));
  // Attributes: name[=value]; only class and src matter.
  while (p < inner.size()) {
    while (p < inner.size() && (isSpace(inner[p]) || inner[p] == '/')) p++;
    std::string attr;
    while (p < inner.size() && !isSpace(inner[p]) && inner[p] != '=' && inner[p] != '/')
      attr.push_back(lower(inner[p++]));
    if (attr.empty()) break;
    while (p < inner.size() && isSpace(inner[p])) p++;
    std::string value;
    if (p < inner.size() && inner[p] == '=') {
      p++;
      while (p < inner.size() && isSpace(inner[p])) p++;
      if (p < inner.size() && (inner[p] == '"' || inner[p] == '\'')) {
        const char q = inner[p++];
        while (p < inner.size() && inner[p] != q) value.push_back(inner[p++]);
        if (p < inner.size()) p++;
      } else {
        while (p < inner.size() && !isSpace(inner[p])) value.push_back(inner[p++]);
      }
    }
    if (attr == "class")
      tag.cls = value;
    else if (attr == "src")
      tag.src = value;
  }
}

bool hasClass(const std::string& cls, const char* wanted) {
  size_t p = 0;
  while (p < cls.size()) {
    while (p < cls.size() && isSpace(cls[p])) p++;
    size_t e = p;
    while (e < cls.size() && !isSpace(cls[e])) e++;
    if (e > p && cls.compare(p, e - p, wanted) == 0) return true;
    p = e;
  }
  return false;
}

// Position just past the matching end tag of a raw-text element (style,
// script), or npos.
size_t skipRawElement(const std::string& s, size_t from, const char* name) {
  const size_t n = std::strlen(name);
  for (size_t p = s.find('<', from); p != std::string::npos; p = s.find('<', p + 1)) {
    if (p + 1 < s.size() && s[p + 1] == '/' && p + 2 + n <= s.size()) {
      bool match = true;
      for (size_t k = 0; k < n && match; k++) match = lower(s[p + 2 + k]) == name[k];
      if (match) {
        const size_t close = s.find('>', p);
        return close == std::string::npos ? s.size() : close + 1;
      }
    }
  }
  return std::string::npos;
}

std::string trim(const std::string& s) {
  size_t a = 0;
  size_t b = s.size();
  while (a < b && isSpace(s[a])) a++;
  while (b > a && isSpace(s[b - 1])) b--;
  return s.substr(a, b - a);
}

class Converter {
 public:
  unsigned images = 0;
  bool plain = false;  // text only: no bold/italic/image markers

  void feed(const std::string& s) {
    size_t i = 0;
    while (i < s.size()) {
      const char c = s[i];
      if (c == '<') {
        if (s.compare(i, 4, "<!--") == 0) {
          const size_t end = s.find("-->", i + 4);
          i = end == std::string::npos ? s.size() : end + 3;
          continue;
        }
        const size_t close = findTagEnd(s, i);
        if (close == std::string::npos) {
          emitChar('<');
          i++;
          continue;
        }
        Tag tag;
        parseTag(s.substr(i + 1, close - i - 1), tag);
        i = close + 1;
        if (!tag.closing && (tag.name == "style" || tag.name == "script")) {
          const size_t after = skipRawElement(s, i, tag.name.c_str());
          i = after == std::string::npos ? s.size() : after;
          continue;
        }
        if (tag.closing)
          endTag(tag);
        else
          startTag(tag);
        continue;
      }
      if (c == '[' && (s.compare(i, 10, "[anki:play") == 0 || s.compare(i, 9, "[anki:tts") == 0 ||
                       s.compare(i, 7, "[sound:") == 0)) {
        const size_t end = s.find(']', i);
        i = end == std::string::npos ? s.size() : end + 1;
        continue;
      }
      if (c == '&') {
        std::string decoded;
        const size_t used = decodeEntity(s, i, decoded);
        if (used) {
          emit(decoded);
          i += used;
          continue;
        }
      }
      emitChar(c == '\r' || c == '\n' ? ' ' : c);
      i++;
    }
  }

  // Collapses whitespace per line, trims lines, squeezes blank-line runs.
  std::string text() const {
    std::string result;
    std::string line;
    size_t blankRun = 0;
    auto flushLine = [&]() {
      // Collapse runs of spaces/tabs inside the line and trim it.
      std::string collapsed;
      bool pendingSpace = false;
      for (const char c : line) {
        if (c == ' ' || c == '\t' || c == '\f' || c == '\v' || c == '\r') {
          pendingSpace = !collapsed.empty();
        } else {
          if (pendingSpace) collapsed.push_back(' ');
          pendingSpace = false;
          collapsed.push_back(c);
        }
      }
      line.clear();
      if (collapsed.empty()) {
        blankRun++;
        return;
      }
      if (!result.empty()) {
        result.push_back('\n');
        if (blankRun > 0) result.push_back('\n');  // at most one blank line between paragraphs
      }
      blankRun = 0;
      result += collapsed;
    };
    for (const char c : out) {
      if (c == '\n')
        flushLine();
      else
        line.push_back(c);
    }
    flushLine();
    return result;
  }

 private:
  std::string out;
  std::string clozeBuf;
  int clozeDepth = 0;

  static size_t findTagEnd(const std::string& s, const size_t lt) {
    // Only a letter, '/' or '!' after '<' starts a tag; a stray '<' is text.
    if (lt + 1 >= s.size()) return std::string::npos;
    const char n = s[lt + 1];
    if (!std::isalpha(static_cast<unsigned char>(n)) && n != '/' && n != '!') return std::string::npos;
    char quote = 0;
    for (size_t p = lt + 1; p < s.size(); p++) {
      const char c = s[p];
      if (quote) {
        if (c == quote) quote = 0;
      } else if (c == '"' || c == '\'') {
        quote = c;
      } else if (c == '>') {
        return p;
      }
    }
    return std::string::npos;
  }

  void emit(const std::string& s) { (clozeDepth ? clozeBuf : out) += s; }
  void emitChar(const char c) { (clozeDepth ? clozeBuf : out).push_back(c); }

  void startTag(const Tag& t) {
    if (t.name == "span") {
      if (hasClass(t.cls, "cloze")) {
        if (clozeDepth == 0) clozeBuf.clear();
        clozeDepth++;
        return;
      }
      if (clozeDepth) {
        clozeDepth++;
        return;
      }
    }
    if (t.name == "b" || t.name == "strong") {
      if (!plain) emit("**");
    } else if (t.name == "i" || t.name == "em") {
      if (!plain) emit("_");
    } else if (isBlock(t.name)) {
      emitChar('\n');
    } else if (t.name == "img") {
      images++;
      if (!plain) emit("[img:" + t.src + "]");
    }
  }

  void endTag(const Tag& t) {
    if (t.name == "span" && clozeDepth) {
      clozeDepth--;
      if (clozeDepth == 0) {
        // Question side: Anki already renders "[...]" or "[hint]"; keep it.
        // Answer side: the span holds the answer text; bracket it.
        const std::string inner = trim(clozeBuf);
        if (inner.size() >= 2 && inner.front() == '[' && inner.back() == ']')
          out += inner;
        else
          out += "[" + inner + "]";
        clozeBuf.clear();
      }
      return;
    }
    if (t.name == "b" || t.name == "strong") {
      if (!plain) emit("**");
    } else if (t.name == "i" || t.name == "em") {
      if (!plain) emit("_");
    } else if (t.name != "br" && isBlock(t.name)) {
      emitChar('\n');
    }
  }
};

}  // namespace

namespace ankihtml {

std::string toMarkup(const std::string& html, unsigned* images) {
  Converter conv;
  conv.feed(html);
  if (images) *images = conv.images;
  return conv.text();
}

std::string toTextLine(const std::string& html) {
  Converter conv;
  conv.plain = true;
  conv.feed(html);
  std::string text = conv.text();
  std::string out;
  out.reserve(text.size());
  for (const char c : text) {
    if (c != '\n') {
      out.push_back(c);
    } else if (!out.empty() && out.back() != ' ') {
      out.push_back(' ');
    }
  }
  return out;
}

std::string answerPart(const std::string& answerHtml) {
  // <hr id=answer>, <hr id="answer">, <hr id='answer' />, any case.
  for (size_t p = answerHtml.find('<'); p != std::string::npos; p = answerHtml.find('<', p + 1)) {
    if (p + 3 >= answerHtml.size() || lower(answerHtml[p + 1]) != 'h' || lower(answerHtml[p + 2]) != 'r' ||
        !isSpace(answerHtml[p + 3]))
      continue;
    const size_t close = answerHtml.find('>', p);
    if (close == std::string::npos) break;
    // parseTag only keeps class/src; look for id=answer by hand.
    std::string lowered;
    for (size_t k = p + 1; k < close; k++) lowered.push_back(lower(answerHtml[k]));
    size_t idPos = lowered.find("id=");
    if (idPos == std::string::npos) continue;
    idPos += 3;
    if (idPos < lowered.size() && (lowered[idPos] == '"' || lowered[idPos] == '\'')) idPos++;
    if (lowered.compare(idPos, 6, "answer") == 0) return answerHtml.substr(close + 1);
  }
  return answerHtml;
}

}  // namespace ankihtml
