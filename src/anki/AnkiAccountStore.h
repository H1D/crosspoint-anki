#pragma once

#include <AnkiTypes.h>
#include <ArduinoJson.h>
#include <PersistableStore.h>

#include <string>
#include <vector>

/**
 * Singleton store for Anki accounts (AnkiDo or AnkiConnect, "backend" key),
 * persisted at /.crosspoint/anki.json.
 * Tokens are XOR-obfuscated with the device MAC and base64-encoded on disk
 * (token_obf); a plain "token" key written by hand is accepted and rewritten
 * obfuscated on the next save. Each account gets a stable numeric id that
 * names its data directory (see AnkiPaths.h), so deleting or reordering
 * accounts never mixes up caches and queues.
 */
class AnkiAccountStore : public PersistableStore<AnkiAccountStore> {
 private:
  std::vector<AnkiAccount> accounts;
  uint32_t nextId = 1;
  uint32_t reviewAccountId = 0;  // 0 = none chosen

  AnkiAccountStore() = default;
  friend class PersistableStore<AnkiAccountStore>;

 public:
  static constexpr size_t MAX_ACCOUNTS = 8;

  static const char* getFilePath() { return "/.crosspoint/anki.json"; }
  void toJson(JsonDocument& doc) const;
  bool fromJson(JsonVariantConst doc);

  // Assigns account.id, appends, saves. Fails when the limit is reached.
  bool addAccount(AnkiAccount& account);
  bool updateAccount(size_t index, const AnkiAccount& account);
  // Removes the entry and its SD data directory.
  bool removeAccount(size_t index);

  const std::vector<AnkiAccount>& getAccounts() const { return accounts; }
  const AnkiAccount* getAccount(size_t index) const;
  int findById(uint32_t id) const;
  size_t getCount() const { return accounts.size(); }
  bool hasAccounts() const { return !accounts.empty(); }
  bool hasEnabledAccounts() const;

  // The single account whose queue is reviewed on the device.
  int getReviewAccountIndex() const;
  const AnkiAccount* getReviewAccount() const;
  bool setReviewAccount(size_t index);

  // Remembers the deck last used by add-word for this account (saved).
  bool setLastDeck(size_t index, const std::string& deck);
};

#define ANKI_STORE AnkiAccountStore::getInstance()
