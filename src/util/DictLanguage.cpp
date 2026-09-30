#include "DictLanguage.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <iterator>

namespace DictLanguage {
namespace {

bool isAlpha(const char c) { return std::isalpha(static_cast<unsigned char>(c)) != 0; }

char lower(const char c) { return static_cast<char>(std::tolower(static_cast<unsigned char>(c))); }

// "xx-yy" / "xxx_yy" pair ending at s[end) (end exclusive): returns the first
// code, or "" when the text before `end` is not <2-3 letters><-_><2-3 letters>
// preceded by the start or a non-letter.
std::string pairEndingAt(const std::string& s, const size_t end) {
  size_t i = end;
  while (i > 0 && isAlpha(s[i - 1])) i--;
  const size_t secondLen = end - i;
  if (secondLen < 2 || secondLen > 3 || i == 0 || (s[i - 1] != '-' && s[i - 1] != '_')) return "";
  const size_t sep = i - 1;
  size_t j = sep;
  while (j > 0 && isAlpha(s[j - 1])) j--;
  const size_t firstLen = sep - j;
  if (firstLen < 2 || firstLen > 3) return "";
  return primary(s.substr(j, firstLen));
}

// Value of `key=` at the start of a line, up to the end of that line.
std::string ifoValue(const std::string& ifo, const char* key) {
  const size_t keyLen = strlen(key);
  size_t pos = 0;
  while (pos < ifo.size()) {
    const size_t eol = ifo.find('\n', pos);
    const size_t lineEnd = eol == std::string::npos ? ifo.size() : eol;
    if (ifo.compare(pos, keyLen, key) == 0 && pos + keyLen < lineEnd && ifo[pos + keyLen] == '=') {
      size_t valueEnd = lineEnd;
      while (valueEnd > pos + keyLen + 1 && (ifo[valueEnd - 1] == '\r' || ifo[valueEnd - 1] == ' ')) valueEnd--;
      return ifo.substr(pos + keyLen + 1, valueEnd - pos - keyLen - 1);
    }
    if (eol == std::string::npos) break;
    pos = eol + 1;
  }
  return "";
}

struct Iso3 {
  const char* three;
  const char* two;
};

// ISO 639-2 codes (bibliographic and terminology forms) that FreeDict names
// and some EPUBs use, for the languages with a two-letter code.
constexpr Iso3 ISO3_TO_2[] = {
    {"ara", "ar"}, {"ces", "cs"}, {"cze", "cs"}, {"dan", "da"}, {"deu", "de"}, {"ger", "de"}, {"ell", "el"},
    {"gre", "el"}, {"eng", "en"}, {"spa", "es"}, {"fin", "fi"}, {"fra", "fr"}, {"fre", "fr"}, {"heb", "he"},
    {"hun", "hu"}, {"ita", "it"}, {"jpn", "ja"}, {"kor", "ko"}, {"nld", "nl"}, {"dut", "nl"}, {"nor", "no"},
    {"nob", "nb"}, {"pol", "pl"}, {"por", "pt"}, {"ron", "ro"}, {"rum", "ro"}, {"rus", "ru"}, {"swe", "sv"},
    {"tur", "tr"}, {"ukr", "uk"}, {"zho", "zh"}, {"chi", "zh"},
};

}  // namespace

std::string primary(const std::string& tag) {
  std::string out;
  for (const char c : tag) {
    if (c == '-' || c == '_') break;
    if (!isAlpha(c)) return "";
    out.push_back(lower(c));
  }
  if (out.size() < 2 || out.size() > 3) return "";
  const auto* code =
      std::find_if(std::begin(ISO3_TO_2), std::end(ISO3_TO_2), [&out](const Iso3& c) { return out == c.three; });
  return code != std::end(ISO3_TO_2) ? code->two : out;
}

std::string sourceOf(const std::string& ifoText, const std::string& folderName) {
  std::string lang = primary(ifoValue(ifoText, "lang"));
  if (!lang.empty()) return lang;

  const std::string bookname = ifoValue(ifoText, "bookname");
  const size_t close = bookname.rfind(')');
  if (close != std::string::npos && bookname.find_first_not_of(' ', close + 1) == std::string::npos) {
    lang = pairEndingAt(bookname, close);
    if (!lang.empty()) return lang;
  }

  return pairEndingAt(folderName, folderName.size());
}

}  // namespace DictLanguage
