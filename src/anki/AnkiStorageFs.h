#pragma once

#include <AnkiFs.h>

// AnkiFs over HalStorage (the SD card). Every call goes through the
// HalStorage mutex; HalFile handles are wrapped so the client library never
// touches SdFat directly.
class AnkiStorageFs final : public AnkiFs {
 public:
  static AnkiStorageFs& instance() {
    static AnkiStorageFs fs;
    return fs;
  }

  std::unique_ptr<AnkiFile> open(const std::string& path, Mode mode) override;
  bool exists(const std::string& path) override;
  bool remove(const std::string& path) override;
  bool rename(const std::string& from, const std::string& to) override;
  bool mkdirs(const std::string& path) override;
  bool removeDir(const std::string& path) override;

 private:
  AnkiStorageFs() = default;
};
