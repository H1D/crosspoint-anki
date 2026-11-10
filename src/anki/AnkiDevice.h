#pragma once

#include <cstdint>
#include <string>

// Device-side identity and time for the Anki client:
//  - deviceId(): short hex id derived from the eFuse MAC; stable per device.
//  - nextClientId(): "<deviceId>-<n>" with a counter persisted in
//    /.crosspoint/anki/state.json, so every review/note ever sent from this
//    device has a unique idempotency key (AnkiDo journals them for 90 days).
//  - nowEpoch(): UTC epoch seconds from the RTC, 0 when the clock is not set
//    (then reviews are sent without answered_at and dated at receipt).
//  - wallEpoch(): the best wall clock for placing days on the dashboard and
//    anchoring AnkiConnect's days: the RTC when set, else the trusted system
//    clock (SNTP on Wi-Fi joins, floored in NVS), else 0. Never journaled:
//    a drifted answered_at could be rejected and dropped.
namespace ankidevice {

const std::string& deviceId();
std::string nextClientId();
int64_t nowEpoch();
int64_t wallEpoch();

}  // namespace ankidevice
