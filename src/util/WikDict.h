#pragma once

#include <string>
#include <vector>

// Definitions in the WikDict/FreeDict HTML layout (every WikDict language
// pair): per entry, pronunciation and a part-of-speech <div> sit outside the
// sense list; each top-level <li> is a sense whose bare text is a
// source-language gloss and whose leaf <div>s are the translations. A lookup
// may append several entries (noun, verb) into one string. Pure string
// functions, host-testable.
namespace WikDict {

struct Sense {
  std::string gloss;               // source-language explanation, may be empty
  std::vector<std::string> terms;  // translations, as written (stress accents kept)
};

struct Entry {
  std::string partOfSpeech;  // "noun", "possessive pronoun"; may be empty
  std::vector<Sense> senses;
};

// Entries of `html`; empty when it has no part-of-speech marker (<font
// class="grammar">) or no translation (another dictionary layout).
std::vector<Entry> parse(const std::string& html);

// Anki Back field: up to `maxTerms` distinct translations, the first of every
// sense before the rest, senses of several entries taken in turn (noun 1,
// verb 1, noun 2, ...), joined with ", ", stress accents dropped. Empty when
// parse() finds nothing.
std::string shortTranslation(const std::string& html, size_t maxTerms = 6);

// Definition screen: one paragraph per part of speech and per sense,
// "1. <i>gloss</i> — term, term", no pronunciation (the reader fonts have no
// IPA glyphs). Empty when parse() finds nothing.
std::string compactHtml(const std::string& html);

}  // namespace WikDict
