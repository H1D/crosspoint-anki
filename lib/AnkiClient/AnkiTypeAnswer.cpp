#include "AnkiTypeAnswer.h"

#include <algorithm>
#include <cstdint>
#include <memory>
#include <new>

#include "AnkiHtml.h"

namespace ankitype {

namespace {

constexpr char MARKER_OPEN[] = "[[type:";
constexpr size_t MARKER_OPEN_LEN = sizeof(MARKER_OPEN) - 1;
// Longest answer (in code points) the character diff runs on; longer ones are
// marked whole. The LCS table is (n+1)^2 bytes, ~6.5 KB at the cap.
constexpr size_t MAX_DIFF_CODEPOINTS = 80;
// Longer inputs skip the diff without being decoded.
constexpr size_t MAX_COMPARE_BYTES = 4 * MAX_DIFF_CODEPOINTS;
constexpr char MIDDLE_DOT[] = "\xC2\xB7";  // stands in for a marked space, which would draw as nothing

// Base letter of each code point in U+00C0..U+017F that Unicode decomposes into
// letter + combining mark ('.' = no such decomposition: æ ø ß ł đ ħ œ ...).
// Stripping those marks is what Anki's "nc:" does.
constexpr char FOLD_LATIN[] =
    "AAAAAA.CEEEEIIII"   // C0
    ".NOOOOO..UUUUY.."   // D0
    "aaaaaa.ceeeeiiii"   // E0
    ".nooooo..uuuuy.y"   // F0
    "AaAaAaCcCcCcCcDd"   // 100
    "..EeEeEeEeEeGgGg"   // 110
    "GgGgHh..IiIiIiIi"   // 120
    "I...JjKk.LlLlLl."   // 130
    "...NnNnNn...OoOo"   // 140
    "Oo..RrRrRrSsSsSs"   // 150
    "SsTtTt..UuUuUuUu"   // 160
    "UuUuWwYyYZzZzZz.";  // 170
static_assert(sizeof(FOLD_LATIN) - 1 == 0x180 - 0xC0, "one entry per code point");

bool isCombining(const uint32_t cp) { return cp >= 0x300 && cp <= 0x36F; }

uint32_t foldAccent(const uint32_t cp) {
  if (cp >= 0xC0 && cp < 0x180) {
    const char base = FOLD_LATIN[cp - 0xC0];
    return base == '.' ? cp : static_cast<uint32_t>(base);
  }
  switch (cp) {
    case 0x218:  // Ș
      return 'S';
    case 0x219:  // ș
      return 's';
    case 0x21A:  // Ț
      return 'T';
    case 0x21B:  // ț
      return 't';
    case 0x401:  // Ё
      return 0x415;
    case 0x407:  // Ї
      return 0x406;
    case 0x40E:  // Ў
      return 0x423;
    case 0x419:  // Й
      return 0x418;
    case 0x439:  // й
      return 0x438;
    case 0x451:  // ё
      return 0x435;
    case 0x457:  // ї
      return 0x456;
    case 0x45E:  // ў
      return 0x443;
    default:
      return cp;
  }
}

std::vector<uint32_t> decode(const std::string& s) {
  std::vector<uint32_t> out;
  out.reserve(s.size());
  for (size_t i = 0; i < s.size();) {
    const auto c = static_cast<uint8_t>(s[i]);
    int extra = 0;
    uint32_t cp = c;
    if (c >= 0xF0) {
      extra = 3;
      cp = c & 0x07;
    } else if (c >= 0xE0) {
      extra = 2;
      cp = c & 0x0F;
    } else if (c >= 0xC0) {
      extra = 1;
      cp = c & 0x1F;
    }
    i++;
    for (int k = 0; k < extra && i < s.size(); k++, i++) {
      cp = (cp << 6) | (static_cast<uint8_t>(s[i]) & 0x3F);
    }
    out.push_back(cp);
  }
  return out;
}

void encode(const uint32_t cp, std::string& out) {
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

// Code points to show, and the keys they are compared by.
struct Chars {
  std::vector<uint32_t> shown;
  std::vector<uint32_t> keys;
};

Chars prepare(const std::string& text, const bool ignoreAccents) {
  Chars c;
  for (const uint32_t cp : decode(normalize(text))) {
    if (ignoreAccents && isCombining(cp)) continue;
    c.shown.push_back(cp);
    c.keys.push_back(ignoreAccents ? foldAccent(cp) : cp);
  }
  return c;
}

// Appends `chars` as runs, one per stretch of equal marking. A combining mark
// follows its base letter's marking so the two are drawn together.
void appendRuns(std::vector<ankimarkup::Run>& runs, const std::vector<uint32_t>& chars, std::vector<bool> marks,
                const bool bold) {
  for (size_t i = 1; i < chars.size(); i++) {
    if (isCombining(chars[i])) marks[i] = marks[i - 1];
  }
  for (size_t i = 0; i < chars.size();) {
    ankimarkup::Run run;
    run.bold = bold;
    run.mark = marks[i];
    size_t j = i;
    for (; j < chars.size() && marks[j] == run.mark; j++) {
      if (run.mark && chars[j] == ' ')
        run.text += MIDDLE_DOT;
      else
        encode(chars[j], run.text);
    }
    runs.push_back(std::move(run));
    i = j;
  }
}

void appendNewline(std::vector<ankimarkup::Run>& runs) {
  ankimarkup::Run nl;
  nl.newline = true;
  runs.push_back(nl);
}

// Marks the characters of each side that are not part of a longest common
// subsequence. Returns false (leaving everything marked) when the strings are
// too long for the table or it cannot be allocated.
bool diff(const std::vector<uint32_t>& a, const std::vector<uint32_t>& b, std::vector<bool>& markA,
          std::vector<bool>& markB) {
  markA.assign(a.size(), true);
  markB.assign(b.size(), true);
  if (a.size() > MAX_DIFF_CODEPOINTS || b.size() > MAX_DIFF_CODEPOINTS) return false;
  const size_t n = a.size();
  const size_t m = b.size();
  const size_t w = m + 1;
  // Suffix LCS lengths: len[i][j] = LCS(a[i..], b[j..]). Fits uint8_t at the cap.
  std::unique_ptr<uint8_t[]> len(new (std::nothrow) uint8_t[(n + 1) * w]());
  if (!len) return false;
  for (size_t i = n; i-- > 0;) {
    for (size_t j = m; j-- > 0;) {
      len[i * w + j] = a[i] == b[j] ? static_cast<uint8_t>(len[(i + 1) * w + j + 1] + 1)
                                    : std::max(len[(i + 1) * w + j], len[i * w + j + 1]);
    }
  }
  for (size_t i = 0, j = 0; i < n && j < m;) {
    if (a[i] == b[j]) {
      markA[i++] = false;
      markB[j++] = false;
    } else if (len[(i + 1) * w + j] >= len[i * w + j + 1]) {
      i++;
    } else {
      j++;
    }
  }
  return true;
}

// Text of the {{cN::text::hint}} deletions in `s`, with nested deletions
// revealed. When `ordinal` > 0 only deletions numbered `ordinal` are
// collected into `found`; the returned string is `s` with every deletion
// replaced by its text.
std::string revealClozes(const std::string& s, const int ordinal, std::vector<std::string>* found) {
  std::string out;
  size_t i = 0;
  while (i < s.size()) {
    if (s.compare(i, 3, "{{c") == 0) {
      size_t p = i + 3;
      int number = 0;
      while (p < s.size() && s[p] >= '0' && s[p] <= '9') number = number * 10 + (s[p++] - '0');
      if (p > i + 3 && s.compare(p, 2, "::") == 0) {
        // Find the matching "}}", skipping nested deletions.
        const size_t bodyStart = p + 2;
        int depth = 1;
        size_t q = bodyStart;
        size_t hintAt = std::string::npos;
        while (q < s.size() && depth > 0) {
          if (s.compare(q, 2, "{{") == 0) {
            depth++;
            q += 2;
          } else if (s.compare(q, 2, "}}") == 0) {
            depth--;
            if (depth == 0) break;
            q += 2;
          } else {
            if (depth == 1 && hintAt == std::string::npos && s.compare(q, 2, "::") == 0) hintAt = q;
            q++;
          }
        }
        if (depth == 0) {
          const size_t bodyEnd = hintAt == std::string::npos ? q : hintAt;
          const std::string body = revealClozes(s.substr(bodyStart, bodyEnd - bodyStart), ordinal, found);
          if (found && number == ordinal) found->push_back(body);
          out += body;
          i = q + 2;
          continue;
        }
      }
    }
    out.push_back(s[i++]);
  }
  return out;
}

}  // namespace

Spec parseSpec(const std::string& spec) {
  Spec out;
  std::string rest = spec;
  for (;;) {
    if (rest.compare(0, 6, "cloze:") == 0) {
      out.cloze = true;
      rest.erase(0, 6);
    } else if (rest.compare(0, 3, "nc:") == 0) {
      out.ignoreAccents = true;
      rest.erase(0, 3);
    } else {
      break;
    }
  }
  out.field = rest;
  return out;
}

bool findSpec(const std::string& text, Spec& out) {
  const size_t open = text.find(MARKER_OPEN);
  if (open == std::string::npos) return false;
  const size_t close = text.find("]]", open + MARKER_OPEN_LEN);
  if (close == std::string::npos) return false;
  out = parseSpec(text.substr(open + MARKER_OPEN_LEN, close - open - MARKER_OPEN_LEN));
  return true;
}

std::string stripMarkers(const std::string& text) {
  std::string out;
  size_t from = 0;
  for (size_t open = text.find(MARKER_OPEN); open != std::string::npos; open = text.find(MARKER_OPEN, from)) {
    const size_t close = text.find("]]", open + MARKER_OPEN_LEN);
    if (close == std::string::npos) break;
    out.append(text, from, open - from);
    from = close + 2;
  }
  out.append(text, from, std::string::npos);
  return out;
}

std::string clozeForTyping(const std::string& fieldHtml, const int ordinal) {
  std::vector<std::string> found;
  revealClozes(fieldHtml, ordinal, &found);
  if (found.empty()) return "";
  bool allSame = true;
  for (const std::string& f : found) allSame = allSame && f == found.front();
  if (allSame) return found.front();
  std::string out;
  for (size_t i = 0; i < found.size(); i++) {
    if (i) out += ", ";
    out += found[i];
  }
  return out;
}

std::string expectedFromField(const std::string& fieldHtml, const Spec& spec, const int cardOrd) {
  const std::string source = spec.cloze ? clozeForTyping(fieldHtml, cardOrd + 1) : fieldHtml;
  return normalize(ankihtml::toTextLine(source));
}

std::string normalize(const std::string& text) {
  std::string out;
  out.reserve(text.size());
  bool pendingSpace = false;
  for (size_t i = 0; i < text.size(); i++) {
    const char c = text[i];
    const bool nbsp =
        static_cast<uint8_t>(c) == 0xC2 && i + 1 < text.size() && static_cast<uint8_t>(text[i + 1]) == 0xA0;
    if (c == ' ' || c == '\t' || c == '\n' || c == '\r' || nbsp) {
      if (nbsp) i++;
      pendingSpace = !out.empty();
      continue;
    }
    if (pendingSpace) out.push_back(' ');
    pendingSpace = false;
    out.push_back(c);
  }
  return out;
}

std::vector<ankimarkup::Run> compare(const std::string& typed, const std::string& expected, const bool ignoreAccents) {
  std::vector<ankimarkup::Run> runs;
  // Far past the diff cap: no need to decode (4 bytes per code point) to mark it all.
  if (typed.size() > MAX_COMPARE_BYTES || expected.size() > MAX_COMPARE_BYTES) {
    ankimarkup::Run got;
    got.text = normalize(typed);
    got.mark = true;
    ankimarkup::Run want;
    want.text = normalize(expected);
    want.bold = true;
    want.mark = !got.text.empty();
    if (!got.text.empty()) {
      runs.push_back(std::move(got));
      appendNewline(runs);
    }
    runs.push_back(std::move(want));
    return runs;
  }
  const Chars want = prepare(expected, ignoreAccents);
  const Chars got = prepare(typed, ignoreAccents);
  if (got.keys.empty()) {
    appendRuns(runs, want.shown, std::vector<bool>(want.shown.size(), false), true);
    return runs;
  }
  if (got.keys == want.keys) {
    appendRuns(runs, want.shown, std::vector<bool>(want.shown.size(), false), true);
    return runs;
  }
  std::vector<bool> markGot;
  std::vector<bool> markWant;
  diff(got.keys, want.keys, markGot, markWant);
  appendRuns(runs, got.shown, markGot, false);
  appendNewline(runs);
  appendRuns(runs, want.shown, markWant, true);
  return runs;
}

}  // namespace ankitype
