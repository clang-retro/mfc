// kernel_comm.cpp - serial ports over termios: the port's settings as a
// DCB, its time-outs, its modem lines and events, and reads, writes and
// event waits that can run overlapped.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#include "file.h"

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/time.h>
#include <termios.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

using namespace winport;

namespace winport {

struct Comm {
  std::mutex lock;
  COMMTIMEOUTS timeouts = {0, 0, 0, 0, 0};
  DWORD mask = 0;
  // Bumped by SetCommMask (which ends a wait for events) and by CancelIo
  // (which ends everything going on).
  std::atomic<unsigned> maskGeneration{0};
  std::atomic<unsigned> cancelGeneration{0};
  // The DCB's fields termios has no place for.
  DCB extra;
  DWORD errors = 0;
  bool breakOn = false;
  DWORD lastLines = 0;  // for EV_CTS, EV_DSR, EV_RLSD and EV_RING
};

}  // namespace winport

namespace {

const int kSlice = 20;  // ms: how often a wait looks whether it was ended

ULONGLONG Now() {
  timeval tv;
  gettimeofday(&tv, NULL);
  return (ULONGLONG)tv.tv_sec * 1000 + tv.tv_usec / 1000;
}

struct Speed {
  DWORD baud;
  speed_t code;
};
const Speed kSpeeds[] = {
    {50, B50},         {75, B75},         {110, B110},       {134, B134},       {150, B150},
    {200, B200},       {300, B300},       {600, B600},       {1200, B1200},     {1800, B1800},
    {2400, B2400},     {4800, B4800},     {9600, B9600},     {19200, B19200},   {38400, B38400},
    {57600, B57600},   {115200, B115200}, {230400, B230400},
#ifdef B460800
    {460800, B460800},
#endif
#ifdef B500000
    {500000, B500000},
#endif
#ifdef B576000
    {576000, B576000},
#endif
#ifdef B921600
    {921600, B921600},
#endif
#ifdef B1000000
    {1000000, B1000000},
#endif
#ifdef B1152000
    {1152000, B1152000},
#endif
#ifdef B1500000
    {1500000, B1500000},
#endif
#ifdef B2000000
    {2000000, B2000000},
#endif
#ifdef B3000000
    {3000000, B3000000},
#endif
#ifdef B4000000
    {4000000, B4000000},
#endif
};

bool SpeedCode(DWORD baud, speed_t* code) {
  for (const Speed& s : kSpeeds)
    if (s.baud == baud) {
      *code = s.code;
      return true;
    }
#ifdef __APPLE__
  // macOS takes any rate as itself.
  *code = (speed_t)baud;
  return true;
#else
  // Windows rates termios lacks go to the nearest it has.
  if (baud == 14400 || baud == 28800) {
    *code = baud == 14400 ? B19200 : B38400;
    return true;
  }
  if (baud == 56000 || baud == 128000 || baud == 256000) {
    *code = baud == 56000 ? B57600 : B230400;
    return true;
  }
  return false;
#endif
}

DWORD BaudOf(speed_t code) {
  for (const Speed& s : kSpeeds)
    if (s.code == code) return s.baud;
  return (DWORD)code;
}

Comm* GetComm(HANDLE h, File** file = NULL) {
  File* f = GetFile(h);
  if (!f) return NULL;
  if (!f->comm) {
    SetLastError(ERROR_INVALID_HANDLE);
    return NULL;
  }
  if (file) *file = f;
  return f->comm;
}

DWORD Lines(int fd) {
  int m = 0;
  if (ioctl(fd, TIOCMGET, &m) != 0) return 0;
  DWORD r = 0;
  if (m & TIOCM_CTS) r |= MS_CTS_ON;
  if (m & TIOCM_DSR) r |= MS_DSR_ON;
  if (m & TIOCM_RI) r |= MS_RING_ON;
  if (m & TIOCM_CD) r |= MS_RLSD_ON;
  return r;
}

void SetLine(int fd, int bit, bool on) { ioctl(fd, on ? TIOCMBIS : TIOCMBIC, &bit); }

void DefaultDcb(DCB* d) {
  memset(d, 0, sizeof *d);
  d->DCBlength = sizeof *d;
  d->fBinary = 1;
  d->fDtrControl = DTR_CONTROL_ENABLE;
  d->fRtsControl = RTS_CONTROL_ENABLE;
  d->fTXContinueOnXoff = 1;
  d->XonLim = 2048;
  d->XoffLim = 512;
  d->ByteSize = 8;
  d->XonChar = 0x11;
  d->XoffChar = 0x13;
}

BOOL Fail(DWORD e) {
  SetLastError(e);
  return FALSE;
}

// Waits for fd to be ready for ev, up to ms (-1: no limit), a slice at a
// time so that SetCommMask and CancelIo can end it. 1 ready, 0 timed
// out, -1 ended.
int Wait(Comm* c, int fd, short ev, long long ms, unsigned cancelGen) {
  ULONGLONG end = ms >= 0 ? Now() + (ULONGLONG)ms : 0;
  for (;;) {
    if (c->cancelGeneration.load() != cancelGen) return -1;
    int slice = kSlice;
    if (ms >= 0) {
      ULONGLONG now = Now();
      if (now >= end) {
        pollfd p = {fd, ev, 0};
        return poll(&p, 1, 0) > 0 ? 1 : 0;
      }
      slice = (int)std::min<ULONGLONG>(end - now, kSlice);
    }
    pollfd p = {fd, ev, 0};
    int r = poll(&p, 1, slice);
    if (r > 0) return 1;
    if (r < 0 && errno != EINTR) return 1;  // the read or write reports it
  }
}

// A read that keeps to the port's time-outs, as Windows reads a port.
DWORD DoRead(File* f, char* buf, DWORD n, DWORD* done, unsigned cancelGen) {
  Comm* c = f->comm;
  COMMTIMEOUTS t;
  {
    std::lock_guard<std::mutex> g(c->lock);
    t = c->timeouts;
  }
  *done = 0;
  if (n == 0) return ERROR_SUCCESS;
  const DWORD kMax = MAXDWORD;
  bool returnNow = t.ReadIntervalTimeout == kMax && t.ReadTotalTimeoutMultiplier == 0 && t.ReadTotalTimeoutConstant == 0;
  bool firstByte = t.ReadIntervalTimeout == kMax && t.ReadTotalTimeoutMultiplier == kMax &&
                   t.ReadTotalTimeoutConstant > 0 && t.ReadTotalTimeoutConstant < kMax;
  long long total = -1;
  if (firstByte)
    total = t.ReadTotalTimeoutConstant;
  else if (!returnNow && (t.ReadTotalTimeoutMultiplier || t.ReadTotalTimeoutConstant))
    total = (long long)t.ReadTotalTimeoutMultiplier * n + t.ReadTotalTimeoutConstant;
  long long interval = (!returnNow && !firstByte && t.ReadIntervalTimeout && t.ReadIntervalTimeout != kMax)
                           ? (long long)t.ReadIntervalTimeout
                           : -1;
  ULONGLONG start = Now();
  while (*done < n) {
    ssize_t r = read(f->fd, buf + *done, n - *done);
    if (r > 0) {
      *done += (DWORD)r;
      if (returnNow || firstByte) break;
      continue;
    }
    // 0 or EAGAIN: nothing more without waiting (the port is non-blocking).
    if (r < 0 && errno == EINTR) continue;
    if (r < 0 && errno != EAGAIN) return ErrorFromErrno(errno);
    if (returnNow) break;
    long long wait = -1;
    if (total >= 0) {
      long long left = total - (long long)(Now() - start);
      if (left <= 0) break;
      wait = left;
    }
    if (interval >= 0 && *done > 0) wait = wait < 0 ? interval : std::min(wait, interval);
    int w = Wait(c, f->fd, POLLIN, wait, cancelGen);
    if (w < 0) return ERROR_OPERATION_ABORTED;
    if (w == 0) break;  // the interval between bytes ran out, or the whole time
  }
  return ERROR_SUCCESS;
}

DWORD DoWrite(File* f, const char* buf, DWORD n, DWORD* done, unsigned cancelGen) {
  Comm* c = f->comm;
  COMMTIMEOUTS t;
  {
    std::lock_guard<std::mutex> g(c->lock);
    t = c->timeouts;
  }
  long long total = (t.WriteTotalTimeoutMultiplier || t.WriteTotalTimeoutConstant)
                        ? (long long)t.WriteTotalTimeoutMultiplier * n + t.WriteTotalTimeoutConstant
                        : -1;
  ULONGLONG start = Now();
  *done = 0;
  while (*done < n) {
    ssize_t r = write(f->fd, buf + *done, n - *done);
    if (r > 0) {
      *done += (DWORD)r;
      continue;
    }
    if (r < 0 && errno == EINTR) continue;
    if (r < 0 && errno != EAGAIN) return ErrorFromErrno(errno);
    long long wait = -1;
    if (total >= 0) {
      wait = total - (long long)(Now() - start);
      if (wait <= 0) return ERROR_SUCCESS;  // timed out: *done tells how much went
    }
    int w = Wait(c, f->fd, POLLOUT, wait, cancelGen);
    if (w < 0) return ERROR_OPERATION_ABORTED;
    if (w == 0) return ERROR_SUCCESS;
  }
  return ERROR_SUCCESS;
}

// Waits for one of the events in the mask.
DWORD DoWaitEvent(File* f, DWORD* events, unsigned maskGen, unsigned cancelGen) {
  Comm* c = f->comm;
  *events = 0;
  DWORD mask;
  {
    std::lock_guard<std::mutex> g(c->lock);
    mask = c->mask;
  }
  if (!mask) return ERROR_INVALID_PARAMETER;
  for (;;) {
    if (c->maskGeneration.load() != maskGen) return ERROR_SUCCESS;  // *events is 0
    if (c->cancelGeneration.load() != cancelGen) return ERROR_OPERATION_ABORTED;
    DWORD ev = 0;
    int avail = 0;
    if ((mask & (EV_RXCHAR | EV_RXFLAG)) && ioctl(f->fd, FIONREAD, &avail) == 0 && avail > 0) ev |= mask & EV_RXCHAR;
    if (mask & EV_TXEMPTY) {
      int out = 0;
#ifdef TIOCOUTQ
      if (ioctl(f->fd, TIOCOUTQ, &out) == 0 && out == 0) ev |= EV_TXEMPTY;
#else
      ev |= EV_TXEMPTY;
#endif
    }
    if (mask & (EV_CTS | EV_DSR | EV_RLSD | EV_RING)) {
      DWORD lines = Lines(f->fd);
      std::lock_guard<std::mutex> g(c->lock);
      DWORD changed = lines ^ c->lastLines;
      if ((changed & MS_CTS_ON) && (mask & EV_CTS)) ev |= EV_CTS;
      if ((changed & MS_DSR_ON) && (mask & EV_DSR)) ev |= EV_DSR;
      if ((changed & MS_RLSD_ON) && (mask & EV_RLSD)) ev |= EV_RLSD;
      if ((changed & lines & MS_RING_ON) && (mask & EV_RING)) ev |= EV_RING;
      c->lastLines = lines;
    }
    if (ev) {
      *events = ev;
      return ERROR_SUCCESS;
    }
    if (mask & (EV_RXCHAR | EV_RXFLAG)) {
      pollfd p = {f->fd, POLLIN, 0};
      poll(&p, 1, kSlice);
    } else {
      usleep(kSlice * 1000);
    }
  }
}

// Runs op now, or on a thread of its own when the port is overlapped.
template <typename Op>
BOOL Run(File* f, LPOVERLAPPED ov, LPDWORD done, Op op) {
  if (f->overlapped && ov) {
    ov->Internal = STATUS_PENDING;
    ov->InternalHigh = 0;
    HANDLE e = (HANDLE)((UINT_PTR)ov->hEvent & ~(UINT_PTR)1);
    if (e) ResetEvent(e);
    f->refs.fetch_add(1);
    std::thread([f, ov, op] {
      DWORD n = 0;
      DWORD err = op(&n);
      CompleteOverlapped(ov, err, n, f);
      Release(f);
    }).detach();
    if (done) *done = 0;
    SetLastError(ERROR_IO_PENDING);
    return FALSE;
  }
  DWORD n = 0;
  DWORD err = op(&n);
  if (done) *done = n;
  if (ov) CompleteOverlapped(ov, err, n, f);
  if (err) return Fail(err);
  return TRUE;
}

BOOL ApplyDcb(File* f, const DCB* d) {
  termios t;
  if (tcgetattr(f->fd, &t) != 0) return Fail(ErrorFromErrno(errno));
  speed_t sp;
  if (!SpeedCode(d->BaudRate, &sp)) return Fail(ERROR_INVALID_PARAMETER);
  if (d->ByteSize < 5 || d->ByteSize > 8 || d->Parity > SPACEPARITY || d->StopBits > TWOSTOPBITS)
    return Fail(ERROR_INVALID_PARAMETER);
  cfmakeraw(&t);
  cfsetispeed(&t, sp);
  cfsetospeed(&t, sp);
  t.c_cflag &= ~(CSIZE | PARENB | PARODD | CSTOPB);
#ifdef CMSPAR
  t.c_cflag &= ~CMSPAR;
#endif
  t.c_cflag |= CREAD | CLOCAL;
  static const tcflag_t kSizes[] = {CS5, CS6, CS7, CS8};
  t.c_cflag |= kSizes[d->ByteSize - 5];
  switch (d->Parity) {
    case ODDPARITY: t.c_cflag |= PARENB | PARODD; break;
    case EVENPARITY: t.c_cflag |= PARENB; break;
#ifdef CMSPAR
    case MARKPARITY: t.c_cflag |= PARENB | PARODD | CMSPAR; break;
    case SPACEPARITY: t.c_cflag |= PARENB | CMSPAR; break;
#else
    case MARKPARITY:
    case SPACEPARITY: return Fail(ERROR_INVALID_PARAMETER);
#endif
    default: break;
  }
  if (d->StopBits != ONESTOPBIT) t.c_cflag |= CSTOPB;
  if (d->fParity && d->Parity != NOPARITY) t.c_iflag |= INPCK;
  bool hw = d->fOutxCtsFlow || d->fRtsControl == RTS_CONTROL_HANDSHAKE;
#ifdef CRTSCTS
  if (hw)
    t.c_cflag |= CRTSCTS;
  else
    t.c_cflag &= ~CRTSCTS;
#endif
  t.c_iflag &= ~(IXON | IXOFF | IXANY);
  if (d->fOutX) t.c_iflag |= IXON;
  if (d->fInX) t.c_iflag |= IXOFF;
  t.c_cc[VSTART] = (cc_t)d->XonChar;
  t.c_cc[VSTOP] = (cc_t)d->XoffChar;
  t.c_cc[VMIN] = 0;
  t.c_cc[VTIME] = 0;
  if (tcsetattr(f->fd, TCSANOW, &t) != 0) return Fail(ErrorFromErrno(errno));
  // The modem lines the DCB holds steady.
  if (d->fDtrControl == DTR_CONTROL_ENABLE || d->fDtrControl == DTR_CONTROL_HANDSHAKE) SetLine(f->fd, TIOCM_DTR, true);
  if (d->fDtrControl == DTR_CONTROL_DISABLE) SetLine(f->fd, TIOCM_DTR, false);
  if (!hw && d->fRtsControl == RTS_CONTROL_ENABLE) SetLine(f->fd, TIOCM_RTS, true);
  if (!hw && d->fRtsControl == RTS_CONTROL_DISABLE) SetLine(f->fd, TIOCM_RTS, false);
  std::lock_guard<std::mutex> g(f->comm->lock);
  f->comm->extra = *d;
  return TRUE;
}

// "COM1:9600,n,8,1", "baud=9600 parity=N data=8 stop=1 dtr=on", and the
// time-outs ("to=on").
bool ParseDcb(std::string s, DCB* d, COMMTIMEOUTS* to) {
  for (char& ch : s) ch = (char)tolower((unsigned char)ch);
  size_t colon = s.find(':');
  if (colon != std::string::npos && s.compare(0, 3, "com") == 0) s = s.substr(colon + 1);
  while (!s.empty() && isspace((unsigned char)s[0])) s.erase(0, 1);
  if (s.empty()) return false;
  if (s.find('=') == std::string::npos) {
    // The mode command's short form: baud,parity,data,stop[,x|p]
    std::vector<std::string> f;
    size_t at = 0;
    for (;;) {
      size_t c = s.find(',', at);
      std::string part = s.substr(at, c == std::string::npos ? std::string::npos : c - at);
      while (!part.empty() && isspace((unsigned char)part.back())) part.pop_back();
      while (!part.empty() && isspace((unsigned char)part[0])) part.erase(0, 1);
      f.push_back(part);
      if (c == std::string::npos) break;
      at = c + 1;
    }
    if (f.empty() || f[0].empty()) return false;
    DWORD b = (DWORD)strtoul(f[0].c_str(), NULL, 10);
    // "96" is 9600 and "11" is 110 in the old short form.
    if (b == 11) b = 110;
    else if (b == 15) b = 150;
    else if (b == 30) b = 300;
    else if (b == 60) b = 600;
    else if (b == 12) b = 1200;
    else if (b == 24) b = 2400;
    else if (b == 48) b = 4800;
    else if (b == 96) b = 9600;
    else if (b == 19) b = 19200;
    d->BaudRate = b;
    if (f.size() > 1 && !f[1].empty()) {
      const char* p = "noems";
      const char* q = strchr(p, f[1][0]);
      if (!q) return false;
      d->Parity = (BYTE)(q - p);
    }
    if (f.size() > 2 && !f[2].empty()) d->ByteSize = (BYTE)atoi(f[2].c_str());
    if (f.size() > 3 && !f[3].empty()) d->StopBits = f[3] == "1.5" ? ONE5STOPBITS : f[3] == "2" ? TWOSTOPBITS : ONESTOPBIT;
    if (f.size() > 4) {
      d->fOutX = d->fInX = f[4] == "x";
      d->fOutxCtsFlow = d->fOutxDsrFlow = f[4] == "p";
      d->fRtsControl = f[4] == "p" ? RTS_CONTROL_HANDSHAKE : RTS_CONTROL_ENABLE;
      d->fDtrControl = f[4] == "p" ? DTR_CONTROL_HANDSHAKE : DTR_CONTROL_ENABLE;
    }
    return d->ByteSize >= 5 && d->ByteSize <= 8;
  }
  size_t at = 0;
  while (at < s.size()) {
    while (at < s.size() && isspace((unsigned char)s[at])) at++;
    size_t e = at;
    while (e < s.size() && !isspace((unsigned char)s[e])) e++;
    std::string kv = s.substr(at, e - at);
    at = e;
    if (kv.empty()) continue;
    size_t eq = kv.find('=');
    if (eq == std::string::npos) return false;
    std::string k = kv.substr(0, eq), v = kv.substr(eq + 1);
    bool on = v == "on";
    if (k == "baud")
      d->BaudRate = (DWORD)strtoul(v.c_str(), NULL, 10);
    else if (k == "parity") {
      const char* p = "noems";
      const char* q = v.empty() ? NULL : strchr(p, v[0]);
      if (!q) return false;
      d->Parity = (BYTE)(q - p);
    } else if (k == "data")
      d->ByteSize = (BYTE)atoi(v.c_str());
    else if (k == "stop")
      d->StopBits = v == "1.5" ? ONE5STOPBITS : v == "2" ? TWOSTOPBITS : ONESTOPBIT;
    else if (k == "xon")
      d->fOutX = d->fInX = on;
    else if (k == "odsr")
      d->fOutxDsrFlow = on;
    else if (k == "octs")
      d->fOutxCtsFlow = on;
    else if (k == "dtr")
      d->fDtrControl = v == "hs" ? DTR_CONTROL_HANDSHAKE : on ? DTR_CONTROL_ENABLE : DTR_CONTROL_DISABLE;
    else if (k == "rts")
      d->fRtsControl = v == "hs" ? RTS_CONTROL_HANDSHAKE : v == "tg" ? RTS_CONTROL_TOGGLE
                                                         : on ? RTS_CONTROL_ENABLE : RTS_CONTROL_DISABLE;
    else if (k == "idsr")
      d->fDsrSensitivity = on;
    else if (k == "to") {
      if (to) {
        memset(to, 0, sizeof *to);
        if (on) to->WriteTotalTimeoutConstant = 60000;
      }
    } else
      return false;
  }
  return d->ByteSize >= 5 && d->ByteSize <= 8;
}

}  // namespace

namespace winport {

std::string CommDevice(const std::string& name) {
  // "COM1", "COM1:", "\\.\COM12".
  std::string n = name;
  if (n.compare(0, 4, "\\\\.\\") == 0) n = n.substr(4);
  if (n.size() < 4 || strncasecmp(n.c_str(), "COM", 3) != 0) return std::string();
  size_t i = 3;
  int num = 0;
  while (i < n.size() && isdigit((unsigned char)n[i])) num = num * 10 + (n[i++] - '0');
  if (i == 3 || num < 1 || (i < n.size() && !(n[i] == ':' && i + 1 == n.size()))) return std::string();
  char env[32];
  snprintf(env, sizeof env, "WINPORT_COM%d", num);
  if (const char* e = getenv(env)) return *e ? std::string(e) : std::string("-");
#ifdef __APPLE__
  // The call-out devices, in order (the Bluetooth stand-ins left out).
  std::vector<std::string> devs;
  if (DIR* d = opendir("/dev")) {
    while (dirent* e = readdir(d))
      if (strncmp(e->d_name, "cu.", 3) == 0 && !strstr(e->d_name, "Bluetooth")) devs.push_back(e->d_name);
    closedir(d);
  }
  std::sort(devs.begin(), devs.end());
  if ((size_t)num <= devs.size()) return "/dev/" + devs[num - 1];
  return "-";
#else
  return "/dev/ttyS" + std::to_string(num - 1);
#endif
}

bool IsTerminal(int fd) {
  termios t;
  return tcgetattr(fd, &t) == 0;
}

void OpenComm(File* f) {
  f->comm = new Comm;
  DefaultDcb(&f->comm->extra);
  // Raw, as a Windows port is: no echo, no line editing, no translation;
  // the port keeps its speed.
  termios t;
  if (tcgetattr(f->fd, &t) == 0) {
    speed_t in = cfgetispeed(&t), out = cfgetospeed(&t);
    cfmakeraw(&t);
    cfsetispeed(&t, in);
    cfsetospeed(&t, out);
    t.c_cflag |= CREAD | CLOCAL;
    t.c_cc[VMIN] = 0;
    t.c_cc[VTIME] = 0;
    tcsetattr(f->fd, TCSANOW, &t);
    f->comm->extra.BaudRate = BaudOf(out);
  }
  fcntl(f->fd, F_SETFL, fcntl(f->fd, F_GETFL) | O_NONBLOCK);
  f->comm->lastLines = Lines(f->fd);
}

void CloseComm(File* f) {
  delete f->comm;
  f->comm = nullptr;
}

void CancelComm(File* f) { f->comm->cancelGeneration.fetch_add(1); }

BOOL CommRead(File* f, void* buf, DWORD n, LPDWORD done, LPOVERLAPPED ov) {
  unsigned gen = f->comm->cancelGeneration.load();
  return Run(f, ov, done, [f, buf, n, gen](DWORD* k) { return DoRead(f, (char*)buf, n, k, gen); });
}

BOOL CommWrite(File* f, const void* buf, DWORD n, LPDWORD done, LPOVERLAPPED ov) {
  unsigned gen = f->comm->cancelGeneration.load();
  return Run(f, ov, done, [f, buf, n, gen](DWORD* k) { return DoWrite(f, (const char*)buf, n, k, gen); });
}

}  // namespace winport

extern "C" {

BOOL WINAPI GetCommState(HANDLE h, LPDCB d) {
  File* f;
  Comm* c = GetComm(h, &f);
  if (!c || !d) return FALSE;
  termios t;
  if (tcgetattr(f->fd, &t) != 0) return Fail(ErrorFromErrno(errno));
  std::lock_guard<std::mutex> g(c->lock);
  *d = c->extra;
  d->DCBlength = sizeof(DCB);
  d->fBinary = 1;
  d->BaudRate = BaudOf(cfgetospeed(&t));
  switch (t.c_cflag & CSIZE) {
    case CS5: d->ByteSize = 5; break;
    case CS6: d->ByteSize = 6; break;
    case CS7: d->ByteSize = 7; break;
    default: d->ByteSize = 8; break;
  }
  d->Parity = NOPARITY;
  if (t.c_cflag & PARENB) {
#ifdef CMSPAR
    if (t.c_cflag & CMSPAR)
      d->Parity = (t.c_cflag & PARODD) ? MARKPARITY : SPACEPARITY;
    else
#endif
      d->Parity = (t.c_cflag & PARODD) ? ODDPARITY : EVENPARITY;
  }
  d->StopBits = (t.c_cflag & CSTOPB) ? (c->extra.StopBits == ONE5STOPBITS ? ONE5STOPBITS : TWOSTOPBITS) : ONESTOPBIT;
  d->fParity = (t.c_iflag & INPCK) != 0;
#ifdef CRTSCTS
  bool hw = (t.c_cflag & CRTSCTS) != 0;
  d->fOutxCtsFlow = hw;
  if (hw) d->fRtsControl = RTS_CONTROL_HANDSHAKE;
  else if (c->extra.fRtsControl == RTS_CONTROL_HANDSHAKE) d->fRtsControl = RTS_CONTROL_ENABLE;
#endif
  d->fOutX = (t.c_iflag & IXON) != 0;
  d->fInX = (t.c_iflag & IXOFF) != 0;
  d->XonChar = (char)t.c_cc[VSTART];
  d->XoffChar = (char)t.c_cc[VSTOP];
  return TRUE;
}

BOOL WINAPI SetCommState(HANDLE h, LPDCB d) {
  File* f;
  if (!GetComm(h, &f)) return FALSE;
  if (!d) return Fail(ERROR_INVALID_PARAMETER);
  return ApplyDcb(f, d);
}

BOOL WINAPI BuildCommDCBAndTimeoutsA(LPCSTR def, LPDCB d, LPCOMMTIMEOUTS to) {
  if (!def || !d) return Fail(ERROR_INVALID_PARAMETER);
  DCB n = *d;
  if (!ParseDcb(def, &n, to)) return Fail(ERROR_INVALID_PARAMETER);
  *d = n;
  return TRUE;
}
BOOL WINAPI BuildCommDCBAndTimeoutsW(LPCWSTR def, LPDCB d, LPCOMMTIMEOUTS to) {
  return BuildCommDCBAndTimeoutsA(def ? Narrow(def).c_str() : NULL, d, to);
}
BOOL WINAPI BuildCommDCBA(LPCSTR def, LPDCB d) { return BuildCommDCBAndTimeoutsA(def, d, NULL); }
BOOL WINAPI BuildCommDCBW(LPCWSTR def, LPDCB d) { return BuildCommDCBAndTimeoutsW(def, d, NULL); }

BOOL WINAPI GetCommTimeouts(HANDLE h, LPCOMMTIMEOUTS t) {
  Comm* c = GetComm(h);
  if (!c || !t) return FALSE;
  std::lock_guard<std::mutex> g(c->lock);
  *t = c->timeouts;
  return TRUE;
}

BOOL WINAPI SetCommTimeouts(HANDLE h, LPCOMMTIMEOUTS t) {
  Comm* c = GetComm(h);
  if (!c || !t) return FALSE;
  if (t->ReadIntervalTimeout == MAXDWORD && t->ReadTotalTimeoutMultiplier == MAXDWORD &&
      t->ReadTotalTimeoutConstant == MAXDWORD)
    return Fail(ERROR_INVALID_PARAMETER);
  std::lock_guard<std::mutex> g(c->lock);
  c->timeouts = *t;
  return TRUE;
}

BOOL WINAPI SetupComm(HANDLE h, DWORD, DWORD) { return GetComm(h) != NULL; }

BOOL WINAPI PurgeComm(HANDLE h, DWORD flags) {
  File* f;
  Comm* c = GetComm(h, &f);
  if (!c) return FALSE;
  if (flags & (PURGE_TXABORT | PURGE_RXABORT)) c->cancelGeneration.fetch_add(1);
  bool rx = flags & PURGE_RXCLEAR, tx = flags & PURGE_TXCLEAR;
  if (rx || tx) tcflush(f->fd, rx && tx ? TCIOFLUSH : rx ? TCIFLUSH : TCOFLUSH);
  return TRUE;
}

BOOL WINAPI ClearCommError(HANDLE h, LPDWORD errors, LPCOMSTAT st) {
  File* f;
  Comm* c = GetComm(h, &f);
  if (!c) return FALSE;
  {
    std::lock_guard<std::mutex> g(c->lock);
    if (errors) *errors = c->errors | (c->breakOn ? CE_BREAK : 0);
    c->errors = 0;
  }
  if (st) {
    memset(st, 0, sizeof *st);
    int in = 0, out = 0;
    if (ioctl(f->fd, FIONREAD, &in) == 0) st->cbInQue = (DWORD)in;
#ifdef TIOCOUTQ
    if (ioctl(f->fd, TIOCOUTQ, &out) == 0) st->cbOutQue = (DWORD)out;
#endif
    DWORD lines = Lines(f->fd);
    DCB d;
    {
      std::lock_guard<std::mutex> g(c->lock);
      d = c->extra;
    }
    st->fCtsHold = d.fOutxCtsFlow && !(lines & MS_CTS_ON) && st->cbOutQue;
    st->fDsrHold = d.fOutxDsrFlow && !(lines & MS_DSR_ON) && st->cbOutQue;
  }
  return TRUE;
}

BOOL WINAPI SetCommBreak(HANDLE h) { return EscapeCommFunction(h, SETBREAK); }
BOOL WINAPI ClearCommBreak(HANDLE h) { return EscapeCommFunction(h, CLRBREAK); }

BOOL WINAPI EscapeCommFunction(HANDLE h, DWORD fn) {
  File* f;
  Comm* c = GetComm(h, &f);
  if (!c) return FALSE;
  switch (fn) {
    case SETDTR: SetLine(f->fd, TIOCM_DTR, true); break;
    case CLRDTR: SetLine(f->fd, TIOCM_DTR, false); break;
    case SETRTS: SetLine(f->fd, TIOCM_RTS, true); break;
    case CLRRTS: SetLine(f->fd, TIOCM_RTS, false); break;
    case SETBREAK:
      if (ioctl(f->fd, TIOCSBRK) != 0) return Fail(ErrorFromErrno(errno));
      c->breakOn = true;
      break;
    case CLRBREAK:
      if (ioctl(f->fd, TIOCCBRK) != 0) return Fail(ErrorFromErrno(errno));
      c->breakOn = false;
      break;
    case SETXOFF: tcflow(f->fd, TCOOFF); break;
    case SETXON: tcflow(f->fd, TCOON); break;
    case RESETDEV: break;
    default: return Fail(ERROR_INVALID_PARAMETER);
  }
  return TRUE;
}

BOOL WINAPI GetCommModemStatus(HANDLE h, LPDWORD status) {
  File* f;
  if (!GetComm(h, &f) || !status) return FALSE;
  *status = Lines(f->fd);
  return TRUE;
}

BOOL WINAPI GetCommMask(HANDLE h, LPDWORD mask) {
  Comm* c = GetComm(h);
  if (!c || !mask) return FALSE;
  std::lock_guard<std::mutex> g(c->lock);
  *mask = c->mask;
  return TRUE;
}

BOOL WINAPI SetCommMask(HANDLE h, DWORD mask) {
  File* f;
  Comm* c = GetComm(h, &f);
  if (!c) return FALSE;
  {
    std::lock_guard<std::mutex> g(c->lock);
    c->mask = mask;
    c->lastLines = Lines(f->fd);
  }
  // A wait going on ends, with no events.
  c->maskGeneration.fetch_add(1);
  return TRUE;
}

BOOL WINAPI WaitCommEvent(HANDLE h, LPDWORD events, LPOVERLAPPED ov) {
  File* f;
  Comm* c = GetComm(h, &f);
  if (!c || !events) return FALSE;
  unsigned maskGen = c->maskGeneration.load(), cancelGen = c->cancelGeneration.load();
  if (f->overlapped && ov) {
    // The mask goes into *events when the wait ends, as on Windows.
    return Run(f, ov, NULL, [f, events, maskGen, cancelGen](DWORD* n) {
      DWORD ev = 0;
      DWORD err = DoWaitEvent(f, &ev, maskGen, cancelGen);
      *events = ev;
      *n = 0;
      return err;
    });
  }
  DWORD err = DoWaitEvent(f, events, maskGen, cancelGen);
  if (err) return Fail(err);
  return TRUE;
}

BOOL WINAPI TransmitCommChar(HANDLE h, char ch) {
  File* f;
  if (!GetComm(h, &f)) return FALSE;
  ssize_t r;
  do r = write(f->fd, &ch, 1); while (r < 0 && errno == EINTR);
  if (r != 1) return Fail(r < 0 ? ErrorFromErrno(errno) : ERROR_COUNTER_TIMEOUT);
  return TRUE;
}

BOOL WINAPI GetCommProperties(HANDLE h, LPCOMMPROP p) {
  if (!GetComm(h) || !p) return FALSE;
  memset(p, 0, sizeof *p);
  p->wPacketLength = sizeof *p;
  p->wPacketVersion = 2;
  p->dwServiceMask = SP_SERIALCOMM;
  p->dwMaxTxQueue = 0;
  p->dwMaxRxQueue = 0;
  p->dwMaxBaud = BAUD_USER;
  p->dwProvSubType = PST_RS232;
  p->dwProvCapabilities = PCF_DTRDSR | PCF_RTSCTS | PCF_RLSD | PCF_PARITY_CHECK | PCF_XONXOFF | PCF_SETXCHAR |
                          PCF_TOTALTIMEOUTS | PCF_INTTIMEOUTS;
  p->dwSettableParams = SP_PARITY | SP_BAUD | SP_DATABITS | SP_STOPBITS | SP_HANDSHAKING | SP_PARITY_CHECK | SP_RLSD;
  p->dwSettableBaud = BAUD_110 | BAUD_300 | BAUD_600 | BAUD_1200 | BAUD_2400 | BAUD_4800 | BAUD_9600 | BAUD_19200 |
                      BAUD_38400 | BAUD_57600 | BAUD_115200 | BAUD_USER;
  p->wSettableData = DATABITS_5 | DATABITS_6 | DATABITS_7 | DATABITS_8;
  p->wSettableStopParity = STOPBITS_10 | STOPBITS_20 | PARITY_NONE | PARITY_ODD | PARITY_EVEN
#ifdef CMSPAR
                           | PARITY_MARK | PARITY_SPACE
#endif
      ;
  p->dwCurrentTxQueue = 4096;
  p->dwCurrentRxQueue = 4096;
  return TRUE;
}

BOOL WINAPI GetCommConfig(HANDLE h, LPCOMMCONFIG cc, LPDWORD size) {
  if (!size) return Fail(ERROR_INVALID_PARAMETER);
  if (!cc || *size < sizeof(COMMCONFIG)) {
    *size = sizeof(COMMCONFIG);
    return Fail(ERROR_INSUFFICIENT_BUFFER);
  }
  memset(cc, 0, sizeof *cc);
  cc->dwSize = sizeof *cc;
  cc->wVersion = 1;
  cc->dwProviderSubType = PST_RS232;
  *size = sizeof *cc;
  return GetCommState(h, &cc->dcb);
}

BOOL WINAPI SetCommConfig(HANDLE h, LPCOMMCONFIG cc, DWORD size) {
  if (!cc || size < offsetof(COMMCONFIG, dwProviderSubType)) return Fail(ERROR_INVALID_PARAMETER);
  return SetCommState(h, &cc->dcb);
}

BOOL WINAPI GetDefaultCommConfigA(LPCSTR name, LPCOMMCONFIG cc, LPDWORD size) {
  if (!name || !size) return Fail(ERROR_INVALID_PARAMETER);
  std::string dev = CommDevice(name);
  if (dev.empty() || dev == "-" || access(dev.c_str(), F_OK) != 0) return Fail(ERROR_FILE_NOT_FOUND);
  if (!cc || *size < sizeof(COMMCONFIG)) {
    *size = sizeof(COMMCONFIG);
    return Fail(ERROR_INSUFFICIENT_BUFFER);
  }
  memset(cc, 0, sizeof *cc);
  cc->dwSize = sizeof *cc;
  cc->wVersion = 1;
  cc->dwProviderSubType = PST_RS232;
  DefaultDcb(&cc->dcb);
  cc->dcb.BaudRate = CBR_9600;
  *size = sizeof *cc;
  return TRUE;
}
BOOL WINAPI GetDefaultCommConfigW(LPCWSTR name, LPCOMMCONFIG cc, LPDWORD size) {
  return GetDefaultCommConfigA(name ? Narrow(name).c_str() : NULL, cc, size);
}

}  // extern "C"
