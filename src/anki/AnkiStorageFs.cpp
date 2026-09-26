#include "AnkiStorageFs.h"

#include <HalStorage.h>
#include <Logging.h>
#include <Memory.h>

namespace {

class StorageFile final : public AnkiFile {
 public:
  explicit StorageFile(HalFile&& file) : file(std::move(file)) {}
  ~StorageFile() override { close(); }

  int read(void* buf, size_t len) override { return file.read(buf, len); }
  size_t write(const void* buf, size_t len) override { return file.write(buf, len); }
  bool close() override {
    if (!open) return true;
    open = false;
    return file.close();
  }

 private:
  HalFile file;
  bool open = true;
};

}  // namespace

std::unique_ptr<AnkiFile> AnkiStorageFs::open(const std::string& path, Mode mode) {
  oflag_t flags = O_RDONLY;
  if (mode == Mode::Write) flags = O_WRONLY | O_CREAT | O_TRUNC;
  else if (mode == Mode::Append) flags = O_WRONLY | O_CREAT | O_APPEND;
  HalFile file = Storage.open(path.c_str(), flags);
  if (!file) {
    if (mode != Mode::Read) LOG_ERR("ANKI", "Cannot open %s for writing", path.c_str());
    return nullptr;
  }
  auto wrapped = makeUniqueNoThrow<StorageFile>(std::move(file));
  if (!wrapped) LOG_ERR("ANKI", "OOM: file handle");
  return wrapped;
}

bool AnkiStorageFs::exists(const std::string& path) { return Storage.exists(path.c_str()); }

bool AnkiStorageFs::remove(const std::string& path) { return Storage.remove(path.c_str()); }

bool AnkiStorageFs::rename(const std::string& from, const std::string& to) {
  return Storage.rename(from.c_str(), to.c_str());
}

bool AnkiStorageFs::mkdirs(const std::string& path) {
  // HalStorage::mkdir(path, pFlag=true) creates parent directories.
  return Storage.exists(path.c_str()) || Storage.mkdir(path.c_str(), true);
}

bool AnkiStorageFs::removeDir(const std::string& path) {
  if (!Storage.exists(path.c_str())) return true;
  return Storage.removeDir(path.c_str());
}
