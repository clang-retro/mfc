// wave.cpp - waveform audio out and in. Each open handle has a thread
// that feeds the sound device from the queued headers (or fills them from
// it), converting through the layer's converter, so that any format the
// converter reads plays as the wave mapper would play it. Headers come
// back done when the device has played them, not when they were handed
// over, so that positions and WOM_DONE agree with what is heard.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#include <windows.h>
#include <mmsystem.h>
#include <mmreg.h>

#include "audio.h"
#include "winport.h"

#include <string.h>

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <memory>
#include <mutex>
#include <set>
#include <thread>
#include <vector>

using namespace winport::audio;

namespace {

// The handles that are open, so that a stale or wrong one is refused.
std::mutex& LiveLock() {
  static std::mutex* m = new std::mutex;
  return *m;
}
std::set<void*>& Live() {
  static std::set<void*>* s = new std::set<void*>;
  return *s;
}
bool IsLive(void* h) {
  std::lock_guard<std::mutex> g(LiveLock());
  return h && Live().count(h);
}

struct Callback {
  DWORD kind = CALLBACK_NULL;
  DWORD_PTR target = 0, instance = 0;
  void Send(HANDLE h, UINT msg, DWORD_PTR p1) const {
    switch (kind) {
      case CALLBACK_WINDOW:
        if (target) PostMessageA((HWND)target, msg, (WPARAM)h, (LPARAM)p1);
        break;
      case CALLBACK_THREAD:
        if (target) PostThreadMessageA((DWORD)target, msg, (WPARAM)h, (LPARAM)p1);
        break;
      case CALLBACK_FUNCTION:
        if (target) ((DRVCALLBACK)target)(h, msg, instance, p1, 0);
        break;
      case CALLBACK_EVENT:
        if (target) SetEvent((HANDLE)target);
        break;
    }
  }
};

bool IsPcm(Kind k) { return k == kPcm8 || k == kPcm16 || k == kPcm24 || k == kPcm32 || k == kFloat; }

MMRESULT CheckFormat(const WAVEFORMATEX* wfx, DWORD flags, Format* f) {
  if (!wfx) return MMSYSERR_INVALPARAM;
  *f = Parse(wfx);
  if (f->kind == kNone || f->channels < 1 || f->channels > 8 || f->rate < 1000 || f->rate > 384000)
    return WAVERR_BADFORMAT;
  if ((flags & WAVE_FORMAT_DIRECT) && !IsPcm(f->kind)) return WAVERR_BADFORMAT;
  return MMSYSERR_NOERROR;
}

Format DeviceFormat(const Format& f) {
  Format d;
  d.kind = kPcm16;
  d.channels = std::min(f.channels, 2);
  d.rate = f.rate;
  d.blockAlign = 2 * d.channels;
  d.avgBytesPerSec = d.blockAlign * d.rate;
  return d;
}

// Bytes of a format for frames of it.
DWORD FramesToBytes(const Format& f, uint64_t frames) {
  return (DWORD)(frames * (uint64_t)f.blockAlign / (uint64_t)std::max(1, f.framesPerBlock));
}

void FillTime(LPMMTIME t, const Format& f, uint64_t frames) {
  switch (t->wType) {
    case TIME_SAMPLES: t->u.sample = (DWORD)frames; break;
    case TIME_MS: t->u.ms = (DWORD)(frames * 1000 / (uint64_t)f.rate); break;
    case TIME_SMPTE: {
      uint64_t ms = frames * 1000 / (uint64_t)f.rate;
      memset(&t->u.smpte, 0, sizeof t->u.smpte);
      t->u.smpte.hour = (BYTE)(ms / 3600000);
      t->u.smpte.min = (BYTE)(ms / 60000 % 60);
      t->u.smpte.sec = (BYTE)(ms / 1000 % 60);
      t->u.smpte.fps = 30;
      t->u.smpte.frame = (BYTE)(ms % 1000 * 30 / 1000);
      break;
    }
    default:
      // What is not kept is given in bytes, as the system does.
      t->wType = TIME_BYTES;
      t->u.cb = FramesToBytes(f, frames);
      break;
  }
}

const size_t kChunkMs = 20;

// ---- Out ----

struct WaveOut {
  Format from, dev;
  std::unique_ptr<Converter> conv;
  std::unique_ptr<Device> device;
  Callback cb;
  std::mutex lock;
  std::condition_variable changed;
  std::deque<WAVEHDR*> queue;
  std::deque<uint64_t> ends;  // per header: the device frame its last write ends at
  size_t cur = 0;             // the header being written, in queue
  size_t offset = 0;          // its bytes written
  long loopStart = -1;
  DWORD loopsLeft = 0;
  bool breakLoop = false;
  uint64_t written = 0;  // device frames since the last reset
  uint64_t generation = 0;
  bool paused = false, closing = false, inWrite = false;
  int holds = 0;  // callers waiting to use the device themselves
  DWORD volume = 0xFFFFFFFF;
  std::thread worker;

  uint64_t Played() {
    size_t delay = device->Delay();
    return written > delay ? written - delay : 0;
  }
  // Waits for the worker to be out of the device, so that the device can be
  // paused or emptied; the worker stays out until the Hold ends.
  struct Hold {
    WaveOut* w;
    Hold(WaveOut* o, std::unique_lock<std::mutex>& g) : w(o) {
      w->holds++;
      w->changed.wait(g, [&] { return !w->inWrite; });
    }
    ~Hold() {
      w->holds--;
      w->changed.notify_all();
    }
  };
  void Done(std::unique_lock<std::mutex>& g, std::vector<WAVEHDR*>* list) {
    g.unlock();
    for (WAVEHDR* h : *list) cb.Send((HANDLE)this, WOM_DONE, (DWORD_PTR)h);
    g.lock();
    list->clear();
  }
  // Hands back the headers the device has played.
  void Complete(std::unique_lock<std::mutex>& g) {
    std::vector<WAVEHDR*> done;
    uint64_t played = Played();
    while (!queue.empty() && cur > 0 && (loopStart < 0 || loopStart > 0) && ends.front() <= played) {
      WAVEHDR* h = queue.front();
      queue.pop_front();
      ends.pop_front();
      cur--;
      if (loopStart > 0) loopStart--;
      h->dwFlags = (h->dwFlags & ~WHDR_INQUEUE) | WHDR_DONE;
      done.push_back(h);
    }
    if (!done.empty()) Done(g, &done);
  }
  void Run() {
    std::unique_lock<std::mutex> g(lock);
    std::vector<int16_t> frames;
    for (;;) {
      Complete(g);
      if (closing) break;
      if (holds) {
        changed.wait(g);
        continue;
      }
      if (paused || cur >= queue.size()) {
        // Headers still waiting to be heard: look again soon.
        if (!paused && !queue.empty())
          changed.wait_for(g, std::chrono::milliseconds(5));
        else
          changed.wait(g);
        continue;
      }
      WAVEHDR* h = queue[cur];
      if (offset == 0 && (h->dwFlags & WHDR_BEGINLOOP) && loopStart < 0) {
        loopStart = (long)cur;
        loopsLeft = h->dwLoops;
      }
      size_t unitBytes = (size_t)from.blockAlign, unitFrames = (size_t)std::max(1, from.framesPerBlock);
      size_t chunkUnits = std::max<size_t>(1, (size_t)from.rate * kChunkMs / 1000 / unitFrames);
      size_t left = h->dwBufferLength > offset ? h->dwBufferLength - offset : 0;
      size_t take = std::min(left, chunkUnits * unitBytes);
      frames.resize(conv->OutputSize(take) / 2 + 8192);
      size_t used = 0, produced = 0;
      conv->Convert((const BYTE*)h->lpData + offset, take, (BYTE*)frames.data(), frames.size() * 2, false, &used,
                    &produced);
      if (used == 0) used = left;  // less than a frame or block: nothing to play
      size_t n = produced / (size_t)dev.blockAlign;
      if (volume != 0xFFFFFFFF) {
        int vol[2] = {(int)(volume & 0xFFFF), (int)(volume >> 16)};
        if (dev.channels == 1) vol[0] = (vol[0] + vol[1]) / 2;
        for (size_t i = 0; i < n * dev.channels; i++)
          frames[i] = (int16_t)(frames[i] * vol[i % dev.channels] / 0xFFFF);
      }
      uint64_t gen = generation;
      if (n) {
        inWrite = true;
        g.unlock();
        device->Write(frames.data(), n);
        g.lock();
        inWrite = false;
        changed.notify_all();
      }
      if (gen != generation) continue;  // reset meanwhile
      written += n;
      offset += used;
      ends[cur] = written;
      if (offset < h->dwBufferLength) continue;
      offset = 0;
      if ((h->dwFlags & WHDR_ENDLOOP) && loopStart >= 0) {
        if (loopsLeft > 1 && !breakLoop) {
          loopsLeft--;
          cur = (size_t)loopStart;
          continue;
        }
        loopStart = -1;
        breakLoop = false;
      }
      cur++;
    }
  }
};

WaveOut* Out(HWAVEOUT h) { return IsLive(h) ? (WaveOut*)h : nullptr; }

// ---- In ----

struct WaveIn {
  Format to, dev;
  std::unique_ptr<Converter> conv;
  std::unique_ptr<Device> device;
  Callback cb;
  std::mutex lock;
  std::condition_variable changed;
  std::deque<WAVEHDR*> queue;
  uint64_t recorded = 0;  // device frames since the last reset
  uint64_t generation = 0;
  bool started = false, closing = false, inRead = false;
  int holds = 0;
  std::thread worker;

  struct Hold {
    WaveIn* w;
    Hold(WaveIn* o, std::unique_lock<std::mutex>& g) : w(o) {
      w->holds++;
      w->changed.wait(g, [&] { return !w->inRead; });
    }
    ~Hold() {
      w->holds--;
      w->changed.notify_all();
    }
  };
  void Done(std::unique_lock<std::mutex>& g, std::vector<WAVEHDR*>* list) {
    g.unlock();
    for (WAVEHDR* h : *list) cb.Send((HANDLE)this, WIM_DATA, (DWORD_PTR)h);
    g.lock();
    list->clear();
  }
  WAVEHDR* Pop() {
    WAVEHDR* h = queue.front();
    queue.pop_front();
    h->dwFlags = (h->dwFlags & ~WHDR_INQUEUE) | WHDR_DONE;
    return h;
  }
  // Hands back every buffer, the first with what it holds.
  void Flush(std::unique_lock<std::mutex>& g, bool all) {
    std::vector<WAVEHDR*> done;
    if (!queue.empty() && (all || queue.front()->dwBytesRecorded)) done.push_back(Pop());
    while (all && !queue.empty()) done.push_back(Pop());
    Done(g, &done);
  }
  void Run() {
    std::unique_lock<std::mutex> g(lock);
    std::vector<int16_t> frames;
    std::vector<WAVEHDR*> done;
    for (;;) {
      if (closing) break;
      if (!started || holds) {
        changed.wait(g);
        continue;
      }
      size_t n = std::max<size_t>(1, (size_t)dev.rate * kChunkMs / 1000);
      frames.resize(n * dev.channels);
      uint64_t gen = generation;
      inRead = true;
      g.unlock();
      size_t got = device->Read(frames.data(), n);
      g.lock();
      inRead = false;
      changed.notify_all();
      if (gen != generation || !started) continue;
      if (got == 0) {
        changed.wait_for(g, std::chrono::milliseconds(kChunkMs));
        continue;
      }
      recorded += got;
      const BYTE* src = (const BYTE*)frames.data();
      size_t srcLen = got * (size_t)dev.blockAlign, in = 0;
      size_t unit = (size_t)to.blockAlign;
      for (;;) {
        if (queue.empty()) break;  // no buffers: the sound is lost
        WAVEHDR* h = queue.front();
        size_t room = h->dwBufferLength - h->dwBytesRecorded;
        size_t used = 0, produced = 0;
        conv->Convert(src + in, srcLen - in, (BYTE*)h->lpData + h->dwBytesRecorded, room, false, &used, &produced);
        in += used;
        h->dwBytesRecorded += (DWORD)produced;
        if (h->dwBufferLength - h->dwBytesRecorded < unit) done.push_back(Pop());
        if (used == 0 && produced == 0) break;
      }
      if (!done.empty()) Done(g, &done);
    }
  }
};

WaveIn* In(HWAVEIN h) { return IsLive(h) ? (WaveIn*)h : nullptr; }

template <typename Caps>
void CopyName(Caps* caps, bool capture, char*) {
  lstrcpynA(caps->szPname, DeviceName(capture).c_str(), MAXPNAMELEN);
}
template <typename Caps>
void CopyName(Caps* caps, bool capture, WCHAR*) {
  std::wstring w = winport::Widen(DeviceName(capture).c_str());
  lstrcpynW(caps->szPname, w.c_str(), MAXPNAMELEN);
}

const DWORD kAllFormats = 0xFFFFF;

template <typename Caps>
MMRESULT OutCaps(UINT_PTR id, Caps* caps, UINT size) {
  if (!caps || size < sizeof(Caps)) return MMSYSERR_INVALPARAM;
  if (id != (UINT_PTR)WAVE_MAPPER && id >= (UINT_PTR)DeviceCount(false) && !IsLive((void*)id))
    return MMSYSERR_BADDEVICEID;
  memset(caps, 0, sizeof *caps);
  caps->wMid = 1;
  caps->wPid = 1;
  caps->vDriverVersion = 0x0100;
  CopyName(caps, false, caps->szPname);
  caps->dwFormats = kAllFormats;
  caps->wChannels = 2;
  caps->dwSupport = WAVECAPS_VOLUME | WAVECAPS_LRVOLUME | WAVECAPS_SAMPLEACCURATE;
  return MMSYSERR_NOERROR;
}

template <typename Caps>
MMRESULT InCaps(UINT_PTR id, Caps* caps, UINT size) {
  if (!caps || size < sizeof(Caps)) return MMSYSERR_INVALPARAM;
  if (id != (UINT_PTR)WAVE_MAPPER && id >= (UINT_PTR)DeviceCount(true) && !IsLive((void*)id))
    return MMSYSERR_BADDEVICEID;
  memset(caps, 0, sizeof *caps);
  caps->wMid = 1;
  caps->wPid = 2;
  caps->vDriverVersion = 0x0100;
  CopyName(caps, true, caps->szPname);
  caps->dwFormats = kAllFormats;
  caps->wChannels = 2;
  return MMSYSERR_NOERROR;
}

const char* ErrorText(MMRESULT e) {
  switch (e) {
    case MMSYSERR_NOERROR: return "The command was successful.";
    case MMSYSERR_ERROR: return "An unknown error occurred.";
    case MMSYSERR_BADDEVICEID: return "The device ID is out of range.";
    case MMSYSERR_NOTENABLED: return "The driver is not enabled.";
    case MMSYSERR_ALLOCATED: return "The device is already in use.";
    case MMSYSERR_INVALHANDLE: return "The device handle is not valid.";
    case MMSYSERR_NODRIVER: return "There is no driver installed for the device.";
    case MMSYSERR_NOMEM: return "There is not enough memory.";
    case MMSYSERR_NOTSUPPORTED: return "The function is not supported.";
    case MMSYSERR_BADERRNUM: return "The error number does not exist.";
    case MMSYSERR_INVALFLAG: return "A flag is not valid.";
    case MMSYSERR_INVALPARAM: return "A parameter is not valid.";
    case MMSYSERR_HANDLEBUSY: return "The handle is in use by another thread.";
    case WAVERR_BADFORMAT: return "The wave format is not supported.";
    case WAVERR_STILLPLAYING: return "Buffers are still queued.";
    case WAVERR_UNPREPARED: return "The header is not prepared.";
    case WAVERR_SYNC: return "The device is synchronous.";
  }
  return nullptr;
}

MMRESULT GetErrorTextA(MMRESULT e, LPSTR text, UINT n) {
  if (!text || !n) return MMSYSERR_INVALPARAM;
  const char* s = ErrorText(e);
  if (!s) return MMSYSERR_BADERRNUM;
  lstrcpynA(text, s, (int)n);
  return MMSYSERR_NOERROR;
}

MMRESULT GetErrorTextW(MMRESULT e, LPWSTR text, UINT n) {
  if (!text || !n) return MMSYSERR_INVALPARAM;
  const char* s = ErrorText(e);
  if (!s) return MMSYSERR_BADERRNUM;
  lstrcpynW(text, winport::Widen(s).c_str(), (int)n);
  return MMSYSERR_NOERROR;
}

MMRESULT CheckCallback(DWORD flags, DWORD_PTR target, Callback* cb, DWORD_PTR instance) {
  cb->kind = flags & CALLBACK_TYPEMASK;
  cb->target = target;
  cb->instance = instance;
  switch (cb->kind) {
    case CALLBACK_NULL:
    case CALLBACK_FUNCTION:
    case CALLBACK_THREAD:
    case CALLBACK_EVENT: return MMSYSERR_NOERROR;
    case CALLBACK_WINDOW: return target && !IsWindow((HWND)target) ? MMSYSERR_INVALPARAM : MMSYSERR_NOERROR;
  }
  return MMSYSERR_INVALFLAG;
}

DWORD gVolume = 0xFFFFFFFF;  // for the device, when set by its number

}  // namespace

extern "C" {

UINT WINAPI waveOutGetNumDevs(void) { return (UINT)DeviceCount(false); }
MMRESULT WINAPI waveOutGetDevCapsA(UINT_PTR id, LPWAVEOUTCAPSA caps, UINT size) { return OutCaps(id, caps, size); }
MMRESULT WINAPI waveOutGetDevCapsW(UINT_PTR id, LPWAVEOUTCAPSW caps, UINT size) { return OutCaps(id, caps, size); }
MMRESULT WINAPI waveOutGetErrorTextA(MMRESULT e, LPSTR t, UINT n) { return GetErrorTextA(e, t, n); }
MMRESULT WINAPI waveOutGetErrorTextW(MMRESULT e, LPWSTR t, UINT n) { return GetErrorTextW(e, t, n); }

MMRESULT WINAPI waveOutOpen(LPHWAVEOUT out, UINT id, LPCWAVEFORMATEX wfx, DWORD_PTR callback, DWORD_PTR instance,
                            DWORD flags) {
  int count = DeviceCount(false);
  if (id != WAVE_MAPPER && (int)id >= count) return MMSYSERR_BADDEVICEID;
  if (count == 0) return MMSYSERR_NODRIVER;
  Format f;
  MMRESULT r = CheckFormat(wfx, flags, &f);
  if (r) return r;
  if (flags & WAVE_FORMAT_QUERY) return MMSYSERR_NOERROR;
  if (!out) return MMSYSERR_INVALPARAM;
  Callback cb;
  if ((r = CheckCallback(flags, callback, &cb, instance))) return r;
  WaveOut* w = new WaveOut;
  w->from = f;
  w->dev = DeviceFormat(f);
  w->cb = cb;
  w->volume = gVolume;
  w->conv.reset(new Converter(w->from, w->dev));
  w->device.reset(OpenDevice(false, w->dev.rate, w->dev.channels));
  if (!w->device) {
    delete w;
    return MMSYSERR_ALLOCATED;
  }
  {
    std::lock_guard<std::mutex> g(LiveLock());
    Live().insert(w);
  }
  w->worker = std::thread([w] { w->Run(); });
  *out = (HWAVEOUT)w;
  w->cb.Send((HANDLE)w, WOM_OPEN, 0);
  return MMSYSERR_NOERROR;
}

MMRESULT WINAPI waveOutClose(HWAVEOUT h) {
  WaveOut* w = Out(h);
  if (!w) return MMSYSERR_INVALHANDLE;
  {
    std::unique_lock<std::mutex> g(w->lock);
    if (!w->queue.empty()) return WAVERR_STILLPLAYING;
    w->closing = true;
    w->changed.notify_all();
  }
  {
    std::lock_guard<std::mutex> g(LiveLock());
    Live().erase(w);
  }
  if (w->worker.get_id() == std::this_thread::get_id())
    w->worker.detach();
  else
    w->worker.join();
  Callback cb = w->cb;
  delete w;
  cb.Send((HANDLE)h, WOM_CLOSE, 0);
  return MMSYSERR_NOERROR;
}

MMRESULT WINAPI waveOutPrepareHeader(HWAVEOUT h, LPWAVEHDR hdr, UINT size) {
  if (!Out(h)) return MMSYSERR_INVALHANDLE;
  if (!hdr || size < sizeof(WAVEHDR) || !hdr->lpData) return MMSYSERR_INVALPARAM;
  if (hdr->dwFlags & WHDR_INQUEUE) return WAVERR_STILLPLAYING;
  hdr->dwFlags |= WHDR_PREPARED;
  return MMSYSERR_NOERROR;
}

MMRESULT WINAPI waveOutUnprepareHeader(HWAVEOUT h, LPWAVEHDR hdr, UINT size) {
  if (!Out(h)) return MMSYSERR_INVALHANDLE;
  if (!hdr || size < sizeof(WAVEHDR)) return MMSYSERR_INVALPARAM;
  if (hdr->dwFlags & WHDR_INQUEUE) return WAVERR_STILLPLAYING;
  hdr->dwFlags &= ~WHDR_PREPARED;
  return MMSYSERR_NOERROR;
}

MMRESULT WINAPI waveOutWrite(HWAVEOUT h, LPWAVEHDR hdr, UINT size) {
  WaveOut* w = Out(h);
  if (!w) return MMSYSERR_INVALHANDLE;
  if (!hdr || size < sizeof(WAVEHDR)) return MMSYSERR_INVALPARAM;
  if (!(hdr->dwFlags & WHDR_PREPARED)) return WAVERR_UNPREPARED;
  std::lock_guard<std::mutex> g(w->lock);
  if (hdr->dwFlags & WHDR_INQUEUE) return WAVERR_STILLPLAYING;
  hdr->dwFlags = (hdr->dwFlags & ~WHDR_DONE) | WHDR_INQUEUE;
  hdr->lpNext = nullptr;
  w->queue.push_back(hdr);
  w->ends.push_back(0);
  w->changed.notify_all();
  return MMSYSERR_NOERROR;
}

MMRESULT WINAPI waveOutPause(HWAVEOUT h) {
  WaveOut* w = Out(h);
  if (!w) return MMSYSERR_INVALHANDLE;
  std::unique_lock<std::mutex> g(w->lock);
  if (w->paused) return MMSYSERR_NOERROR;
  w->paused = true;
  WaveOut::Hold hold(w, g);
  w->device->Pause(true);
  return MMSYSERR_NOERROR;
}

MMRESULT WINAPI waveOutRestart(HWAVEOUT h) {
  WaveOut* w = Out(h);
  if (!w) return MMSYSERR_INVALHANDLE;
  std::lock_guard<std::mutex> g(w->lock);
  if (!w->paused) return MMSYSERR_NOERROR;
  w->paused = false;
  w->device->Pause(false);
  w->changed.notify_all();
  return MMSYSERR_NOERROR;
}

MMRESULT WINAPI waveOutReset(HWAVEOUT h) {
  WaveOut* w = Out(h);
  if (!w) return MMSYSERR_INVALHANDLE;
  std::unique_lock<std::mutex> g(w->lock);
  w->generation++;
  WaveOut::Hold hold(w, g);
  if (w->paused) w->device->Pause(false);
  w->device->Drop();
  std::vector<WAVEHDR*> done;
  for (WAVEHDR* hdr : w->queue) {
    hdr->dwFlags = (hdr->dwFlags & ~WHDR_INQUEUE) | WHDR_DONE;
    done.push_back(hdr);
  }
  w->queue.clear();
  w->ends.clear();
  w->cur = w->offset = 0;
  w->loopStart = -1;
  w->breakLoop = false;
  w->written = 0;
  w->paused = false;
  w->conv->Reset();
  w->changed.notify_all();
  w->Done(g, &done);
  return MMSYSERR_NOERROR;
}

MMRESULT WINAPI waveOutBreakLoop(HWAVEOUT h) {
  WaveOut* w = Out(h);
  if (!w) return MMSYSERR_INVALHANDLE;
  std::lock_guard<std::mutex> g(w->lock);
  if (w->loopStart >= 0) w->breakLoop = true;
  return MMSYSERR_NOERROR;
}

MMRESULT WINAPI waveOutGetPosition(HWAVEOUT h, LPMMTIME t, UINT size) {
  WaveOut* w = Out(h);
  if (!w) return MMSYSERR_INVALHANDLE;
  if (!t || size < sizeof(MMTIME)) return MMSYSERR_INVALPARAM;
  std::lock_guard<std::mutex> g(w->lock);
  FillTime(t, w->from, w->Played());
  return MMSYSERR_NOERROR;
}

MMRESULT WINAPI waveOutGetVolume(HWAVEOUT h, LPDWORD v) {
  if (!v) return MMSYSERR_INVALPARAM;
  if (WaveOut* w = Out(h)) {
    std::lock_guard<std::mutex> g(w->lock);
    *v = w->volume;
    return MMSYSERR_NOERROR;
  }
  if ((UINT_PTR)h >= (UINT_PTR)DeviceCount(false)) return MMSYSERR_INVALHANDLE;
  *v = gVolume;
  return MMSYSERR_NOERROR;
}

MMRESULT WINAPI waveOutSetVolume(HWAVEOUT h, DWORD v) {
  if (WaveOut* w = Out(h)) {
    std::lock_guard<std::mutex> g(w->lock);
    w->volume = v;
    return MMSYSERR_NOERROR;
  }
  if ((UINT_PTR)h >= (UINT_PTR)DeviceCount(false)) return MMSYSERR_INVALHANDLE;
  gVolume = v;
  return MMSYSERR_NOERROR;
}

MMRESULT WINAPI waveOutGetPitch(HWAVEOUT h, LPDWORD) { return Out(h) ? MMSYSERR_NOTSUPPORTED : MMSYSERR_INVALHANDLE; }
MMRESULT WINAPI waveOutSetPitch(HWAVEOUT h, DWORD) { return Out(h) ? MMSYSERR_NOTSUPPORTED : MMSYSERR_INVALHANDLE; }
MMRESULT WINAPI waveOutGetPlaybackRate(HWAVEOUT h, LPDWORD) {
  return Out(h) ? MMSYSERR_NOTSUPPORTED : MMSYSERR_INVALHANDLE;
}
MMRESULT WINAPI waveOutSetPlaybackRate(HWAVEOUT h, DWORD) {
  return Out(h) ? MMSYSERR_NOTSUPPORTED : MMSYSERR_INVALHANDLE;
}
MMRESULT WINAPI waveOutGetID(HWAVEOUT h, LPUINT id) {
  if (!Out(h)) return MMSYSERR_INVALHANDLE;
  if (!id) return MMSYSERR_INVALPARAM;
  *id = 0;
  return MMSYSERR_NOERROR;
}
MMRESULT WINAPI waveOutMessage(HWAVEOUT h, UINT, DWORD_PTR, DWORD_PTR) {
  return Out(h) ? MMSYSERR_NOTSUPPORTED : MMSYSERR_INVALHANDLE;
}

UINT WINAPI waveInGetNumDevs(void) { return (UINT)DeviceCount(true); }
MMRESULT WINAPI waveInGetDevCapsA(UINT_PTR id, LPWAVEINCAPSA caps, UINT size) { return InCaps(id, caps, size); }
MMRESULT WINAPI waveInGetDevCapsW(UINT_PTR id, LPWAVEINCAPSW caps, UINT size) { return InCaps(id, caps, size); }
MMRESULT WINAPI waveInGetErrorTextA(MMRESULT e, LPSTR t, UINT n) { return GetErrorTextA(e, t, n); }
MMRESULT WINAPI waveInGetErrorTextW(MMRESULT e, LPWSTR t, UINT n) { return GetErrorTextW(e, t, n); }

MMRESULT WINAPI waveInOpen(LPHWAVEIN out, UINT id, LPCWAVEFORMATEX wfx, DWORD_PTR callback, DWORD_PTR instance,
                           DWORD flags) {
  int count = DeviceCount(true);
  if (id != WAVE_MAPPER && (int)id >= count) return MMSYSERR_BADDEVICEID;
  if (count == 0) return MMSYSERR_NODRIVER;
  Format f;
  MMRESULT r = CheckFormat(wfx, flags, &f);
  if (r) return r;
  if (flags & WAVE_FORMAT_QUERY) return MMSYSERR_NOERROR;
  if (!out) return MMSYSERR_INVALPARAM;
  Callback cb;
  if ((r = CheckCallback(flags, callback, &cb, instance))) return r;
  WaveIn* w = new WaveIn;
  w->to = f;
  w->dev = DeviceFormat(f);
  w->cb = cb;
  w->conv.reset(new Converter(w->dev, w->to));
  w->device.reset(OpenDevice(true, w->dev.rate, w->dev.channels));
  if (!w->device) {
    delete w;
    return MMSYSERR_ALLOCATED;
  }
  {
    std::lock_guard<std::mutex> g(LiveLock());
    Live().insert(w);
  }
  w->worker = std::thread([w] { w->Run(); });
  *out = (HWAVEIN)w;
  w->cb.Send((HANDLE)w, WIM_OPEN, 0);
  return MMSYSERR_NOERROR;
}

MMRESULT WINAPI waveInClose(HWAVEIN h) {
  WaveIn* w = In(h);
  if (!w) return MMSYSERR_INVALHANDLE;
  {
    std::unique_lock<std::mutex> g(w->lock);
    if (!w->queue.empty()) return WAVERR_STILLPLAYING;
    w->started = false;
    w->closing = true;
    w->changed.notify_all();
    WaveIn::Hold hold(w, g);
  }
  {
    std::lock_guard<std::mutex> g(LiveLock());
    Live().erase(w);
  }
  if (w->worker.get_id() == std::this_thread::get_id())
    w->worker.detach();
  else
    w->worker.join();
  Callback cb = w->cb;
  delete w;
  cb.Send((HANDLE)h, WIM_CLOSE, 0);
  return MMSYSERR_NOERROR;
}

MMRESULT WINAPI waveInPrepareHeader(HWAVEIN h, LPWAVEHDR hdr, UINT size) {
  if (!In(h)) return MMSYSERR_INVALHANDLE;
  if (!hdr || size < sizeof(WAVEHDR) || !hdr->lpData) return MMSYSERR_INVALPARAM;
  if (hdr->dwFlags & WHDR_INQUEUE) return WAVERR_STILLPLAYING;
  hdr->dwFlags |= WHDR_PREPARED;
  return MMSYSERR_NOERROR;
}

MMRESULT WINAPI waveInUnprepareHeader(HWAVEIN h, LPWAVEHDR hdr, UINT size) {
  if (!In(h)) return MMSYSERR_INVALHANDLE;
  if (!hdr || size < sizeof(WAVEHDR)) return MMSYSERR_INVALPARAM;
  if (hdr->dwFlags & WHDR_INQUEUE) return WAVERR_STILLPLAYING;
  hdr->dwFlags &= ~WHDR_PREPARED;
  return MMSYSERR_NOERROR;
}

MMRESULT WINAPI waveInAddBuffer(HWAVEIN h, LPWAVEHDR hdr, UINT size) {
  WaveIn* w = In(h);
  if (!w) return MMSYSERR_INVALHANDLE;
  if (!hdr || size < sizeof(WAVEHDR)) return MMSYSERR_INVALPARAM;
  if (!(hdr->dwFlags & WHDR_PREPARED)) return WAVERR_UNPREPARED;
  std::lock_guard<std::mutex> g(w->lock);
  if (hdr->dwFlags & WHDR_INQUEUE) return WAVERR_STILLPLAYING;
  hdr->dwFlags = (hdr->dwFlags & ~WHDR_DONE) | WHDR_INQUEUE;
  hdr->dwBytesRecorded = 0;
  hdr->lpNext = nullptr;
  w->queue.push_back(hdr);
  w->changed.notify_all();
  return MMSYSERR_NOERROR;
}

MMRESULT WINAPI waveInStart(HWAVEIN h) {
  WaveIn* w = In(h);
  if (!w) return MMSYSERR_INVALHANDLE;
  std::unique_lock<std::mutex> g(w->lock);
  if (w->started) return MMSYSERR_NOERROR;
  WaveIn::Hold hold(w, g);
  w->device->Drop();  // nothing from before the start
  w->device->Start();
  w->started = true;
  w->changed.notify_all();
  return MMSYSERR_NOERROR;
}

MMRESULT WINAPI waveInStop(HWAVEIN h) {
  WaveIn* w = In(h);
  if (!w) return MMSYSERR_INVALHANDLE;
  std::unique_lock<std::mutex> g(w->lock);
  if (w->started) {
    w->started = false;
    w->generation++;
    WaveIn::Hold hold(w, g);
  }
  w->Flush(g, false);
  return MMSYSERR_NOERROR;
}

MMRESULT WINAPI waveInReset(HWAVEIN h) {
  WaveIn* w = In(h);
  if (!w) return MMSYSERR_INVALHANDLE;
  std::unique_lock<std::mutex> g(w->lock);
  w->started = false;
  w->generation++;
  {
    WaveIn::Hold hold(w, g);
  }
  w->recorded = 0;
  w->conv->Reset();
  w->Flush(g, true);
  return MMSYSERR_NOERROR;
}

MMRESULT WINAPI waveInGetPosition(HWAVEIN h, LPMMTIME t, UINT size) {
  WaveIn* w = In(h);
  if (!w) return MMSYSERR_INVALHANDLE;
  if (!t || size < sizeof(MMTIME)) return MMSYSERR_INVALPARAM;
  std::lock_guard<std::mutex> g(w->lock);
  FillTime(t, w->to, w->recorded);
  return MMSYSERR_NOERROR;
}

MMRESULT WINAPI waveInGetID(HWAVEIN h, LPUINT id) {
  if (!In(h)) return MMSYSERR_INVALHANDLE;
  if (!id) return MMSYSERR_INVALPARAM;
  *id = 0;
  return MMSYSERR_NOERROR;
}

MMRESULT WINAPI waveInMessage(HWAVEIN h, UINT, DWORD_PTR, DWORD_PTR) {
  return In(h) ? MMSYSERR_NOTSUPPORTED : MMSYSERR_INVALHANDLE;
}

}  // extern "C"
