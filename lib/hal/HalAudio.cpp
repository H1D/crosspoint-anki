#include "HalAudio.h"

#if HAL_AUDIO_AVAILABLE

#include <AudioManager.h>
#include <Logging.h>
#include <Wire.h>

namespace {

// ES8311 on the board's I2C bus (SDA41/SCL42, 0x18) with the NS4150B enable on
// GPIO39, from the vendor's board_cfg.txt ("S3_ePaper_3_97"). MCLK (GPIO13)
// stays unused: the SDK clocks the codec from BCLK. The pinned SDK profile
// still says NO_AUDIO; the assert below fails when BoardConfig carries it.
constexpr BoardConfig::AudioConfig WS397_AUDIO = {BoardConfig::AudioOutput::I2sEs8311,
                                                  14,                           // bclk
                                                  47,                           // lrclk
                                                  48,                           // dout
                                                  BoardConfig::PIN_UNASSIGNED,  // mclk
                                                  BoardConfig::PIN_UNASSIGNED,  // enable: no codec rail pin
                                                  true,
                                                  39,                            // ampEnable
                                                  41,                            // codecSda
                                                  42,                            // codecScl
                                                  0x18,                          // codecAddr
                                                  BoardConfig::PIN_UNASSIGNED};  // buzzer

static_assert(BoardConfig::WS_EPAPER_397.audio.output == BoardConfig::AudioOutput::None,
              "The SDK profile has audio now: remove WS397_AUDIO and -DFREEINK_CAP_AUDIO=1");

// The SDK maps 100 to +16 dB of digital gain (register 0xCF), which can clip
// speech recorded near full scale. 92 is about 0 dB. Raise it if a unit is too
// quiet.
constexpr uint8_t VOLUME_PERCENT = 92;

// Measured on one unit with a stepped tone and a microphone: after a wake
// from standby the first 150 to 160 ms of a stream are lost (90 to 100 ms of
// an MP3). 200 ms of silence in front still lost up to 25 ms, 400 ms lost
// nothing, also during a screen refresh. With the codec awake nothing is
// lost; the silence is given then too, to keep one rule.
constexpr uint32_t LEAD_IN_MS = 400;

AudioManager speaker;
bool codecAwake = false;

struct RegVal {
  uint8_t reg;
  uint8_t val;
};

// es8311_suspend() from Espressif's esp_codec_dev. This board has no codec
// rail to cut, so without it the DAC stays powered through deep sleep. Every
// register the SDK needs for playback is written again by its begin().
constexpr RegVal ES8311_STANDBY[] = {{0x32, 0x00}, {0x17, 0x00}, {0x0E, 0xFF}, {0x12, 0x02}, {0x14, 0x00},
                                     {0x0D, 0xFA}, {0x15, 0x00}, {0x02, 0x10}, {0x00, 0x00}, {0x00, 0x1F},
                                     {0x01, 0x30}, {0x01, 0x00}, {0x45, 0x00}, {0x0D, 0xFC}, {0x02, 0x00}};

}  // namespace

bool HalAudio::available() { return true; }

uint32_t HalAudio::leadInMs() { return LEAD_IN_MS; }

bool HalAudio::play(const Source& source) {
  BoardConfig::ACTIVE.audio = WS397_AUDIO;
  if (!speaker.begin()) {
    LOG_ERR("AUDIO", "Codec did not answer");
    return false;
  }
  codecAwake = true;
  speaker.setVolume(VOLUME_PERCENT);
  return speaker.play({source.read, source.seek}, false);
}

void HalAudio::stop() { speaker.stop(); }

bool HalAudio::isPlaying() { return speaker.isPlaying(); }

void HalAudio::powerDown() {
  if (!codecAwake) return;
  codecAwake = false;
  speaker.powerDown();
  for (const RegVal& step : ES8311_STANDBY) {
    Wire.beginTransmission(WS397_AUDIO.codecAddr);
    Wire.write(step.reg);
    Wire.write(step.val);
    if (Wire.endTransmission() != 0) {
      LOG_ERR("AUDIO", "Codec standby failed at 0x%02X", step.reg);
      return;
    }
  }
}

#else

bool HalAudio::available() { return false; }
uint32_t HalAudio::leadInMs() { return 0; }
bool HalAudio::play(const Source&) { return false; }
void HalAudio::stop() {}
bool HalAudio::isPlaying() { return false; }
void HalAudio::powerDown() {}

#endif
