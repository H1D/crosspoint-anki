#pragma once

#include <string>
#include <vector>

// Helpers for AnkiDo's compact text markup (render=text):
//   **bold**  _italic_  [cloze]  [img:name.png]  \n
// plus Anki's own "[[type:Field]]" type-in-the-answer marker, and for the odd
// characters AnkiDo passes through from Anki.
namespace ankimarkup {

struct Run {
  std::string text;
  bool bold = false;
  bool italic = false;
  bool cloze = false;    // draw boxed
  bool newline = false;  // a line break; text is empty
  bool typeIn = false;   // "[[type:...]]" marker; text holds the spec after "type:"
  bool mark = false;     // draw inverted (a type-in comparison error)
};

// Removes Unicode bidi isolate/embedding controls (U+2066..U+2069,
// U+202A..U+202E) that Anki's "next" strings carry and the fonts cannot draw.
std::string stripBidiControls(const std::string& s);

// Splits card text into styled runs. Image markers become "[image]" runs;
// unknown/unbalanced markers are kept as literal text.
// "[[type:spec]]" becomes one typeIn run (never a cloze box).
std::vector<Run> parse(const std::string& text);

// Plain text with all markup removed (for measurements/logging).
std::string toPlain(const std::string& text);

// Escapes text for use inside an HTML note field.
std::string htmlEscape(const std::string& s);

}  // namespace ankimarkup
