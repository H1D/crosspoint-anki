#include "DictStemmer.h"

#include <algorithm>
#include <cstring>
#include <iterator>

namespace DictStemmer {
namespace {

struct Irregular {
  const char* form;
  const char* lemma;
};

// Dutch irregular and strong verb forms that the suffix rules below cannot
// map back: present forms of the irregular verbs, past tenses with a vowel
// change, and past participles whose ge- stem is not the infinitive.
// Participles like gelopen / gevallen are left to the rules (strip ge-).
constexpr Irregular DUTCH_IRREGULAR[] = {
    {"ben", "zijn"},
    {"bent", "zijn"},
    {"is", "zijn"},
    {"was", "zijn"},
    {"waren", "zijn"},
    {"geweest", "zijn"},
    {"heb", "hebben"},
    {"hebt", "hebben"},
    {"heeft", "hebben"},
    {"had", "hebben"},
    {"hadden", "hebben"},
    {"gehad", "hebben"},
    {"kan", "kunnen"},
    {"kun", "kunnen"},
    {"kunt", "kunnen"},
    {"kon", "kunnen"},
    {"konden", "kunnen"},
    {"gekund", "kunnen"},
    {"wou", "willen"},
    {"wouden", "willen"},
    {"zal", "zullen"},
    {"zul", "zullen"},
    {"zult", "zullen"},
    {"zou", "zullen"},
    {"zouden", "zullen"},
    {"mag", "mogen"},
    {"mocht", "mogen"},
    {"mochten", "mogen"},
    {"moest", "moeten"},
    {"moesten", "moeten"},
    {"ga", "gaan"},
    {"gaat", "gaan"},
    {"ging", "gaan"},
    {"gingen", "gaan"},
    {"sta", "staan"},
    {"staat", "staan"},
    {"stond", "staan"},
    {"stonden", "staan"},
    {"doe", "doen"},
    {"doet", "doen"},
    {"deed", "doen"},
    {"deden", "doen"},
    {"gedaan", "doen"},
    {"zie", "zien"},
    {"ziet", "zien"},
    {"zag", "zien"},
    {"zagen", "zien"},
    {"sla", "slaan"},
    {"slaat", "slaan"},
    {"sloeg", "slaan"},
    {"sloegen", "slaan"},
    {"geslagen", "slaan"},
    {"kwam", "komen"},
    {"kwamen", "komen"},
    {"liep", "lopen"},
    {"liepen", "lopen"},
    {"riep", "roepen"},
    {"riepen", "roepen"},
    {"sliep", "slapen"},
    {"sliepen", "slapen"},
    {"liet", "laten"},
    {"lieten", "laten"},
    {"viel", "vallen"},
    {"vielen", "vallen"},
    {"hield", "houden"},
    {"hielden", "houden"},
    {"hing", "hangen"},
    {"hingen", "hangen"},
    {"ving", "vangen"},
    {"vingen", "vangen"},
    {"kreeg", "krijgen"},
    {"kregen", "krijgen"},
    {"gekregen", "krijgen"},
    {"bleef", "blijven"},
    {"bleven", "blijven"},
    {"gebleven", "blijven"},
    {"schreef", "schrijven"},
    {"schreven", "schrijven"},
    {"geschreven", "schrijven"},
    {"keek", "kijken"},
    {"keken", "kijken"},
    {"gekeken", "kijken"},
    {"leek", "lijken"},
    {"leken", "lijken"},
    {"geleken", "lijken"},
    {"bleek", "blijken"},
    {"bleken", "blijken"},
    {"gebleken", "blijken"},
    {"reed", "rijden"},
    {"reden", "rijden"},
    {"gereden", "rijden"},
    {"gleed", "glijden"},
    {"gleden", "glijden"},
    {"gegleden", "glijden"},
    {"sneed", "snijden"},
    {"sneden", "snijden"},
    {"gesneden", "snijden"},
    {"beet", "bijten"},
    {"beten", "bijten"},
    {"gebeten", "bijten"},
    {"greep", "grijpen"},
    {"grepen", "grijpen"},
    {"gegrepen", "grijpen"},
    {"begreep", "begrijpen"},
    {"begrepen", "begrijpen"},
    {"steeg", "stijgen"},
    {"stegen", "stijgen"},
    {"gestegen", "stijgen"},
    {"zweeg", "zwijgen"},
    {"zwegen", "zwijgen"},
    {"gezwegen", "zwijgen"},
    {"wees", "wijzen"},
    {"wezen", "wijzen"},
    {"gewezen", "wijzen"},
    {"bewees", "bewijzen"},
    {"bewezen", "bewijzen"},
    {"verdween", "verdwijnen"},
    {"verdwenen", "verdwijnen"},
    {"scheen", "schijnen"},
    {"schenen", "schijnen"},
    {"geschenen", "schijnen"},
    {"bood", "bieden"},
    {"boden", "bieden"},
    {"geboden", "bieden"},
    {"verbood", "verbieden"},
    {"verboden", "verbieden"},
    {"vloog", "vliegen"},
    {"vlogen", "vliegen"},
    {"gevlogen", "vliegen"},
    {"loog", "liegen"},
    {"logen", "liegen"},
    {"gelogen", "liegen"},
    {"koos", "kiezen"},
    {"kozen", "kiezen"},
    {"gekozen", "kiezen"},
    {"verloor", "verliezen"},
    {"verloren", "verliezen"},
    {"vroor", "vriezen"},
    {"vroren", "vriezen"},
    {"gevroren", "vriezen"},
    {"schoot", "schieten"},
    {"schoten", "schieten"},
    {"geschoten", "schieten"},
    {"genoot", "genieten"},
    {"genoten", "genieten"},
    {"goot", "gieten"},
    {"goten", "gieten"},
    {"gegoten", "gieten"},
    {"sloot", "sluiten"},
    {"sloten", "sluiten"},
    {"gesloten", "sluiten"},
    {"floot", "fluiten"},
    {"floten", "fluiten"},
    {"gefloten", "fluiten"},
    {"rook", "ruiken"},
    {"roken", "ruiken"},
    {"geroken", "ruiken"},
    {"boog", "buigen"},
    {"bogen", "buigen"},
    {"gebogen", "buigen"},
    {"schoof", "schuiven"},
    {"schoven", "schuiven"},
    {"geschoven", "schuiven"},
    {"kroop", "kruipen"},
    {"kropen", "kruipen"},
    {"gekropen", "kruipen"},
    {"sloop", "sluipen"},
    {"slopen", "sluipen"},
    {"geslopen", "sluipen"},
    {"dook", "duiken"},
    {"doken", "duiken"},
    {"gedoken", "duiken"},
    {"dronk", "drinken"},
    {"dronken", "drinken"},
    {"gedronken", "drinken"},
    {"zong", "zingen"},
    {"zongen", "zingen"},
    {"gezongen", "zingen"},
    {"sprong", "springen"},
    {"sprongen", "springen"},
    {"gesprongen", "springen"},
    {"dwong", "dwingen"},
    {"dwongen", "dwingen"},
    {"gedwongen", "dwingen"},
    {"klonk", "klinken"},
    {"klonken", "klinken"},
    {"geklonken", "klinken"},
    {"schonk", "schenken"},
    {"schonken", "schenken"},
    {"geschonken", "schenken"},
    {"stonk", "stinken"},
    {"stonken", "stinken"},
    {"gestonken", "stinken"},
    {"zonk", "zinken"},
    {"zonken", "zinken"},
    {"gezonken", "zinken"},
    {"begon", "beginnen"},
    {"begonnen", "beginnen"},
    {"vond", "vinden"},
    {"vonden", "vinden"},
    {"gevonden", "vinden"},
    {"bond", "binden"},
    {"bonden", "binden"},
    {"gebonden", "binden"},
    {"won", "winnen"},
    {"wonnen", "winnen"},
    {"gewonnen", "winnen"},
    {"zwom", "zwemmen"},
    {"zwommen", "zwemmen"},
    {"gezwommen", "zwemmen"},
    {"klom", "klimmen"},
    {"klommen", "klimmen"},
    {"geklommen", "klimmen"},
    {"dacht", "denken"},
    {"dachten", "denken"},
    {"gedacht", "denken"},
    {"bracht", "brengen"},
    {"brachten", "brengen"},
    {"gebracht", "brengen"},
    {"kocht", "kopen"},
    {"kochten", "kopen"},
    {"gekocht", "kopen"},
    {"zocht", "zoeken"},
    {"zochten", "zoeken"},
    {"gezocht", "zoeken"},
    {"vocht", "vechten"},
    {"vochten", "vechten"},
    {"gevochten", "vechten"},
    {"vroeg", "vragen"},
    {"vroegen", "vragen"},
    {"droeg", "dragen"},
    {"droegen", "dragen"},
    {"voer", "varen"},
    {"voeren", "varen"},
    {"at", "eten"},
    {"aten", "eten"},
    {"gegeten", "eten"},
    {"vergat", "vergeten"},
    {"vergaten", "vergeten"},
    {"las", "lezen"},
    {"lazen", "lezen"},
    {"gaf", "geven"},
    {"gaven", "geven"},
    {"zat", "zitten"},
    {"zaten", "zitten"},
    {"gezeten", "zitten"},
    {"lag", "liggen"},
    {"lagen", "liggen"},
    {"gelegen", "liggen"},
    {"sprak", "spreken"},
    {"spraken", "spreken"},
    {"gesproken", "spreken"},
    {"brak", "breken"},
    {"braken", "breken"},
    {"gebroken", "breken"},
    {"nam", "nemen"},
    {"namen", "nemen"},
    {"genomen", "nemen"},
    {"hielp", "helpen"},
    {"hielpen", "helpen"},
    {"geholpen", "helpen"},
    {"stierf", "sterven"},
    {"stierven", "sterven"},
    {"gestorven", "sterven"},
    {"wierp", "werpen"},
    {"wierpen", "werpen"},
    {"geworpen", "werpen"},
    {"werd", "worden"},
    {"werden", "worden"},
    {"wist", "weten"},
    {"wisten", "weten"},
    {"zei", "zeggen"},
    {"zeiden", "zeggen"},
    {"trok", "trekken"},
    {"trokken", "trekken"},
    {"getrokken", "trekken"},
    {"schrok", "schrikken"},
    {"schrokken", "schrikken"},
    {"geschrokken", "schrikken"},
    {"woog", "wegen"},
    {"wogen", "wegen"},
    {"gewogen", "wegen"},
    {"bewoog", "bewegen"},
    {"bewogen", "bewegen"},
    {"blies", "blazen"},
    {"bliezen", "blazen"},
};

bool isVowel(const char c) { return c == 'a' || c == 'e' || c == 'i' || c == 'o' || c == 'u'; }

bool isConsonant(const char c) { return c >= 'a' && c <= 'z' && !isVowel(c); }

bool endsWith(const std::string& s, const char* suffix) {
  const size_t len = strlen(suffix);
  return s.size() > len && s.compare(s.size() - len, len, suffix) == 0;
}

std::string dropSuffix(const std::string& s, const size_t len) { return s.substr(0, s.size() - len); }

// Longest first, so the first match is the whole ending.
constexpr const char* DIMINUTIVES[] = {"etjes", "etje", "tjes", "tje", "pjes", "pje", "jes", "je"};
constexpr const char* WEAK_PAST[] = {"ten", "den", "te", "de"};

// The first of `suffixes` that `s` ends with, nullptr for none.
template <size_t N>
const char* firstSuffix(const std::string& s, const char* const (&suffixes)[N]) {
  const auto* it =
      std::find_if(std::begin(suffixes), std::end(suffixes), [&s](const char* suffix) { return endsWith(s, suffix); });
  return it != std::end(suffixes) ? *it : nullptr;
}

// "katt" -> "kat": a doubled final consonant after a short vowel.
std::string undouble(const std::string& s) {
  const size_t n = s.size();
  if (n >= 3 && s[n - 1] == s[n - 2] && isConsonant(s[n - 1])) return s.substr(0, n - 1);
  return s;
}

// "huiz" -> "huis", "briev" -> "brief": z/v are only written before a vowel.
std::string devoice(const std::string& s) {
  if (s.empty()) return s;
  std::string out = s;
  if (out.back() == 'z') out.back() = 's';
  if (out.back() == 'v') out.back() = 'f';
  return out;
}

// "lees" -> "lez", "geef" -> "gev": the voiced consonant before -en.
std::string voice(const std::string& s) {
  if (s.empty()) return s;
  std::string out = s;
  if (out.back() == 's') out.back() = 'z';
  if (out.back() == 'f') out.back() = 'v';
  return out;
}

// "bom" -> "boom": a single a/e/o/u before one final consonant is long in an
// open syllable (bo-men), so the closed singular writes it twice.
std::string lengthen(const std::string& s) {
  const size_t n = s.size();
  if (n < 2 || !isConsonant(s[n - 1])) return s;
  const char v = s[n - 2];
  if (v != 'a' && v != 'e' && v != 'o' && v != 'u') return s;
  if (n >= 3 && !isConsonant(s[n - 3])) return s;
  std::string out = s;
  out.insert(out.begin() + static_cast<std::ptrdiff_t>(n - 2), v);
  return out;
}

// "loop" -> "lop": the reverse of lengthen, for building the infinitive.
std::string shorten(const std::string& s) {
  const size_t n = s.size();
  if (n < 3 || !isConsonant(s[n - 1])) return s;
  const char v = s[n - 2];
  if (s[n - 3] != v || (v != 'a' && v != 'e' && v != 'o' && v != 'u')) return s;
  if (n >= 4 && isVowel(s[n - 4])) return s;  // "leeuw"-like clusters stay
  return s.substr(0, n - 3) + s.substr(n - 2);
}

// "zit" -> "zitt": a short vowel keeps its consonant doubled before -en.
std::string doubleFinal(const std::string& s) {
  const size_t n = s.size();
  if (n < 2 || !isConsonant(s[n - 1]) || !isVowel(s[n - 2])) return s;
  if (n >= 3 && isVowel(s[n - 3])) return s;
  return s + s[n - 1];
}

class Candidates {
 public:
  Candidates(const std::string& word, std::vector<std::string>& out) : word(word), out(out) {}
  void add(const std::string& v) {
    if (v.size() < 2 || v == word) return;
    if (std::find(out.begin(), out.end(), v) == out.end()) out.push_back(v);
  }

 private:
  const std::string& word;
  std::vector<std::string>& out;
};

// Singular / base form of a noun or adjective whose ending was cut off,
// Dutch spelling restored: long vowel first (open syllable), then a doubled
// consonant, then z/v, then the bare cut.
void addNounBase(Candidates& c, const std::string& base) {
  c.add(lengthen(devoice(base)));
  c.add(lengthen(base));
  c.add(undouble(base));
  c.add(devoice(base));
  c.add(base);
}

// Infinitive for a verb stem (the "ik" form): loop -> lopen, lees -> lezen,
// zeg -> zeggen, werk -> werken. A long vowel written twice is single in the
// open syllable of the infinitive; a short vowel before one consonant doubles
// it (zeg -> zeggen, not zegen, which is another word).
void addInfinitive(Candidates& c, const std::string& stem) {
  // Vowel-final stems (ga, doe, zie) are all irregular and in the table;
  // "-en" on anything else vowel-final makes other words (de -> deen).
  if (stem.size() < 2 || !isConsonant(stem.back())) return;
  const std::string shortened = shorten(stem);
  if (shortened != stem) {
    c.add(voice(shortened) + "en");
    c.add(shortened + "en");
  } else {
    const std::string doubled = doubleFinal(stem);
    if (doubled != stem) c.add(doubled + "en");
  }
  c.add(voice(stem) + "en");
  c.add(stem + "en");
}

void dutchVariants(const std::string& w, Candidates& c) {
  // Plural / verb forms in -en: bomen, huizen, katten; lopend -> lopen.
  if (endsWith(w, "end")) c.add(dropSuffix(w, 1));
  if (endsWith(w, "en")) addNounBase(c, dropSuffix(w, 2));
  // Plural -s / -'s: tafels, auto's.
  if (endsWith(w, "'s")) c.add(dropSuffix(w, 2));
  if (endsWith(w, "\xE2\x80\x99s")) c.add(dropSuffix(w, 4));
  if (endsWith(w, "s")) c.add(dropSuffix(w, 1));
  // Adjective -e, comparative -er, superlative -st(e): grote, groter, grootste.
  if (endsWith(w, "ste")) addNounBase(c, dropSuffix(w, 3));
  if (endsWith(w, "st")) addNounBase(c, dropSuffix(w, 2));
  if (endsWith(w, "er")) addNounBase(c, dropSuffix(w, 2));
  if (endsWith(w, "e")) addNounBase(c, dropSuffix(w, 1));
  // Diminutives: huisje, boompje, balletje, mannetjes.
  if (const char* dim = firstSuffix(w, DIMINUTIVES)) {
    const std::string base = dropSuffix(w, strlen(dim));
    c.add(undouble(base));
    c.add(base);
  }
  // Verbs. Past participle ge-...-t/-d (gewerkt) and strong ge-...-en (gelopen).
  if (w.size() > 4 && w.compare(0, 2, "ge") == 0) {
    const std::string rest = w.substr(2);
    if (endsWith(rest, "en")) c.add(rest);
    if (endsWith(rest, "t") || endsWith(rest, "d")) addInfinitive(c, dropSuffix(rest, 1));
  }
  // Weak past: werkte(n), woonde(n).
  if (const char* past = firstSuffix(w, WEAK_PAST)) addInfinitive(c, dropSuffix(w, strlen(past)));
  // Present: the stem itself (ik loop, zit) before stem + t (hij loopt).
  addInfinitive(c, w);
  if (endsWith(w, "t")) addInfinitive(c, dropSuffix(w, 1));
}

void englishVariants(const std::string& word, Candidates& c) {
  const size_t n = word.size();
  if (endsWith(word, "'s")) c.add(word.substr(0, n - 2));
  if (endsWith(word, "\xE2\x80\x99s")) c.add(word.substr(0, n - 4));  // U+2019 apostrophe
  if (endsWith(word, "ies")) c.add(word.substr(0, n - 3) + "y");      // stories -> story
  if (endsWith(word, "es")) c.add(word.substr(0, n - 2));             // boxes -> box
  if (endsWith(word, "s")) c.add(word.substr(0, n - 1));              // dogs -> dog
  if (endsWith(word, "ed")) {
    c.add(word.substr(0, n - 2));                                            // walked -> walk
    c.add(word.substr(0, n - 1));                                            // loved -> love
    if (n >= 4 && word[n - 3] == word[n - 4]) c.add(word.substr(0, n - 3));  // stopped -> stop
  }
  if (endsWith(word, "ing")) {
    c.add(word.substr(0, n - 3));                                            // walking -> walk
    c.add(word.substr(0, n - 3) + "e");                                      // making -> make
    if (n >= 5 && word[n - 4] == word[n - 5]) c.add(word.substr(0, n - 4));  // running -> run
  }
}

}  // namespace

const char* irregularLemma(const std::string& word, const std::string& lang) {
  if (lang != "nl") return nullptr;
  const auto* entry = std::find_if(std::begin(DUTCH_IRREGULAR), std::end(DUTCH_IRREGULAR),
                                   [&word](const Irregular& e) { return word == e.form; });
  return entry != std::end(DUTCH_IRREGULAR) ? entry->lemma : nullptr;
}

void variants(const std::string& word, const std::string& lang, std::vector<std::string>& out) {
  out.clear();
  out.reserve(lang == "nl" ? 24 : 6);
  Candidates c(word, out);
  if (lang == "nl") {
    dutchVariants(word, c);
  } else {
    englishVariants(word, c);
  }
}

}  // namespace DictStemmer
