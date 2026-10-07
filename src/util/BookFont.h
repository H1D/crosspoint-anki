#pragma once
#include <string>

// A book's own reader font: the font (family and size) picked while the book
// is open, kept as font.json in the book's cache folder. Books without one
// follow the default font from Settings.
namespace BookFont {

// Opens the book's font scope: SETTINGS switches to the book's font, or stays
// on the default, until end().
void begin(const std::string& cachePath);

// Puts the default font back. Returns true when the reader font changed.
bool end();

// Stores the current reader font as the open book's font. A font equal to the
// default is forgotten instead, so picking the default puts the book back on it.
// No-op outside a book.
void remember();

}  // namespace BookFont
