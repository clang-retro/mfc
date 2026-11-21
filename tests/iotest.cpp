// iotest.cpp - console tests for kernel32's pipes and serial ports:
// CreatePipe, PeekNamedPipe, a child's standard handles through
// STARTF_USESTDHANDLES, a broken pipe; and, on Linux and macOS, a serial
// port (a pseudo-terminal named as COM9): its DCB, BuildCommDCB, its
// time-outs, reads and writes, and WaitCommEvent overlapped, ended by
// data and by SetCommMask; the environment block, GetUserName and the
// standard folders; and named mutexes, events and semaphores shared with
// child processes.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#include <afx.h>
#include <shlobj.h>
#include <stdio.h>
#include <string.h>
#ifdef _WINPORT
#include <fcntl.h>
#include <poll.h>
#include <stdlib.h>
#include <termios.h>
#include <unistd.h>
#endif

static int g_failures = 0;
#define CHECK(cond)                                          \
  do {                                                       \
    if (!(cond)) {                                           \
      printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
      g_failures++;                                          \
    }                                                        \
  } while (0)

// The child: its input, upper-cased, to its output, and a word to its
// error output.
static int Child() {
  HANDLE in = ::GetStdHandle(STD_INPUT_HANDLE), out = ::GetStdHandle(STD_OUTPUT_HANDLE);
  char buf[256];
  DWORD n;
  while (::ReadFile(in, buf, sizeof buf, &n, NULL) && n > 0) {
    for (DWORD i = 0; i < n; i++)
      if (buf[i] >= 'a' && buf[i] <= 'z') buf[i] -= 32;
    DWORD w;
    ::WriteFile(out, buf, n, &w, NULL);
  }
  DWORD w;
  ::WriteFile(::GetStdHandle(STD_ERROR_HANDLE), "done", 4, &w, NULL);
  return 7;
}

static void TestPipes() {
  // A pipe within the process: peeking leaves the data for ReadFile.
  HANDLE r, w;
  CHECK(::CreatePipe(&r, &w, NULL, 0));
  CHECK(::GetFileType(r) == FILE_TYPE_PIPE);
  DWORD n = 0, avail = 99, left = 99;
  CHECK(::PeekNamedPipe(r, NULL, 0, NULL, &avail, NULL) && avail == 0);
  CHECK(::WriteFile(w, "hello pipe", 10, &n, NULL) && n == 10);
  char buf[32] = {};
  // (What is left of the message means nothing on a byte pipe: Windows
  // and Wine give the message length, 0, less what was read.)
  CHECK(::PeekNamedPipe(r, buf, 5, &n, &avail, &left));
  CHECK(n == 5 && memcmp(buf, "hello", 5) == 0 && avail == 10);
  memset(buf, 0, sizeof buf);
  CHECK(::ReadFile(r, buf, sizeof buf, &n, NULL));
  // What was peeked comes first; the rest may need a second read.
  if (n < 10) {
    DWORD more = 0;
    CHECK(::ReadFile(r, buf + n, sizeof buf - n, &more, NULL));
    n += more;
  }
  CHECK(n == 10 && strcmp(buf, "hello pipe") == 0);
  // The writing end closed: reads and peeks fail with a broken pipe.
  ::CloseHandle(w);
  ::SetLastError(0);
  CHECK(!::ReadFile(r, buf, sizeof buf, &n, NULL) && ::GetLastError() == ERROR_BROKEN_PIPE && n == 0);
  CHECK(!::PeekNamedPipe(r, NULL, 0, NULL, &avail, NULL) && ::GetLastError() == ERROR_BROKEN_PIPE);
  ::CloseHandle(r);
  // The reading end closed: writes fail, and the program goes on.
  CHECK(::CreatePipe(&r, &w, NULL, 0));
  ::CloseHandle(r);
  CHECK(!::WriteFile(w, "x", 1, &n, NULL));
  CHECK(::GetLastError() == ERROR_NO_DATA || ::GetLastError() == ERROR_BROKEN_PIPE);
  ::CloseHandle(w);
}

static void TestChild() {
  // A child with pipes for its standard handles, as a program starts a
  // helper and talks to it.
  SECURITY_ATTRIBUTES sa = {sizeof sa, NULL, TRUE};
  HANDLE inR, inW, outR, outW, errR, errW;
  CHECK(::CreatePipe(&inR, &inW, &sa, 0));
  CHECK(::CreatePipe(&outR, &outW, &sa, 0));
  CHECK(::CreatePipe(&errR, &errW, &sa, 0));
  CHECK(::SetHandleInformation(inW, HANDLE_FLAG_INHERIT, 0));
  CHECK(::SetHandleInformation(outR, HANDLE_FLAG_INHERIT, 0));
  CHECK(::SetHandleInformation(errR, HANDLE_FLAG_INHERIT, 0));
  TCHAR exe[MAX_PATH];
  ::GetModuleFileName(NULL, exe, MAX_PATH);
  TCHAR cmd[MAX_PATH + 16];
  wsprintf(cmd, TEXT("\"%s\" -child"), exe);
  STARTUPINFO si = {};
  si.cb = sizeof si;
  si.dwFlags = STARTF_USESTDHANDLES;
  si.hStdInput = inR;
  si.hStdOutput = outW;
  si.hStdError = errW;
  PROCESS_INFORMATION pi = {};
  BOOL ok = ::CreateProcess(NULL, cmd, NULL, NULL, TRUE, 0, NULL, NULL, &si, &pi);
  CHECK(ok);
  // The child's ends belong to the child now.
  ::CloseHandle(inR);
  ::CloseHandle(outW);
  ::CloseHandle(errW);
  if (!ok) return;
  DWORD n;
  CHECK(::WriteFile(inW, "at&f\r", 5, &n, NULL) && n == 5);
  // Polled, as a program polls a helper's output without blocking.
  DWORD avail = 0;
  for (int i = 0; i < 500 && avail < 5; i++) {
    if (!::PeekNamedPipe(outR, NULL, 0, NULL, &avail, NULL)) break;
    if (avail < 5) ::Sleep(10);
  }
  CHECK(avail == 5);
  char buf[64] = {};
  CHECK(::ReadFile(outR, buf, 5, &n, NULL) && n == 5 && memcmp(buf, "AT&F\r", 5) == 0);
  ::CloseHandle(inW);  // the child sees the end of its input
  CHECK(::WaitForSingleObject(pi.hProcess, 10000) == WAIT_OBJECT_0);
  DWORD code = 0;
  CHECK(::GetExitCodeProcess(pi.hProcess, &code) && code == 7);
  memset(buf, 0, sizeof buf);
  CHECK(::ReadFile(errR, buf, sizeof buf, &n, NULL) && n == 4 && strcmp(buf, "done") == 0);
  CHECK(!::ReadFile(outR, buf, sizeof buf, &n, NULL) && ::GetLastError() == ERROR_BROKEN_PIPE);
  ::CloseHandle(outR);
  ::CloseHandle(errR);
  ::CloseHandle(pi.hProcess);
  ::CloseHandle(pi.hThread);
}

static void TestBuildDcb() {
  DCB dcb = {};
  dcb.DCBlength = sizeof dcb;
  CHECK(::BuildCommDCB(TEXT("baud=19200 parity=E data=7 stop=2 dtr=off rts=on"), &dcb));
  CHECK(dcb.BaudRate == 19200 && dcb.Parity == EVENPARITY && dcb.ByteSize == 7 && dcb.StopBits == TWOSTOPBITS);
  CHECK(dcb.fDtrControl == DTR_CONTROL_DISABLE && dcb.fRtsControl == RTS_CONTROL_ENABLE);
  // Windows (as the MODE command) also takes handshaking for the lines;
  // Wine takes only on and off.
  if (!::GetProcAddress(::GetModuleHandleA("ntdll.dll"), "wine_get_version")) {
    CHECK(::BuildCommDCB(TEXT("baud=9600 dtr=hs rts=hs"), &dcb));
    CHECK(dcb.fDtrControl == DTR_CONTROL_HANDSHAKE && dcb.fRtsControl == RTS_CONTROL_HANDSHAKE);
  }
  CHECK(::BuildCommDCB(TEXT("COM2:9600,n,8,1"), &dcb));
  CHECK(dcb.BaudRate == 9600 && dcb.Parity == NOPARITY && dcb.ByteSize == 8 && dcb.StopBits == ONESTOPBIT);
  CHECK(!::BuildCommDCB(TEXT("baud=9600 color=blue"), &dcb));
}

#ifdef _WINPORT
// A serial port: the far end of a pseudo-terminal stands in for the modem.
static int ReadMaster(int fd, char* buf, int n, int ms) {
  int got = 0;
  while (got < n) {
    pollfd p = {fd, POLLIN, 0};
    if (poll(&p, 1, ms) <= 0) break;
    int r = (int)read(fd, buf + got, n - got);
    if (r <= 0) break;
    got += r;
  }
  return got;
}

static void TestPort() {
  int master = posix_openpt(O_RDWR | O_NOCTTY);
  CHECK(master >= 0);
  if (master < 0) return;
  CHECK(grantpt(master) == 0 && unlockpt(master) == 0);
  setenv("WINPORT_COM9", ptsname(master), 1);
  // The far end raw too, so that what the port writes comes as it is.
  termios t;
  tcgetattr(master, &t);
  cfmakeraw(&t);
  tcsetattr(master, TCSANOW, &t);

  HANDLE h = ::CreateFile(TEXT("\\\\.\\COM9"), GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING,
                          FILE_FLAG_OVERLAPPED, NULL);
  CHECK(h != INVALID_HANDLE_VALUE);
  if (h == INVALID_HANDLE_VALUE) {
    close(master);
    return;
  }
  CHECK(::GetFileType(h) == FILE_TYPE_CHAR);
  CHECK(::SetupComm(h, 4096, 4096));

  // The DCB, set and read back.
  DCB dcb = {};
  dcb.DCBlength = sizeof dcb;
  CHECK(::GetCommState(h, &dcb));
  dcb.BaudRate = CBR_57600;
  dcb.ByteSize = 7;
  dcb.Parity = ODDPARITY;
  dcb.StopBits = TWOSTOPBITS;
  dcb.fOutX = dcb.fInX = TRUE;
  dcb.XonChar = 0x11;
  dcb.XoffChar = 0x13;
  dcb.EvtChar = '\n';
  CHECK(::SetCommState(h, &dcb));
  DCB back = {};
  back.DCBlength = sizeof back;
  CHECK(::GetCommState(h, &back));
  // A pseudo-terminal keeps the speed, but always has 8 bits and no
  // parity (Linux), so those are not checked.
  CHECK(back.BaudRate == CBR_57600 && back.fOutX && back.fInX && back.EvtChar == '\n');
  dcb.BaudRate = CBR_115200;
  dcb.ByteSize = 8;
  dcb.Parity = NOPARITY;
  dcb.StopBits = ONESTOPBIT;
  dcb.fOutX = dcb.fInX = FALSE;
  CHECK(::SetCommState(h, &dcb));
  dcb.ByteSize = 9;
  CHECK(!::SetCommState(h, &dcb) && ::GetLastError() == ERROR_INVALID_PARAMETER);
  COMMPROP prop = {};
  CHECK(::GetCommProperties(h, &prop) && prop.dwProvSubType == PST_RS232);
  DWORD sz = 0;
  CHECK(!::GetCommConfig(h, NULL, &sz) && sz >= sizeof(COMMCONFIG));

  // Time-outs: a read that returns at once, and one that waits a while.
  COMMTIMEOUTS to = {MAXDWORD, 0, 0, 0, 1000};
  CHECK(::SetCommTimeouts(h, &to));
  COMMTIMEOUTS to2 = {};
  CHECK(::GetCommTimeouts(h, &to2) && to2.ReadIntervalTimeout == MAXDWORD && to2.WriteTotalTimeoutConstant == 1000);
  OVERLAPPED ov = {};
  ov.hEvent = ::CreateEvent(NULL, TRUE, FALSE, NULL);
  char buf[64];
  DWORD n = 99;
  // Overlapped: done now, or pending and done soon.
  BOOL ok = ::ReadFile(h, buf, sizeof buf, &n, &ov);
  if (!ok) CHECK(::GetLastError() == ERROR_IO_PENDING);
  CHECK(::GetOverlappedResult(h, &ov, &n, TRUE) && n == 0);
  COMMTIMEOUTS wait = {0, 0, 150, 0, 1000};
  CHECK(::SetCommTimeouts(h, &wait));
  DWORD start = ::GetTickCount();
  ok = ::ReadFile(h, buf, sizeof buf, &n, &ov);
  if (!ok) CHECK(::GetLastError() == ERROR_IO_PENDING);
  CHECK(::GetOverlappedResult(h, &ov, &n, TRUE) && n == 0);
  DWORD took = ::GetTickCount() - start;
  CHECK(took >= 120 && took < 2000);

  // A command out, a reply in.
  ok = ::WriteFile(h, "ATZ\r", 4, &n, &ov);
  if (!ok) CHECK(::GetLastError() == ERROR_IO_PENDING);
  CHECK(::GetOverlappedResult(h, &ov, &n, TRUE) && n == 4);
  char got[16] = {};
  CHECK(ReadMaster(master, got, 4, 2000) == 4 && memcmp(got, "ATZ\r", 4) == 0);
  // An interval time-out: the read ends when the line goes quiet.
  COMMTIMEOUTS interval = {50, 0, 2000, 0, 1000};
  CHECK(::SetCommTimeouts(h, &interval));
  CHECK(write(master, "\r\nOK\r\n", 6) == 6);
  memset(buf, 0, sizeof buf);
  ok = ::ReadFile(h, buf, sizeof buf, &n, &ov);
  if (!ok) CHECK(::GetLastError() == ERROR_IO_PENDING);
  CHECK(::GetOverlappedResult(h, &ov, &n, TRUE) && n == 6 && strcmp(buf, "\r\nOK\r\n") == 0);
  COMSTAT st;
  DWORD errors = 99;
  CHECK(::ClearCommError(h, &errors, &st) && errors == 0 && st.cbInQue == 0);

  // WaitCommEvent: pending until a character comes.
  CHECK(::SetCommMask(h, EV_RXCHAR | EV_CTS | EV_DSR));
  DWORD mask = 0;
  CHECK(::GetCommMask(h, &mask) && mask == (EV_RXCHAR | EV_CTS | EV_DSR));
  DWORD ev = 0;
  ::ResetEvent(ov.hEvent);
  ok = ::WaitCommEvent(h, &ev, &ov);
  CHECK(!ok && ::GetLastError() == ERROR_IO_PENDING);
  ::Sleep(50);
  CHECK(!HasOverlappedIoCompleted(&ov));
  CHECK(!::GetOverlappedResult(h, &ov, &n, FALSE) && ::GetLastError() == ERROR_IO_INCOMPLETE);
  CHECK(write(master, "R", 1) == 1);
  CHECK(::WaitForSingleObject(ov.hEvent, 2000) == WAIT_OBJECT_0);
  CHECK(::GetOverlappedResult(h, &ov, &n, TRUE) && (ev & EV_RXCHAR));
  CHECK(::PurgeComm(h, PURGE_RXCLEAR | PURGE_TXCLEAR));
  // SetCommMask ends a wait, with no events.
  ev = 99;
  ok = ::WaitCommEvent(h, &ev, &ov);
  CHECK(!ok && ::GetLastError() == ERROR_IO_PENDING);
  ::Sleep(30);
  CHECK(::SetCommMask(h, EV_RXCHAR));
  CHECK(::WaitForSingleObject(ov.hEvent, 2000) == WAIT_OBJECT_0);
  CHECK(::GetOverlappedResult(h, &ov, &n, TRUE) && ev == 0);
  // CancelIo ends a read going on.
  COMMTIMEOUTS forever = {0, 0, 0, 0, 0};
  CHECK(::SetCommTimeouts(h, &forever));
  ok = ::ReadFile(h, buf, sizeof buf, &n, &ov);
  CHECK(!ok && ::GetLastError() == ERROR_IO_PENDING);
  ::Sleep(30);
  CHECK(::CancelIo(h));
  CHECK(!::GetOverlappedResult(h, &ov, &n, TRUE) && ::GetLastError() == ERROR_OPERATION_ABORTED);

  // The modem lines a pseudo-terminal has none of; the calls still work.
  DWORD lines = 0;
  CHECK(::GetCommModemStatus(h, &lines));
  CHECK(::EscapeCommFunction(h, SETDTR) && ::EscapeCommFunction(h, CLRRTS));
  CHECK(!::EscapeCommFunction(h, 99));
  CHECK(::FlushFileBuffers(h));
  ::CloseHandle(ov.hEvent);
  ::CloseHandle(h);
  close(master);

  // A name that is no port of this computer.
  setenv("WINPORT_COM9", "/dev/no-such-port", 1);
  CHECK(::CreateFile(TEXT("COM9"), GENERIC_READ, 0, NULL, OPEN_EXISTING, 0, NULL) == INVALID_HANDLE_VALUE);
  unsetenv("WINPORT_COM9");
}
#endif

// The environment as a block, and the user's name.
static void TestEnvironment() {
  CHECK(::SetEnvironmentVariableA("IOTEST_VAR", "yes"));
  bool found = false;
#ifndef UNICODE
  // The SDK names the A form only without UNICODE.
  LPCH block = ::GetEnvironmentStringsA();
  CHECK(block != NULL);
  for (const char* p = block; p && *p; p += strlen(p) + 1)
    if (strcmp(p, "IOTEST_VAR=yes") == 0) found = true;
  CHECK(found);
  CHECK(::FreeEnvironmentStringsA(block));
#endif
  LPWCH wide = ::GetEnvironmentStringsW();
  CHECK(wide != NULL);
  found = false;
  for (const WCHAR* p = wide; p && *p; p += lstrlenW(p) + 1)
    if (lstrcmpW(p, L"IOTEST_VAR=yes") == 0) found = true;
  CHECK(found);
  CHECK(::FreeEnvironmentStringsW(wide));

  char name[257];
  DWORD n = 1;
  CHECK(!::GetUserNameA(name, &n) && ::GetLastError() == ERROR_INSUFFICIENT_BUFFER && n > 1);
  n = sizeof name;
  CHECK(::GetUserNameA(name, &n));
  CHECK(name[0] != 0 && n == strlen(name) + 1);
  WCHAR wname[257];
  n = 257;
  CHECK(::GetUserNameW(wname, &n) && n == (DWORD)lstrlenW(wname) + 1);

  // The standard folders.
  char path[MAX_PATH];
  CHECK(::SHGetFolderPathA(NULL, CSIDL_APPDATA | CSIDL_FLAG_CREATE, NULL, SHGFP_TYPE_CURRENT, path) == S_OK);
  DWORD attr = ::GetFileAttributesA(path);
  CHECK(attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY));
  CHECK(::SHGetFolderPathA(NULL, CSIDL_PROFILE, NULL, SHGFP_TYPE_CURRENT, path) == S_OK && path[0]);
  WCHAR wpath[MAX_PATH];
  CHECK(::SHGetFolderPathW(NULL, CSIDL_LOCAL_APPDATA | CSIDL_FLAG_CREATE, NULL, SHGFP_TYPE_CURRENT, wpath) == S_OK);
  CHECK(::SHGetSpecialFolderPathA(NULL, path, CSIDL_APPDATA, FALSE));
#ifdef _WINPORT
  // The home folder, and the XDG folders where they are named.
  CHECK(::SHGetFolderPathA(NULL, CSIDL_PROFILE, NULL, SHGFP_TYPE_CURRENT, path) == S_OK);
  CHECK(strcmp(path, getenv("HOME")) == 0);
#ifndef __APPLE__
  char tmp[MAX_PATH], want[MAX_PATH];
  ::GetTempPathA(MAX_PATH, tmp);
  snprintf(want, sizeof want, "%s/iotest-config-%d", tmp, (int)getpid());
  setenv("XDG_CONFIG_HOME", want, 1);
  CHECK(::SHGetFolderPathA(NULL, CSIDL_APPDATA, NULL, SHGFP_TYPE_CURRENT, path) == S_FALSE);  // not there yet
  CHECK(::SHGetFolderPathW(NULL, CSIDL_APPDATA, NULL, SHGFP_TYPE_CURRENT, wpath) == E_FAIL);
  CHECK(::SHGetFolderPathA(NULL, CSIDL_APPDATA | CSIDL_FLAG_CREATE, NULL, SHGFP_TYPE_CURRENT, path) == S_OK);
  CHECK(strcmp(path, want) == 0);
  rmdir(want);
  unsetenv("XDG_CONFIG_HOME");
#endif
#endif
}

// Named kernel objects between processes: what a child started with
// one of these finds, as its exit code (0 for what the parent expects).
static int NamedChild(const char* what) {
  if (strcmp(what, "-exists") == 0) {
    // The parent owns the mutex: it exists, and is not this process's to take.
    HANDLE m = ::CreateMutexA(NULL, TRUE, "Local\\IoTestMutex");
    if (!m || ::GetLastError() != ERROR_ALREADY_EXISTS) return 1;
    if (::WaitForSingleObject(m, 0) != WAIT_TIMEOUT) return 2;
    if (::ReleaseMutex(m)) return 3;  // not the owner
    // The parent releases it once this event is set.
    HANDLE go = ::OpenEventA(EVENT_ALL_ACCESS, FALSE, "Local\\IoTestGo");
    if (!go) return 4;
    ::SetEvent(go);
    if (::WaitForSingleObject(m, 10000) != WAIT_OBJECT_0) return 5;
    if (!::ReleaseMutex(m)) return 6;
    ::CloseHandle(go);
    ::CloseHandle(m);
    return 0;
  }
  if (strcmp(what, "-abandon") == 0) {
    // Owns the mutex and ends without letting it go.
    HANDLE m = ::CreateMutexA(NULL, TRUE, "Local\\IoTestAbandoned");
    HANDLE ready = ::OpenEventA(EVENT_ALL_ACCESS, FALSE, "Local\\IoTestReady");
    if (!m || !ready) return 1;
    ::SetEvent(ready);
    ::Sleep(200);
    return 0;
  }
  if (strcmp(what, "-semaphore") == 0) {
    HANDLE s = ::OpenSemaphoreA(SEMAPHORE_ALL_ACCESS, FALSE, "Local\\IoTestSemaphore");
    if (!s) return 1;
    if (::WaitForSingleObject(s, 0) != WAIT_OBJECT_0) return 2;  // takes one of two
    LONG prev = -1;
    if (!::ReleaseSemaphore(s, 2, &prev) || prev != 0) return 3;  // back, and one more
    ::CloseHandle(s);
    return 0;
  }
  return 99;
}

static DWORD RunNamedChild(const char* what, HANDLE* process = NULL) {
  char exe[MAX_PATH], cmd[MAX_PATH + 32];
  ::GetModuleFileNameA(NULL, exe, MAX_PATH);
  wsprintfA(cmd, "\"%s\" %s", exe, what);
  STARTUPINFOA si = {};
  si.cb = sizeof si;
  PROCESS_INFORMATION pi = {};
  if (!::CreateProcessA(NULL, cmd, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi)) return 1000;
  ::CloseHandle(pi.hThread);
  if (process) {
    *process = pi.hProcess;
    return 0;
  }
  DWORD code = 1001;
  if (::WaitForSingleObject(pi.hProcess, 20000) == WAIT_OBJECT_0) ::GetExitCodeProcess(pi.hProcess, &code);
  ::CloseHandle(pi.hProcess);
  return code;
}

static void TestNamedObjects() {
  // A mutex another process owns, then waits for.
  HANDLE m = ::CreateMutexA(NULL, TRUE, "Local\\IoTestMutex");
  CHECK(m != NULL && ::GetLastError() == 0);
  HANDLE go = ::CreateEventA(NULL, FALSE, FALSE, "Local\\IoTestGo");
  CHECK(go != NULL);
  HANDLE child = NULL;
  CHECK(RunNamedChild("-exists", &child) == 0);
  // The child's event, set in its process, wakes this wait.
  CHECK(::WaitForSingleObject(go, 10000) == WAIT_OBJECT_0);
  CHECK(::ReleaseMutex(m));
  DWORD code = 1001;
  if (child) {
    CHECK(::WaitForSingleObject(child, 20000) == WAIT_OBJECT_0);
    ::GetExitCodeProcess(child, &code);
    ::CloseHandle(child);
  }
  CHECK(code == 0);
  // Free again once the child let go, and now this thread's.
  CHECK(::WaitForSingleObject(m, 5000) == WAIT_OBJECT_0);
  CHECK(::ReleaseMutex(m));
  ::CloseHandle(go);
  ::CloseHandle(m);
  // With every handle closed, the name is free: a new mutex, not the old.
  m = ::CreateMutexA(NULL, FALSE, "Local\\IoTestMutex");
  CHECK(m != NULL && ::GetLastError() == 0);
  ::CloseHandle(m);

  // A mutex whose owner ended without letting it go is abandoned to the
  // next waiter, which then owns it.
  HANDLE ready = ::CreateEventA(NULL, TRUE, FALSE, "Local\\IoTestReady");
  CHECK(RunNamedChild("-abandon", &child) == 0);
  CHECK(::WaitForSingleObject(ready, 10000) == WAIT_OBJECT_0);
  m = ::OpenMutexA(MUTEX_ALL_ACCESS, FALSE, "Local\\IoTestAbandoned");
  CHECK(m != NULL);
  if (child) {
    CHECK(::WaitForSingleObject(child, 20000) == WAIT_OBJECT_0);
    ::CloseHandle(child);
  }
  if (m) {
    CHECK(::WaitForSingleObject(m, 5000) == WAIT_ABANDONED_0);
    CHECK(::ReleaseMutex(m));
    CHECK(::WaitForSingleObject(m, 0) == WAIT_OBJECT_0);
    CHECK(::ReleaseMutex(m));
    ::CloseHandle(m);
  }
  ::CloseHandle(ready);

  // A semaphore's count is one between the processes.
  HANDLE s = ::CreateSemaphoreA(NULL, 2, 5, "Local\\IoTestSemaphore");
  CHECK(s != NULL);
  CHECK(::WaitForSingleObject(s, 0) == WAIT_OBJECT_0);  // one left
  CHECK(RunNamedChild("-semaphore") == 0);
  LONG prev = -1;
  CHECK(::ReleaseSemaphore(s, 1, &prev) && prev == 2);
  ::CloseHandle(s);

  // A name is one kind of object.
  HANDLE e = ::CreateEventA(NULL, FALSE, FALSE, "Local\\IoTestKind");
  CHECK(::CreateMutexA(NULL, FALSE, "Local\\IoTestKind") == NULL && ::GetLastError() == ERROR_INVALID_HANDLE);
  ::CloseHandle(e);
}

int main(int argc, char** argv) {
  if (argc > 1 && strcmp(argv[1], "-child") == 0) return Child();
  if (argc > 1 && argv[1][0] == '-') return NamedChild(argv[1]);
  TestPipes();
  printf("pipes: %d failure(s) so far\n", g_failures);
  TestChild();
  printf("child: %d failure(s) so far\n", g_failures);
  TestBuildDcb();
  TestEnvironment();
  TestNamedObjects();
  printf("named objects: %d failure(s) so far\n", g_failures);
#ifdef _WINPORT
  TestPort();
  printf("port: %d failure(s) so far\n", g_failures);
#endif
  printf(g_failures ? "FAIL: %d failure(s)\n" : "PASS: %d failure(s)\n", g_failures);
  return g_failures ? 1 : 0;
}
