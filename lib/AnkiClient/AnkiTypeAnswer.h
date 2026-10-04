#pragma once

#include <string>
#include <vector>

#include "AnkiMarkup.h"

// Anki's "type in the answer" cards. A template's {{type:Field}} reaches the
// device as the literal "[[type:Field]]" (also "[[type:nc:Field]]" and
// "[[type:cloze:Field]]"): on the question side it marks where the answer is
// typed, on the answer side where the comparison goes. These helpers mirror
// Anki desktop's reviewer: which text is expected, and how a typed answer is
// compared against it.
namespace ankitype {

struct Spec {
  std::string field;
  bool cloze = false;          // compare against the card's own cloze deletions
  bool ignoreAccents = false;  // "nc:": diacritics do not count
};

// Parses a marker's spec (the text after "type:"); prefixes in either order.
Spec parseSpec(const std::string& spec);

// The first "[[type:...]]" marker in `text`; false when there is none.
bool findSpec(const std::string& text, Spec& out);

// `text` with every "[[type:...]]" marker removed.
std::string stripMarkers(const std::string& text);

// Longest expected answer kept; longer ones count as unknown (typed text is
// then shown uncompared) so a huge field cannot bloat the cache or the heap.
constexpr size_t MAX_ANSWER_BYTES = 512;

// Anki's extract_cloze_for_typing: the text of every {{cN::...}} deletion
// with N == ordinal (1-based), joined by ", " unless they are all the same.
std::string clozeForTyping(const std::string& fieldHtml, int ordinal);

// The plain text a typed answer is compared against: the field (or, for
// cloze specs, its deletions for card ordinal `cardOrd`, 0-based) as one line.
std::string expectedFromField(const std::string& fieldHtml, const Spec& spec, int cardOrd);

// Whitespace runs (and no-break spaces) collapsed to one space, trimmed.
std::string normalize(const std::string& text);

// Comparison runs for the answer side, after Anki's: the expected text is
// always bold, marked characters are drawn inverted.
//   nothing typed  -> the expected text
//   a match        -> the expected text (exact after normalize(), or ignoring
//                     diacritics when `ignoreAccents`)
//   otherwise      -> the typed text (wrong characters marked) over the
//                     expected text (characters the typing missed marked;
//                     a marked space shows as "·");
//                     Anki's "↓" between them is not in the reader fonts
std::vector<ankimarkup::Run> compare(const std::string& typed, const std::string& expected, bool ignoreAccents);

}  // namespace ankitype
