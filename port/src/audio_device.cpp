// audio_device.cpp - the sound devices: ALSA on Linux (libasound, looked up
// at run time), Core Audio on macOS, and a loopback device that keeps time
// like a real one and hands what was played to whoever records. The
// loopback device is chosen by WINPORT_AUDIO=null, and is what the tests
// use; WINPORT_AUDIO_DEVICE names the ALSA device (default "default").
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#include "audio.h"
#include "audio_ca.h"

#include <dlfcn.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>

namespace winport {
namespace audio {

namespace {

enum Backend { kNull, kAlsa, kCoreAudio };

Backend Chosen() {
  const char* e = getenv("WINPORT_AUDIO");
  if (e && (!strcmp(e, "null") || !strcmp(e, "loopback"))) return kNull;
#ifdef __APPLE__
  return kCoreAudio;
#else
  return kAlsa;
#endif
}

// ---- Loopback ----

typedef std::chrono::steady_clock Clock;

// What was played, for the next recording: 16-bit stereo-or-mono frames at
// the rate they were played at.
struct Loop {
  std::mutex lock;
  std::deque<int16_t> samples;
  int rate = 0, channels = 0;
};
Loop& TheLoop() {
  static Loop* l = new Loop;
  return *l;
}

class NullDevice : public Device {
 public:
  NullDevice(bool capture, int rate, int channels) : capture_(capture), rate_(rate), channels_(channels) {
    start_ = Clock::now();
  }
  // Frames the device has used up (played or captured) by now.
  size_t Clocked() {
    if (paused_ || !started_) return done_;
    double s = std::chrono::duration<double>(Clock::now() - start_).count();
    return done_ + (size_t)(s * rate_);
  }
  bool Write(const int16_t* frames, size_t n) override {
    std::unique_lock<std::mutex> g(lock_);
    dropped_ = false;
    if (!started_) Begin();
    // A device buffer of 50 ms: wait until there is room for these.
    size_t room = (size_t)rate_ / 20;
    for (;;) {
      size_t played = std::min(Clocked(), written_);
      if (written_ - played + n <= room || written_ - played == 0) break;
      size_t wait = written_ - played + n - room;
      g.unlock();
      std::this_thread::sleep_for(std::chrono::microseconds(std::max<size_t>(1000, wait * 1000000 / rate_)));
      g.lock();
      if (dropped_) {  // thrown away meanwhile
        dropped_ = false;
        if (!started_) Begin();
        break;
      }
    }
    if (Clocked() > written_) {  // ran dry: start again from now
      done_ = written_;
      start_ = Clock::now();
    }
    written_ += n;
    Loop& l = TheLoop();
    std::lock_guard<std::mutex> lg(l.lock);
    if (l.rate != rate_ || l.channels != channels_) {
      l.samples.clear();
      l.rate = rate_;
      l.channels = channels_;
    }
    l.samples.insert(l.samples.end(), frames, frames + n * channels_);
    size_t limit = (size_t)rate_ * channels_ * 10;
    while (l.samples.size() > limit) l.samples.pop_front();
    return true;
  }
  size_t Read(int16_t* frames, size_t n) override {
    std::unique_lock<std::mutex> g(lock_);
    dropped_ = false;
    if (!started_) Begin();
    // Captured sound arrives in real time.
    while (Clocked() < read_ + n) {
      size_t wait = read_ + n - Clocked();
      g.unlock();
      std::this_thread::sleep_for(std::chrono::microseconds(std::max<size_t>(500, wait * 1000000 / rate_)));
      g.lock();
      if (dropped_) {
        dropped_ = false;
        return 0;
      }
    }
    read_ += n;
    Loop& l = TheLoop();
    std::lock_guard<std::mutex> lg(l.lock);
    for (size_t f = 0; f < n; f++) {
      for (int c = 0; c < channels_; c++) {
        int16_t v = 0;
        if (!l.samples.empty() && l.channels) {
          int from = std::min(c, l.channels - 1);
          v = l.samples[from];
        }
        frames[f * channels_ + c] = v;
      }
      if (!l.samples.empty() && l.channels)
        l.samples.erase(l.samples.begin(), l.samples.begin() + std::min<size_t>(l.channels, l.samples.size()));
    }
    return n;
  }
  size_t Delay() override {
    std::lock_guard<std::mutex> g(lock_);
    if (capture_) return 0;
    size_t played = std::min(Clocked(), written_);
    return written_ - played;
  }
  void Pause(bool on) override {
    std::lock_guard<std::mutex> g(lock_);
    if (on == paused_) return;
    if (on) {
      done_ = Clocked();
      if (!capture_) done_ = std::min(done_, written_);
    } else {
      start_ = Clock::now();
    }
    paused_ = on;
  }
  void Drop() override {
    std::lock_guard<std::mutex> g(lock_);
    done_ = written_ = read_ = 0;
    started_ = false;
    dropped_ = true;
  }

 private:
  void Begin() {
    started_ = true;
    start_ = Clock::now();
  }
  std::mutex lock_;
  bool capture_;
  int rate_, channels_;
  bool started_ = false, paused_ = false, dropped_ = false;
  Clock::time_point start_;
  size_t done_ = 0, written_ = 0, read_ = 0;
};

// ---- ALSA ----

struct Alsa {
  bool ok = false;
  int (*open)(void**, const char*, int, int);
  int (*set_params)(void*, int, int, unsigned, unsigned, int, unsigned);
  long (*writei)(void*, const void*, unsigned long);
  long (*readi)(void*, void*, unsigned long);
  int (*recover)(void*, int, int);
  int (*delay)(void*, long*);
  int (*pause)(void*, int);
  int (*drop)(void*);
  int (*prepare)(void*);
  int (*start)(void*);
  int (*close)(void*);
  int (*card_next)(int*);
};
const int kStreamPlayback = 0, kStreamCapture = 1, kNonBlock = 1;
const int kFormatS16LE = 2, kAccessRwInterleaved = 3;

const Alsa& LoadAlsa() {
  static Alsa api = [] {
    Alsa a;
    void* lib = dlopen("libasound.so.2", RTLD_NOW | RTLD_LOCAL);
    if (!lib) lib = dlopen("libasound.so", RTLD_NOW | RTLD_LOCAL);
    if (!lib) return a;
#define WINPORT_ALSA(name, sym) a.name = (decltype(a.name))dlsym(lib, sym)
    WINPORT_ALSA(open, "snd_pcm_open");
    WINPORT_ALSA(set_params, "snd_pcm_set_params");
    WINPORT_ALSA(writei, "snd_pcm_writei");
    WINPORT_ALSA(readi, "snd_pcm_readi");
    WINPORT_ALSA(recover, "snd_pcm_recover");
    WINPORT_ALSA(delay, "snd_pcm_delay");
    WINPORT_ALSA(pause, "snd_pcm_pause");
    WINPORT_ALSA(drop, "snd_pcm_drop");
    WINPORT_ALSA(prepare, "snd_pcm_prepare");
    WINPORT_ALSA(start, "snd_pcm_start");
    WINPORT_ALSA(close, "snd_pcm_close");
    WINPORT_ALSA(card_next, "snd_card_next");
#undef WINPORT_ALSA
    a.ok = a.open && a.set_params && a.writei && a.readi && a.recover && a.delay && a.drop && a.prepare &&
           a.close;
    return a;
  }();
  return api;
}

const char* AlsaName() {
  const char* e = getenv("WINPORT_AUDIO_DEVICE");
  return e && *e ? e : "default";
}

class AlsaDevice : public Device {
 public:
  AlsaDevice(void* pcm, int channels, bool capture) : pcm_(pcm), channels_(channels), capture_(capture) {}
  ~AlsaDevice() override { LoadAlsa().close(pcm_); }
  bool Write(const int16_t* frames, size_t n) override {
    const Alsa& a = LoadAlsa();
    while (n) {
      long r = a.writei(pcm_, frames, n);
      if (r < 0) {
        if (a.recover(pcm_, (int)r, 1) < 0) return false;
        continue;
      }
      frames += r * channels_;
      n -= (size_t)r;
    }
    return true;
  }
  size_t Read(int16_t* frames, size_t n) override {
    const Alsa& a = LoadAlsa();
    size_t got = 0;
    while (got < n) {
      long r = a.readi(pcm_, frames + got * channels_, n - got);
      if (r < 0) {
        if (a.recover(pcm_, (int)r, 1) < 0) break;
        continue;
      }
      if (r == 0) break;
      got += (size_t)r;
    }
    return got;
  }
  size_t Delay() override {
    long d = 0;
    if (LoadAlsa().delay(pcm_, &d) < 0 || d < 0) return 0;
    return (size_t)d;
  }
  void Pause(bool on) override {
    const Alsa& a = LoadAlsa();
    // Not every device can pause; one that cannot plays out what it has.
    if (a.pause) a.pause(pcm_, on ? 1 : 0);
  }
  void Drop() override {
    const Alsa& a = LoadAlsa();
    a.drop(pcm_);
    a.prepare(pcm_);
    if (capture_ && a.start) a.start(pcm_);
  }
  void Start() override {
    const Alsa& a = LoadAlsa();
    if (capture_ && a.start) a.start(pcm_);
  }

 private:
  void* pcm_;
  int channels_;
  bool capture_;
};

void* AlsaOpen(bool capture, int rate, int channels, int mode) {
  const Alsa& a = LoadAlsa();
  if (!a.ok) return nullptr;
  void* pcm = nullptr;
  if (a.open(&pcm, AlsaName(), capture ? kStreamCapture : kStreamPlayback, mode) < 0) return nullptr;
  // Let ALSA resample; ask for 100 ms of buffering.
  if (a.set_params(pcm, kFormatS16LE, kAccessRwInterleaved, (unsigned)channels, (unsigned)rate, 1, 100000) < 0) {
    a.close(pcm);
    return nullptr;
  }
  return pcm;
}

bool AlsaAvailable(bool capture) {
  static int known[2] = {-1, -1};
  int& k = known[capture ? 1 : 0];
  if (k < 0) {
    void* pcm = AlsaOpen(capture, 8000, 1, kNonBlock);
    k = pcm != nullptr;
    if (pcm) LoadAlsa().close(pcm);
  }
  return k == 1;
}

// ---- Core Audio ----

#ifdef __APPLE__
class CoreAudioDevice : public Device {
 public:
  explicit CoreAudioDevice(ca::Stream* s) : s_(s) {}
  ~CoreAudioDevice() override { ca::Close(s_); }
  bool Write(const int16_t* frames, size_t n) override { return ca::Write(s_, frames, n); }
  size_t Read(int16_t* frames, size_t n) override { return ca::Read(s_, frames, n); }
  size_t Delay() override { return ca::Delay(s_); }
  void Pause(bool on) override { ca::Pause(s_, on); }
  void Drop() override { ca::Drop(s_); }

 private:
  ca::Stream* s_;
};
#endif

}  // namespace

int DeviceCount(bool capture) {
  switch (Chosen()) {
    case kNull: return 1;
    case kAlsa: return AlsaAvailable(capture) ? 1 : 0;
    case kCoreAudio:
#ifdef __APPLE__
      return ca::Available(capture) ? 1 : 0;
#else
      return 0;
#endif
  }
  return 0;
}

std::string DeviceName(bool capture) {
  switch (Chosen()) {
    case kNull: return "Loopback";
    case kAlsa: return std::string(capture ? "ALSA capture " : "ALSA ") + AlsaName();
    case kCoreAudio: return capture ? "Default input" : "Default output";
  }
  return "";
}

Device* OpenDevice(bool capture, int rate, int channels) {
  switch (Chosen()) {
    case kNull: return new NullDevice(capture, rate, channels);
    case kAlsa: {
      void* pcm = AlsaOpen(capture, rate, channels, 0);
      return pcm ? new AlsaDevice(pcm, channels, capture) : nullptr;
    }
    case kCoreAudio: {
#ifdef __APPLE__
      ca::Stream* s = ca::Open(capture, rate, channels);
      return s ? new CoreAudioDevice(s) : nullptr;
#else
      return nullptr;
#endif
    }
  }
  return nullptr;
}

}  // namespace audio
}  // namespace winport
