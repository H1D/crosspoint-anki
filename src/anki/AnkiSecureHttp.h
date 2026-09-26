#pragma once

#include <AnkiHttp.h>

// AnkiHttp over freeink::SecureHttpClient. One request per call; the body is
// streamed to the callback so nothing larger than a TLS record sits in RAM.
// Certificate verification is off (setInsecure), matching every other HTTPS
// client in the firmware.
class AnkiSecureHttp final : public AnkiHttp {
 public:
  static AnkiSecureHttp& instance() {
    static AnkiSecureHttp http;
    return http;
  }

  int request(const char* method, const std::string& url, const std::vector<Header>& headers,
              const std::string& body, const DataCallback& onData) override;

  // Milliseconds to wait for headers / between body chunks.
  uint32_t timeoutMs = 20000;

 private:
  AnkiSecureHttp() = default;
};
