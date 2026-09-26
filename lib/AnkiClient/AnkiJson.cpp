#include "AnkiJson.h"

#include <cstdlib>

namespace ankijson {

namespace {
const char* const HEX = "0123456789abcdef";

int hexValue(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}
}  // namespace

void appendQuoted(std::string& out, const char* s) {
  out.push_back('"');
  for (const unsigned char* p = reinterpret_cast<const unsigned char*>(s); *p; ++p) {
    const unsigned char c = *p;
    switch (c) {
      case '"':
        out += "\\\"";
        break;
      case '\\':
        out += "\\\\";
        break;
      case '\n':
        out += "\\n";
        break;
      case '\r':
        out += "\\r";
        break;
      case '\t':
        out += "\\t";
        break;
      default:
        if (c < 0x20) {
          out += "\\u00";
          out.push_back(HEX[c >> 4]);
          out.push_back(HEX[c & 0xF]);
        } else {
          out.push_back(static_cast<char>(c));
        }
    }
  }
  out.push_back('"');
}

void appendQuoted(std::string& out, const std::string& s) { appendQuoted(out, s.c_str()); }

int64_t toInt64(const std::string& text) {
  if (text.empty()) return 0;
  char* end = nullptr;
  const long long v = strtoll(text.c_str(), &end, 10);
  return end == text.c_str() ? 0 : static_cast<int64_t>(v);
}

void Reader::push(bool isArray) {
  if (depthCount >= MAX_DEPTH) {
    error = true;
    return;
  }
  isArrayStack[depthCount++] = isArray;
  expectKey = !isArray;
}

void Reader::pop() {
  if (depthCount == 0) {
    error = true;
    return;
  }
  depthCount--;
}

void Reader::appendUtf8(uint32_t cp) {
  if (cp < 0x80) {
    token.push_back(static_cast<char>(cp));
  } else if (cp < 0x800) {
    token.push_back(static_cast<char>(0xC0 | (cp >> 6)));
    token.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
  } else if (cp < 0x10000) {
    token.push_back(static_cast<char>(0xE0 | (cp >> 12)));
    token.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
    token.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
  } else {
    token.push_back(static_cast<char>(0xF0 | (cp >> 18)));
    token.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
    token.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
    token.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
  }
}

void Reader::endString() {
  if (tokenIsKey) {
    if (cb.onKey) cb.onKey(token);
    expectKey = false;
  } else {
    if (cb.onString) cb.onString(token);
    // After a value inside an object the next string is a key again.
    expectKey = depthCount > 0 && !isArrayStack[depthCount - 1];
  }
  token.clear();
  state = State::Value;
}

void Reader::endNumber() {
  if (cb.onNumber) cb.onNumber(token);
  token.clear();
  expectKey = depthCount > 0 && !isArrayStack[depthCount - 1];
  state = State::Value;
}

void Reader::endLiteral() {
  if (token == "true") {
    if (cb.onBool) cb.onBool(true);
  } else if (token == "false") {
    if (cb.onBool) cb.onBool(false);
  } else if (token == "null") {
    if (cb.onNull) cb.onNull();
  } else {
    error = true;
  }
  token.clear();
  expectKey = depthCount > 0 && !isArrayStack[depthCount - 1];
  state = State::Value;
}

void Reader::handleValueChar(char c) {
  switch (c) {
    case ' ':
    case '\t':
    case '\n':
    case '\r':
    case ',':
    case ':':
      return;
    case '{':
      push(false);
      if (!error && cb.onObjectStart) cb.onObjectStart();
      return;
    case '}':
      pop();
      if (!error && cb.onObjectEnd) cb.onObjectEnd();
      expectKey = depthCount > 0 && !isArrayStack[depthCount - 1];
      return;
    case '[':
      push(true);
      if (!error && cb.onArrayStart) cb.onArrayStart();
      return;
    case ']':
      pop();
      if (!error && cb.onArrayEnd) cb.onArrayEnd();
      expectKey = depthCount > 0 && !isArrayStack[depthCount - 1];
      return;
    case '"':
      tokenIsKey = expectKey;
      token.clear();
      state = State::InString;
      return;
    case 't':
    case 'f':
    case 'n':
      token.assign(1, c);
      state = State::InLiteral;
      return;
    default:
      if (c == '-' || (c >= '0' && c <= '9')) {
        token.assign(1, c);
        state = State::InNumber;
        return;
      }
      error = true;
  }
}

void Reader::feed(const char* data, size_t len) {
  for (size_t i = 0; i < len && !error; i++) {
    const char c = data[i];
    switch (state) {
      case State::Value:
        handleValueChar(c);
        break;
      case State::InString:
        if (c == '"') {
          endString();
        } else if (c == '\\') {
          state = State::InEscape;
        } else {
          token.push_back(c);
        }
        break;
      case State::InEscape:
        state = State::InString;
        switch (c) {
          case '"':
            token.push_back('"');
            break;
          case '\\':
            token.push_back('\\');
            break;
          case '/':
            token.push_back('/');
            break;
          case 'b':
            token.push_back('\b');
            break;
          case 'f':
            token.push_back('\f');
            break;
          case 'n':
            token.push_back('\n');
            break;
          case 'r':
            token.push_back('\r');
            break;
          case 't':
            token.push_back('\t');
            break;
          case 'u':
            unicodeValue = 0;
            unicodeDigits = 0;
            state = State::InUnicode;
            break;
          default:
            error = true;
        }
        break;
      case State::InUnicode: {
        const int v = hexValue(c);
        if (v < 0) {
          error = true;
          break;
        }
        unicodeValue = (unicodeValue << 4) | static_cast<uint32_t>(v);
        if (++unicodeDigits == 4) {
          state = State::InString;
          if (unicodeValue >= 0xD800 && unicodeValue <= 0xDBFF) {
            pendingHighSurrogate = unicodeValue;
          } else if (unicodeValue >= 0xDC00 && unicodeValue <= 0xDFFF && pendingHighSurrogate) {
            appendUtf8(0x10000 + ((pendingHighSurrogate - 0xD800) << 10) + (unicodeValue - 0xDC00));
            pendingHighSurrogate = 0;
          } else {
            pendingHighSurrogate = 0;
            appendUtf8(unicodeValue);
          }
        }
        break;
      }
      case State::InNumber:
        if (c == '-' || c == '+' || c == '.' || c == 'e' || c == 'E' || (c >= '0' && c <= '9')) {
          token.push_back(c);
        } else {
          endNumber();
          handleValueChar(c);
        }
        break;
      case State::InLiteral:
        if (c >= 'a' && c <= 'z') {
          token.push_back(c);
          if (token.size() > 5) error = true;
        } else {
          endLiteral();
          handleValueChar(c);
        }
        break;
    }
  }
}

}  // namespace ankijson
