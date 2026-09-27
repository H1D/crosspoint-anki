#include "AnkiCardCache.h"

#include "AnkiJson.h"

std::string AnkiCardCache::toLine(const AnkiCard& c) {
  std::string line = "{\"card_id\":" + std::to_string(c.cardId);
  line += ",\"deck\":";
  ankijson::appendQuoted(line, c.deck);
  line += ",\"kind\":";
  ankijson::appendQuoted(line, c.kind);
  line += ",\"q\":";
  ankijson::appendQuoted(line, c.q);
  line += ",\"a\":";
  ankijson::appendQuoted(line, c.a);
  line += ",\"next\":[";
  for (int i = 0; i < 4; i++) {
    if (i) line.push_back(',');
    ankijson::appendQuoted(line, c.next[i]);
  }
  line += "],\"media\":" + std::to_string(static_cast<int>(c.mediaCount)) + "}\n";
  return line;
}

bool AnkiCardCache::parseLine(const std::string& line, AnkiCard& out) {
  std::string key;
  int nextIndex = -1;
  ankijson::Reader::Callbacks cb;
  cb.onKey = [&](const std::string& k) { key = k; };
  cb.onArrayStart = [&]() {
    if (key == "next") nextIndex = 0;
  };
  cb.onArrayEnd = [&]() { nextIndex = -1; };
  cb.onString = [&](const std::string& v) {
    if (nextIndex >= 0) {
      if (nextIndex < 4) out.next[nextIndex] = v;
      nextIndex++;
      return;
    }
    if (key == "deck")
      out.deck = v;
    else if (key == "kind")
      out.kind = v;
    else if (key == "q")
      out.q = v;
    else if (key == "a")
      out.a = v;
  };
  cb.onNumber = [&](const std::string& v) {
    if (key == "card_id")
      out.cardId = ankijson::toInt64(v);
    else if (key == "media")
      out.mediaCount = static_cast<uint8_t>(ankijson::toInt64(v));
  };
  ankijson::Reader reader(std::move(cb));
  reader.feed(line);
  return !reader.hasError() && out.cardId != 0;
}

int64_t AnkiCardCache::lineCardId(const std::string& line) {
  static constexpr char KEY[] = "\"card_id\":";
  const size_t at = line.find(KEY);
  if (at == std::string::npos) return 0;
  size_t i = at + sizeof(KEY) - 1;
  while (i < line.size() && line[i] == ' ') i++;
  const bool negative = i < line.size() && line[i] == '-';
  if (negative) i++;
  int64_t value = 0;
  bool digits = false;
  for (; i < line.size() && line[i] >= '0' && line[i] <= '9'; i++) {
    value = value * 10 + (line[i] - '0');
    digits = true;
  }
  if (!digits) return 0;
  return negative ? -value : value;
}

std::vector<int64_t> AnkiCardCache::cardIds() {
  std::vector<int64_t> ids;
  auto f = fs.open(cardsPath, AnkiFs::Mode::Read);
  if (!f) return ids;
  ids.reserve(count());
  AnkiLineReader lines(*f);
  std::string line;
  // toLine() writes card_id first; a generous cap still covers hand-edited lines.
  while (lines.next(line, 512)) {
    if (line.empty()) continue;  // same skip rule as load()
    ids.push_back(lineCardId(line));
  }
  f->close();
  return ids;
}

size_t AnkiCardCache::count() {
  auto f = fs.open(cardsPath, AnkiFs::Mode::Read);
  if (!f) return 0;
  AnkiLineReader lines(*f);
  std::string line;
  size_t n = 0;
  while (lines.next(line, 64)) {  // only the length matters here; keep it cheap
    if (!line.empty()) n++;
  }
  f->close();
  return n;
}

bool AnkiCardCache::load(size_t index, AnkiCard& out) {
  auto f = fs.open(cardsPath, AnkiFs::Mode::Read);
  if (!f) return false;
  AnkiLineReader lines(*f);
  std::string line;
  size_t n = 0;
  bool found = false;
  while (lines.next(line)) {
    if (line.empty()) continue;
    if (n++ == index) {
      found = parseLine(line, out);
      break;
    }
  }
  f->close();
  return found;
}

bool AnkiCardCache::readMeta(Meta& out) {
  std::string text;
  if (!fs.readAll(metaPath, text)) return false;
  std::string key;
  ankijson::Reader::Callbacks cb;
  cb.onKey = [&](const std::string& k) { key = k; };
  cb.onNumber = [&](const std::string& v) {
    const int64_t n = ankijson::toInt64(v);
    if (key == "new")
      out.counts.newCount = static_cast<uint16_t>(n);
    else if (key == "learning")
      out.counts.learning = static_cast<uint16_t>(n);
    else if (key == "due")
      out.counts.due = static_cast<uint16_t>(n);
    else if (key == "returned")
      out.counts.returned = static_cast<uint16_t>(n);
    else if (key == "fetched_at")
      out.fetchedAt = n;
  };
  cb.onString = [&](const std::string& v) {
    if (key == "sync_error") out.syncError = v;
  };
  ankijson::Reader reader(std::move(cb));
  reader.feed(text);
  return !reader.hasError();
}

bool AnkiCardCache::writeMeta(const Meta& m) {
  std::string text = "{\"new\":" + std::to_string(m.counts.newCount) +
                     ",\"learning\":" + std::to_string(m.counts.learning) + ",\"due\":" + std::to_string(m.counts.due) +
                     ",\"returned\":" + std::to_string(m.counts.returned) +
                     ",\"fetched_at\":" + std::to_string(m.fetchedAt) + ",\"sync_error\":";
  ankijson::appendQuoted(text, m.syncError);
  text += "}";
  return fs.writeAllAtomic(metaPath, text);
}

bool AnkiCardCache::clear() {
  bool ok = true;
  if (fs.exists(cardsPath)) ok = fs.remove(cardsPath) && ok;
  if (fs.exists(metaPath)) ok = fs.remove(metaPath) && ok;
  return ok;
}
