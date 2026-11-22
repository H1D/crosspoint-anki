#include "AnkiSecureHttp.h"

#include <HalMemory.h>
#include <Logging.h>
#include <SecureHttpClient.h>

namespace {
// Same preflight margins as KOReaderSyncClient: a TLS handshake that starts
// without them fragments the heap and fails anyway.
constexpr uint32_t MIN_FREE_FOR_TLS = 35000;
constexpr uint32_t MIN_BLOCK_FOR_TLS = 20000;

bool insufficientHeap(const std::string& url) {
  if (url.rfind("http://", 0) == 0) return false;  // plain HTTP needs no TLS buffers
  const auto heap = HalMemory::getDefaultHeap();
  if (heap.freeBytes < MIN_FREE_FOR_TLS || heap.largestBlockBytes < MIN_BLOCK_FOR_TLS) {
    LOG_ERR("ANKI", "Low heap for TLS: %zu free, %zu largest", heap.freeBytes, heap.largestBlockBytes);
    return true;
  }
  return false;
}
}  // namespace

int AnkiSecureHttp::request(const char* method, const std::string& url, const std::vector<Header>& headers,
                            const std::string& body, const DataCallback& onData) {
  if (insufficientHeap(url)) return -2;

  freeink::SecureHttpClient http;
  http.setInsecure();
  http.setTimeout(timeoutMs);
  http.setFollowRedirects(2);
  if (!http.begin(url)) {
    LOG_ERR("ANKI", "Bad URL: %s", url.c_str());
    return -1;
  }
  for (const Header& h : headers) http.addHeader(h.first, h.second);

  LOG_DBG("ANKI", "%s %s (%u bytes, heap %u)", method, url.c_str(), static_cast<unsigned>(body.size()),
          static_cast<unsigned>(HalMemory::getDefaultHeap().freeBytes));
  const int status =
      http.sendRequest(method, reinterpret_cast<const uint8_t*>(body.data()), body.size(),
                       [&](const uint8_t* data, size_t len) { return onData(http.getStatus(), data, len); });
  // The client reports the header status even when the connection dropped in
  // the middle of the body: a cut-off media file would be kept as complete.
  const bool truncated = status > 0 && !http.responseComplete() && !http.callbackAborted();
  http.end();
  LOG_DBG("ANKI", "-> %d%s", status, truncated ? " (body cut off)" : "");
  return truncated ? -3 : status;
}
