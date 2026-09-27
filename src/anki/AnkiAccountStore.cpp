#include "AnkiAccountStore.h"

#include <AnkiPaths.h>
#include <HalStorage.h>
#include <Logging.h>
#include <ObfuscationUtils.h>

#include <algorithm>

namespace {
constexpr size_t MAX_TOKEN_LENGTH = 200;

std::string trimSlash(std::string url) {
  while (!url.empty() && url.back() == '/') url.pop_back();
  return url;
}
}  // namespace

void AnkiAccountStore::toJson(JsonDocument& doc) const {
  doc["nextId"] = nextId;
  doc["reviewAccount"] = reviewAccountId;
  JsonArray arr = doc["accounts"].to<JsonArray>();
  for (const auto& a : accounts) {
    JsonObject obj = arr.add<JsonObject>();
    obj["id"] = a.id;
    obj["name"] = a.name;
    obj["backend"] = a.isAnkiConnect() ? "ankiconnect" : "ankido";
    obj["url"] = a.url;
    obj["profile"] = a.profile;
    obj["token_obf"] = obfuscation::obfuscateToBase64(a.token);
    obj["enabled"] = a.enabled;
    obj["model"] = a.model;
    JsonArray decks = obj["decks"].to<JsonArray>();
    for (const auto& d : a.decks) decks.add(d);
    obj["cacheSize"] = a.cacheSize;
    obj["lastDeck"] = a.lastDeck;
  }
}

bool AnkiAccountStore::fromJson(JsonVariantConst doc) {
  accounts.clear();
  nextId = doc["nextId"] | 1U;
  reviewAccountId = doc["reviewAccount"] | 0U;
  bool needsResave = false;

  JsonArrayConst arr = doc["accounts"].as<JsonArrayConst>();
  accounts.reserve(std::min(arr.size(), MAX_ACCOUNTS));
  for (JsonObjectConst obj : arr) {
    if (accounts.size() >= MAX_ACCOUNTS) break;
    AnkiAccount a;
    a.id = obj["id"] | 0U;
    if (a.id == 0) {
      // Hand-written file without ids: assign one now.
      a.id = nextId++;
      needsResave = true;
    }
    if (a.id >= nextId) nextId = a.id + 1;
    a.name = obj["name"] | "";
    a.backend = strcmp(obj["backend"] | "ankido", "ankiconnect") == 0 ? AnkiBackend::AnkiConnect : AnkiBackend::AnkiDo;
    a.url = trimSlash(obj["url"] | "");
    a.profile = obj["profile"] | "";
    a.enabled = obj["enabled"] | true;
    a.model = obj["model"] | "Basic";
    if (a.model.empty()) a.model = "Basic";
    a.cacheSize = static_cast<uint8_t>(std::min<int>(obj["cacheSize"] | 60, 200));
    if (a.cacheSize == 0) a.cacheSize = 60;
    // Not read: per-day limits are Anki deck options; 0 = no fetch cap.
    a.maxNewPerDay = 0;
    a.lastDeck = obj["lastDeck"] | "";
    JsonArrayConst decks = obj["decks"].as<JsonArrayConst>();
    a.decks.reserve(decks.size());
    for (JsonVariantConst d : decks) {
      const char* name = d | "";
      if (name[0]) a.decks.emplace_back(name);
    }

    // token_obf (normal) or plain token (hand-written): plain gets rewritten.
    bool ok = false;
    bool tooLong = false;
    a.token = obfuscation::deobfuscateFromBase64(obj["token_obf"] | "", MAX_TOKEN_LENGTH, &ok, &tooLong);
    if (!ok || a.token.empty()) {
      const char* plain = obj["token"] | "";
      if (plain[0] && strlen(plain) <= MAX_TOKEN_LENGTH) {
        a.token = plain;
        needsResave = true;
      }
    }
    accounts.push_back(std::move(a));
  }

  if (reviewAccountId != 0 && findById(reviewAccountId) < 0) reviewAccountId = 0;
  // A single account is the review account by default.
  if (reviewAccountId == 0 && accounts.size() == 1) {
    reviewAccountId = accounts[0].id;
    needsResave = true;
  }

  LOG_DBG("ANKI", "Loaded %zu Anki accounts", accounts.size());
  if (needsResave) requestResave();
  return true;
}

bool AnkiAccountStore::addAccount(AnkiAccount& account) {
  if (accounts.size() >= MAX_ACCOUNTS) {
    LOG_ERR("ANKI", "Account limit (%zu) reached", MAX_ACCOUNTS);
    return false;
  }
  account.id = nextId++;
  account.maxNewPerDay = 0;  // deck limits are applied server-side
  account.url = trimSlash(account.url);
  if (account.model.empty()) account.model = "Basic";
  // nextId lives only in anki.json: if that file was lost while the data
  // directories survived, a new account must not inherit an old id's queued
  // reviews and notes and push them to a different server.
  const std::string dir = ankipaths::accountDir(account.id);
  if (Storage.exists(dir.c_str())) Storage.removeDir(dir.c_str());
  accounts.push_back(account);
  if (reviewAccountId == 0) reviewAccountId = account.id;
  return saveToFile();
}

bool AnkiAccountStore::updateAccount(size_t index, const AnkiAccount& account) {
  if (index >= accounts.size()) return false;
  const uint32_t id = accounts[index].id;
  accounts[index] = account;
  accounts[index].id = id;  // ids are never reassigned
  accounts[index].url = trimSlash(accounts[index].url);
  if (accounts[index].model.empty()) accounts[index].model = "Basic";
  return saveToFile();
}

bool AnkiAccountStore::removeAccount(size_t index) {
  if (index >= accounts.size()) return false;
  const uint32_t id = accounts[index].id;
  accounts.erase(accounts.begin() + static_cast<ptrdiff_t>(index));
  if (reviewAccountId == id) reviewAccountId = accounts.size() == 1 ? accounts[0].id : 0;
  const std::string dir = ankipaths::accountDir(id);
  if (Storage.exists(dir.c_str()) && !Storage.removeDir(dir.c_str())) {
    LOG_ERR("ANKI", "Could not remove %s", dir.c_str());
  }
  return saveToFile();
}

const AnkiAccount* AnkiAccountStore::getAccount(size_t index) const {
  return index < accounts.size() ? &accounts[index] : nullptr;
}

int AnkiAccountStore::findById(uint32_t id) const {
  for (size_t i = 0; i < accounts.size(); i++) {
    if (accounts[i].id == id) return static_cast<int>(i);
  }
  return -1;
}

bool AnkiAccountStore::hasEnabledAccounts() const {
  return std::any_of(accounts.begin(), accounts.end(), [](const AnkiAccount& a) { return a.enabled; });
}

int AnkiAccountStore::getReviewAccountIndex() const { return reviewAccountId == 0 ? -1 : findById(reviewAccountId); }

const AnkiAccount* AnkiAccountStore::getReviewAccount() const {
  const int idx = getReviewAccountIndex();
  return idx < 0 ? nullptr : &accounts[static_cast<size_t>(idx)];
}

bool AnkiAccountStore::setReviewAccount(size_t index) {
  if (index >= accounts.size()) return false;
  reviewAccountId = accounts[index].id;
  return saveToFile();
}

bool AnkiAccountStore::setLastDeck(size_t index, const std::string& deck) {
  if (index >= accounts.size()) return false;
  if (accounts[index].lastDeck == deck) return true;
  accounts[index].lastDeck = deck;
  return saveToFile();
}
