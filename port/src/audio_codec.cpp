// audio_codec.cpp - the sample formats the layer reads and writes (PCM of
// 8, 16, 24 and 32 bits and floats, G.711 mu-law and A-law, IMA ADPCM)
// and the converter between any two of them, with channels and rate.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#include "audio.h"

#include <math.h>
#include <string.h>

#include <algorithm>

namespace winport {
namespace audio {

namespace {

const int kImaSteps[89] = {
    7,     8,     9,     10,    11,    12,    13,    14,    16,    17,    19,    21,    23,    25,    28,
    31,    34,    37,    41,    45,    50,    55,    60,    66,    73,    80,    88,    97,    107,   118,
    130,   143,   157,   173,   190,   209,   230,   253,   279,   307,   337,   371,   408,   449,   494,
    544,   598,   658,   724,   796,   876,   963,   1060,  1166,  1282,  1411,  1552,  1707,  1878,  2066,
    2272,  2499,  2749,  3024,  3327,  3660,  4026,  4428,  4871,  5358,  5894,  6484,  7132,  7845,  8630,
    9493,  10442, 11487, 12635, 13899, 15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794, 32767};
const int kImaIndex[8] = {-1, -1, -1, -1, 2, 4, 6, 8};

inline int16_t Clamp16(int v) { return (int16_t)(v < -32768 ? -32768 : v > 32767 ? 32767 : v); }

// One ADPCM nibble applied to the predictor.
inline void ImaStep(int nibble, int* pred, int* index) {
  int step = kImaSteps[*index];
  int diff = step >> 3;
  if (nibble & 4) diff += step;
  if (nibble & 2) diff += step >> 1;
  if (nibble & 1) diff += step >> 2;
  *pred = nibble & 8 ? *pred - diff : *pred + diff;
  *pred = Clamp16(*pred);
  *index += kImaIndex[nibble & 7];
  *index = *index < 0 ? 0 : *index > 88 ? 88 : *index;
}

inline int ImaNibble(int sample, int* pred, int* index) {
  int step = kImaSteps[*index];
  int diff = sample - *pred;
  int n = 0;
  if (diff < 0) {
    n = 8;
    diff = -diff;
  }
  if (diff >= step) n |= 4, diff -= step;
  step >>= 1;
  if (diff >= step) n |= 2, diff -= step;
  step >>= 1;
  if (diff >= step) n |= 1;
  ImaStep(n, pred, index);  // the decoder's view, so both stay together
  return n;
}

// The bytes one unit of a format takes, and the frames in it.
size_t UnitBytes(const Format& f) { return (size_t)f.blockAlign; }
size_t UnitFrames(const Format& f) { return f.kind == kImaAdpcm ? (size_t)f.framesPerBlock : 1; }

int BytesPerSample(Kind k) {
  switch (k) {
    case kPcm8:
    case kMuLaw:
    case kALaw: return 1;
    case kPcm16: return 2;
    case kPcm24: return 3;
    case kPcm32:
    case kFloat: return 4;
    default: return 0;
  }
}

}  // namespace

uint8_t MuLawEncode(int16_t s16) {
  const int kBias = 0x84, kClip = 32635;
  int s = s16;
  int sign = 0;
  if (s < 0) {
    sign = 0x80;
    s = -s;
  }
  if (s > kClip) s = kClip;
  s += kBias;
  int exponent = 7;
  for (int mask = 0x4000; exponent > 0 && !(s & mask); mask >>= 1) exponent--;
  int mantissa = (s >> (exponent + 3)) & 0x0F;
  return (uint8_t)~(sign | (exponent << 4) | mantissa);
}

int16_t MuLawDecode(uint8_t u) {
  u = (uint8_t)~u;
  int exponent = (u >> 4) & 7, mantissa = u & 0x0F;
  int s = (((mantissa << 3) + 0x84) << exponent) - 0x84;
  return (int16_t)((u & 0x80) ? -s : s);
}

uint8_t ALawEncode(int16_t s16) {
  static const int kSegEnd[8] = {0x1F, 0x3F, 0x7F, 0xFF, 0x1FF, 0x3FF, 0x7FF, 0xFFF};
  int pcm = s16 >> 3;
  int mask;
  if (pcm >= 0) {
    mask = 0xD5;
  } else {
    mask = 0x55;
    pcm = -pcm - 1;
  }
  int seg = 0;
  while (seg < 8 && pcm > kSegEnd[seg]) seg++;
  if (seg >= 8) return (uint8_t)(0x7F ^ mask);
  int a = seg << 4;
  a |= seg < 2 ? (pcm >> 1) & 0x0F : (pcm >> seg) & 0x0F;
  return (uint8_t)(a ^ mask);
}

int16_t ALawDecode(uint8_t a) {
  a ^= 0x55;
  int t = (a & 0x0F) << 4;
  int seg = (a & 0x70) >> 4;
  if (seg == 0)
    t += 8;
  else if (seg == 1)
    t += 0x108;
  else {
    t += 0x108;
    t <<= seg - 1;
  }
  return (int16_t)((a & 0x80) ? t : -t);
}

int ImaBlockAlign(int rate, int channels) {
  int per = rate <= 11025 ? 256 : rate <= 22050 ? 512 : 1024;
  return per * channels;
}

int ImaFramesPerBlock(int blockAlign, int channels) {
  return (blockAlign - 4 * channels) * 8 / (4 * channels) + 1;
}

Format Parse(const WAVEFORMATEX* w) {
  Format f;
  if (!w || w->nChannels == 0 || w->nChannels > 8 || w->nSamplesPerSec == 0 || w->nSamplesPerSec > 384000) return f;
  WORD tag = w->wFormatTag;
  WORD bits = w->wBitsPerSample;
  if (tag == WAVE_FORMAT_EXTENSIBLE && w->cbSize >= 22) {
    const WAVEFORMATEXTENSIBLE* x = (const WAVEFORMATEXTENSIBLE*)w;
    // The subformat GUIDs share their tail; the first part is the tag.
    tag = (WORD)x->SubFormat.Data1;
  }
  f.channels = w->nChannels;
  f.rate = (int)w->nSamplesPerSec;
  switch (tag) {
    case WAVE_FORMAT_PCM:
      f.kind = bits == 8 ? kPcm8 : bits == 16 ? kPcm16 : bits == 24 ? kPcm24 : bits == 32 ? kPcm32 : kNone;
      break;
    case WAVE_FORMAT_IEEE_FLOAT: f.kind = bits == 32 ? kFloat : kNone; break;
    case WAVE_FORMAT_MULAW: f.kind = bits == 8 ? kMuLaw : kNone; break;
    case WAVE_FORMAT_ALAW: f.kind = bits == 8 ? kALaw : kNone; break;
    case WAVE_FORMAT_IMA_ADPCM:
      f.kind = bits == 4 ? kImaAdpcm : kNone;
      break;
    default: break;
  }
  if (f.kind == kNone) return f;
  if (f.kind == kImaAdpcm) {
    f.blockAlign = w->nBlockAlign;
    if (f.blockAlign < 4 * f.channels + 4 * f.channels || (f.blockAlign - 4 * f.channels) % (4 * f.channels)) {
      f.kind = kNone;
      return f;
    }
    f.framesPerBlock = ImaFramesPerBlock(f.blockAlign, f.channels);
    if (w->cbSize >= 2 && ((const IMAADPCMWAVEFORMAT*)w)->wSamplesPerBlock != f.framesPerBlock) {
      f.kind = kNone;
      return f;
    }
    f.avgBytesPerSec = (int)((long long)f.rate * f.blockAlign / f.framesPerBlock);
  } else {
    f.blockAlign = BytesPerSample(f.kind) * f.channels;
    if (w->nBlockAlign != f.blockAlign) {
      f.kind = kNone;
      return f;
    }
    f.avgBytesPerSec = f.rate * f.blockAlign;
  }
  return f;
}

Format Make(Kind k, int channels, int rate) {
  Format f;
  f.kind = k;
  f.channels = channels;
  f.rate = rate;
  if (k == kImaAdpcm) {
    f.blockAlign = ImaBlockAlign(rate, channels);
    f.framesPerBlock = ImaFramesPerBlock(f.blockAlign, channels);
    f.avgBytesPerSec = (int)((long long)rate * f.blockAlign / f.framesPerBlock);
  } else {
    f.blockAlign = BytesPerSample(k) * channels;
    f.avgBytesPerSec = rate * f.blockAlign;
  }
  return f;
}

std::vector<BYTE> Describe(const Format& f) {
  std::vector<BYTE> b(f.kind == kImaAdpcm ? sizeof(IMAADPCMWAVEFORMAT) : sizeof(WAVEFORMATEX));
  WAVEFORMATEX* w = (WAVEFORMATEX*)b.data();
  static const WORD kTags[] = {0, WAVE_FORMAT_PCM, WAVE_FORMAT_PCM, WAVE_FORMAT_PCM, WAVE_FORMAT_PCM,
                               WAVE_FORMAT_IEEE_FLOAT, WAVE_FORMAT_MULAW, WAVE_FORMAT_ALAW, WAVE_FORMAT_IMA_ADPCM};
  w->wFormatTag = kTags[f.kind];
  w->nChannels = (WORD)f.channels;
  w->nSamplesPerSec = (DWORD)f.rate;
  w->nBlockAlign = (WORD)f.blockAlign;
  w->wBitsPerSample = (WORD)(f.kind == kImaAdpcm ? 4 : BytesPerSample(f.kind) * 8);
  w->nAvgBytesPerSec = (DWORD)(f.kind == kImaAdpcm ? (long long)f.rate * f.blockAlign / f.framesPerBlock
                                                   : (long long)f.rate * f.blockAlign);
  w->cbSize = 0;
  if (f.kind == kImaAdpcm) {
    w->cbSize = 2;
    ((IMAADPCMWAVEFORMAT*)w)->wSamplesPerBlock = (WORD)f.framesPerBlock;
  }
  return b;
}

Converter::Converter(const Format& from, const Format& to) : from_(from), to_(to) { Reset(); }

void Converter::Reset() {
  decoded_.clear();
  ready_.clear();
  pos_ = 0;
  haveLast_ = false;
  last_.assign(to_.channels, 0);
  predictor_.assign(to_.channels, 0);
  index_.assign(to_.channels, 0);
}

// Decodes whole units of the source into ready_, through channels and rate.
void Converter::Decode(const BYTE* src, size_t units) {
  int ch = from_.channels;
  size_t frames = units * UnitFrames(from_);
  decoded_.resize(frames * ch);
  int16_t* d = decoded_.data();
  if (from_.kind == kImaAdpcm) {
    size_t fpb = (size_t)from_.framesPerBlock;
    for (size_t b = 0; b < units; b++) {
      const BYTE* blk = src + b * from_.blockAlign;
      int16_t* out = d + b * fpb * ch;
      std::vector<int> pred(ch), idx(ch);
      for (int c = 0; c < ch; c++) {
        pred[c] = (int16_t)(blk[c * 4] | blk[c * 4 + 1] << 8);
        idx[c] = std::min<int>(blk[c * 4 + 2], 88);
        out[c] = (int16_t)pred[c];
      }
      const BYTE* p = blk + 4 * ch;
      size_t frame = 1;
      while (frame < fpb) {
        for (int c = 0; c < ch; c++) {
          for (int k = 0; k < 4; k++) {
            BYTE v = *p++;
            for (int half = 0; half < 2; half++) {
              int n = half ? v >> 4 : v & 0x0F;
              ImaStep(n, &pred[c], &idx[c]);
              size_t f = frame + k * 2 + half;
              if (f < fpb) out[f * ch + c] = (int16_t)pred[c];
            }
          }
        }
        frame += 8;
      }
    }
  } else {
    size_t n = frames * ch;
    for (size_t i = 0; i < n; i++) {
      switch (from_.kind) {
        case kPcm8: d[i] = (int16_t)((src[i] - 128) << 8); break;
        case kPcm16: d[i] = (int16_t)(src[2 * i] | src[2 * i + 1] << 8); break;
        case kPcm24: d[i] = (int16_t)(src[3 * i + 1] | src[3 * i + 2] << 8); break;
        case kPcm32: d[i] = (int16_t)(src[4 * i + 2] | src[4 * i + 3] << 8); break;
        case kFloat: {
          float v;
          memcpy(&v, src + 4 * i, 4);
          d[i] = Clamp16((int)lrintf(v * 32767.0f));
          break;
        }
        case kMuLaw: d[i] = MuLawDecode(src[i]); break;
        case kALaw: d[i] = ALawDecode(src[i]); break;
        default: d[i] = 0; break;
      }
    }
  }
  // Channels.
  int oc = to_.channels;
  std::vector<int16_t> mixed;
  const int16_t* in = d;
  if (oc != ch) {
    mixed.resize(frames * oc);
    for (size_t f = 0; f < frames; f++) {
      if (oc == 1) {
        int sum = 0;
        for (int c = 0; c < ch; c++) sum += d[f * ch + c];
        mixed[f] = (int16_t)(sum / ch);
      } else {
        for (int c = 0; c < oc; c++) mixed[f * oc + c] = d[f * ch + (c % ch)];
      }
    }
    in = mixed.data();
  }
  // Rate: straight through, or by linear interpolation.
  if (from_.rate == to_.rate) {
    ready_.insert(ready_.end(), in, in + frames * oc);
    return;
  }
  double step = (double)from_.rate / to_.rate;
  auto at = [&](long i, int c) -> int { return i < 0 ? last_[c] : in[(size_t)i * oc + c]; };
  // Frame -1 is the last of the previous call.
  long first = haveLast_ ? -1 : 0;
  double pos = haveLast_ ? pos_ - 1 : pos_;
  while (frames > 0 && pos + 1 <= (double)(frames - 1) + 1e-9) {
    long i = (long)floor(pos);
    if (i < first) i = first;
    double frac = pos - i;
    for (int c = 0; c < oc; c++) {
      int a = at(i, c), b = at(i + 1 > (long)frames - 1 ? (long)frames - 1 : i + 1, c);
      ready_.push_back(Clamp16((int)lrint(a + (b - a) * frac)));
    }
    pos += step;
  }
  if (frames > 0) {
    for (int c = 0; c < oc; c++) last_[c] = in[(frames - 1) * oc + c];
    // Where the next frame falls, counted from the new last frame (-1 next time... as index 0 of S).
    pos_ = pos - (double)(frames - 1);
    haveLast_ = true;
  }
}

// Encodes what is ready into dst: whole frames, or whole ADPCM blocks.
size_t Converter::Encode(BYTE* dst, size_t dstLen, bool end) {
  int ch = to_.channels;
  size_t out = 0;
  if (to_.kind == kImaAdpcm) {
    size_t fpb = (size_t)to_.framesPerBlock;
    while (out + to_.blockAlign <= dstLen) {
      size_t have = ready_.size() / ch;
      if (have < fpb && !(end && have > 0)) break;
      std::vector<int16_t> blockFrames(fpb * ch, 0);
      size_t take = std::min(have, fpb);
      for (size_t i = 0; i < take * ch; i++) blockFrames[i] = ready_[i];
      ready_.erase(ready_.begin(), ready_.begin() + take * ch);
      BYTE* blk = dst + out;
      memset(blk, 0, to_.blockAlign);
      for (int c = 0; c < ch; c++) {
        predictor_[c] = blockFrames[c];
        blk[c * 4] = (BYTE)(predictor_[c] & 0xFF);
        blk[c * 4 + 1] = (BYTE)((predictor_[c] >> 8) & 0xFF);
        blk[c * 4 + 2] = (BYTE)index_[c];
        blk[c * 4 + 3] = 0;
      }
      BYTE* p = blk + 4 * ch;
      for (size_t frame = 1; frame < fpb; frame += 8) {
        for (int c = 0; c < ch; c++) {
          for (int k = 0; k < 4; k++) {
            BYTE v = 0;
            for (int half = 0; half < 2; half++) {
              size_t f = frame + k * 2 + half;
              int s = f < fpb ? blockFrames[f * ch + c] : predictor_[c];
              v |= (BYTE)(ImaNibble(s, &predictor_[c], &index_[c]) << (half * 4));
            }
            *p++ = v;
          }
        }
      }
      out += to_.blockAlign;
    }
    return out;
  }
  size_t room = dstLen / to_.blockAlign;
  size_t frames = std::min(room, ready_.size() / ch);
  size_t n = frames * ch;
  BYTE* p = dst;
  for (size_t i = 0; i < n; i++) {
    int16_t s = ready_[i];
    switch (to_.kind) {
      case kPcm8: *p++ = (BYTE)((s >> 8) + 128); break;
      case kPcm16:
        *p++ = (BYTE)(s & 0xFF);
        *p++ = (BYTE)((s >> 8) & 0xFF);
        break;
      case kPcm24:
        *p++ = 0;
        *p++ = (BYTE)(s & 0xFF);
        *p++ = (BYTE)((s >> 8) & 0xFF);
        break;
      case kPcm32:
        *p++ = 0;
        *p++ = 0;
        *p++ = (BYTE)(s & 0xFF);
        *p++ = (BYTE)((s >> 8) & 0xFF);
        break;
      case kFloat: {
        float v = s / 32768.0f;
        memcpy(p, &v, 4);
        p += 4;
        break;
      }
      case kMuLaw: *p++ = MuLawEncode(s); break;
      case kALaw: *p++ = ALawEncode(s); break;
      default: break;
    }
  }
  ready_.erase(ready_.begin(), ready_.begin() + n);
  return (size_t)(p - dst);
}

void Converter::Convert(const BYTE* src, size_t srcLen, BYTE* dst, size_t dstLen, bool end, size_t* used,
                        size_t* produced) {
  size_t in = 0, out = 0;
  size_t unit = UnitBytes(from_);
  size_t outFrameBytes = to_.kind == kImaAdpcm ? 1 : (size_t)to_.blockAlign;
  for (;;) {
    out += Encode(dst + out, dstLen - out, false);
    if (srcLen - in < unit) break;
    // More input while what is ready would fit in what is left.
    size_t readyBytes = ready_.size() / to_.channels * outFrameBytes;
    size_t left = dstLen - out;
    if (to_.kind == kImaAdpcm) left = left / to_.blockAlign * to_.framesPerBlock + to_.framesPerBlock;
    if (readyBytes >= left + 4096) break;
    size_t units = std::min((srcLen - in) / unit, std::max<size_t>(1, 4096 / UnitFrames(from_)));
    Decode(src + in, units);
    in += units * unit;
    if (dstLen - out < (to_.kind == kImaAdpcm ? (size_t)to_.blockAlign : outFrameBytes) && ready_.size()) break;
  }
  if (end) out += Encode(dst + out, dstLen - out, true);
  *used = in;
  *produced = out;
}

size_t Converter::OutputSize(size_t inBytes) const {
  size_t frames = inBytes / UnitBytes(from_) * UnitFrames(from_);
  size_t outFrames = from_.rate == to_.rate ? frames : (size_t)ceil((double)frames * to_.rate / from_.rate) + 1;
  if (to_.kind == kImaAdpcm)
    return (outFrames + to_.framesPerBlock - 1) / to_.framesPerBlock * to_.blockAlign;
  return outFrames * to_.blockAlign;
}

size_t Converter::InputSize(size_t outBytes) const {
  size_t frames = outBytes / UnitBytes(to_) * UnitFrames(to_);
  size_t inFrames = from_.rate == to_.rate ? frames : (size_t)floor((double)frames * from_.rate / to_.rate);
  return inFrames / UnitFrames(from_) * UnitBytes(from_);
}

}  // namespace audio
}  // namespace winport
