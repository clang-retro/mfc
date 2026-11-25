// audio_coreaudio.cpp - sound on macOS through audio queues. The queue
// runs on its own thread; a ring between it and the layer's writes and
// reads lets those block the way the other devices do. AudioToolbox is
// looked up at run time, so that programs need not link it.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#ifdef __APPLE__

#include "audio_ca.h"

// Audio queues came with Mac OS X 10.5. With an older SDK there is no
// Core Audio device here, and the layer has only the loopback device.
#if !__has_include(<AudioToolbox/AudioQueue.h>)
namespace winport {
namespace audio {
namespace ca {
/// Says that there is no device: this SDK has no audio queues.
bool Available(bool) { return false; }
/// Opens nothing.
Stream* Open(bool, int, int) { return nullptr; }
/// Writes nothing.
bool Write(Stream*, const int16_t*, size_t) { return false; }
/// Reads nothing.
size_t Read(Stream*, int16_t*, size_t) { return 0; }
/// Has no frames waiting.
size_t Delay(Stream*) { return 0; }
/// Does nothing.
void Pause(Stream*, bool) {}
/// Does nothing.
void Drop(Stream*) {}
/// Does nothing.
void Close(Stream*) {}
}  // namespace ca
}  // namespace audio
}  // namespace winport
#else

#include <AudioToolbox/AudioQueue.h>
#include <dlfcn.h>
#include <string.h>

#include <algorithm>
#include <condition_variable>
#include <deque>
#include <mutex>

namespace winport {
namespace audio {
namespace ca {

namespace {

struct Api {
  bool ok = false;
  decltype(&AudioQueueNewOutput) NewOutput;
  decltype(&AudioQueueNewInput) NewInput;
  decltype(&AudioQueueAllocateBuffer) AllocateBuffer;
  decltype(&AudioQueueEnqueueBuffer) EnqueueBuffer;
  decltype(&AudioQueueStart) Start;
  decltype(&AudioQueuePause) Pause;
  decltype(&AudioQueueStop) Stop;
  decltype(&AudioQueueDispose) Dispose;
};

const Api& Load() {
  static Api api = [] {
    Api a;
    void* lib = dlopen("/System/Library/Frameworks/AudioToolbox.framework/AudioToolbox", RTLD_NOW | RTLD_LOCAL);
    if (!lib) return a;
#define WINPORT_CA(name) a.name = (decltype(a.name))dlsym(lib, "AudioQueue" #name)
    WINPORT_CA(NewOutput);
    WINPORT_CA(NewInput);
    WINPORT_CA(AllocateBuffer);
    WINPORT_CA(EnqueueBuffer);
    WINPORT_CA(Start);
    WINPORT_CA(Pause);
    WINPORT_CA(Stop);
    WINPORT_CA(Dispose);
#undef WINPORT_CA
    a.ok = a.NewOutput && a.NewInput && a.AllocateBuffer && a.EnqueueBuffer && a.Start && a.Pause && a.Stop &&
           a.Dispose;
    return a;
  }();
  return api;
}

const int kBuffers = 3;

}  // namespace

struct Stream {
  bool capture = false;
  int channels = 1;
  size_t bufferFrames = 0;
  AudioQueueRef queue = nullptr;
  AudioQueueBufferRef buffers[kBuffers] = {};
  size_t real[kBuffers] = {};  // frames of sound (not padding) in each buffer
  std::mutex lock;
  std::condition_variable changed;
  std::deque<int16_t> ring;
  size_t ringLimit = 0;  // samples
  size_t inFlight = 0;   // frames handed to the queue and not yet back
  bool started = false, paused = false, closing = false;
};

namespace {

int Index(Stream* s, AudioQueueBufferRef b) {
  for (int i = 0; i < kBuffers; i++)
    if (s->buffers[i] == b) return i;
  return 0;
}

// Output: refills a buffer the queue has finished with.
void Fill(Stream* s, AudioQueueBufferRef b) {
  int i = Index(s, b);
  s->inFlight -= s->real[i];
  size_t samples = s->bufferFrames * s->channels;
  int16_t* out = (int16_t*)b->mAudioData;
  size_t n = std::min(samples, s->ring.size());
  n -= n % s->channels;
  for (size_t k = 0; k < n; k++) {
    out[k] = s->ring.front();
    s->ring.pop_front();
  }
  memset(out + n, 0, (samples - n) * sizeof(int16_t));
  b->mAudioDataByteSize = (UInt32)(samples * sizeof(int16_t));
  s->real[i] = n / s->channels;
  s->inFlight += s->real[i];
}

void OutputDone(void* user, AudioQueueRef q, AudioQueueBufferRef b) {
  Stream* s = (Stream*)user;
  std::lock_guard<std::mutex> g(s->lock);
  if (s->closing) return;
  Fill(s, b);
  Load().EnqueueBuffer(q, b, 0, nullptr);
  s->changed.notify_all();
}

void InputDone(void* user, AudioQueueRef q, AudioQueueBufferRef b, const AudioTimeStamp*, UInt32,
               const AudioStreamPacketDescription*) {
  Stream* s = (Stream*)user;
  std::lock_guard<std::mutex> g(s->lock);
  if (s->closing) return;
  const int16_t* in = (const int16_t*)b->mAudioData;
  size_t n = b->mAudioDataByteSize / sizeof(int16_t);
  for (size_t k = 0; k < n; k++) s->ring.push_back(in[k]);
  // Keep at most a second; a reader that falls behind loses the oldest.
  while (s->ring.size() > s->ringLimit) s->ring.pop_front();
  Load().EnqueueBuffer(q, b, 0, nullptr);
  s->changed.notify_all();
}

AudioStreamBasicDescription Describe(int rate, int channels) {
  AudioStreamBasicDescription d;
  memset(&d, 0, sizeof d);
  d.mSampleRate = rate;
  d.mFormatID = kAudioFormatLinearPCM;
  d.mFormatFlags = kAudioFormatFlagIsSignedInteger | kAudioFormatFlagIsPacked;
  d.mBytesPerPacket = d.mBytesPerFrame = (UInt32)(2 * channels);
  d.mFramesPerPacket = 1;
  d.mChannelsPerFrame = (UInt32)channels;
  d.mBitsPerChannel = 16;
  return d;
}

}  // namespace

bool Available(bool capture) {
  static int known[2] = {-1, -1};
  int& k = known[capture ? 1 : 0];
  if (k < 0) {
    Stream* s = Open(capture, 8000, 1);
    k = s != nullptr;
    if (s) Close(s);
  }
  return k == 1;
}

Stream* Open(bool capture, int rate, int channels) {
  const Api& api = Load();
  if (!api.ok) return nullptr;
  Stream* s = new Stream;
  s->capture = capture;
  s->channels = channels;
  s->bufferFrames = (size_t)(rate / 50 > 64 ? rate / 50 : 64);  // 20 ms
  s->ringLimit = capture ? (size_t)rate * channels : s->bufferFrames * kBuffers * channels;
  AudioStreamBasicDescription d = Describe(rate, channels);
  OSStatus st = capture ? api.NewInput(&d, InputDone, s, nullptr, nullptr, 0, &s->queue)
                        : api.NewOutput(&d, OutputDone, s, nullptr, nullptr, 0, &s->queue);
  if (st != 0) {
    delete s;
    return nullptr;
  }
  UInt32 bytes = (UInt32)(s->bufferFrames * channels * sizeof(int16_t));
  for (int i = 0; i < kBuffers; i++) {
    if (api.AllocateBuffer(s->queue, bytes, &s->buffers[i]) != 0) {
      api.Dispose(s->queue, true);
      delete s;
      return nullptr;
    }
    if (capture) api.EnqueueBuffer(s->queue, s->buffers[i], 0, nullptr);
  }
  if (capture) {
    if (api.Start(s->queue, nullptr) != 0) {
      api.Dispose(s->queue, true);
      delete s;
      return nullptr;
    }
    s->started = true;
  }
  return s;
}

bool Write(Stream* s, const int16_t* frames, size_t n) {
  const Api& api = Load();
  size_t samples = n * s->channels;
  size_t done = 0;
  std::unique_lock<std::mutex> g(s->lock);
  while (done < samples) {
    s->changed.wait(g, [&] { return s->ring.size() < s->ringLimit || s->closing; });
    if (s->closing) return false;
    while (done < samples && s->ring.size() < s->ringLimit) s->ring.push_back(frames[done++]);
    if (!s->started && (s->ring.size() >= s->ringLimit || done == samples)) {
      // Prime every buffer, then start.
      for (int i = 0; i < kBuffers; i++) {
        Fill(s, s->buffers[i]);
        api.EnqueueBuffer(s->queue, s->buffers[i], 0, nullptr);
      }
      s->started = true;
      if (!s->paused && api.Start(s->queue, nullptr) != 0) return false;
    }
  }
  return true;
}

size_t Read(Stream* s, int16_t* frames, size_t n) {
  size_t samples = n * s->channels;
  std::unique_lock<std::mutex> g(s->lock);
  s->changed.wait(g, [&] { return s->ring.size() >= samples || s->closing; });
  size_t got = std::min(samples, s->ring.size());
  got -= got % s->channels;
  for (size_t k = 0; k < got; k++) {
    frames[k] = s->ring.front();
    s->ring.pop_front();
  }
  return got / s->channels;
}

size_t Delay(Stream* s) {
  std::lock_guard<std::mutex> g(s->lock);
  if (s->capture) return s->ring.size() / s->channels;
  return s->ring.size() / s->channels + s->inFlight;
}

void Pause(Stream* s, bool on) {
  const Api& api = Load();
  std::lock_guard<std::mutex> g(s->lock);
  if (s->paused == on) return;
  s->paused = on;
  if (!s->started) return;
  if (on)
    api.Pause(s->queue);
  else
    api.Start(s->queue, nullptr);
}

void Drop(Stream* s) {
  std::lock_guard<std::mutex> g(s->lock);
  s->ring.clear();
  s->changed.notify_all();
}

void Close(Stream* s) {
  const Api& api = Load();
  {
    std::lock_guard<std::mutex> g(s->lock);
    s->closing = true;
    s->changed.notify_all();
  }
  api.Stop(s->queue, true);
  api.Dispose(s->queue, true);
  delete s;
}

}  // namespace ca
}  // namespace audio
}  // namespace winport

#endif  // __has_include(<AudioToolbox/AudioQueue.h>)
#endif
