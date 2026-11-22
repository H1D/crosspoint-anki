#include "AnkiAudio.h"

#include <BoardConfig.h>

// Same condition as HAL_AUDIO_AVAILABLE. It is repeated so that builds without
// HalAudio.h (the simulator has its own HAL headers) do not include it.
#if FREEINK_CAP_AUDIO && FREEINK_DEVICE_WS397

#include <AnkiMedia.h>
#include <HalAudio.h>
#include <HalStorage.h>
#include <Logging.h>
#include <Memory.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <mp3dec.h>

#include <algorithm>
#include <cstring>
#include <memory>

namespace {

constexpr size_t WAV_HEADER_BYTES = 44;

// One media file as the WAV stream HalAudio plays: a WAV header, a lead-in of
// silence, then the samples of the file. An .mp3 file is decoded one frame at
// a time; a .wav file must hold 16-bit PCM. The data length in the header is
// 0, which HalAudio reads as "until the stream ends".
//
// Heap while a sound plays: this object (about 9 KB, mostly the two buffers)
// plus about 24 KB that the Helix decoder allocates for itself. Both are freed
// by ankiaudio::stop().
class SoundStream {
 public:
  ~SoundStream() {
    if (decoder) MP3FreeDecoder(decoder);
  }

  // `leadInMs` of silence come before the sound.
  bool open(const std::string& path, const uint32_t leadInMs) {
    if (!Storage.openFileForRead("ANKI", path, file)) return false;
    if (path.size() > 4 && path.compare(path.size() - 4, 4, ".wav") == 0) {
      if (!openWav()) return false;
    } else {
      decoder = MP3InitDecoder();
      if (!decoder) {
        LOG_ERR("ANKI", "OOM: MP3 decoder");
        return false;
      }
      skipId3v2();
      // The first frame gives the format for the header; its samples stay
      // buffered for the first read.
      if (!decodeFrame()) return false;
    }
    leadIn = static_cast<size_t>(sampleRate) * leadInMs / 1000 * channels * sizeof(int16_t);
    writeWavHeader();
    return true;
  }

  // Runs in the playback task.
  int read(uint8_t* dst, const size_t len) {
    size_t done = 0;
    while (done < len) {
      size_t n = len - done;
      if (position < WAV_HEADER_BYTES) {
        n = std::min(n, WAV_HEADER_BYTES - position);
        memcpy(dst + done, header + position, n);
      } else if (leadIn > 0) {
        n = std::min(n, leadIn);
        memset(dst + done, 0, n);
        leadIn -= n;
      } else {
        if (pcmOffset == pcmBytes && !nextSamples()) break;
        n = std::min(n, pcmBytes - pcmOffset);
        memcpy(dst + done, reinterpret_cast<const uint8_t*>(pcm) + pcmOffset, n);
        pcmOffset += n;
      }
      position += n;
      done += n;
    }
    if (done == 0) {
      LOG_DBG("ANKI", "Audio task stack left: %u bytes", static_cast<unsigned>(uxTaskGetStackHighWaterMark(nullptr)));
    }
    return static_cast<int>(done);
  }

  // The player only seeks inside the header while it parses it, then to the
  // first sample. The stream cannot go back further than that.
  bool seek(const size_t target) {
    if (target > WAV_HEADER_BYTES || position > WAV_HEADER_BYTES) return false;
    position = target;
    return true;
  }

 private:
  // Reads the format of a 16-bit PCM WAV file and leaves the file at its
  // first sample.
  bool openWav() {
    uint8_t buf[16];
    if (file.read(buf, 12) != 12 || memcmp(buf, "RIFF", 4) != 0 || memcmp(buf + 8, "WAVE", 4) != 0) return false;
    const auto le32 = [](const uint8_t* p) {
      return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) | (static_cast<uint32_t>(p[2]) << 16) |
             (static_cast<uint32_t>(p[3]) << 24);
    };
    size_t next = 12;  // file offset of the next chunk
    while (file.seek(next) && file.read(buf, 8) == 8) {
      const uint32_t size = le32(buf + 4);
      if (memcmp(buf, "data", 4) == 0) {
        wavLeft = size;
        return sampleRate != 0;  // the format came first
      }
      if (memcmp(buf, "fmt ", 4) == 0) {
        if (size < 16 || file.read(buf, 16) != 16) return false;
        const bool pcm16 = buf[0] == 1 && buf[1] == 0 && buf[14] == 16 && buf[15] == 0;
        channels = static_cast<uint16_t>(buf[2] | (buf[3] << 8));
        sampleRate = le32(buf + 4);
        if (!pcm16 || channels < 1 || channels > 2 || sampleRate == 0) return false;
      }
      next += 8 + size + (size & 1);  // chunks are word-aligned
    }
    return false;
  }

  // Fills `pcm` with the next samples of the file. False at its end.
  bool nextSamples() {
    if (decoder) return decodeFrame();
    pcmOffset = 0;
    const int n = file.read(pcm, std::min(sizeof(pcm), wavLeft));
    pcmBytes = n > 0 ? static_cast<size_t>(n) & ~static_cast<size_t>(1) : 0;
    wavLeft -= pcmBytes;
    return pcmBytes > 0;
  }

  // Skips an ID3v2 tag: cover art inside one can hold bytes that look like a
  // frame header.
  void skipId3v2() {
    uint8_t tag[10];
    if (file.read(tag, sizeof(tag)) == static_cast<int>(sizeof(tag)) && memcmp(tag, "ID3", 3) == 0) {
      const size_t size = (static_cast<size_t>(tag[6] & 0x7F) << 21) | (static_cast<size_t>(tag[7] & 0x7F) << 14) |
                          (static_cast<size_t>(tag[8] & 0x7F) << 7) | (tag[9] & 0x7F);
      file.seek(sizeof(tag) + size);
    } else {
      file.seek(0);
    }
  }

  // Tops the input buffer up so that a whole frame (at most MAINBUF_SIZE
  // bytes) is always available to the decoder.
  void refill() {
    if (atEnd || inFill - inPos >= MAINBUF_SIZE) return;
    memmove(in, in + inPos, inFill - inPos);
    inFill -= inPos;
    inPos = 0;
    const int n = file.read(in + inFill, sizeof(in) - inFill);
    if (n <= 0)
      atEnd = true;
    else
      inFill += static_cast<size_t>(n);
  }

  // Decodes the next frame into `pcm`. False at the end of the file, or when
  // the format changes in the middle of it. Every pass uses up input, so the
  // loop ends with the file.
  bool decodeFrame() {
    pcmBytes = 0;
    pcmOffset = 0;
    for (;;) {
      refill();
      const int left = static_cast<int>(inFill - inPos);
      if (left <= 0) return false;
      const int sync = MP3FindSyncWord(in + inPos, left);
      if (sync < 0) {
        inPos = inFill;
        continue;
      }
      inPos += static_cast<size_t>(sync);
      unsigned char* frame = in + inPos;
      int remaining = static_cast<int>(inFill - inPos);
      const int err = MP3Decode(decoder, &frame, &remaining, pcm, 0);
      if (err == ERR_MP3_INDATA_UNDERFLOW && atEnd) return false;  // the file ends inside a frame
      if (err != ERR_MP3_NONE && err != ERR_MP3_MAINDATA_UNDERFLOW) {
        inPos++;  // not a frame: look for the next sync word
        continue;
      }
      inPos = inFill - static_cast<size_t>(remaining);
      if (err == ERR_MP3_MAINDATA_UNDERFLOW) continue;  // frame needs data from one that was skipped

      MP3FrameInfo info;
      MP3GetLastFrameInfo(decoder, &info);
      if (sampleRate == 0) {
        sampleRate = static_cast<uint32_t>(info.samprate);
        channels = static_cast<uint16_t>(info.nChans);
      } else if (sampleRate != static_cast<uint32_t>(info.samprate) || channels != info.nChans) {
        return false;
      }
      pcmBytes = static_cast<size_t>(info.outputSamps) * sizeof(int16_t);
      if (pcmBytes > 0) return true;
    }
  }

  void writeWavHeader() {
    const auto put16 = [this](const size_t at, const uint16_t v) {
      header[at] = static_cast<uint8_t>(v);
      header[at + 1] = static_cast<uint8_t>(v >> 8);
    };
    const auto put32 = [&put16](const size_t at, const uint32_t v) {
      put16(at, static_cast<uint16_t>(v));
      put16(at + 2, static_cast<uint16_t>(v >> 16));
    };
    const uint16_t frameBytes = static_cast<uint16_t>(channels * sizeof(int16_t));
    memcpy(header, "RIFF\0\0\0\0WAVEfmt ", 16);
    put32(16, 16);  // fmt chunk size
    put16(20, 1);   // PCM
    put16(22, channels);
    put32(24, sampleRate);
    put32(28, sampleRate * frameBytes);
    put16(32, frameBytes);
    put16(34, 16);  // bits per sample
    memcpy(header + 36, "data\0\0\0\0", 8);
  }

  HalFile file;
  HMP3Decoder decoder = nullptr;
  uint32_t sampleRate = 0;
  uint16_t channels = 0;
  size_t position = 0;  // read position in the stream: header, lead-in, then samples
  size_t leadIn = 0;    // bytes of silence still to give
  size_t wavLeft = 0;   // .wav only: sample bytes still in the file
  uint8_t header[WAV_HEADER_BYTES] = {};
  uint8_t in[2 * MAINBUF_SIZE];
  size_t inPos = 0;
  size_t inFill = 0;
  bool atEnd = false;
  int16_t pcm[MAX_NGRAN * MAX_NCHAN * MAX_NSAMP];
  size_t pcmBytes = 0;
  size_t pcmOffset = 0;
};

std::unique_ptr<SoundStream> stream;

// Frees the stream after playback was asked to stop. The SDK waits a bounded
// time for its task, so the task can still be reading: then the stream is
// leaked instead of freed, and false tells the caller not to start a sound.
bool releaseStream() {
  if (HalAudio::isPlaying()) {
    LOG_ERR("ANKI", "Playback did not stop; keeping its stream");
    (void)stream.release();
    return false;
  }
  stream.reset();
  return true;
}

}  // namespace

namespace ankiaudio {

bool available() { return HalAudio::available(); }

bool play(const uint32_t accountId, const std::string& name) {
  HalAudio::stop();
  if (!releaseStream()) return false;
  stream = makeUniqueNoThrow<SoundStream>();
  if (!stream) {
    LOG_ERR("ANKI", "OOM: audio stream");
    return false;
  }
  SoundStream* const s = stream.get();
  const bool started = s->open(ankimedia::path(accountId, name), HalAudio::leadInMs()) &&
                       HalAudio::play({[s](uint8_t* dst, const size_t len) { return s->read(dst, len); },
                                       [s](const size_t pos) { return s->seek(pos); }});
  if (!started) {
    LOG_DBG("ANKI", "Cannot play %s", name.c_str());
    stream.reset();
  }
  return started;
}

bool isPlaying() { return HalAudio::isPlaying(); }

void stop() {
  HalAudio::powerDown();
  releaseStream();
}

}  // namespace ankiaudio

#else

namespace ankiaudio {

bool available() { return false; }
bool play(uint32_t, const std::string&) { return false; }
bool isPlaying() { return false; }
void stop() {}

}  // namespace ankiaudio

#endif
