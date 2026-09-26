#include "AnkiJournal.h"

#include <algorithm>

#include "AnkiJson.h"

namespace {
// Parses one journal line. Flat object, so a key/value pair walk is enough.
bool parseLine(const std::string& line, AnkiJournal::Entry& out) {
  std::string key;
  bool ok = false;
  ankijson::Reader::Callbacks cb;
  cb.onKey = [&](const std::string& k) { key = k; };
  cb.onString = [&](const std::string& v) {
    if (key == "client_id") {
      out.clientId = v;
      ok = true;
    }
  };
  cb.onNumber = [&](const std::string& v) {
    const int64_t n = ankijson::toInt64(v);
    if (key == "card_id") out.cardId = n;
    else if (key == "ease") out.ease = static_cast<uint8_t>(n);
    else if (key == "answered_at") out.answeredAt = n;
  };
  ankijson::Reader reader(std::move(cb));
  reader.feed(line);
  return ok && !reader.hasError() && out.cardId != 0;
}
}  // namespace

bool AnkiJournal::append(const Entry& e) {
  std::string line = "{\"client_id\":";
  ankijson::appendQuoted(line, e.clientId);
  line += ",\"card_id\":" + std::to_string(e.cardId);
  line += ",\"ease\":" + std::to_string(static_cast<int>(e.ease));
  if (e.answeredAt > 0) line += ",\"answered_at\":" + std::to_string(e.answeredAt);
  line += "}\n";
  auto f = fs.open(path, AnkiFs::Mode::Append);
  if (!f) return false;
  const bool ok = f->write(line) == line.size();
  f->close();
  return ok;
}

std::vector<AnkiJournal::Entry> AnkiJournal::load() const {
  std::vector<Entry> entries;
  auto f = fs.open(path, AnkiFs::Mode::Read);
  if (!f) return entries;
  AnkiLineReader lines(*f);
  std::string line;
  while (lines.next(line)) {
    if (line.empty()) continue;
    Entry e;
    if (parseLine(line, e)) entries.push_back(std::move(e));
  }
  f->close();
  return entries;
}

bool AnkiJournal::empty() const {
  auto f = fs.open(path, AnkiFs::Mode::Read);
  if (!f) return true;
  AnkiLineReader lines(*f);
  std::string line;
  bool any = false;
  while (lines.next(line)) {
    if (!line.empty()) {
      any = true;
      break;
    }
  }
  f->close();
  return !any;
}

std::vector<int64_t> AnkiJournal::gradedCardIds() const {
  std::vector<int64_t> ids;
  for (const Entry& e : load()) ids.push_back(e.cardId);
  return ids;
}

std::string AnkiJournal::toReviewsJson(const std::vector<Entry>& entries) {
  std::string out = "[";
  bool first = true;
  for (const Entry& e : entries) {
    if (!first) out.push_back(',');
    first = false;
    out += "{\"client_id\":";
    ankijson::appendQuoted(out, e.clientId);
    out += ",\"card_id\":" + std::to_string(e.cardId);
    out += ",\"ease\":" + std::to_string(static_cast<int>(e.ease));
    if (e.answeredAt > 0) out += ",\"answered_at\":" + std::to_string(e.answeredAt);
    out.push_back('}');
  }
  out.push_back(']');
  return out;
}

bool AnkiJournal::removeAcked(const std::vector<std::string>& ackedIds) {
  if (ackedIds.empty()) return true;
  const std::vector<Entry> entries = load();
  std::string remaining;
  size_t kept = 0;
  for (const Entry& e : entries) {
    if (std::find(ackedIds.begin(), ackedIds.end(), e.clientId) != ackedIds.end()) continue;
    kept++;
    remaining += "{\"client_id\":";
    ankijson::appendQuoted(remaining, e.clientId);
    remaining += ",\"card_id\":" + std::to_string(e.cardId);
    remaining += ",\"ease\":" + std::to_string(static_cast<int>(e.ease));
    if (e.answeredAt > 0) remaining += ",\"answered_at\":" + std::to_string(e.answeredAt);
    remaining += "}\n";
  }
  if (kept == 0) return clear();
  return fs.writeAllAtomic(path, remaining);
}
