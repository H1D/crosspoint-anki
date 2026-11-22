#pragma once

#include <cstdint>
#include <string>

// Card audio on boards with a speaker (HalAudio): plays the media files the
// sync stored on the SD card (lib/AnkiClient/AnkiMedia.h), .mp3 or .wav.
// Call from the main loop task. On other boards nothing plays.
namespace ankiaudio {

// True on boards with a speaker the reader can play card sounds on.
bool available();
// Starts one sound of an account, replacing what is playing. False when the
// board has no speaker, the file is not on the card or it cannot be decoded.
bool play(uint32_t accountId, const std::string& name);
bool isPlaying();
// Stops, frees the decoder and puts the codec in standby.
void stop();

}  // namespace ankiaudio
