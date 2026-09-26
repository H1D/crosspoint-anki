#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>

// Minimal JSON helpers for the Anki client. No ArduinoJson here: the client
// library also builds on the host for unit tests, and the exchange response
// (up to ~60 cards of text) is streamed straight to the SD card rather than
// parsed into a document.
namespace ankijson {

// Appends `s` to `out` as a quoted JSON string literal.
void appendQuoted(std::string& out, const std::string& s);
void appendQuoted(std::string& out, const char* s);

// SAX-style streaming reader. Strings are accumulated in a std::string (no
// fixed token cap) and escapes are decoded, including \uXXXX and surrogate
// pairs, into UTF-8. Numbers are delivered as text; callers parse the ones
// they care about. Nesting depth is bounded by MAX_DEPTH; deeper input sets
// the error flag and further events stop.
class Reader {
 public:
  struct Callbacks {
    std::function<void(const std::string& key)> onKey;
    std::function<void(const std::string& value)> onString;
    std::function<void(const std::string& text)> onNumber;
    std::function<void(bool value)> onBool;
    std::function<void()> onNull;
    std::function<void()> onObjectStart;
    std::function<void()> onObjectEnd;
    std::function<void()> onArrayStart;
    std::function<void()> onArrayEnd;
  };

  static constexpr int MAX_DEPTH = 24;

  explicit Reader(Callbacks callbacks) : cb(std::move(callbacks)) {}

  void feed(const char* data, size_t len);
  void feed(const std::string& s) { feed(s.data(), s.size()); }
  bool hasError() const { return error; }
  // Depth of the current container: 0 outside the top-level value.
  int depth() const { return depthCount; }
  // True when the current container is an array (keys are not expected).
  bool inArray() const { return depthCount > 0 && isArrayStack[depthCount - 1]; }

 private:
  enum class State : uint8_t { Value, InString, InEscape, InUnicode, InNumber, InLiteral };

  void handleValueChar(char c);
  void endString();
  void endNumber();
  void endLiteral();
  void push(bool isArray);
  void pop();
  void appendUtf8(uint32_t cp);

  Callbacks cb;
  State state = State::Value;
  std::string token;
  bool tokenIsKey = false;
  bool expectKey = false;  // inside an object and the next string is a key
  bool error = false;
  int depthCount = 0;
  bool isArrayStack[MAX_DEPTH] = {};
  // \uXXXX decoding
  uint32_t unicodeValue = 0;
  uint8_t unicodeDigits = 0;
  uint32_t pendingHighSurrogate = 0;
};

// Parses a JSON integer literal; returns 0 on malformed input.
int64_t toInt64(const std::string& text);

}  // namespace ankijson
