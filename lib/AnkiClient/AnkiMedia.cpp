#include "AnkiMedia.h"

#include <cstdio>

#include "AnkiPaths.h"

namespace ankimedia {

namespace {

constexpr char MARKER[] = "[sound:";
constexpr size_t MARKER_LEN = sizeof(MARKER) - 1;

// ".mp3" or ".wav" when the name ends in one (any case), else nullptr.
const char* playableExtension(const std::string& name) {
  if (name.size() < 5) return nullptr;
  std::string ext = name.substr(name.size() - 4);
  for (char& c : ext) {
    if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
  }
  if (ext == ".mp3") return ".mp3";
  if (ext == ".wav") return ".wav";
  return nullptr;
}

}  // namespace

bool isPlayable(const std::string& name) { return playableExtension(name) != nullptr; }

std::string path(const uint32_t accountId, const std::string& name) {
  uint64_t hash = 0xcbf29ce484222325ull;  // FNV-1a
  for (const char c : name) {
    hash ^= static_cast<unsigned char>(c);
    hash *= 0x100000001b3ull;
  }
  char file[17];
  snprintf(file, sizeof(file), "%016llx", static_cast<unsigned long long>(hash));
  const char* ext = playableExtension(name);
  return ankipaths::mediaDir(accountId) + "/" + file + (ext ? ext : "");
}

std::string markers(const std::vector<std::string>& names, const std::string& text) {
  std::string out;
  for (const std::string& name : names) {
    if (!isPlayable(name) || name.find(']') != std::string::npos) continue;
    const std::string marker = MARKER + name + "]";
    if (text.find(marker) == std::string::npos && out.find(marker) == std::string::npos) out += marker;
  }
  return out;
}

void soundNames(const std::string& text, std::vector<std::string>& out) {
  for (size_t at = text.find(MARKER); at != std::string::npos; at = text.find(MARKER, at + 1)) {
    const size_t close = text.find(']', at);
    if (close == std::string::npos) return;
    out.push_back(text.substr(at + MARKER_LEN, close - at - MARKER_LEN));
  }
}

std::string urlEncode(const std::string& name) {
  static constexpr char HEX[] = "0123456789ABCDEF";
  std::string out;
  for (const char ch : name) {
    const unsigned char c = static_cast<unsigned char>(ch);
    const bool unreserved = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' ||
                            c == '.' || c == '_' || c == '~';
    if (unreserved) {
      out.push_back(ch);
    } else {
      out.push_back('%');
      out.push_back(HEX[c >> 4]);
      out.push_back(HEX[c & 15]);
    }
  }
  return out;
}

}  // namespace ankimedia
