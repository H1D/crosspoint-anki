#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <utility>
#include <vector>

// HTTP seam for the Anki client. The firmware implements it over
// freeink::SecureHttpClient (src/anki/AnkiSecureHttp.*); host tests use a fake
// that replays canned responses.
class AnkiHttp {
 public:
  using Header = std::pair<std::string, std::string>;
  // Called with the response status (known before the first body byte) and a
  // body chunk; return false to abort the download.
  using DataCallback = std::function<bool(int status, const uint8_t* data, size_t len)>;

  virtual ~AnkiHttp() = default;

  // Performs one request. The response body is streamed to onData in chunks;
  // the whole body is never held by the transport. Returns the HTTP status
  // code, or a negative value on transport failure (no connection, timeout,
  // TLS, low memory).
  virtual int request(const char* method, const std::string& url, const std::vector<Header>& headers,
                      const std::string& body, const DataCallback& onData) = 0;
};
