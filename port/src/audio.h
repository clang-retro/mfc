// audio.h - the layer's audio: sample formats and the converter between
// them (shared by the ACM and the wave mapper), and the sound devices
// (ALSA on Linux, Core Audio on macOS, or a loopback device).
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#ifndef _WINPORT_AUDIO_H
#define _WINPORT_AUDIO_H

#include <mmsystem.h>
#include <mmreg.h>

#include <stdint.h>

#include <deque>
#include <string>
#include <vector>

namespace winport {
namespace audio {

// A wave format the layer reads and writes.
enum Kind { kNone, kPcm8, kPcm16, kPcm24, kPcm32, kFloat, kMuLaw, kALaw, kImaAdpcm };
struct Format {
  Kind kind = kNone;
  int channels = 0;
  int rate = 0;
  int blockAlign = 0;       // bytes in a frame, or in an ADPCM block
  int framesPerBlock = 1;   // frames in a block (ADPCM), else 1
  int avgBytesPerSec = 0;
};

// What a WAVEFORMATEX describes, if the layer has it: kind is kNone if not.
Format Parse(const WAVEFORMATEX* wfx);
// A format of a kind, with the usual block size.
Format Make(Kind k, int channels, int rate);
// The WAVEFORMATEX (with its extra bytes) for a format.
std::vector<BYTE> Describe(const Format& f);
// The usual IMA ADPCM block size for a rate and channels.
int ImaBlockAlign(int rate, int channels);
int ImaFramesPerBlock(int blockAlign, int channels);

// Converts a stream of one format into another: sample size, channels,
// rate, and encoding. Frames on the way are 16-bit.
class Converter {
 public:
  Converter(const Format& from, const Format& to);
  // Converts what fits: takes whole frames (or blocks) of src, writes
  // whole frames (or blocks) to dst, and keeps what is left over for the
  // next call. end pads a last partial ADPCM block with silence.
  void Convert(const BYTE* src, size_t srcLen, BYTE* dst, size_t dstLen, bool end, size_t* used,
               size_t* produced);
  // The bytes out for n bytes in (or the reverse), at most.
  size_t OutputSize(size_t in) const;
  size_t InputSize(size_t out) const;
  void Reset();
  const Format& from() const { return from_; }
  const Format& to() const { return to_; }

 private:
  void Decode(const BYTE* src, size_t units);
  size_t Encode(BYTE* dst, size_t dstLen, bool end);
  Format from_, to_;
  std::vector<int16_t> decoded_;  // frames from the source, 16-bit
  std::deque<int16_t> ready_;     // frames at the destination's rate and channels
  // The resampler: where it is between two source frames.
  double pos_ = 0;
  std::vector<int16_t> last_;
  bool haveLast_ = false;
  // The ADPCM encoder's state per channel.
  std::vector<int> predictor_, index_;
};

// G.711 and IMA ADPCM, one sample (or block) at a time.
uint8_t MuLawEncode(int16_t s);
int16_t MuLawDecode(uint8_t u);
uint8_t ALawEncode(int16_t s);
int16_t ALawDecode(uint8_t a);

// A sound device: 16-bit frames out or in, at the rate and channels it
// was opened with. Writes and reads block until done.
class Device {
 public:
  virtual ~Device() {}
  virtual bool Write(const int16_t* frames, size_t n) = 0;
  virtual size_t Read(int16_t* frames, size_t n) = 0;
  // Frames written and not yet heard.
  virtual size_t Delay() = 0;
  virtual void Pause(bool on) = 0;
  // Throws away what is waiting to be played or read.
  virtual void Drop() = 0;
  virtual void Start() {}
};

// The devices there are (0 or 1 each way), their names, and opening one.
int DeviceCount(bool capture);
std::string DeviceName(bool capture);
Device* OpenDevice(bool capture, int rate, int channels);

}  // namespace audio
}  // namespace winport

#endif
