#pragma once

#include <string>
#include <vector>

// One StarDict dictionary found under /dictionaries or /.dictionaries: a
// subfolder holding <stem>.idx plus <stem>.dict or <stem>.dict.dz.
struct DictionaryEntry {
  std::string name;  // subfolder name (shown to the user, stored in settings)
  std::string stem;  // index basename without .idx
};

namespace DictionaryRegistry {

// Scan /dictionaries/*/ and /.dictionaries/*/ for dictionaries. Folders with
// multiple index stems are ambiguous and skipped. Result is sorted
// case-insensitively by name.
void discover(std::vector<DictionaryEntry>& out);

// Resolve a folder name to its extensionless base path
// ("/dictionaries/<folder>/<stem>" or "/.dictionaries/<folder>/<stem>").
// Returns false if the folder holds no usable dictionary in either root.
bool resolveBasePath(const char* folderName, std::string& basePathOut);

// Head of "<basePath>.ifo" (the first 2KB, which holds every key the reader
// uses), "" when it is missing or unreadable.
std::string readIfo(const std::string& basePath);

// Dictionary folder for a book whose EPUB language is `bookLanguage`:
// `selected` (the Settings choice) when it matches the book's language, the
// book has none, or no other dictionary matches; otherwise the first
// dictionary (by name) whose source language matches. "" when `selected` is
// empty: "None" turns lookups off for every book.
std::string pickForBook(const std::string& bookLanguage, const char* selected);

}  // namespace DictionaryRegistry
