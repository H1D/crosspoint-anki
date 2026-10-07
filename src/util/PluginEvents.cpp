#include "PluginEvents.h"

#include <ArduinoJson.h>
#include <HalStorage.h>
#include <Logging.h>
#include <esp_random.h>
#include <time.h>

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "PluginHttp.h"
#include "PluginLocations.h"
#include "WakeSchedule.h"
#include "components/UITheme.h"
#include "network/ProtectedPaths.h"

namespace {

constexpr const char* EVENT_NAMES[] = {"reader.open", "reader.exit", "reader.session", "book.downloaded",
                                       "sleep.enter"};
static_assert(sizeof(EVENT_NAMES) / sizeof(EVENT_NAMES[0]) == static_cast<size_t>(pluginevents::Event::COUNT),
              "event name table out of sync");

constexpr const char* OUTBOX_NAME = "/events.jsonl";
// Drop-oldest wholesale: a plugin that is never drained must not grow a file
// forever, and by the time 4KB of events piled up the old ones describe stale
// state anyway.
constexpr size_t MAX_OUTBOX_BYTES = 4 * 1024;
constexpr size_t MAX_MANIFEST_SIZE = 8 * 1024;
constexpr size_t MAX_EVENT_LINE = 512;
// Event handler responses are acknowledgements, not content.
constexpr size_t MAX_EVENT_RESPONSE = 8 * 1024;

// Static subscription table: one slot per installed plugin that declares an
// "events" section. Rebuilt by refreshSubscriptions(); sized for a full
// plugin list screen, not a marketplace.
constexpr size_t MAX_EVENT_PLUGINS = 8;
constexpr size_t MAX_WAKE_TIMES = 4;
// Downloads one event may list (today's image plus the next days').
constexpr size_t MAX_DOWNLOADS_PER_EVENT = 4;
struct Subscriber {
  char name[24] = {0};      // plugin folder name; "" = empty slot
  char dir[64] = {0};       // "<root>/<name>"
  uint8_t mask = 0;         // bit per Event
  uint8_t connectMask = 0;  // events whose handler declares "connect": true
  uint8_t wakeCount = 0;
  int16_t wakeMinutes[MAX_WAKE_TIMES] = {0};  // sleep.enter "wake" times, minutes after local midnight
};
Subscriber subscribers[MAX_EVENT_PLUGINS];

uint8_t eventBit(const pluginevents::Event e) { return static_cast<uint8_t>(1u << static_cast<uint8_t>(e)); }

int eventFromName(const char* name) {
  for (size_t i = 0; i < static_cast<size_t>(pluginevents::Event::COUNT); i++) {
    if (strcmp(EVENT_NAMES[i], name) == 0) return static_cast<int>(i);
  }
  return -1;
}

std::string outboxPath(const Subscriber& sub) { return std::string(sub.dir) + OUTBOX_NAME; }

}  // namespace

namespace pluginevents {

void refreshSubscriptions() {
  for (auto& sub : subscribers) sub = Subscriber{};

  size_t slot = 0;
  for (const auto& entry : PluginLocations::scanPlugins()) {
    if (!entry.hasDevice) continue;
    if (slot >= MAX_EVENT_PLUGINS) {
      LOG_ERR("PEVT", "subscription table full; ignoring %s", entry.name.c_str());
      break;
    }
    // The fixed-size table cannot hold these: a truncated name or folder would
    // point emit() and drain() at a different outbox than the plugin's own.
    if (entry.name.size() >= sizeof(Subscriber::name) || entry.dir.size() >= sizeof(Subscriber::dir)) {
      LOG_ERR("PEVT", "plugin name/path too long for events; ignoring %s", entry.name.c_str());
      continue;
    }
    std::string raw;
    if (!Storage.readFileToString("PEVT", entry.dir + "/device.json", MAX_MANIFEST_SIZE, raw)) continue;
    // Filtered parse: only the events section, so a big manifest costs a few
    // hundred bytes here instead of a full document.
    JsonDocument filter;
    filter["events"] = true;
    JsonDocument doc;
    if (deserializeJson(doc, raw, DeserializationOption::Filter(filter)) != DeserializationError::Ok) continue;
    uint8_t mask = 0;
    uint8_t connectMask = 0;
    uint8_t wakeCount = 0;
    int16_t wakeMinutes[MAX_WAKE_TIMES] = {0};
    for (JsonPairConst kv : doc["events"].as<JsonObjectConst>()) {
      const int e = eventFromName(kv.key().c_str());
      if (e < 0) {
        LOG_DBG("PEVT", "%s: unknown event '%s' ignored", entry.name.c_str(), kv.key().c_str());
        continue;
      }
      mask |= static_cast<uint8_t>(1u << e);
      if (kv.value()["connect"] | false) connectMask |= static_cast<uint8_t>(1u << e);
      if (e != static_cast<int>(Event::SleepEnter)) continue;
      for (JsonVariantConst at : kv.value()["wake"].as<JsonArrayConst>()) {
        const int minute = wakeschedule::parseClock(at.as<const char*>());
        if (minute < 0 || wakeCount >= MAX_WAKE_TIMES) {
          LOG_ERR("PEVT", "%s: wake time ignored (want up to %u \"HH:MM\")", entry.name.c_str(),
                  static_cast<unsigned>(MAX_WAKE_TIMES));
          continue;
        }
        wakeMinutes[wakeCount++] = static_cast<int16_t>(minute);
      }
    }
    // sleep.enter exists to act before the chip powers down (sleep image,
    // pre-sleep sync), so subscribing implies "connect": true; requiring the
    // flag would make the common case silently defer to the next session.
    if (mask & eventBit(Event::SleepEnter)) connectMask |= eventBit(Event::SleepEnter);
    if (mask == 0) continue;
    Subscriber& sub = subscribers[slot++];
    // Lengths checked above, so these copy whole strings.
    strncpy(sub.name, entry.name.c_str(), sizeof(sub.name) - 1);
    strncpy(sub.dir, entry.dir.c_str(), sizeof(sub.dir) - 1);
    sub.mask = mask;
    sub.connectMask = connectMask;
    sub.wakeCount = wakeCount;
    memcpy(sub.wakeMinutes, wakeMinutes, sizeof(wakeMinutes));
    LOG_DBG("PEVT", "%s subscribes mask=0x%02x", sub.name, sub.mask);
  }
}

bool anySubscriber(const Event e) {
  for (const auto& sub : subscribers) {
    if (sub.name[0] != '\0' && (sub.mask & eventBit(e))) return true;
  }
  return false;
}

uint8_t subscriptionMask(const char* plugin) {
  for (const auto& sub : subscribers) {
    if (sub.name[0] != '\0' && strcmp(sub.name, plugin) == 0) return sub.mask;
  }
  return 0;
}

int64_t nextWake(const int64_t now) {
  int64_t best = 0;
  for (const auto& sub : subscribers) {
    if (sub.name[0] == '\0' || sub.wakeCount == 0) continue;
    const int64_t at = wakeschedule::nextWake(now, sub.wakeMinutes, sub.wakeCount);
    if (at != 0 && (best == 0 || at < best)) best = at;
  }
  return best;
}

bool wantsConnectAny() {
  for (const auto& sub : subscribers) {
    if (sub.name[0] == '\0' || sub.connectMask == 0) continue;
    std::string raw;
    if (!Storage.readFileToString("PEVT", outboxPath(sub), MAX_OUTBOX_BYTES + MAX_EVENT_LINE, raw)) continue;
    for (size_t i = 0; i < static_cast<size_t>(Event::COUNT); i++) {
      if (!(sub.connectMask & eventBit(static_cast<Event>(i)))) continue;
      char eventField[48];
      snprintf(eventField, sizeof(eventField), "\"e\":\"%s\"", EVENT_NAMES[i]);
      if (raw.find(eventField) != std::string::npos) return true;
    }
  }
  return false;
}

void emit(const Event e, const Var* vars, const size_t varCount) {
  if (!anySubscriber(e)) return;

  // One line: {"e":"reader.exit","id":"3fa9c21b-7","ts":1734212345,"vars":{"book":"...","percent":"74"}}
  JsonDocument doc;
  doc["e"] = EVENT_NAMES[static_cast<size_t>(e)];
  // Unique id for server-side dedupe of at-least-once delivery: a per-boot
  // nonce plus an in-session counter, unique across queued events and reboots
  // without an SD read-modify-write per event. ts alone repeats (1-second
  // resolution, and 0 whenever the clock was never set).
  static const uint32_t bootNonce = esp_random();
  static uint32_t seq = 0;
  char id[24];
  snprintf(id, sizeof(id), "%08lx-%lu", static_cast<unsigned long>(bootNonce), static_cast<unsigned long>(++seq));
  doc["id"] = id;
  // Best-effort unix time: 0 when the clock was never set (no RTC, no NTP yet).
  doc["ts"] = static_cast<long long>(time(nullptr));
  JsonObject varsObj = doc["vars"].to<JsonObject>();
  for (size_t i = 0; i < varCount; i++) {
    varsObj[vars[i].key] = vars[i].value;
  }
  std::string line;
  line.reserve(160);
  serializeJson(doc, line);
  line += '\n';
  if (line.size() > MAX_EVENT_LINE) {
    LOG_ERR("PEVT", "event line too large (%u); dropped", static_cast<unsigned>(line.size()));
    return;
  }

  for (const auto& sub : subscribers) {
    if (sub.name[0] == '\0' || !(sub.mask & eventBit(e))) continue;
    const std::string path = outboxPath(sub);
    HalFile file = Storage.open(path.c_str(), O_WRONLY | O_CREAT | O_APPEND);
    if (!file || !file.isOpen()) {
      LOG_ERR("PEVT", "%s: outbox open failed", sub.name);
      continue;
    }
    if (file.fileSize() > MAX_OUTBOX_BYTES) {
      // Drop-oldest wholesale (see MAX_OUTBOX_BYTES).
      file.close();  // explicit: remove follows on the same path
      Storage.remove(path.c_str());
      file = Storage.open(path.c_str(), O_WRONLY | O_CREAT | O_APPEND);
      if (!file || !file.isOpen()) continue;
      LOG_DBG("PEVT", "%s: outbox over cap, dropped", sub.name);
    }
    const uint64_t before = file.fileSize64();
    if (file.write(reinterpret_cast<const uint8_t*>(line.data()), line.size()) != line.size()) {
      // A torn line would glue the next event onto it and the drain would drop
      // both as corrupt: cut back to the last complete line, losing only this one.
      LOG_ERR("PEVT", "%s: short outbox append; event dropped", sub.name);
      file.truncate(before);
    }
    file.flush();
  }
}

namespace {

// The manifest subset a drain needs: the handlers plus the token/config/auth
// vocabulary shared with the catalog browser.
struct DrainManifest {
  std::string tokenFile, tokenPath, configFile;
  std::string authType, authTokenPath;
  pluginhttp::RequestSpec authReq;
  struct Handler {
    int event = -1;
    pluginhttp::RequestSpec req;
    std::string toast;
    // "download" variant: the response streams to `dest` on SD (e.g. a fresh
    // sleep image) instead of being read as an acknowledgement.
    std::string dest;
    bool isDownload() const { return !dest.empty(); }
  };
  std::vector<Handler> handlers;
  bool hasPasswordGrant() const { return authType == "password" && !authReq.url.empty(); }
};

// False only when device.json cannot be read or parsed; a manifest that parses
// but declares no runnable handlers returns true with out.handlers empty.
bool loadDrainManifest(const Subscriber& sub, DrainManifest& out) {
  std::string raw;
  if (!Storage.readFileToString("PEVT", std::string(sub.dir) + "/device.json", MAX_MANIFEST_SIZE, raw)) return false;
  // Filtered parse: the drain runs at the heap-worst moments (sleep entry,
  // web session), so only the sections it reads are materialized.
  JsonDocument filter;
  filter["token"] = true;
  filter["config"] = true;
  filter["auth"] = true;
  filter["events"] = true;
  JsonDocument doc;
  if (deserializeJson(doc, raw, DeserializationOption::Filter(filter)) != DeserializationError::Ok) return false;

  out.tokenFile = pluginhttp::inPluginDir(sub.dir, doc["token"]["file"] | "");
  out.tokenPath = doc["token"]["path"] | "token";
  out.configFile = pluginhttp::inPluginDir(sub.dir, doc["config"]["file"] | "");
  JsonVariantConst auth = doc["auth"];
  out.authType = auth["type"] | "device_code";
  pluginhttp::readRequest(auth["request"], "POST", out.authReq);
  out.authTokenPath = auth["token_path"] | "access_token";

  out.handlers.reserve(2);
  for (JsonPairConst kv : doc["events"].as<JsonObjectConst>()) {
    const int e = eventFromName(kv.key().c_str());
    if (e < 0) continue;
    const char* toast = kv.value()["toast"] | "";
    // "download" is one {url, dest} or a list of them (e.g. today's image and
    // the next days'); each becomes a handler for the same event.
    JsonVariantConst dl = kv.value()["download"];
    if (dl.is<JsonArrayConst>()) {
      size_t count = 0;
      for (JsonVariantConst one : dl.as<JsonArrayConst>()) {
        if (count == MAX_DOWNLOADS_PER_EVENT) {
          LOG_ERR("PEVT", "%s: downloads past %u ignored", sub.name, static_cast<unsigned>(MAX_DOWNLOADS_PER_EVENT));
          break;
        }
        DrainManifest::Handler h;
        h.event = e;
        pluginhttp::readRequest(one, "GET", h.req);
        h.dest = one["dest"] | "";
        if (h.req.url.empty() || h.dest.empty()) continue;
        // One popup per event, after its last download.
        h.toast = toast;
        if (count > 0) out.handlers.back().toast.clear();
        out.handlers.push_back(std::move(h));
        count++;
      }
      continue;
    }
    DrainManifest::Handler h;
    h.event = e;
    if (dl["url"].as<const char*>()) {
      pluginhttp::readRequest(dl, "GET", h.req);
      h.dest = dl["dest"] | "";
      if (h.dest.empty()) continue;  // a download without a destination is meaningless
    } else {
      pluginhttp::readRequest(kv.value()["request"], "POST", h.req);
    }
    h.toast = toast;
    if (!h.req.url.empty()) out.handlers.push_back(std::move(h));
  }
  return true;
}

// {token}, {cfg.*}, {meta.*}, and {event.*} from the queued line's vars
// object. `config` and `meta` hold pre-built patterns ("{cfg.KEY}" /
// "{meta.KEY}") so the keys are not re-concatenated for every template.
std::string drainSubstituted(std::string tpl, const std::string& token, const pluginhttp::Headers& config,
                             const pluginhttp::Headers& meta, JsonVariantConst vars, const long long ts,
                             const char* id) {
  pluginhttp::substituteAll(tpl, "{token}", token);
  for (const auto& kv : config) pluginhttp::substituteAll(tpl, kv.first.c_str(), kv.second);
  for (const auto& kv : meta) pluginhttp::substituteAll(tpl, kv.first.c_str(), kv.second);
  char tsBuf[16];
  snprintf(tsBuf, sizeof(tsBuf), "%lld", ts);
  pluginhttp::substituteAll(tpl, "{event.ts}", tsBuf);
  pluginhttp::substituteAll(tpl, "{event.id}", id);
  for (JsonPairConst kv : vars.as<JsonObjectConst>()) {
    pluginhttp::substituteAll(tpl, (std::string("{event.") + kv.key().c_str() + "}").c_str(),
                              pluginhttp::variantToString(kv.value()));
  }
  return tpl;
}

// Runs one handler for a queued line; false on a transport or auth failure.
bool deliverToHandler(const DrainManifest& mf, const DrainManifest::Handler* handler, JsonVariantConst vars,
                      long long ts, const char* id, const pluginhttp::Headers& meta, std::string& token,
                      const pluginhttp::Headers& config, GfxRenderer* renderer);

// Replays one queued line. True = delivered (drop the line); false = transport
// or auth failure (keep it for the next drain). A line with no matching
// handler counts as delivered so a manifest edit can't wedge the queue.
// `token` is shared across the drain: a 401-minted refresh persists to the
// remaining lines instead of re-minting per line.
bool deliverLine(const DrainManifest& mf, const std::string& lineText, std::string& token,
                 const pluginhttp::Headers& config, GfxRenderer* renderer) {
  JsonDocument doc;
  if (deserializeJson(doc, lineText) != DeserializationError::Ok) return true;  // corrupt line: drop
  const int e = eventFromName(doc["e"] | "");
  JsonVariantConst vars = doc["vars"];
  const long long ts = doc["ts"] | 0LL;
  // Lines queued by pre-id firmware substitute {event.id} as empty.
  const char* id = doc["id"] | "";
  // Book-scoped events expose the book's plugin sidecar ("<book>.meta.json",
  // flat fields written at download time) as {meta.*} variables, e.g. a
  // service book id for a sync handler.
  pluginhttp::Headers meta;
  const char* book = vars["book"] | "";
  if (book[0] != '\0') {
    pluginhttp::loadConfigFile(std::string(book) + ".meta.json", meta);
    for (auto& kv : meta) kv.first = "{meta." + kv.first + "}";
  }
  // Every handler of the event (a download list has several). A failure keeps
  // the line queued, and the retry runs them all again.
  for (const auto& h : mf.handlers) {
    if (h.event == e && !deliverToHandler(mf, &h, vars, ts, id, meta, token, config, renderer)) return false;
  }
  return true;
}

bool deliverToHandler(const DrainManifest& mf, const DrainManifest::Handler* handler, JsonVariantConst vars,
                      const long long ts, const char* id, const pluginhttp::Headers& meta, std::string& token,
                      const pluginhttp::Headers& config, GfxRenderer* renderer) {
  const auto run = [&](const std::string& tok) {
    pluginhttp::Headers headers;
    headers.reserve(handler->req.headers.size());
    for (const auto& h : handler->req.headers) {
      headers.emplace_back(h.first, drainSubstituted(h.second, tok, config, meta, vars, ts, id));
    }
    if (handler->isDownload()) {
      const std::string dest = drainSubstituted(handler->dest, tok, config, meta, vars, ts, id);
      // Substituted fields must not climb out of the tree or land on a
      // credential store (same guard as the catalog sidecar writer).
      if (!protectedpaths::isPluginPath(dest)) {
        LOG_ERR("PEVT", "unsafe download dest rejected: %s", dest.c_str());
        return 200;  // treat as delivered: retrying can never fix the manifest
      }
      // Generous for images (a 4-bit 800x480 BMP is ~192KB), still bounded.
      constexpr size_t MAX_EVENT_DOWNLOAD = 1024 * 1024;
      // Stream to a sibling temp and swap it in only on a clean 2xx, so a
      // 404/500 error body can never replace an existing dest (e.g. /sleep.bmp).
      const std::string tmp = dest + ".part";
      const int st = pluginhttp::requestToFile(
          nullptr, drainSubstituted(handler->req.url, tok, config, meta, vars, ts, id), handler->req.method,
          drainSubstituted(handler->req.body, tok, config, meta, vars, ts, id), headers, tmp.c_str(),
          MAX_EVENT_DOWNLOAD);
      if (st >= 200 && st < 300) {
        // rename won't overwrite an existing file, so park the old dest as a
        // backup and restore it if the swap fails: a failed commit must not
        // lose both the old file and the fresh download. -1 (local failure,
        // same convention as pluginhttp) keeps the line queued for retry.
        const std::string bak = dest + ".bak";
        Storage.remove(bak.c_str());
        const bool hadDest = Storage.exists(dest.c_str());
        if (hadDest && !Storage.rename(dest.c_str(), bak.c_str())) {
          Storage.remove(tmp.c_str());
          return -1;
        }
        if (!Storage.rename(tmp.c_str(), dest.c_str())) {
          Storage.remove(tmp.c_str());
          if (hadDest && !Storage.rename(bak.c_str(), dest.c_str())) {
            LOG_ERR("PEVT", "restore of %s failed", dest.c_str());
          }
          return -1;
        }
        Storage.remove(bak.c_str());
      } else {
        Storage.remove(tmp.c_str());
      }
      return st;
    }
    String response;
    return pluginhttp::request(
        nullptr, drainSubstituted(handler->req.url, tok, config, meta, vars, ts, id), handler->req.method,
        drainSubstituted(handler->req.body, tok, config, meta, vars, ts, id), headers, response, MAX_EVENT_RESPONSE);
  };

  int status = run(token);
  // A password-grant token expires; on 401/403 mint a fresh one and retry once.
  if ((status == 401 || status == 403) && mf.hasPasswordGrant()) {
    std::string minted;
    // Same template vocabulary as the delivery request (e.g. a {cfg.*} client
    // secret in an auth header).
    pluginhttp::Headers authHeaders;
    authHeaders.reserve(mf.authReq.headers.size());
    for (const auto& h : mf.authReq.headers) {
      authHeaders.emplace_back(h.first, drainSubstituted(h.second, token, config, meta, vars, ts, id));
    }
    if (pluginhttp::mintPasswordToken(nullptr, drainSubstituted(mf.authReq.url, token, config, meta, vars, ts, id),
                                      mf.authReq.method,
                                      drainSubstituted(mf.authReq.body, token, config, meta, vars, ts, id), authHeaders,
                                      mf.authTokenPath, minted)) {
      pluginhttp::saveTokenToFile(mf.tokenFile, mf.tokenPath, minted);
      token = minted;
      status = run(token);
    }
  }
  if (status < 200 || status >= 300) return false;

  if (renderer && !handler->toast.empty()) {
    GUI.drawPopup(*renderer, drainSubstituted(handler->toast, token, config, meta, vars, ts, id).c_str());
  }
  return true;
}

}  // namespace

void drain(GfxRenderer* renderer, const size_t maxEvents) {
  size_t budget = maxEvents;
  for (const auto& sub : subscribers) {
    if (budget == 0) break;
    if (sub.name[0] == '\0') continue;
    const std::string path = outboxPath(sub);
    if (!Storage.exists(path.c_str())) continue;

    std::string raw;
    if (!Storage.readFileToString("PEVT", path, MAX_OUTBOX_BYTES + MAX_EVENT_LINE, raw)) continue;

    DrainManifest mf;
    // Unreadable or malformed right now (SD fault, a half-written manifest):
    // keep the queue for the next drain.
    if (!loadDrainManifest(sub, mf)) continue;
    if (mf.handlers.empty()) {
      // Subscribed but no runnable handlers (JS-only consumer, or manifest
      // edited away): the queue would never advance, so clear it.
      Storage.remove(path.c_str());
      continue;
    }
    std::string token;
    pluginhttp::Headers config;
    pluginhttp::loadTokenFromFile(mf.tokenFile, mf.tokenPath, token);
    pluginhttp::loadConfigFile(mf.configFile, config);
    // Pre-build the substitution patterns (see drainSubstituted).
    for (auto& kv : config) kv.first = "{cfg." + kv.first + "}";

    // Walk lines; stop at the first failure so order is preserved.
    size_t pos = 0;
    bool stalled = false;
    while (pos < raw.size() && budget > 0 && !stalled) {
      size_t nl = raw.find('\n', pos);
      if (nl == std::string::npos) nl = raw.size();
      const std::string lineText = raw.substr(pos, nl - pos);
      if (!lineText.empty()) {
        if (deliverLine(mf, lineText, token, config, renderer)) {
          budget--;
        } else {
          stalled = true;
          break;
        }
      }
      pos = nl + 1;
    }

    if (pos >= raw.size() && !stalled) {
      Storage.remove(path.c_str());
    } else if (pos > 0) {
      // Rewrite the unprocessed tail so delivered events are not replayed.
      // writeFile keeps the old outbox if the rewrite fails: replaying delivered
      // events beats losing undelivered ones.
      if (!Storage.writeFile(path.c_str(), String(raw.c_str() + pos))) {
        LOG_ERR("PEVT", "%s: outbox rewrite failed", sub.name);
      }
    }
    LOG_DBG("PEVT", "%s: drained (stalled=%d)", sub.name, stalled);
  }
}

}  // namespace pluginevents
