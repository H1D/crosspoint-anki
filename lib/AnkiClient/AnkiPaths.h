#pragma once

#include <cstdint>
#include <string>

// SD card layout for the Anki feature. Everything lives under one directory
// so a user can wipe it in one go and so an account's data is one folder.
//
//   /.crosspoint/anki.json                account store (src/anki/AnkiAccountStore)
//   /.crosspoint/anki/state.json          device-wide state: review counter, device id
//   /.crosspoint/anki/<id>/cards.jsonl    cached queue, one card per line
//   /.crosspoint/anki/<id>/cache.json     counts + fetch time for the cache
//   /.crosspoint/anki/<id>/reviews.jsonl  pending reviews (journal)
//   /.crosspoint/anki/<id>/notes.jsonl    pending add-word notes
//   /.crosspoint/anki/<id>/decks.json     deck list from the last sync
namespace ankipaths {

inline const char* root() { return "/.crosspoint/anki"; }
inline std::string stateFile() { return std::string(root()) + "/state.json"; }
inline std::string accountDir(uint32_t id) { return std::string(root()) + "/" + std::to_string(id); }
inline std::string cardsFile(uint32_t id) { return accountDir(id) + "/cards.jsonl"; }
inline std::string cacheMetaFile(uint32_t id) { return accountDir(id) + "/cache.json"; }
inline std::string reviewsFile(uint32_t id) { return accountDir(id) + "/reviews.jsonl"; }
inline std::string notesFile(uint32_t id) { return accountDir(id) + "/notes.jsonl"; }
inline std::string decksFile(uint32_t id) { return accountDir(id) + "/decks.json"; }

}  // namespace ankipaths
