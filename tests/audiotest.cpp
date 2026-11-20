// audiotest.cpp - console tests for voice audio: the ACM (format details,
// enumeration and suggestion, and streams through mu-law, IMA ADPCM and a
// rate change), RIFF files through mmio, the multimedia timers, and, on a
// device, waveOut and waveIn: a tone played in mu-law and recorded back
// (on Linux and macOS through the loopback device), positions, pause,
// reset, loops, a partly filled recording buffer, PlaySound and Beep.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#include <afx.h>
#include <mmsystem.h>
#include <mmreg.h>
#include <msacm.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if !defined(_AFX_NOFORCE_LIBS) && defined(_WIN32) && !defined(_WINPORT)
__asm__(".section .drectve,\"yn\"\n"
        ".ascii \" -defaultlib:winmm -defaultlib:msacm32\"\n"
        ".text");
#endif

static int g_failures = 0;
#define CHECK(cond)                                          \
  do {                                                       \
    if (!(cond)) {                                           \
      printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
      g_failures++;                                          \
    }                                                        \
  } while (0)

static WAVEFORMATEX Pcm(int rate, int channels, int bits) {
  WAVEFORMATEX w;
  memset(&w, 0, sizeof w);
  w.wFormatTag = WAVE_FORMAT_PCM;
  w.nChannels = (WORD)channels;
  w.nSamplesPerSec = (DWORD)rate;
  w.wBitsPerSample = (WORD)bits;
  w.nBlockAlign = (WORD)(channels * bits / 8);
  w.nAvgBytesPerSec = w.nBlockAlign * (DWORD)rate;
  return w;
}

static WAVEFORMATEX MuLaw(int rate) {
  WAVEFORMATEX w = Pcm(rate, 1, 8);
  w.wFormatTag = WAVE_FORMAT_MULAW;
  return w;
}

// A tone: n frames of 16-bit mono at 8000 Hz, 500 Hz, at amplitude a.
static void Tone(short* out, int n, int a) {
  for (int i = 0; i < n; i++) out[i] = (short)(a * sin(2 * 3.14159265358979 * 500 * i / 8000.0));
}

// Converts src through a stream into dst, returning the bytes made.
static DWORD Convert(LPWAVEFORMATEX from, LPWAVEFORMATEX to, const void* src, DWORD srcLen, void* dst, DWORD dstLen,
                     DWORD* used) {
  HACMSTREAM s = NULL;
  MMRESULT r = acmStreamOpen(&s, NULL, from, to, NULL, 0, 0, ACM_STREAMOPENF_NONREALTIME);
  CHECK(r == MMSYSERR_NOERROR);
  if (r) return 0;
  DWORD need = 0;
  CHECK(acmStreamSize(s, srcLen, &need, ACM_STREAMSIZEF_SOURCE) == MMSYSERR_NOERROR);
  CHECK(need <= dstLen);
  ACMSTREAMHEADER h;
  memset(&h, 0, sizeof h);
  h.cbStruct = sizeof h;
  h.pbSrc = (LPBYTE)src;
  h.cbSrcLength = srcLen;
  h.pbDst = (LPBYTE)dst;
  h.cbDstLength = dstLen;
  CHECK(acmStreamPrepareHeader(s, &h, 0) == MMSYSERR_NOERROR);
  CHECK(acmStreamConvert(s, &h, ACM_STREAMCONVERTF_START | ACM_STREAMCONVERTF_END) == MMSYSERR_NOERROR);
  CHECK(h.fdwStatus & ACMSTREAMHEADER_STATUSF_DONE);
  CHECK(acmStreamUnprepareHeader(s, &h, 0) == MMSYSERR_NOERROR);
  CHECK(acmStreamClose(s, 0) == MMSYSERR_NOERROR);
  if (used) *used = h.cbSrcLengthUsed;
  return h.cbDstLengthUsed;
}

static int g_enumCount = 0, g_enumWrong = 0;
static BOOL CALLBACK EnumFormat(HACMDRIVERID, LPACMFORMATDETAILSA d, DWORD_PTR, DWORD) {
  g_enumCount++;
  if (d->pwfx->wFormatTag != WAVE_FORMAT_PCM || d->pwfx->nChannels != 1 || d->pwfx->nSamplesPerSec != 8000)
    g_enumWrong++;
  return TRUE;
}

static void TestAcm() {
  DWORD n = 0;
  CHECK(acmMetrics(NULL, ACM_METRIC_COUNT_DRIVERS, &n) == MMSYSERR_NOERROR && n >= 1);
  CHECK(acmMetrics(NULL, ACM_METRIC_MAX_SIZE_FORMAT, &n) == MMSYSERR_NOERROR && n >= sizeof(IMAADPCMWAVEFORMAT));

  // The tags there are.
  ACMFORMATTAGDETAILSA tag;
  memset(&tag, 0, sizeof tag);
  tag.cbStruct = sizeof tag;
  tag.dwFormatTag = WAVE_FORMAT_IMA_ADPCM;
  CHECK(acmFormatTagDetailsA(NULL, &tag, ACM_FORMATTAGDETAILSF_FORMATTAG) == MMSYSERR_NOERROR);
  CHECK(tag.cbFormatSize >= sizeof(IMAADPCMWAVEFORMAT));
  tag.dwFormatTag = WAVE_FORMAT_MULAW;
  CHECK(acmFormatTagDetailsA(NULL, &tag, ACM_FORMATTAGDETAILSF_FORMATTAG) == MMSYSERR_NOERROR);

  // A format's name.
  WAVEFORMATEX pcm = Pcm(8000, 1, 16);
  ACMFORMATDETAILSA fd;
  memset(&fd, 0, sizeof fd);
  fd.cbStruct = sizeof fd;
  fd.pwfx = &pcm;
  fd.cbwfx = sizeof pcm;
  fd.dwFormatTag = WAVE_FORMAT_PCM;
  CHECK(acmFormatDetailsA(NULL, &fd, ACM_FORMATDETAILSF_FORMAT) == MMSYSERR_NOERROR);
  CHECK(strstr(fd.szFormat, "16") != NULL);

  // Formats matching a mask.
  BYTE buf[64];
  memset(buf, 0, sizeof buf);
  memcpy(buf, &pcm, sizeof pcm);
  memset(&fd, 0, sizeof fd);
  fd.cbStruct = sizeof fd;
  fd.pwfx = (LPWAVEFORMATEX)buf;
  fd.cbwfx = sizeof buf;
  fd.dwFormatTag = WAVE_FORMAT_PCM;
  CHECK(acmFormatEnumA(NULL, &fd, EnumFormat, 0,
                       ACM_FORMATENUMF_WFORMATTAG | ACM_FORMATENUMF_NCHANNELS | ACM_FORMATENUMF_NSAMPLESPERSEC) ==
        MMSYSERR_NOERROR);
  CHECK(g_enumCount >= 1);
  CHECK(g_enumWrong == 0);

  // A suggestion: IMA ADPCM for 16-bit mono at 8000 Hz.
  BYTE sug[64];
  memset(sug, 0, sizeof sug);
  LPWAVEFORMATEX ima = (LPWAVEFORMATEX)sug;
  ima->wFormatTag = WAVE_FORMAT_IMA_ADPCM;
  CHECK(acmFormatSuggest(NULL, &pcm, ima, sizeof sug, ACM_FORMATSUGGESTF_WFORMATTAG) == MMSYSERR_NOERROR);
  CHECK(ima->wFormatTag == WAVE_FORMAT_IMA_ADPCM);
  CHECK(ima->nChannels == 1 && ima->nSamplesPerSec == 8000 && ima->wBitsPerSample == 4);
  CHECK(ima->nBlockAlign == 256);
  CHECK(ima->cbSize >= 2 && ((IMAADPCMWAVEFORMAT*)ima)->wSamplesPerBlock == 505);

  // Streams: 16-bit PCM to mu-law and back.
  const int kFrames = 2020;  // four IMA blocks
  short tone[kFrames], back[kFrames + 1024];
  Tone(tone, kFrames, 8000);
  WAVEFORMATEX mu = MuLaw(8000);
  BYTE law[kFrames + 64];
  DWORD used = 0;
  CHECK(Convert(&pcm, &mu, tone, sizeof tone, law, sizeof law, &used) == (DWORD)kFrames);
  CHECK(used == sizeof tone);
  CHECK(Convert(&mu, &pcm, law, kFrames, back, sizeof back, NULL) == sizeof tone);
  int worst = 0;
  for (int i = 0; i < kFrames; i++) {
    int d = abs(back[i] - tone[i]);
    if (d > worst) worst = d;
  }
  CHECK(worst < 400);  // mu-law keeps about 3% at this level

  // To IMA ADPCM and back.
  BYTE adpcm[4 * 256 + 256];
  DWORD made = Convert(&pcm, ima, tone, sizeof tone, adpcm, sizeof adpcm, NULL);
  CHECK(made == 4 * 256);
  DWORD out = Convert(ima, &pcm, adpcm, made, back, sizeof back, NULL);
  CHECK(out == sizeof tone);
  // The step size starts small, so the first few samples lag behind.
  worst = 0;
  for (int i = 16; i < kFrames; i++) {
    int d = abs(back[i] - tone[i]);
    if (d > worst) worst = d;
  }
  CHECK(worst < 1500);

  // A rate change: 8000 Hz to 16000 Hz is twice the frames.
  WAVEFORMATEX pcm16k = Pcm(16000, 1, 16);
  short twice[2 * kFrames + 64];
  out = Convert(&pcm, &pcm16k, tone, sizeof tone, twice, sizeof twice, NULL);
  CHECK(out >= (DWORD)(2 * kFrames - 8) * 2 && out <= (DWORD)(2 * kFrames + 8) * 2);

  // A filter is not a format, and nonsense is no format.
  WAVEFORMATEX bad = pcm;
  bad.wFormatTag = 0x7777;
  HACMSTREAM s;
  CHECK(acmStreamOpen(&s, NULL, &pcm, &bad, NULL, 0, 0, 0) == ACMERR_NOTPOSSIBLE);
  CHECK(acmStreamOpen(NULL, NULL, &pcm, &mu, NULL, 0, 0, ACM_STREAMOPENF_QUERY) == MMSYSERR_NOERROR);
}

static void TestMmio() {
  char path[MAX_PATH];
  GetTempPathA(MAX_PATH, path);
  strcat(path, "audiotest.wav");
  short tone[800];
  Tone(tone, 800, 5000);

  // Write a WAVE file.
  HMMIO h = mmioOpenA(path, NULL, MMIO_CREATE | MMIO_WRITE | MMIO_ALLOCBUF);
  CHECK(h != NULL);
  if (!h) return;
  MMCKINFO riff, ck;
  memset(&riff, 0, sizeof riff);
  riff.fccType = mmioFOURCC('W', 'A', 'V', 'E');
  CHECK(mmioCreateChunk(h, &riff, MMIO_CREATERIFF) == MMSYSERR_NOERROR);
  memset(&ck, 0, sizeof ck);
  ck.ckid = mmioFOURCC('f', 'm', 't', ' ');
  CHECK(mmioCreateChunk(h, &ck, 0) == MMSYSERR_NOERROR);
  WAVEFORMATEX pcm = Pcm(8000, 1, 16);
  CHECK(mmioWrite(h, (const char*)&pcm, 16) == 16);
  CHECK(mmioAscend(h, &ck, 0) == MMSYSERR_NOERROR);
  CHECK(ck.cksize == 16);
  memset(&ck, 0, sizeof ck);
  ck.ckid = mmioFOURCC('d', 'a', 't', 'a');
  CHECK(mmioCreateChunk(h, &ck, 0) == MMSYSERR_NOERROR);
  // Half through mmioWrite, half straight into the buffer.
  CHECK(mmioWrite(h, (const char*)tone, 800) == 800);
  MMIOINFO info;
  CHECK(mmioGetInfo(h, &info, 0) == MMSYSERR_NOERROR);
  const char* p = (const char*)tone + 800;
  for (int i = 0; i < 800; i++) {
    if (info.pchNext == info.pchEndWrite) {
      info.dwFlags |= MMIO_DIRTY;
      CHECK(mmioAdvance(h, &info, MMIO_WRITE) == MMSYSERR_NOERROR);
    }
    *info.pchNext++ = p[i];
  }
  info.dwFlags |= MMIO_DIRTY;
  CHECK(mmioSetInfo(h, &info, 0) == MMSYSERR_NOERROR);
  CHECK(mmioAscend(h, &ck, 0) == MMSYSERR_NOERROR);
  CHECK(ck.cksize == sizeof tone);
  CHECK(mmioAscend(h, &riff, 0) == MMSYSERR_NOERROR);
  CHECK(riff.cksize == 4 + 8 + 16 + 8 + sizeof tone);
  CHECK(mmioClose(h, 0) == MMSYSERR_NOERROR);

  // Read it back.
  h = mmioOpenA(path, NULL, MMIO_READ | MMIO_ALLOCBUF);
  CHECK(h != NULL);
  if (!h) return;
  memset(&riff, 0, sizeof riff);
  riff.fccType = mmioFOURCC('W', 'A', 'V', 'E');
  CHECK(mmioDescend(h, &riff, NULL, MMIO_FINDRIFF) == MMSYSERR_NOERROR);
  memset(&ck, 0, sizeof ck);
  ck.ckid = mmioFOURCC('d', 'a', 't', 'a');  // past fmt
  CHECK(mmioDescend(h, &ck, &riff, MMIO_FINDCHUNK) == MMSYSERR_NOERROR);
  CHECK(ck.cksize == sizeof tone);
  short back[800];
  CHECK(mmioRead(h, (HPSTR)back, sizeof back) == (LONG)sizeof back);
  CHECK(memcmp(back, tone, sizeof tone) == 0);
  CHECK(mmioAscend(h, &ck, 0) == MMSYSERR_NOERROR);
  CHECK(mmioSeek(h, 0, SEEK_CUR) == (LONG)(riff.cksize + 8));
  CHECK(mmioSeek(h, riff.dwDataOffset + 4, SEEK_SET) == (LONG)(riff.dwDataOffset + 4));
  memset(&ck, 0, sizeof ck);
  ck.ckid = mmioFOURCC('f', 'm', 't', ' ');
  CHECK(mmioDescend(h, &ck, &riff, MMIO_FINDCHUNK) == MMSYSERR_NOERROR);
  WAVEFORMATEX got;
  memset(&got, 0, sizeof got);
  CHECK(mmioRead(h, (HPSTR)&got, 16) == 16);
  CHECK(got.nSamplesPerSec == 8000 && got.wBitsPerSample == 16);
  memset(&ck, 0, sizeof ck);
  ck.ckid = mmioFOURCC('n', 'o', 'n', 'e');
  CHECK(mmioDescend(h, &ck, &riff, MMIO_FINDCHUNK) == MMIOERR_CHUNKNOTFOUND);
  CHECK(mmioClose(h, 0) == MMSYSERR_NOERROR);
  CHECK(mmioOpenA(path, NULL, MMIO_DELETE) != NULL);
  CHECK(mmioOpenA(path, NULL, MMIO_EXIST) == NULL);

  // A file in memory.
  char mem[64];
  memset(mem, 0, sizeof mem);
  memset(&info, 0, sizeof info);
  info.fccIOProc = FOURCC_MEM;
  info.pchBuffer = mem;
  info.cchBuffer = sizeof mem;
  h = mmioOpenA(NULL, &info, MMIO_READWRITE | MMIO_CREATE);
  CHECK(h != NULL);
  if (!h) return;
  CHECK(mmioWrite(h, "hello", 5) == 5);
  CHECK(memcmp(mem, "hello", 5) == 0);
  CHECK(mmioSeek(h, 1, SEEK_SET) == 1);
  char four[4];
  CHECK(mmioRead(h, four, 4) == 4 && memcmp(four, "ello", 4) == 0);
  CHECK(mmioClose(h, 0) == MMSYSERR_NOERROR);

  CHECK(mmioStringToFOURCCA("wave", MMIO_TOUPPER) == mmioFOURCC('W', 'A', 'V', 'E'));
}

static volatile LONG g_ticks = 0;
static void CALLBACK Tick(UINT, UINT, DWORD_PTR user, DWORD_PTR, DWORD_PTR) {
  InterlockedIncrement(&g_ticks);
  CHECK(user == 42);
}

static void TestTimers() {
  DWORD t0 = timeGetTime();
  Sleep(50);
  DWORD dt = timeGetTime() - t0;
  CHECK(dt >= 40 && dt < 2000);
  TIMECAPS caps;
  CHECK(timeGetDevCaps(&caps, sizeof caps) == TIMERR_NOERROR && caps.wPeriodMin >= 1);
  CHECK(timeBeginPeriod(1) == TIMERR_NOERROR);

  DWORD start = timeGetTime();
  UINT id = timeSetEvent(10, 1, Tick, 42, TIME_PERIODIC);
  CHECK(id != 0);
  Sleep(300);
  CHECK(timeKillEvent(id) == TIMERR_NOERROR);
  // About one tick each 10 ms of however long the sleep really took.
  LONG ticks = g_ticks;
  LONG most = (LONG)((timeGetTime() - start) / 10) + 5;
  CHECK(ticks >= 5 && ticks <= most);
  Sleep(50);
  CHECK(g_ticks == ticks);  // none after the kill
  CHECK(timeKillEvent(id) != TIMERR_NOERROR);

  HANDLE e = CreateEventA(NULL, FALSE, FALSE, NULL);
  id = timeSetEvent(20, 1, (LPTIMECALLBACK)e, 0, TIME_ONESHOT | TIME_CALLBACK_EVENT_SET);
  CHECK(id != 0);
  CHECK(WaitForSingleObject(e, 2000) == WAIT_OBJECT_0);
  CloseHandle(e);
  CHECK(timeEndPeriod(1) == TIMERR_NOERROR);
}

static volatile LONG g_done = 0, g_opened = 0, g_closed = 0;
static void CALLBACK OutProc(HWAVEOUT, UINT msg, DWORD_PTR, DWORD_PTR, DWORD_PTR) {
  if (msg == WOM_DONE) InterlockedIncrement(&g_done);
  if (msg == WOM_OPEN) InterlockedIncrement(&g_opened);
  if (msg == WOM_CLOSE) InterlockedIncrement(&g_closed);
}

static bool WaitFor(volatile LONG* v, LONG want, DWORD ms) {
  DWORD t0 = GetTickCount();
  while (*v < want && GetTickCount() - t0 < ms) Sleep(5);
  return *v >= want;
}

static void Header(WAVEHDR* h, void* data, DWORD len) {
  memset(h, 0, sizeof *h);
  h->lpData = (LPSTR)data;
  h->dwBufferLength = len;
}

static void TestWave() {
  WAVEFORMATEX pcm = Pcm(8000, 1, 16), mu = MuLaw(8000);
  WAVEFORMATEX bad = pcm;
  bad.wFormatTag = 0x7777;
  CHECK(waveOutOpen(NULL, WAVE_MAPPER, &pcm, 0, 0, WAVE_FORMAT_QUERY) == MMSYSERR_NOERROR);
  CHECK(waveOutOpen(NULL, WAVE_MAPPER, &bad, 0, 0, WAVE_FORMAT_QUERY) == WAVERR_BADFORMAT);
  CHECK(waveOutOpen(NULL, WAVE_MAPPER, &mu, 0, 0, WAVE_FORMAT_QUERY) == MMSYSERR_NOERROR);
  WAVEOUTCAPSA caps;
  CHECK(waveOutGetDevCapsA(0, &caps, sizeof caps) == MMSYSERR_NOERROR);
  CHECK(caps.szPname[0] != 0);
  char text[MAXERRORLENGTH];
  CHECK(waveOutGetErrorTextA(WAVERR_STILLPLAYING, text, sizeof text) == MMSYSERR_NOERROR && text[0]);

  // A tone in mu-law, played while recording.
  const int kFrames = 2400;  // 0.3 s
  static short tone[kFrames];
  static BYTE law[kFrames];
  Tone(tone, kFrames, 8000);
  DWORD used;
  Convert(&pcm, &mu, tone, sizeof tone, law, sizeof law, &used);

  HWAVEIN in = NULL;
  HANDLE inEvent = CreateEventA(NULL, FALSE, FALSE, NULL);
  bool haveIn = waveInGetNumDevs() > 0 &&
                waveInOpen(&in, WAVE_MAPPER, &pcm, (DWORD_PTR)inEvent, 0, CALLBACK_EVENT) == MMSYSERR_NOERROR;
  const int kInBufs = 8, kInFrames = 800;
  static short rec[kInBufs][kInFrames];
  WAVEHDR inHdr[kInBufs];
  if (haveIn) {
    for (int i = 0; i < kInBufs; i++) {
      Header(&inHdr[i], rec[i], sizeof rec[i]);
      CHECK(waveInPrepareHeader(in, &inHdr[i], sizeof(WAVEHDR)) == MMSYSERR_NOERROR);
      CHECK(waveInAddBuffer(in, &inHdr[i], sizeof(WAVEHDR)) == MMSYSERR_NOERROR);
    }
    CHECK(waveInStart(in) == MMSYSERR_NOERROR);
  }

  HWAVEOUT out = NULL;
  CHECK(waveOutOpen(&out, WAVE_MAPPER, &mu, (DWORD_PTR)OutProc, 0, CALLBACK_FUNCTION) == MMSYSERR_NOERROR);
  CHECK(g_opened == 1);
  WAVEHDR h[2];
  Header(&h[0], law, kFrames / 2);
  Header(&h[1], law + kFrames / 2, kFrames / 2);
  CHECK(waveOutWrite(out, &h[0], sizeof(WAVEHDR)) == WAVERR_UNPREPARED);
  DWORD t0 = GetTickCount();
  for (int i = 0; i < 2; i++) {
    CHECK(waveOutPrepareHeader(out, &h[i], sizeof(WAVEHDR)) == MMSYSERR_NOERROR);
    CHECK(waveOutWrite(out, &h[i], sizeof(WAVEHDR)) == MMSYSERR_NOERROR);
  }
  CHECK(waveOutClose(out) == WAVERR_STILLPLAYING);
  CHECK(WaitFor(&g_done, 2, 5000));
  DWORD took = GetTickCount() - t0;
  CHECK(took >= 200);  // played in real time
  CHECK(h[0].dwFlags & WHDR_DONE);
  CHECK(!(h[1].dwFlags & WHDR_INQUEUE));
  MMTIME pos;
  pos.wType = TIME_SAMPLES;
  CHECK(waveOutGetPosition(out, &pos, sizeof pos) == MMSYSERR_NOERROR);
  CHECK(pos.wType == TIME_SAMPLES && pos.u.sample >= kFrames - 100 && pos.u.sample <= kFrames);
  pos.wType = TIME_BYTES;
  CHECK(waveOutGetPosition(out, &pos, sizeof pos) == MMSYSERR_NOERROR);
  CHECK(pos.u.cb >= kFrames - 100 && pos.u.cb <= kFrames);  // a byte a sample in mu-law

  if (haveIn) {
    Sleep(150);
    CHECK(waveInStop(in) == MMSYSERR_NOERROR);
    MMTIME ipos;
    ipos.wType = TIME_SAMPLES;
    CHECK(waveInGetPosition(in, &ipos, sizeof ipos) == MMSYSERR_NOERROR);
    CHECK(ipos.u.sample >= (DWORD)kFrames);
    CHECK(waveInReset(in) == MMSYSERR_NOERROR);
#ifdef _WINPORT
    // What the loopback device recorded is the tone, through mu-law.
    int loud = 0, done = 0;
    for (int i = 0; i < kInBufs; i++) {
      if (inHdr[i].dwFlags & WHDR_DONE) done++;
      for (DWORD k = 0; k < inHdr[i].dwBytesRecorded / 2; k++)
        if (abs(rec[i][k]) > 2000) loud++;
    }
    CHECK(done == kInBufs);
    CHECK(loud > kFrames / 2);
#endif
    for (int i = 0; i < kInBufs; i++) CHECK(waveInUnprepareHeader(in, &inHdr[i], sizeof(WAVEHDR)) == MMSYSERR_NOERROR);
    CHECK(waveInClose(in) == MMSYSERR_NOERROR);
  }
  CloseHandle(inEvent);

  // Pause holds the position, and reset hands everything back.
  static short second[8000];
  Tone(second, 8000, 4000);
  CHECK(waveOutClose(out) == MMSYSERR_NOERROR);
  CHECK(g_closed == 1);
  CHECK(waveOutOpen(&out, WAVE_MAPPER, &pcm, (DWORD_PTR)OutProc, 0, CALLBACK_FUNCTION) == MMSYSERR_NOERROR);
  g_done = 0;
  WAVEHDR one;
  Header(&one, second, sizeof second);
  CHECK(waveOutPrepareHeader(out, &one, sizeof one) == MMSYSERR_NOERROR);
  CHECK(waveOutWrite(out, &one, sizeof one) == MMSYSERR_NOERROR);
  Sleep(150);
  CHECK(waveOutPause(out) == MMSYSERR_NOERROR);
  pos.wType = TIME_MS;
  CHECK(waveOutGetPosition(out, &pos, sizeof pos) == MMSYSERR_NOERROR);
  DWORD p1 = pos.u.ms;
  CHECK(p1 > 0 && p1 < 1000);
  Sleep(150);
  CHECK(waveOutGetPosition(out, &pos, sizeof pos) == MMSYSERR_NOERROR);
  CHECK(pos.u.ms == p1);
  CHECK(waveOutRestart(out) == MMSYSERR_NOERROR);
  Sleep(50);
  CHECK(waveOutReset(out) == MMSYSERR_NOERROR);
  CHECK(WaitFor(&g_done, 1, 1000));
  CHECK(one.dwFlags & WHDR_DONE);
  pos.wType = TIME_SAMPLES;
  CHECK(waveOutGetPosition(out, &pos, sizeof pos) == MMSYSERR_NOERROR);
  CHECK(pos.u.sample == 0);

  // A loop: 0.1 s three times.
  g_done = 0;
  Header(&one, second, 1600);
  one.dwFlags = WHDR_BEGINLOOP | WHDR_ENDLOOP;
  one.dwLoops = 3;
  CHECK(waveOutPrepareHeader(out, &one, sizeof one) == MMSYSERR_NOERROR);
  t0 = GetTickCount();
  CHECK(waveOutWrite(out, &one, sizeof one) == MMSYSERR_NOERROR);
  CHECK(WaitFor(&g_done, 1, 5000));
  took = GetTickCount() - t0;
  CHECK(took >= 250);
  pos.wType = TIME_SAMPLES;
  CHECK(waveOutGetPosition(out, &pos, sizeof pos) == MMSYSERR_NOERROR);
  CHECK(pos.u.sample >= 2300 && pos.u.sample <= 2400);
  CHECK(waveOutUnprepareHeader(out, &one, sizeof one) == MMSYSERR_NOERROR);
  CHECK(waveOutClose(out) == MMSYSERR_NOERROR);
  CHECK(waveOutClose(out) == MMSYSERR_INVALHANDLE);

  // A recording stopped early hands back what it has.
  if (waveInGetNumDevs() > 0 && waveInOpen(&in, WAVE_MAPPER, &pcm, 0, 0, CALLBACK_NULL) == MMSYSERR_NOERROR) {
    for (int i = 0; i < 2; i++) {
      Header(&inHdr[i], rec[i], sizeof rec[i]);
      waveInPrepareHeader(in, &inHdr[i], sizeof(WAVEHDR));
      waveInAddBuffer(in, &inHdr[i], sizeof(WAVEHDR));
    }
    CHECK(waveInStart(in) == MMSYSERR_NOERROR);
    // Until some sound is in, however slow the machine.
    MMTIME at = {};
    at.wType = TIME_BYTES;
    for (int i = 0; i < 200 && at.u.cb == 0; i++) {
      Sleep(10);
      waveInGetPosition(in, &at, sizeof at);
    }
    CHECK(at.u.cb > 0 && at.u.cb < sizeof rec[0]);
    CHECK(waveInStop(in) == MMSYSERR_NOERROR);
    CHECK(inHdr[0].dwFlags & WHDR_DONE);
    CHECK(inHdr[0].dwBytesRecorded > 0 && inHdr[0].dwBytesRecorded < sizeof rec[0]);
    CHECK(waveInClose(in) == WAVERR_STILLPLAYING);
    CHECK(waveInReset(in) == MMSYSERR_NOERROR);
    CHECK(inHdr[1].dwFlags & WHDR_DONE);
    for (int i = 0; i < 2; i++) waveInUnprepareHeader(in, &inHdr[i], sizeof(WAVEHDR));
    CHECK(waveInClose(in) == MMSYSERR_NOERROR);
  }

  // PlaySound from memory: a WAVE image of 0.1 s.
  static BYTE image[44 + 1600];
  memcpy(image, "RIFF", 4);
  *(DWORD*)(image + 4) = sizeof image - 8;
  memcpy(image + 8, "WAVEfmt ", 8);
  *(DWORD*)(image + 16) = 16;
  memcpy(image + 20, &pcm, 16);
  memcpy(image + 36, "data", 4);
  *(DWORD*)(image + 40) = 1600;
  memcpy(image + 44, second, 1600);
  t0 = GetTickCount();
  CHECK(PlaySoundA((LPCSTR)image, NULL, SND_MEMORY | SND_SYNC | SND_NODEFAULT));
  CHECK(GetTickCount() - t0 >= 70);
  CHECK(PlaySoundA((LPCSTR)image, NULL, SND_MEMORY | SND_ASYNC | SND_LOOP | SND_NODEFAULT));
  Sleep(150);
  CHECK(PlaySoundA(NULL, NULL, 0));

  // Beep plays its tone through the device.
  t0 = GetTickCount();
  CHECK(Beep(800, 100));
  CHECK(GetTickCount() - t0 >= 80);
}

int main() {
#ifdef _WINPORT
  // The loopback device: what is played is what is recorded.
  setenv("WINPORT_AUDIO", "null", 1);
#endif
  TestAcm();
  TestMmio();
  TestTimers();
  CHECK(!PlaySoundA("no such sound.wav", NULL, SND_FILENAME | SND_NODEFAULT));
  if (waveOutGetNumDevs() > 0)
    TestWave();
  else
    printf("no sound device: waveOut and waveIn not tested\n");
  if (g_failures) {
    printf("%d failures\n", g_failures);
    return 1;
  }
  printf("audiotest: all passed\n");
  return 0;
}
