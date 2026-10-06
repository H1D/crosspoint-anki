#pragma once

#include <string>

// Turns Anki's rendered card HTML (as AnkiConnect's cardsInfo returns it) into
// the same minimal markup AnkiDo produces server-side with render=text:
//   **bold**  _italic_  [cloze]  [img:name.png]  \n
// so the review screen draws both backends' cards through ankimarkup::parse.
// Mirrors AnkiDo's collection/render.py: <style>/<script> bodies, [anki:play]
// and [sound:] markers are dropped, block tags and <br> break lines, cloze
// spans become "[...]" / "[answer]", entities are decoded, whitespace is
// collapsed per line and runs of blank lines are squeezed to one.
namespace ankihtml {

// HTML -> markup text. `images` (optional) receives the number of <img> tags.
std::string toMarkup(const std::string& html, unsigned* images = nullptr);

// HTML -> one plain text line (no markup, lines joined by spaces): a note
// field as Anki compares it against a typed answer.
std::string toTextLine(const std::string& html);

// Anki's answer side repeats the question above <hr id=answer>; returns only
// what follows it (the whole input when the marker is absent).
std::string answerPart(const std::string& answerHtml);

}  // namespace ankihtml
