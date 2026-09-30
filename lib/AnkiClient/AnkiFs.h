#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

// Filesystem seam for the Anki client. The firmware implements it over
// HalStorage (src/anki/AnkiStorageFs.*); host tests use a std::fstream or
// in-memory version. Every path is absolute and uses '/' separators.

class AnkiFile {
 public:
  virtual ~AnkiFile() = default;
  // Returns bytes read, 0 at EOF, <0 on error.
  virtual int read(void* buf, size_t len) = 0;
  // Returns bytes written (== len on success).
  virtual size_t write(const void* buf, size_t len) = 0;
  size_t write(const std::string& s) { return write(s.data(), s.size()); }
  virtual bool close() = 0;
};

class AnkiFs {
 public:
  enum class Mode { Read, Write, Append };  // Write truncates

  virtual ~AnkiFs() = default;
  virtual std::unique_ptr<AnkiFile> open(const std::string& path, Mode mode) = 0;
  virtual bool exists(const std::string& path) = 0;
  virtual bool remove(const std::string& path) = 0;
  virtual bool rename(const std::string& from, const std::string& to) = 0;
  // Creates the directory and its parents.
  virtual bool mkdirs(const std::string& path) = 0;
  // Removes a directory and everything under it (used when an account is deleted).
  virtual bool removeDir(const std::string& path) = 0;

  // Convenience: whole-file read into a string. Returns false when missing.
  bool readAll(const std::string& path, std::string& out) {
    out.clear();
    auto f = open(path, Mode::Read);
    if (!f) return false;
    char buf[256];
    for (;;) {
      const int n = f->read(buf, sizeof(buf));
      if (n <= 0) break;
      out.append(buf, static_cast<size_t>(n));
    }
    f->close();
    return true;
  }

  // Convenience: write-through-temp-then-rename so a power loss never leaves
  // a half-written file behind.
  bool writeAllAtomic(const std::string& path, const std::string& data) {
    const std::string tmp = path + ".tmp";
    auto f = open(tmp, Mode::Write);
    if (!f) return false;
    const bool ok = f->write(data.data(), data.size()) == data.size();
    f->close();
    if (!ok) {
      remove(tmp);
      return false;
    }
    if (exists(path)) remove(path);
    return rename(tmp, path);
  }
};

// Reads '\n'-terminated lines from an AnkiFile through a small buffer, so a
// JSONL file of any length is walked one record at a time.
class AnkiLineReader {
 public:
  explicit AnkiLineReader(AnkiFile& file) : file(file) {}

  // Returns false at EOF (line is then empty). A final line without '\n' is
  // still returned. Lines longer than maxLen are truncated.
  bool next(std::string& line, size_t maxLen = 16384) {
    line.clear();
    for (;;) {
      if (pos >= fill) {
        const int n = file.read(buf, sizeof(buf));
        if (n <= 0) return !line.empty();
        fill = static_cast<size_t>(n);
        pos = 0;
      }
      while (pos < fill) {
        const char c = buf[pos++];
        if (c == '\n') return true;
        if (c != '\r' && line.size() < maxLen) line.push_back(c);
      }
    }
  }

 private:
  AnkiFile& file;
  char buf[256] = {};
  size_t pos = 0;
  size_t fill = 0;
};
