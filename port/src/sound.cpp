// sound.cpp - PlaySound and sndPlaySound, which play a WAVE file, resource
// or image in memory through waveOut, Beep, and the multimedia timers.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#include <windows.h>
#include <mmsystem.h>

#include "winport.h"

#include <math.h>
#include <string.h>
#include <time.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <fstream>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#if defined(__APPLE__) && defined(__has_include)
#if __has_include(<pthread/qos.h>)
#include <pthread.h>
#include <pthread/qos.h>
#define WINPORT_HAVE_QOS 1
#endif
#endif

namespace {

// ---- PlaySound ----

struct Sound {
  std::vector<BYTE> bytes;  // the whole RIFF image
  const WAVEFORMATEX* format = nullptr;
  const BYTE* data = nullptr;
  DWORD size = 0;
};

uint32_t Le32(const BYTE* p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }

// Finds the format and the samples in a RIFF WAVE image.
bool ParseWave(Sound* s) {
  const BYTE* p = s->bytes.data();
  size_t n = s->bytes.size();
  if (n < 12 || memcmp(p, "RIFF", 4) || memcmp(p + 8, "WAVE", 4)) return false;
  size_t end = std::min<size_t>(n, 8 + (size_t)Le32(p + 4));
  for (size_t at = 12; at + 8 <= end;) {
    uint32_t size = Le32(p + at + 4);
    const BYTE* body = p + at + 8;
    size_t have = std::min<size_t>(size, end - at - 8);
    if (!memcmp(p + at, "fmt ", 4) && have >= 16) {
      if (have < sizeof(WAVEFORMATEX)) {
        // A PCMWAVEFORMAT: give it a cbSize of 0 at the end of the image.
        std::vector<BYTE> f(body, body + have);
        f.resize(sizeof(WAVEFORMATEX), 0);
        size_t off = s->bytes.size();
        s->bytes.insert(s->bytes.end(), f.begin(), f.end());
        p = s->bytes.data();
        s->format = (const WAVEFORMATEX*)(p + off);
        body = p + at + 8;
      } else {
        s->format = (const WAVEFORMATEX*)(p + at + 8);
      }
    } else if (!memcmp(p + at, "data", 4)) {
      s->data = (const BYTE*)(uintptr_t)(at + 8);  // an offset until the end
      s->size = (DWORD)have;
    }
    at += 8 + (size_t)size + (size & 1);
  }
  if (!s->format || !s->data) return false;
  // The samples were kept as an offset, since the image may have grown.
  s->data = s->bytes.data() + (uintptr_t)s->data;
  return true;
}

bool LoadFile(const std::string& name, Sound* s) {
  std::string p = name;
  for (char& c : p)
    if (c == '\\') c = '/';
  std::ifstream f(p, std::ios::binary);
  if (!f) return false;
  s->bytes.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
  return true;
}

// A sound's file from the registry, as the sound scheme names it.
bool LoadAlias(const std::string& name, Sound* s) {
  std::string key = "AppEvents\\Schemes\\Apps\\.Default\\" + name + "\\.Current";
  char path[MAX_PATH];
  LONG size = sizeof path;
  if (RegQueryValueA(HKEY_CURRENT_USER, key.c_str(), path, &size) != ERROR_SUCCESS || !path[0]) return false;
  return LoadFile(path, s);
}

struct Player {
  std::mutex lock;
  std::condition_variable changed;
  uint64_t generation = 0;  // the sound that should be playing
  int running = 0;          // asynchronous sounds still going
};
Player& ThePlayer() {
  static Player* p = new Player;
  return *p;
}

// Stops what is playing, and waits until it has.
void StopAll() {
  Player& pl = ThePlayer();
  std::unique_lock<std::mutex> g(pl.lock);
  pl.generation++;
  pl.changed.wait(g, [&] { return pl.running == 0; });
}

bool Play(std::shared_ptr<Sound> s, bool loop, uint64_t generation) {
  Player& pl = ThePlayer();
  HWAVEOUT out;
  HANDLE done = CreateEventA(nullptr, FALSE, FALSE, nullptr);
  if (waveOutOpen(&out, WAVE_MAPPER, s->format, (DWORD_PTR)done, 0, CALLBACK_EVENT) != MMSYSERR_NOERROR) {
    CloseHandle(done);
    return false;
  }
  WAVEHDR h;
  memset(&h, 0, sizeof h);
  h.lpData = (LPSTR)s->data;
  h.dwBufferLength = s->size;
  if (loop) {
    h.dwFlags = WHDR_BEGINLOOP | WHDR_ENDLOOP;
    h.dwLoops = 0xFFFFFFFF;
  }
  waveOutPrepareHeader(out, &h, sizeof h);
  waveOutWrite(out, &h, sizeof h);
  for (;;) {
    WaitForSingleObject(done, 20);
    if (h.dwFlags & WHDR_DONE) break;
    std::lock_guard<std::mutex> g(pl.lock);
    if (pl.generation != generation) break;
  }
  waveOutReset(out);
  waveOutUnprepareHeader(out, &h, sizeof h);
  waveOutClose(out);
  CloseHandle(done);
  return true;
}

BOOL PlayA(const char* name, HMODULE module, DWORD flags) {
  Player& pl = ThePlayer();
  if (!name || (flags & SND_PURGE)) {
    StopAll();
    return TRUE;
  }
  if (flags & SND_NOSTOP) {
    std::lock_guard<std::mutex> g(pl.lock);
    if (pl.running) return FALSE;
  }
  auto s = std::make_shared<Sound>();
  bool loaded = false;
  if (flags & SND_MEMORY) {
    const BYTE* p = (const BYTE*)name;
    if (!memcmp(p, "RIFF", 4)) {
      s->bytes.assign(p, p + 8 + Le32(p + 4));
      loaded = true;
    }
  } else if ((flags & SND_RESOURCE) == SND_RESOURCE) {
    HRSRC r = FindResourceA(module, name, "WAVE");
    if (r) {
      const BYTE* p = (const BYTE*)LockResource(LoadResource(module, r));
      if (p) {
        s->bytes.assign(p, p + SizeofResource(module, r));
        loaded = true;
      }
    }
  } else if ((flags & SND_ALIAS_ID) == SND_ALIAS_ID) {
    loaded = false;
  } else if (flags & SND_ALIAS) {
    loaded = LoadAlias(name, s.get());
  } else if (flags & SND_FILENAME) {
    loaded = LoadFile(name, s.get());
  } else {
    loaded = LoadAlias(name, s.get()) || LoadFile(name, s.get());
  }
  if (!loaded || !ParseWave(s.get())) {
    if (flags & SND_NODEFAULT) return FALSE;
    s = std::make_shared<Sound>();
    if (!LoadAlias(".Default", s.get()) || !ParseWave(s.get())) return FALSE;
  }
  StopAll();
  uint64_t gen;
  {
    std::lock_guard<std::mutex> g(pl.lock);
    gen = pl.generation;
  }
  if (!(flags & SND_ASYNC)) return Play(s, false, gen) ? TRUE : FALSE;
  {
    std::lock_guard<std::mutex> g(pl.lock);
    pl.running++;
  }
  bool loop = (flags & SND_LOOP) != 0;
  std::thread([s, loop, gen] {
    Play(s, loop, gen);
    Player& p = ThePlayer();
    std::lock_guard<std::mutex> g(p.lock);
    p.running--;
    p.changed.notify_all();
  }).detach();
  return TRUE;
}

// ---- Timers ----

struct Timer {
  UINT id;
  UINT delay;
  LPTIMECALLBACK proc;
  DWORD_PTR user;
  UINT flags;
  bool killed = false;
  std::thread thread;
};

struct Timers {
  std::mutex lock;
  std::condition_variable changed;
  std::map<UINT, std::shared_ptr<Timer>> all;
  UINT next = 1;
};
Timers& TheTimers() {
  static Timers* t = new Timers;
  return *t;
}

void RunTimer(std::shared_ptr<Timer> t) {
  Timers& ts = TheTimers();
#ifdef WINPORT_HAVE_QOS
  // Windows runs these callbacks at time-critical priority. macOS gives a
  // thread of a program not in the foreground (one started from a shell
  // or a service, say) a lower quality of service, whose timed waits it
  // lets run late to save power: tens of milliseconds, a 10 ms timer's
  // ticks lost. Asking for interactive service keeps the waits on time.
  if (__builtin_available(macOS 10.10, *))
    pthread_set_qos_class_self_np(QOS_CLASS_USER_INTERACTIVE, 0);
#endif
  auto due = std::chrono::steady_clock::now();
  for (;;) {
    due += std::chrono::milliseconds(t->delay);
    {
      std::unique_lock<std::mutex> g(ts.lock);
      ts.changed.wait_until(g, due, [&] { return t->killed; });
      if (t->killed) return;
    }
    switch (t->flags & (TIME_CALLBACK_EVENT_SET | TIME_CALLBACK_EVENT_PULSE)) {
      case TIME_CALLBACK_EVENT_SET: SetEvent((HANDLE)t->proc); break;
      case TIME_CALLBACK_EVENT_PULSE: PulseEvent((HANDLE)t->proc); break;
      default:
        if (t->proc) t->proc(t->id, 0, t->user, 0, 0);
        break;
    }
    if (!(t->flags & TIME_PERIODIC)) {
      std::lock_guard<std::mutex> g(ts.lock);
      auto it = ts.all.find(t->id);
      if (it != ts.all.end() && it->second == t) {
        it->second->thread.detach();
        ts.all.erase(it);
      }
      return;
    }
    // A periodic timer that has fallen behind does not try to catch up.
    auto now = std::chrono::steady_clock::now();
    if (now > due + std::chrono::milliseconds(t->delay)) due = now;
  }
}

}  // namespace

extern "C" {

BOOL WINAPI PlaySoundA(LPCSTR name, HMODULE module, DWORD flags) { return PlayA(name, module, flags); }

BOOL WINAPI PlaySoundW(LPCWSTR name, HMODULE module, DWORD flags) {
  if (!name || (flags & SND_MEMORY) || IS_INTRESOURCE(name) || (flags & SND_ALIAS_ID) == SND_ALIAS_ID)
    return PlayA((LPCSTR)name, module, flags);
  return PlayA(winport::Narrow(name).c_str(), module, flags);
}

BOOL WINAPI sndPlaySoundA(LPCSTR name, UINT flags) {
  return PlaySoundA(name, nullptr, flags & (SND_ASYNC | SND_NODEFAULT | SND_MEMORY | SND_LOOP | SND_NOSTOP));
}

BOOL WINAPI sndPlaySoundW(LPCWSTR name, UINT flags) {
  return PlaySoundW(name, nullptr, flags & (SND_ASYNC | SND_NODEFAULT | SND_MEMORY | SND_LOOP | SND_NOSTOP));
}

// A tone on the sound device; without one, the time passes all the same.
BOOL WINAPI Beep(DWORD frequency, DWORD ms) {
  if (frequency < 37 || frequency > 32767) {
    SetLastError(ERROR_INVALID_PARAMETER);
    return FALSE;
  }
  const int rate = 22050;
  WAVEFORMATEX f = {WAVE_FORMAT_PCM, 1, rate, rate * 2, 2, 16, 0};
  std::vector<short> tone((size_t)rate * ms / 1000);
  for (size_t i = 0; i < tone.size(); i++) {
    double s = sin(2 * 3.14159265358979 * frequency * (double)i / rate);
    // Soft edges, so that the tone does not click.
    double edge = std::min<double>(1.0, std::min<double>(i, tone.size() - i) / (rate / 200.0));
    tone[i] = (short)(12000 * s * edge);
  }
  HWAVEOUT out;
  HANDLE done = CreateEventA(nullptr, FALSE, FALSE, nullptr);
  if (tone.empty() || waveOutOpen(&out, WAVE_MAPPER, &f, (DWORD_PTR)done, 0, CALLBACK_EVENT) != MMSYSERR_NOERROR) {
    CloseHandle(done);
    Sleep(ms);
    return TRUE;
  }
  WAVEHDR h;
  memset(&h, 0, sizeof h);
  h.lpData = (LPSTR)tone.data();
  h.dwBufferLength = (DWORD)(tone.size() * sizeof(short));
  waveOutPrepareHeader(out, &h, sizeof h);
  waveOutWrite(out, &h, sizeof h);
  while (!(h.dwFlags & WHDR_DONE)) WaitForSingleObject(done, 50);
  waveOutUnprepareHeader(out, &h, sizeof h);
  waveOutClose(out);
  CloseHandle(done);
  return TRUE;
}

DWORD WINAPI timeGetTime(void) {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (DWORD)((uint64_t)ts.tv_sec * 1000 + (uint64_t)ts.tv_nsec / 1000000);
}

MMRESULT WINAPI timeGetSystemTime(LPMMTIME t, UINT size) {
  if (!t || size < sizeof(MMTIME)) return TIMERR_STRUCT;
  t->wType = TIME_MS;
  t->u.ms = timeGetTime();
  return TIMERR_NOERROR;
}

MMRESULT WINAPI timeGetDevCaps(LPTIMECAPS caps, UINT size) {
  if (!caps || size < sizeof(TIMECAPS)) return TIMERR_STRUCT;
  caps->wPeriodMin = 1;
  caps->wPeriodMax = 1000000;
  return TIMERR_NOERROR;
}

MMRESULT WINAPI timeBeginPeriod(UINT period) { return period ? TIMERR_NOERROR : TIMERR_NOCANDO; }
MMRESULT WINAPI timeEndPeriod(UINT period) { return period ? TIMERR_NOERROR : TIMERR_NOCANDO; }

MMRESULT WINAPI timeSetEvent(UINT delay, UINT, LPTIMECALLBACK proc, DWORD_PTR user, UINT flags) {
  if (delay < 1 || delay > 1000000) return 0;
  auto t = std::make_shared<Timer>();
  t->delay = delay;
  t->proc = proc;
  t->user = user;
  t->flags = flags;
  Timers& ts = TheTimers();
  std::lock_guard<std::mutex> g(ts.lock);
  t->id = ts.next++;
  if (!ts.next) ts.next = 1;
  ts.all[t->id] = t;
  t->thread = std::thread(RunTimer, t);
  return t->id;
}

MMRESULT WINAPI timeKillEvent(UINT id) {
  Timers& ts = TheTimers();
  std::shared_ptr<Timer> t;
  {
    std::lock_guard<std::mutex> g(ts.lock);
    auto it = ts.all.find(id);
    if (it == ts.all.end()) return MMSYSERR_INVALPARAM;
    t = it->second;
    ts.all.erase(it);
    t->killed = true;
    ts.changed.notify_all();
  }
  // No callback runs after this returns, unless it is the one calling.
  if (t->thread.get_id() == std::this_thread::get_id())
    t->thread.detach();
  else if (t->thread.joinable())
    t->thread.join();
  return TIMERR_NOERROR;
}

}  // extern "C"
