#pragma once

#include <cstdint>
#include <string>
#include <vector>

// Card audio. On boards with a speaker the sync engine appends a
// "[sound:name]" marker per playable file to the text of the card side that
// plays it, and the review screen plays a side's markers when that side is
// shown. AnkiDo names the side in "q_sounds" and "a_sounds"; an older server
// only lists "media", and then a sound goes to the answer (to the question
// when it shows no text). The files are cached on the SD card by
// AnkiSyncEngine::fetchMedia().
namespace ankimedia {

// Files over this size are not downloaded.
constexpr size_t MAX_BYTES = 2 * 1024 * 1024;

// True for the formats the reader decodes: .mp3 and .wav.
bool isPlayable(const std::string& name);

// SD path of a media file. The name comes from the server, so it is hashed
// and never becomes part of the path; only a known extension is kept.
std::string path(uint32_t accountId, const std::string& name);

// "[sound:a.mp3][sound:b.wav]" for the playable names that `text` does not
// already mark.
std::string markers(const std::vector<std::string>& names, const std::string& text);

// Appends the names of the "[sound:name]" markers in `text`.
void soundNames(const std::string& text, std::vector<std::string>& out);

// Percent-encodes a file name for use as one URL path segment.
std::string urlEncode(const std::string& name);

}  // namespace ankimedia
