#include "WikDict.h"

#include <AnkiHtml.h>
#include <AnkiMarkup.h>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <string_view>

namespace WikDict {
namespace {

bool isBlockTag(const std::string& t) {
  return t == "div" || t == "ol" || t == "ul" || t == "li" || t == "p" || t == "table" || t == "tr" || t == "td" ||
         t == "dl" || t == "dt" || t == "dd" || (t.size() == 2 && t[0] == 'h' && t[1] >= '1' && t[1] <= '6');
}

// "[[lemma|form]]" -> "form", "[[lemma]]" -> "lemma": Wiktionary link syntax
// that WikDict leaves in multi-word translations.
std::string unwrapWikiLinks(const std::string& s) {
  std::string out;
  out.reserve(s.size());
  size_t i = 0;
  while (i < s.size()) {
    if (s.compare(i, 2, "[[") == 0) {
      const size_t end = s.find("]]", i + 2);
      if (end != std::string::npos) {
        const std::string inner = s.substr(i + 2, end - i - 2);
        const size_t bar = inner.rfind('|');
        out += bar == std::string::npos ? inner : inner.substr(bar + 1);
        i = end + 2;
        continue;
      }
    }
    out.push_back(s[i++]);
  }
  return out;
}

// Drops combining acute/grave (U+0301/U+0300, UTF-8 CC 81/CC 80), the stress
// marks of Russian dictionaries, so "гото́вый" and "готовый" are one term.
std::string stripStress(const std::string& s) {
  std::string out;
  out.reserve(s.size());
  for (size_t i = 0; i < s.size(); i++) {
    if (static_cast<uint8_t>(s[i]) == 0xCC && i + 1 < s.size() &&
        (static_cast<uint8_t>(s[i + 1]) == 0x81 || static_cast<uint8_t>(s[i + 1]) == 0x80)) {
      i++;
      continue;
    }
    out.push_back(s[i]);
  }
  return out;
}

// Raw HTML text -> plain text: entities decoded, whitespace collapsed, trimmed.
std::string cleanText(const std::string& raw) {
  std::string text = unwrapWikiLinks(ankihtml::toMarkup(raw));
  size_t b = 0;
  size_t e = text.size();
  while (b < e && std::isspace(static_cast<unsigned char>(text[b]))) b++;
  while (e > b && std::isspace(static_cast<unsigned char>(text[e - 1]))) e--;
  return text.substr(b, e - b);
}

// "1. gebouw" -> "gebouw", "1." -> "": Dutch glosses carry Wiktionary's
// own sense numbers, which clash with the view's numbering.
std::string stripNumbering(const std::string& s) {
  size_t i = 0;
  while (i < s.size() && std::isdigit(static_cast<unsigned char>(s[i]))) i++;
  if (i == 0 || i >= s.size() || s[i] != '.') return s;
  i++;
  while (i < s.size() && s[i] == ' ') i++;
  return s.substr(i);
}

// Walks the tag stream once, building entries as it goes. An entry starts at
// its part-of-speech <div> (the one holding <font class="grammar">) or at a
// sense list with none before it. Three sense shapes occur:
//   <ol><li>gloss<div>t</div><div>t</div></li>...</ol>   senses with glosses
//   gloss<ol><li><div>t</div></li>...</ol>               one sense, listed terms
//   gloss<div>t</div>                                    one sense, no list
class Parser {
 public:
  std::vector<Entry> run(const std::string& html) {
    size_t i = 0;
    while (i < html.size()) {
      if (html[i] != '<') {
        const size_t next = html.find('<', i);
        const size_t end = next == std::string::npos ? html.size() : next;
        text(html, i, end);
        i = end;
        continue;
      }
      if (html.compare(i, 4, "<!--") == 0) {
        const size_t end = html.find("-->", i + 4);
        i = end == std::string::npos ? html.size() : end + 3;
        continue;
      }
      const size_t close = html.find('>', i);
      if (close == std::string::npos) break;
      size_t p = i + 1;
      const bool closing = p < close && html[p] == '/';
      if (closing) p++;
      std::string name;
      while (p < close && std::isalnum(static_cast<unsigned char>(html[p]))) {
        name.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(html[p]))));
        p++;
      }
      // <font class="grammar"> marks the part-of-speech <div>.
      if (name == "font" && !closing && !divs.empty() &&
          std::string_view(html).substr(i, close - i).find("grammar") != std::string_view::npos) {
        divs.back().partOfSpeech = true;
        sawPartOfSpeech = true;
      }
      i = close + 1;
      tag(name, closing);
    }
    flushGloss();

    // No <font class="grammar"> anywhere: not WikDict's layout, whatever
    // lists and <div>s it has.
    std::vector<Entry> out;
    if (!sawPartOfSpeech) return out;
    for (size_t e = 0; e < entries.size(); e++) {
      normalize(entries[e], states[e].listGloss);
      // Entries with no translation at all are pronunciation-only noise.
      const bool hasTerms = std::any_of(entries[e].senses.begin(), entries[e].senses.end(),
                                        [](const Sense& s) { return !s.terms.empty(); });
      if (hasTerms) out.push_back(std::move(entries[e]));
    }
    return out;
  }

 private:
  // One open <div>: its text so far, whether it is still a leaf (no block
  // element opened inside it), and the list depth it was opened at.
  struct DivFrame {
    std::string text;
    bool leaf = true;
    bool partOfSpeech = false;
    int liDepth = 0;
  };
  struct EntryState {
    bool listSeen = false;
    std::string listGloss;  // gloss written before the entry's list
  };

  // Text directly in a sense (not in a <div> opened inside it) is gloss;
  // the whole entry sits in one outer <div>, so that one does not count.
  bool inGloss() const { return liDepth > 0 && (divs.empty() || divs.back().liDepth < liDepth); }

  // Text after the part of speech, outside any list: the outer <div> is no
  // longer a leaf then (the pronunciation before it is still in a leaf).
  bool inEntryGloss() const { return liDepth == 0 && !entries.empty() && !divs.empty() && !divs.back().leaf; }

  void text(const std::string& html, const size_t from, const size_t to) {
    if (inGloss()) {
      gloss.append(html, from, to - from);
    } else if (inEntryGloss()) {
      entryGloss.append(html, from, to - from);
    } else if (!divs.empty()) {
      divs.back().text.append(html, from, to - from);
    }
  }

  // Gloss text of the current sense, one piece per <li> (nested gloss lists
  // hold several explanations of one sense).
  void flushGloss() {
    const std::string piece = stripNumbering(cleanText(gloss));
    gloss.clear();
    if (piece.empty() || !currentSense()) return;
    std::string& g = currentSense()->gloss;
    if (!g.empty()) g += "; ";
    g += piece;
  }

  std::string takeEntryGloss() {
    std::string g = stripNumbering(cleanText(entryGloss));
    entryGloss.clear();
    return g;
  }

  Sense* currentSense() {
    if (entries.empty() || entries.back().senses.empty()) return nullptr;
    return &entries.back().senses.back();
  }

  void startEntry(std::string partOfSpeech) {
    entries.emplace_back();
    entries.back().partOfSpeech = std::move(partOfSpeech);
    states.emplace_back();
    entryGloss.clear();
  }

  void tag(const std::string& name, const bool closing) {
    if (name == "br") {
      if (!divs.empty()) divs.back().text.push_back(' ');
      return;
    }
    if (!isBlockTag(name)) return;  // inline tags: keep their text, drop the tag
    if (inGloss()) flushGloss();

    if (closing) {
      if (name == "li" && liDepth > 0) liDepth--;
      if (name == "div" && !divs.empty()) closeDiv();
      return;
    }
    if (!divs.empty()) divs.back().leaf = false;
    if ((name == "ol" || name == "ul") && liDepth == 0) {
      if (entries.empty() || states.back().listSeen) startEntry("");
      states.back().listSeen = true;
      states.back().listGloss = takeEntryGloss();
    } else if (name == "li") {
      if (liDepth == 0 && !entries.empty()) entries.back().senses.emplace_back();
      liDepth++;
    } else if (name == "div") {
      divs.emplace_back();
      divs.back().liDepth = liDepth;
    }
  }

  void closeDiv() {
    const DivFrame frame = std::move(divs.back());
    divs.pop_back();
    if (!frame.leaf) return;
    std::string value = cleanText(frame.text);
    if (value.empty()) return;
    if (frame.partOfSpeech) {
      startEntry(std::move(value));
    } else if (liDepth > 0) {
      if (Sense* s = currentSense()) s->terms.push_back(std::move(value));
    } else if (!entries.empty()) {
      // A translation right under the part of speech: a one-sense entry.
      Entry& e = entries.back();
      if (e.senses.empty()) {
        e.senses.emplace_back();
        e.senses.back().gloss = takeEntryGloss();
      }
      e.senses.back().terms.push_back(std::move(value));
    }
  }

  // A list whose items carry no gloss and one term each is one sense's
  // synonyms, not separate senses.
  static void normalize(Entry& e, const std::string& listGloss) {
    const bool synonymList = e.senses.size() > 1 && std::all_of(e.senses.begin(), e.senses.end(), [](const Sense& s) {
                               return s.gloss.empty() && s.terms.size() <= 1;
                             });
    if (synonymList) {
      Sense merged;
      merged.gloss = listGloss;
      for (Sense& s : e.senses) {
        for (std::string& t : s.terms) merged.terms.push_back(std::move(t));
      }
      e.senses.clear();
      e.senses.push_back(std::move(merged));
    } else if (e.senses.size() == 1 && e.senses[0].gloss.empty()) {
      e.senses[0].gloss = listGloss;
    }
  }

  std::vector<Entry> entries;
  std::vector<EntryState> states;  // parallel to entries
  std::vector<DivFrame> divs;
  std::string gloss;
  std::string entryGloss;
  int liDepth = 0;
  bool sawPartOfSpeech = false;
};

}  // namespace

std::vector<Entry> parse(const std::string& html) {
  std::vector<Entry> entries = Parser().run(html);
  // "possessivePronoun" -> "possessive pronoun".
  for (Entry& e : entries) {
    std::string pos;
    for (const char c : e.partOfSpeech) {
      if (c >= 'A' && c <= 'Z' && !pos.empty()) {
        pos.push_back(' ');
        pos.push_back(static_cast<char>(c - 'A' + 'a'));
      } else {
        pos.push_back(c);
      }
    }
    e.partOfSpeech = std::move(pos);
  }
  return entries;
}

std::string shortTranslation(const std::string& html, const size_t maxTerms) {
  const std::vector<Entry> entries = parse(html);

  // The first term of each sense, senses taken in turn across the entries,
  // then the remaining terms in order, so a polysemous word shows its range
  // before near-synonyms.
  std::vector<std::string> picked;
  const auto pick = [&picked, maxTerms](const std::string& term) {
    if (picked.size() >= maxTerms) return;
    std::string t = stripStress(term);
    if (std::find(picked.begin(), picked.end(), t) == picked.end()) picked.push_back(std::move(t));
  };
  size_t maxSenses = 0;
  for (const Entry& e : entries) maxSenses = std::max(maxSenses, e.senses.size());
  for (size_t rank = 0; rank < maxSenses; rank++) {
    for (const Entry& e : entries) {
      if (rank < e.senses.size() && !e.senses[rank].terms.empty()) pick(e.senses[rank].terms.front());
    }
  }
  for (const Entry& e : entries) {
    for (const Sense& s : e.senses) {
      for (const std::string& t : s.terms) pick(t);
    }
  }

  std::string out;
  for (const std::string& t : picked) {
    if (!out.empty()) out += ", ";
    out += t;
  }
  return out;
}

std::string compactHtml(const std::string& html) {
  const std::vector<Entry> entries = parse(html);
  std::string out;
  for (const Entry& e : entries) {
    if (!e.partOfSpeech.empty()) out += "<p><b>" + ankimarkup::htmlEscape(e.partOfSpeech) + "</b></p>";
    int number = 0;
    for (const Sense& s : e.senses) {
      if (s.terms.empty()) continue;
      out += "<p>";
      if (e.senses.size() > 1) {
        char num[8];
        snprintf(num, sizeof(num), "%d. ", ++number);
        out += num;
      }
      if (!s.gloss.empty()) out += "<i>" + ankimarkup::htmlEscape(s.gloss) + "</i> \xE2\x80\x94 ";
      // Terms that differ only by stress marks are shown once, accented first.
      std::vector<std::string> shown;
      for (const std::string& term : s.terms) {
        std::string key = stripStress(term);
        if (std::find(shown.begin(), shown.end(), key) != shown.end()) continue;
        if (!shown.empty()) out += ", ";
        shown.push_back(std::move(key));
        out += ankimarkup::htmlEscape(term);
      }
      out += "</p>";
    }
  }
  return out;
}

}  // namespace WikDict
