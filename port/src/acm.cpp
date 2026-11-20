// acm.cpp - the audio compression manager: one built-in driver whose
// codecs are the layer's converter (PCM of every size, G.711 A-law and
// mu-law, and IMA ADPCM), with the format enumeration, suggestion and
// streams that programs use to pick a format and convert to it.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#include <windows.h>
#include <mmsystem.h>
#include <mmreg.h>
#include <msacm.h>

#include "audio.h"
#include "winport.h"

#include <stdio.h>
#include <string.h>

#include <memory>
#include <mutex>
#include <set>
#include <vector>

using namespace winport::audio;

namespace {

struct DriverId {
  int unused;
} gDriverId;
const HACMDRIVERID kDriverId = (HACMDRIVERID)&gDriverId;
const DWORD kSupport = ACMDRIVERDETAILS_SUPPORTF_CODEC | ACMDRIVERDETAILS_SUPPORTF_CONVERTER;

struct Driver {
  int unused;
};

struct Stream {
  Format from, to;
  std::unique_ptr<Converter> conv;
  DWORD kind = 0;
  DWORD_PTR target = 0, instance = 0;
  void Send(UINT msg, LPARAM p1) {
    switch (kind) {
      case CALLBACK_WINDOW: PostMessageA((HWND)target, msg, (WPARAM)this, p1); break;
      case CALLBACK_FUNCTION:
        if (target) ((DRVCALLBACK)target)((HANDLE)this, msg, instance, (DWORD_PTR)p1, 0);
        break;
      case CALLBACK_EVENT: SetEvent((HANDLE)target); break;
    }
  }
};

std::mutex& Lock() {
  static std::mutex* m = new std::mutex;
  return *m;
}
std::set<void*>& Live() {
  static std::set<void*>* s = new std::set<void*>;
  return *s;
}
bool IsLive(void* p) {
  std::lock_guard<std::mutex> g(Lock());
  return p && Live().count(p);
}
void Add(void* p) {
  std::lock_guard<std::mutex> g(Lock());
  Live().insert(p);
}
void Remove(void* p) {
  std::lock_guard<std::mutex> g(Lock());
  Live().erase(p);
}
Driver* AsDriver(HACMDRIVER h) {
  if (!IsLive(h)) return nullptr;
  return (Driver*)h;
}
Stream* AsStream(HACMSTREAM h) {
  if (!IsLive(h)) return nullptr;
  return (Stream*)h;
}
// A driver handle may be NULL (any driver) or one of ours.
bool DriverOk(HACMDRIVER h) { return !h || AsDriver(h); }

// The format tags, and the standard formats of each.
struct Tag {
  WORD tag;
  Kind kind;  // for PCM, the 8-bit kind; 16-bit follows
  const char* name;
  DWORD size;
};
const Tag kTags[] = {
    {WAVE_FORMAT_PCM, kPcm8, "PCM", sizeof(PCMWAVEFORMAT)},
    {WAVE_FORMAT_ALAW, kALaw, "CCITT A-Law", sizeof(WAVEFORMATEX)},
    {WAVE_FORMAT_MULAW, kMuLaw, "CCITT u-Law", sizeof(WAVEFORMATEX)},
    {WAVE_FORMAT_IMA_ADPCM, kImaAdpcm, "IMA ADPCM", sizeof(IMAADPCMWAVEFORMAT)},
};
const int kTagCount = 4;
const int kRates[] = {8000, 11025, 22050, 44100, 48000};
const int kRateCount = 5;

int TagIndex(DWORD tag) {
  for (int i = 0; i < kTagCount; i++)
    if (kTags[i].tag == tag) return i;
  return -1;
}
// Rates x channels, and for PCM x two sizes.
DWORD Standard(int t) { return kRateCount * 2 * (kTags[t].tag == WAVE_FORMAT_PCM ? 2 : 1); }
Format StandardFormat(int t, DWORD index) {
  bool pcm = kTags[t].tag == WAVE_FORMAT_PCM;
  int rate = kRates[index / (pcm ? 4 : 2)];
  int channels = (int)(index % 2) + 1;
  Kind k = kTags[t].kind;
  if (pcm && (index / 2) % 2) k = kPcm16;
  return Make(k, channels, rate);
}
int Bits(const Format& f) {
  switch (f.kind) {
    case kPcm8:
    case kMuLaw:
    case kALaw: return 8;
    case kPcm16: return 16;
    case kPcm24: return 24;
    case kPcm32:
    case kFloat: return 32;
    case kImaAdpcm: return 4;
    default: return 0;
  }
}
WORD TagOf(const Format& f) {
  switch (f.kind) {
    case kFloat: return WAVE_FORMAT_IEEE_FLOAT;
    case kMuLaw: return WAVE_FORMAT_MULAW;
    case kALaw: return WAVE_FORMAT_ALAW;
    case kImaAdpcm: return WAVE_FORMAT_IMA_ADPCM;
    default: return WAVE_FORMAT_PCM;
  }
}

std::string FormatName(const Format& f) {
  char buf[64];
  snprintf(buf, sizeof buf, "%d.%03d kHz, %d Bit, %s", f.rate / 1000, f.rate % 1000, Bits(f),
           f.channels == 1 ? "Mono" : f.channels == 2 ? "Stereo" : "Multichannel");
  return buf;
}

void Put(char* d, size_t n, const char* s) { lstrcpynA(d, s, (int)n); }
void Put(WCHAR* d, size_t n, const char* s) { lstrcpynW(d, winport::Widen(s).c_str(), (int)n); }
template <size_t N, typename C>
void Put(C (&d)[N], const char* s) {
  Put(d, N, s);
}

// Copies a format into a caller's buffer of cb bytes.
bool Store(const Format& f, WAVEFORMATEX* out, DWORD cb) {
  std::vector<BYTE> b = Describe(f);
  if (!out) return false;
  if (TagOf(f) == WAVE_FORMAT_PCM && cb >= sizeof(PCMWAVEFORMAT) && cb < sizeof(WAVEFORMATEX)) {
    memcpy(out, b.data(), sizeof(PCMWAVEFORMAT));
    return true;
  }
  if (cb < b.size()) return false;
  memcpy(out, b.data(), b.size());
  return true;
}

template <typename D>
MMRESULT DriverDetails(HACMDRIVERID id, D* d) {
  if (id != kDriverId) return MMSYSERR_INVALHANDLE;
  if (!d || d->cbStruct < sizeof(DWORD)) return MMSYSERR_INVALPARAM;
  D full;
  memset(&full, 0, sizeof full);
  full.cbStruct = (DWORD)std::min<size_t>(d->cbStruct, sizeof full);
  full.fccType = ACMDRIVERDETAILS_FCCTYPE_AUDIOCODEC;
  full.wMid = 1;
  full.wPid = 3;
  full.vdwACM = 0x04000000;
  full.vdwDriver = 0x01000000;
  full.fdwSupport = kSupport;
  full.cFormatTags = kTagCount;
  Put(full.szShortName, "Built-in");
  Put(full.szLongName, "Built-in audio codecs");
  Put(full.szFeatures, "PCM, CCITT A-Law and u-Law, and IMA ADPCM, at any rate and channels.");
  memcpy(d, &full, full.cbStruct);
  return MMSYSERR_NOERROR;
}

template <typename D>
MMRESULT FormatTagDetails(HACMDRIVER had, D* d, DWORD flags) {
  if (!DriverOk(had)) return MMSYSERR_INVALHANDLE;
  if (!d) return MMSYSERR_INVALPARAM;
  int t = -1;
  switch (flags & ACM_FORMATTAGDETAILSF_QUERYMASK) {
    case ACM_FORMATTAGDETAILSF_INDEX: t = d->dwFormatTagIndex < (DWORD)kTagCount ? (int)d->dwFormatTagIndex : -1; break;
    case ACM_FORMATTAGDETAILSF_FORMATTAG: t = TagIndex(d->dwFormatTag); break;
    case ACM_FORMATTAGDETAILSF_LARGESTSIZE:
      if (d->dwFormatTag == WAVE_FORMAT_UNKNOWN) {
        t = 0;
        for (int i = 1; i < kTagCount; i++)
          if (kTags[i].size > kTags[t].size) t = i;
      } else {
        t = TagIndex(d->dwFormatTag);
      }
      break;
    default: return MMSYSERR_INVALFLAG;
  }
  if (t < 0) return ACMERR_NOTPOSSIBLE;
  d->dwFormatTagIndex = (DWORD)t;
  d->dwFormatTag = kTags[t].tag;
  d->cbFormatSize = kTags[t].size;
  d->fdwSupport = kSupport;
  d->cStandardFormats = Standard(t);
  Put(d->szFormatTag, kTags[t].name);
  return MMSYSERR_NOERROR;
}

template <typename D>
MMRESULT FormatDetails(HACMDRIVER had, D* d, DWORD flags) {
  if (!DriverOk(had)) return MMSYSERR_INVALHANDLE;
  if (!d || !d->pwfx) return MMSYSERR_INVALPARAM;
  Format f;
  switch (flags & ACM_FORMATDETAILSF_QUERYMASK) {
    case ACM_FORMATDETAILSF_INDEX: {
      int t = TagIndex(d->dwFormatTag);
      if (t < 0 || d->dwFormatIndex >= Standard(t)) return ACMERR_NOTPOSSIBLE;
      f = StandardFormat(t, d->dwFormatIndex);
      if (!Store(f, d->pwfx, d->cbwfx)) return MMSYSERR_INVALPARAM;
      break;
    }
    case ACM_FORMATDETAILSF_FORMAT:
      f = Parse(d->pwfx);
      if (f.kind == kNone) return ACMERR_NOTPOSSIBLE;
      d->dwFormatIndex = 0;
      break;
    default: return MMSYSERR_INVALFLAG;
  }
  d->dwFormatTag = TagOf(f);
  d->fdwSupport = kSupport;
  Put(d->szFormat, FormatName(f).c_str());
  return MMSYSERR_NOERROR;
}

template <typename D, typename Callback>
MMRESULT FormatEnum(HACMDRIVER had, D* d, Callback cb, DWORD_PTR instance, DWORD flags) {
  if (!DriverOk(had)) return MMSYSERR_INVALHANDLE;
  if (!d || !d->pwfx || !cb) return MMSYSERR_INVALPARAM;
  if (flags & ACM_FORMATENUMF_HARDWARE) return MMSYSERR_NOERROR;  // no hardware formats
  // What the formats must match, before the buffer is written over.
  WAVEFORMATEX want = {};
  memcpy(&want, d->pwfx, std::min<size_t>(d->cbwfx, sizeof(WAVEFORMATEX)));
  if (flags & (ACM_FORMATENUMF_CONVERT | ACM_FORMATENUMF_SUGGEST)) {
    if (Parse(&want).kind == kNone) return ACMERR_NOTPOSSIBLE;
  }
  bool wantTag = flags & ACM_FORMATENUMF_WFORMATTAG;
  for (int t = 0; t < kTagCount; t++) {
    if (wantTag && kTags[t].tag != d->dwFormatTag) continue;
    for (DWORD i = 0; i < Standard(t); i++) {
      Format f = StandardFormat(t, i);
      if ((flags & ACM_FORMATENUMF_NCHANNELS) && f.channels != want.nChannels) continue;
      if ((flags & ACM_FORMATENUMF_NSAMPLESPERSEC) && (DWORD)f.rate != want.nSamplesPerSec) continue;
      if ((flags & ACM_FORMATENUMF_WBITSPERSAMPLE) && Bits(f) != want.wBitsPerSample) continue;
      if (!Store(f, d->pwfx, d->cbwfx)) return MMSYSERR_INVALPARAM;
      d->dwFormatIndex = i;
      d->dwFormatTag = kTags[t].tag;
      d->fdwSupport = kSupport;
      Put(d->szFormat, FormatName(f).c_str());
      if (!cb(kDriverId, d, instance, kSupport)) return MMSYSERR_NOERROR;
    }
  }
  return MMSYSERR_NOERROR;
}

}  // namespace

extern "C" {

DWORD WINAPI acmGetVersion(void) { return 0x04000000; }

MMRESULT WINAPI acmMetrics(HACMOBJ obj, UINT metric, LPVOID out) {
  if (!out) return MMSYSERR_INVALPARAM;
  if (obj && obj != (HACMOBJ)kDriverId && !IsLive(obj)) return MMSYSERR_INVALHANDLE;
  DWORD* v = (DWORD*)out;
  switch (metric) {
    case ACM_METRIC_COUNT_DRIVERS:
    case ACM_METRIC_COUNT_CODECS:
    case ACM_METRIC_COUNT_LOCAL_DRIVERS:
    case ACM_METRIC_COUNT_LOCAL_CODECS: *v = 1; return MMSYSERR_NOERROR;
    case ACM_METRIC_COUNT_CONVERTERS:
    case ACM_METRIC_COUNT_FILTERS:
    case ACM_METRIC_COUNT_DISABLED:
    case ACM_METRIC_COUNT_HARDWARE:
    case ACM_METRIC_COUNT_LOCAL_CONVERTERS:
    case ACM_METRIC_COUNT_LOCAL_FILTERS:
    case ACM_METRIC_COUNT_LOCAL_DISABLED:
    case ACM_METRIC_MAX_SIZE_FILTER: *v = 0; return MMSYSERR_NOERROR;
    case ACM_METRIC_MAX_SIZE_FORMAT: *v = sizeof(IMAADPCMWAVEFORMAT); return MMSYSERR_NOERROR;
    case ACM_METRIC_HARDWARE_WAVE_INPUT:
    case ACM_METRIC_HARDWARE_WAVE_OUTPUT: *v = (DWORD)-1; return MMSYSERR_NOERROR;
    case ACM_METRIC_DRIVER_SUPPORT: *v = kSupport; return MMSYSERR_NOERROR;
    case ACM_METRIC_DRIVER_PRIORITY: *v = 1; return MMSYSERR_NOERROR;
  }
  return MMSYSERR_NOTSUPPORTED;
}

MMRESULT WINAPI acmDriverEnum(ACMDRIVERENUMCB cb, DWORD_PTR instance, DWORD) {
  if (!cb) return MMSYSERR_INVALPARAM;
  cb(kDriverId, instance, kSupport);
  return MMSYSERR_NOERROR;
}

MMRESULT WINAPI acmDriverDetailsA(HACMDRIVERID id, LPACMDRIVERDETAILSA d, DWORD flags) {
  return flags ? MMSYSERR_INVALFLAG : DriverDetails(id, d);
}
MMRESULT WINAPI acmDriverDetailsW(HACMDRIVERID id, LPACMDRIVERDETAILSW d, DWORD flags) {
  return flags ? MMSYSERR_INVALFLAG : DriverDetails(id, d);
}

MMRESULT WINAPI acmDriverOpen(LPHACMDRIVER out, HACMDRIVERID id, DWORD) {
  if (!out) return MMSYSERR_INVALPARAM;
  if (id != kDriverId) return MMSYSERR_INVALHANDLE;
  Driver* d = new Driver();
  Add(d);
  *out = (HACMDRIVER)d;
  return MMSYSERR_NOERROR;
}

MMRESULT WINAPI acmDriverClose(HACMDRIVER h, DWORD) {
  Driver* d = AsDriver(h);
  if (!d) return MMSYSERR_INVALHANDLE;
  Remove(d);
  delete d;
  return MMSYSERR_NOERROR;
}

MMRESULT WINAPI acmDriverID(HACMOBJ obj, LPHACMDRIVERID out, DWORD) {
  if (!out) return MMSYSERR_INVALPARAM;
  if (obj != (HACMOBJ)kDriverId && !IsLive(obj)) return MMSYSERR_INVALHANDLE;
  *out = kDriverId;
  return MMSYSERR_NOERROR;
}

MMRESULT WINAPI acmFormatTagDetailsA(HACMDRIVER had, LPACMFORMATTAGDETAILSA d, DWORD flags) {
  return FormatTagDetails(had, d, flags);
}
MMRESULT WINAPI acmFormatTagDetailsW(HACMDRIVER had, LPACMFORMATTAGDETAILSW d, DWORD flags) {
  return FormatTagDetails(had, d, flags);
}
MMRESULT WINAPI acmFormatDetailsA(HACMDRIVER had, LPACMFORMATDETAILSA d, DWORD flags) {
  return FormatDetails(had, d, flags);
}
MMRESULT WINAPI acmFormatDetailsW(HACMDRIVER had, LPACMFORMATDETAILSW d, DWORD flags) {
  return FormatDetails(had, d, flags);
}
MMRESULT WINAPI acmFormatEnumA(HACMDRIVER had, LPACMFORMATDETAILSA d, ACMFORMATENUMCBA cb, DWORD_PTR instance,
                               DWORD flags) {
  return FormatEnum(had, d, cb, instance, flags);
}
MMRESULT WINAPI acmFormatEnumW(HACMDRIVER had, LPACMFORMATDETAILSW d, ACMFORMATENUMCBW cb, DWORD_PTR instance,
                               DWORD flags) {
  return FormatEnum(had, d, cb, instance, flags);
}

MMRESULT WINAPI acmFormatSuggest(HACMDRIVER had, LPWAVEFORMATEX src, LPWAVEFORMATEX dst, DWORD cbDst, DWORD flags) {
  if (!DriverOk(had)) return MMSYSERR_INVALHANDLE;
  if (!src || !dst) return MMSYSERR_INVALPARAM;
  if (flags & ~ACM_FORMATSUGGESTF_TYPEMASK) return MMSYSERR_INVALFLAG;
  Format s = Parse(src);
  if (s.kind == kNone) return ACMERR_NOTPOSSIBLE;
  WORD tag = (flags & ACM_FORMATSUGGESTF_WFORMATTAG) ? dst->wFormatTag : (WORD)WAVE_FORMAT_PCM;
  int channels = (flags & ACM_FORMATSUGGESTF_NCHANNELS) ? dst->nChannels : s.channels;
  int rate = (flags & ACM_FORMATSUGGESTF_NSAMPLESPERSEC) ? (int)dst->nSamplesPerSec : s.rate;
  int bits;
  if (flags & ACM_FORMATSUGGESTF_WBITSPERSAMPLE)
    bits = dst->wBitsPerSample;
  else if (tag == WAVE_FORMAT_PCM)
    bits = s.kind == kPcm8 ? 8 : 16;
  else
    bits = tag == WAVE_FORMAT_IMA_ADPCM ? 4 : 8;
  Kind k = kNone;
  switch (tag) {
    case WAVE_FORMAT_PCM: k = bits == 8 ? kPcm8 : bits == 16 ? kPcm16 : bits == 24 ? kPcm24 : bits == 32 ? kPcm32 : kNone; break;
    case WAVE_FORMAT_IEEE_FLOAT: k = bits == 32 ? kFloat : kNone; break;
    case WAVE_FORMAT_ALAW: k = bits == 8 ? kALaw : kNone; break;
    case WAVE_FORMAT_MULAW: k = bits == 8 ? kMuLaw : kNone; break;
    case WAVE_FORMAT_IMA_ADPCM: k = bits == 4 ? kImaAdpcm : kNone; break;
  }
  if (k == kNone || channels < 1 || channels > 8 || rate < 1000 || rate > 384000) return ACMERR_NOTPOSSIBLE;
  if (!Store(Make(k, channels, rate), dst, cbDst)) return MMSYSERR_INVALPARAM;
  return MMSYSERR_NOERROR;
}

MMRESULT WINAPI acmStreamOpen(LPHACMSTREAM out, HACMDRIVER had, LPWAVEFORMATEX src, LPWAVEFORMATEX dst,
                              LPWAVEFILTER filter, DWORD_PTR callback, DWORD_PTR instance, DWORD flags) {
  if (!DriverOk(had)) return MMSYSERR_INVALHANDLE;
  if (!src || !dst) return MMSYSERR_INVALPARAM;
  if (filter) return ACMERR_NOTPOSSIBLE;
  Format from = Parse(src), to = Parse(dst);
  if (from.kind == kNone || to.kind == kNone) return ACMERR_NOTPOSSIBLE;
  if (flags & ACM_STREAMOPENF_QUERY) return MMSYSERR_NOERROR;
  if (!out) return MMSYSERR_INVALPARAM;
  Stream* s = new Stream;
  s->from = from;
  s->to = to;
  s->conv.reset(new Converter(from, to));
  if (flags & ACM_STREAMOPENF_ASYNC) {
    s->kind = flags & CALLBACK_TYPEMASK;
    s->target = callback;
    s->instance = instance;
  }
  Add(s);
  *out = (HACMSTREAM)s;
  s->Send(MM_ACM_OPEN, 0);
  return MMSYSERR_NOERROR;
}

MMRESULT WINAPI acmStreamClose(HACMSTREAM h, DWORD) {
  Stream* s = AsStream(h);
  if (!s) return MMSYSERR_INVALHANDLE;
  Remove(s);
  s->Send(MM_ACM_CLOSE, 0);
  delete s;
  return MMSYSERR_NOERROR;
}

MMRESULT WINAPI acmStreamSize(HACMSTREAM h, DWORD in, LPDWORD out, DWORD flags) {
  Stream* s = AsStream(h);
  if (!s) return MMSYSERR_INVALHANDLE;
  if (!out) return MMSYSERR_INVALPARAM;
  size_t n;
  switch (flags & ACM_STREAMSIZEF_QUERYMASK) {
    case ACM_STREAMSIZEF_SOURCE: n = s->conv->OutputSize(in); break;
    case ACM_STREAMSIZEF_DESTINATION: n = s->conv->InputSize(in); break;
    default: return MMSYSERR_INVALFLAG;
  }
  if (n == 0) return ACMERR_NOTPOSSIBLE;
  *out = (DWORD)n;
  return MMSYSERR_NOERROR;
}

MMRESULT WINAPI acmStreamReset(HACMSTREAM h, DWORD) {
  Stream* s = AsStream(h);
  if (!s) return MMSYSERR_INVALHANDLE;
  s->conv->Reset();
  return MMSYSERR_NOERROR;
}

MMRESULT WINAPI acmStreamMessage(HACMSTREAM h, UINT, LPARAM, LPARAM) {
  return AsStream(h) ? MMSYSERR_NOTSUPPORTED : MMSYSERR_INVALHANDLE;
}

MMRESULT WINAPI acmStreamPrepareHeader(HACMSTREAM h, LPACMSTREAMHEADER hdr, DWORD) {
  if (!AsStream(h)) return MMSYSERR_INVALHANDLE;
  if (!hdr || hdr->cbStruct < sizeof(ACMSTREAMHEADER) || !hdr->pbSrc || !hdr->pbDst) return MMSYSERR_INVALPARAM;
  if (hdr->fdwStatus & ACMSTREAMHEADER_STATUSF_INQUEUE) return ACMERR_BUSY;
  // The lengths it was prepared with, which unpreparing checks.
  hdr->dwReservedDriver[0] = hdr->cbSrcLength;
  hdr->dwReservedDriver[1] = hdr->cbDstLength;
  hdr->fdwStatus = ACMSTREAMHEADER_STATUSF_PREPARED;
  return MMSYSERR_NOERROR;
}

MMRESULT WINAPI acmStreamUnprepareHeader(HACMSTREAM h, LPACMSTREAMHEADER hdr, DWORD) {
  if (!AsStream(h)) return MMSYSERR_INVALHANDLE;
  if (!hdr || hdr->cbStruct < sizeof(ACMSTREAMHEADER)) return MMSYSERR_INVALPARAM;
  if (!(hdr->fdwStatus & ACMSTREAMHEADER_STATUSF_PREPARED)) return ACMERR_UNPREPARED;
  if (hdr->fdwStatus & ACMSTREAMHEADER_STATUSF_INQUEUE) return ACMERR_BUSY;
  if (hdr->cbSrcLength > hdr->dwReservedDriver[0] || hdr->cbDstLength != hdr->dwReservedDriver[1])
    return MMSYSERR_INVALPARAM;
  hdr->fdwStatus &= ~ACMSTREAMHEADER_STATUSF_PREPARED;
  return MMSYSERR_NOERROR;
}

MMRESULT WINAPI acmStreamConvert(HACMSTREAM h, LPACMSTREAMHEADER hdr, DWORD flags) {
  Stream* s = AsStream(h);
  if (!s) return MMSYSERR_INVALHANDLE;
  if (!hdr || hdr->cbStruct < sizeof(ACMSTREAMHEADER)) return MMSYSERR_INVALPARAM;
  if (!(hdr->fdwStatus & ACMSTREAMHEADER_STATUSF_PREPARED)) return ACMERR_UNPREPARED;
  if (hdr->cbSrcLength > hdr->dwReservedDriver[0]) return MMSYSERR_INVALPARAM;
  if (flags & ACM_STREAMCONVERTF_START) s->conv->Reset();
  hdr->fdwStatus &= ~ACMSTREAMHEADER_STATUSF_DONE;
  size_t used = 0, produced = 0;
  s->conv->Convert(hdr->pbSrc, hdr->cbSrcLength, hdr->pbDst, hdr->cbDstLength, (flags & ACM_STREAMCONVERTF_END) != 0,
                   &used, &produced);
  hdr->cbSrcLengthUsed = (DWORD)used;
  hdr->cbDstLengthUsed = (DWORD)produced;
  hdr->fdwStatus |= ACMSTREAMHEADER_STATUSF_DONE;
  s->Send(MM_ACM_DONE, (LPARAM)hdr);
  return MMSYSERR_NOERROR;
}

}  // extern "C"
