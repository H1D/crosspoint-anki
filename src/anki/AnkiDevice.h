#pragma once

#include <cstdint>
#include <string>

// Device-side identity and time for the Anki client:
//  - deviceId(): short hex id derived from the eFuse MAC; stable per device.
//  - nextClientId(): "<deviceId>-<n>" with a counter persisted in
//    /.crosspoint/anki/state.json, so every review/note ever sent from this
//    device has a unique idempotency key (AnkiDo journals them for 90 days).
//  - reserveClientIds(n) / clientId(counter): the same ids for a batch, one
//    counter write for all of them (a looked-up-words send makes hundreds).
//  - nowEpoch(): UTC epoch seconds from the RTC, 0 when the clock is not set
//    (then reviews are sent without answered_at and dated at receipt).
//  - wallEpoch(): the best wall clock for placing days on the dashboard and
//    anchoring AnkiConnect's days: the RTC when set, else the trusted system
//    clock (SNTP on Wi-Fi joins, floored in NVS), else 0. Never journaled:
//    a drifted answered_at could be rejected and dropped.
namespace ankidevice {

const std::string& deviceId();
std::string nextClientId();
// Reserves `count` consecutive ids with one persisted counter bump and returns
// the first counter value; clientId(first + i) is the i-th id.
uint32_t reserveClientIds(uint32_t count);
std::string clientId(uint32_t counterValue);
int64_t nowEpoch();
int64_t wallEpoch();

}  // namespace ankidevice
