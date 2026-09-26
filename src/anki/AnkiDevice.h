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
namespace ankidevice {

const std::string& deviceId();
std::string nextClientId();
int64_t nowEpoch();

}  // namespace ankidevice
