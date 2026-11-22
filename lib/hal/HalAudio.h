#pragma once

#include <BoardConfig.h>

#include <cstddef>
#include <cstdint>
#include <functional>

// Speaker output is wired and tested on the Waveshare 3.97 only (ES8311 codec,
// NS4150B amplifier). The Metalio module also has FREEINK_CAP_AUDIO, but it
// takes 16 kHz streams only and nothing in the reader has been tried on it.
#define HAL_AUDIO_AVAILABLE (FREEINK_CAP_AUDIO && FREEINK_DEVICE_WS397)

// Speaker playback through the SDK AudioManager. Call every function from the
// main loop task. Without HAL_AUDIO_AVAILABLE nothing plays and available()
// is false.
class HalAudio {
 public:
  // A 16-bit PCM WAV stream, mono or stereo, 8 to 48 kHz. A data chunk length
  // of 0 means "until read returns 0". Both callbacks must return promptly;
  // read runs in the playback task.
  struct Source {
    // Copies up to len bytes; returns the count, 0 at the end, < 0 on error.
    std::function<int(uint8_t* dst, size_t len)> read;
    // Absolute position from the start of the stream.
    std::function<bool(size_t pos)> seek;
  };

  static bool available();
  // Milliseconds of silence a stream should start with. After the codec wakes
  // the output loses the first part of what it is given.
  static uint32_t leadInMs();
  // Stops what is playing, then starts `source`. False when it cannot start.
  static bool play(const Source& source);
  // Returns once the playback task has ended; the source is not used after.
  static void stop();
  static bool isPlaying();
  // Stops and puts the codec in standby. play() wakes it again.
  static void powerDown();
};
