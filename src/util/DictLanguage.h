#pragma once

#include <string>

// Language tags for picking a dictionary per book. Pure string functions (no
// SD access) so they are host-testable.
namespace DictLanguage {

// Primary subtag of a BCP 47 / EPUB dc:language value, lowercased, with
// common three-letter codes mapped to their two-letter form: "nl-NL" -> "nl",
// "EN_us" -> "en", "nld" / "dut" -> "nl", "" -> "". Longer than 3 letters
// (not a language code) -> "".
std::string primary(const std::string& tag);

// Source (headword) language of a StarDict dictionary, "" when unknown.
// Checked in order: a "lang=" line in the .ifo ("lang=nl" or "lang=nl-ru"),
// a trailing "(xx-yy)" in its bookname (WikDict/FreeDict), then an "xx-yy"
// pair at the end of the folder name ("wikdict-nl-ru", "nl_en").
std::string sourceOf(const std::string& ifoText, const std::string& folderName);

}  // namespace DictLanguage
