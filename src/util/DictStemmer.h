#pragma once

#include <string>
#include <vector>

// Dictionary-form candidates for an inflected word, per source language of
// the dictionary. Pure string functions (no SD access), host-testable.
namespace DictStemmer {

// Lemma of a Dutch irregular verb form ("liep" -> "lopen", "was" -> "zijn"),
// nullptr when `word` (lowercase) is not in the built-in table or `lang` is
// not "nl". Tried before the exact match (in running text "was" and "lag"
// are far more often verbs); the lookup appends the form's own entry.
const char* irregularLemma(const std::string& word, const std::string& lang);

// Candidate dictionary forms of `word` (lowercase), most likely first, the
// word itself excluded. "nl": plural, diminutive, adjective, comparative and
// regular verb endings mapped back to the singular / base / infinitive with
// Dutch spelling rules (bomen -> boom, huizen -> huis, loopt -> lopen,
// gewerkt -> werken). Any other language, "" included: English possessive,
// plural and -ed/-ing endings (the rules predating per-language stemming).
void variants(const std::string& word, const std::string& lang, std::vector<std::string>& out);

}  // namespace DictStemmer
