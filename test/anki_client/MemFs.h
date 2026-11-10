#pragma once

#include <algorithm>
#include <cstring>
#include <map>
#include <memory>
#include <string>

#include "AnkiFs.h"

// In-memory filesystem for the Anki client tests.
class MemFs final : public AnkiFs {
 public:
  std::map<std::string, std::string> files;

  class File final : public AnkiFile {
   public:
    File(std::string* data, bool writable, size_t pos) : data(data), writable(writable), pos(pos) {}
    int read(void* buf, size_t len) override {
      if (writable || pos >= data->size()) return 0;
      const size_t n = std::min(len, data->size() - pos);
      memcpy(buf, data->data() + pos, n);
      pos += n;
      return static_cast<int>(n);
    }
    size_t write(const void* buf, size_t len) override {
      if (!writable) return 0;
      data->append(static_cast<const char*>(buf), len);
      return len;
    }
    bool close() override { return true; }

   private:
    std::string* data;
    bool writable;
    size_t pos;
  };

  std::unique_ptr<AnkiFile> open(const std::string& path, Mode mode) override {
    if (mode == Mode::Read) {
      auto it = files.find(path);
      if (it == files.end()) return nullptr;
      return std::make_unique<File>(&it->second, false, 0);
    }
    if (mode == Mode::Write) files[path].clear();
    return std::make_unique<File>(&files[path], true, 0);
  }
  bool exists(const std::string& path) override { return files.count(path) > 0; }
  bool remove(const std::string& path) override { return files.erase(path) > 0; }
  bool rename(const std::string& from, const std::string& to) override {
    auto it = files.find(from);
    if (it == files.end()) return false;
    files[to] = it->second;
    files.erase(it);
    return true;
  }
  bool mkdirs(const std::string&) override { return true; }
  bool removeDir(const std::string& path) override {
    for (auto it = files.begin(); it != files.end();) {
      if (it->first.rfind(path + "/", 0) == 0)
        it = files.erase(it);
      else
        ++it;
    }
    return true;
  }
};
