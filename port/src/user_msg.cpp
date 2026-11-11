// user_msg.cpp - messages: the threads' queues, sending and posting, the
// message loop, timers, hooks, and the translation between ANSI and
// Unicode window procedures.
//
// A thread's queue holds what was posted to it, input from the backend,
// messages sent from other threads, and its timers. WM_PAINT and WM_TIMER
// are never stored: GetMessage makes them when nothing else is waiting.
// A thread with nothing to do waits in the backend, so that input keeps
// coming, unless another thread already does; then it waits on its
// queue's condition.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#include "user.h"

#include <string.h>
#include <chrono>
#include <unordered_map>

using namespace winport;

namespace winport {

namespace {

std::unordered_map<DWORD, Queue*>& Queues() {
  static auto* m = new std::unordered_map<DWORD, Queue*>;
  return *m;
}
DWORD g_backendWaiter = 0;  // the thread waiting in the backend, if any

}  // namespace

Queue* QueueFor(DWORD tid) {
  UserGuard g(UserLock());
  Queue*& q = Queues()[tid];
  if (!q) {
    q = new Queue;
    q->tid = tid;
  }
  return q;
}

Queue* CurrentQueue() {
  static thread_local Queue* q = NULL;
  if (!q) q = QueueFor(GetCurrentThreadId());
  return q;
}

void WakeQueue(Queue* q) {
  q->wake.notify_all();
  if (g_backendWaiter == q->tid) GetBackend()->Wake();
}

POINT& CursorPos() {
  static POINT p = {0, 0};
  return p;
}

// ---------------------------------------------------------------------
// Hooks: the newest runs first and passes on with CallNextHookEx. Only
// hooks on the calling thread run.

namespace {

struct Hook {
  int id;
  HOOKPROC proc;
  DWORD tid;
  bool dead;
};
// Unhooked hooks stay in the list, marked dead, so that CallNextHookEx
// from a hook that removed itself still finds its place.
std::vector<Hook*>& Hooks() {
  static auto* v = new std::vector<Hook*>;
  return *v;
}

HOOKPROC NextHook(int id, DWORD tid, Hook* after, Hook** found) {
  UserGuard g(UserLock());
  std::vector<Hook*>& v = Hooks();
  size_t i = v.size();
  if (after) {
    while (i > 0 && v[i - 1] != after) i--;
    if (i == 0) return NULL;
    i--;
  }
  while (i-- > 0) {
    Hook* h = v[i];
    if (!h->dead && h->id == id && h->tid == tid) {
      *found = h;
      return h->proc;
    }
  }
  return NULL;
}

}  // namespace

bool HaveHooks(int idHook) {
  Hook* h;
  return NextHook(idHook, GetCurrentThreadId(), NULL, &h) != NULL;
}

LRESULT CallHooks(int idHook, int code, WPARAM wParam, LPARAM lParam) {
  Hook* h;
  HOOKPROC proc = NextHook(idHook, GetCurrentThreadId(), NULL, &h);
  return proc ? proc(code, wParam, lParam) : 0;
}

// ---------------------------------------------------------------------
// Window procedure handles

namespace {

struct ProcThunk {
  WNDPROC proc;
  bool unicode;
};
std::vector<ProcThunk>& Thunks() {
  static auto* v = new std::vector<ProcThunk>;
  return *v;
}
// Handles are addresses no procedure has: the top 64 KB of the address
// space, as on Windows.
const UINT_PTR kThunkBase = ~(UINT_PTR)0xFFFF;
const size_t kMaxThunks = 0x3FFF;

}  // namespace

WNDPROC ProcHandle(WNDPROC proc, bool procUnicode, bool callerUnicode) {
  proc = ResolveProc(proc, &procUnicode);
  if (!proc || procUnicode == callerUnicode) return proc;
  UserGuard g(UserLock());
  std::vector<ProcThunk>& v = Thunks();
  size_t i = 0;
  while (i < v.size() && (v[i].proc != proc || v[i].unicode != procUnicode)) i++;
  if (i == v.size()) {
    if (i >= kMaxThunks) return proc;
    v.push_back(ProcThunk{proc, procUnicode});
  }
  return (WNDPROC)(kThunkBase + i * 4);
}

WNDPROC ResolveProc(WNDPROC proc, bool* unicode) {
  UINT_PTR p = (UINT_PTR)proc;
  if (p < kThunkBase || (p & 3)) return proc;
  UserGuard g(UserLock());
  size_t i = (p - kThunkBase) / 4;
  if (i >= Thunks().size()) return proc;
  *unicode = Thunks()[i].unicode;
  return Thunks()[i].proc;
}

// ---------------------------------------------------------------------
// ANSI and Unicode. The ANSI code page is UTF-8, so a character beyond
// ASCII is several WM_CHAR messages to an ANSI procedure.

namespace {

bool ListHasStrings(HWND h, bool combo) {
  Window* w = GetWindow(h);
  if (!w) return false;
  if (combo)
    return !(w->style & (CBS_OWNERDRAWFIXED | CBS_OWNERDRAWVARIABLE)) || (w->style & CBS_HASSTRINGS);
  return !(w->style & (LBS_OWNERDRAWFIXED | LBS_OWNERDRAWVARIABLE)) || (w->style & LBS_HASSTRINGS);
}

// Which message takes a string in lParam: 1 always, 2 for a list box
// item, 3 for a combo box item, 0 never.
int StringIn(UINT msg) {
  switch (msg) {
    case WM_SETTEXT: case WM_SETTINGCHANGE: case WM_DEVMODECHANGE: case EM_REPLACESEL: case LB_DIR: case CB_DIR:
      return 1;
    case LB_ADDSTRING: case LB_INSERTSTRING: case LB_FINDSTRING: case LB_FINDSTRINGEXACT: case LB_SELECTSTRING:
      return 2;
    case CB_ADDSTRING: case CB_INSERTSTRING: case CB_FINDSTRING: case CB_FINDSTRINGEXACT: case CB_SELECTSTRING:
      return 3;
  }
  return 0;
}

bool IsCharMsg(UINT msg) {
  return msg == WM_CHAR || msg == WM_DEADCHAR || msg == WM_SYSCHAR || msg == WM_SYSDEADCHAR || msg == WM_IME_CHAR;
}

// Copies UTF-8 into an ANSI buffer of cch bytes with a zero, without
// splitting a character; returns the bytes copied.
size_t CopyA(const std::string& s, LPSTR buf, size_t cch, bool zero) {
  if (!buf || cch == 0) return 0;
  size_t n = s.size();
  size_t room = zero ? cch - 1 : cch;
  if (n > room) {
    n = room;
    while (n > 0 && ((unsigned char)s[n] & 0xC0) == 0x80) n--;
  }
  memcpy(buf, s.data(), n);
  if (zero) buf[n] = 0;
  return n;
}
size_t CopyW(const std::wstring& s, LPWSTR buf, size_t cch, bool zero) {
  if (!buf || cch == 0) return 0;
  size_t n = s.size();
  size_t room = zero ? cch - 1 : cch;
  if (n > room) n = room;
  memcpy(buf, s.data(), n * sizeof(wchar_t));
  if (zero) buf[n] = 0;
  return n;
}

// An ANSI message to a Unicode procedure.
LRESULT CallAtoW(WNDPROC proc, HWND h, UINT msg, WPARAM wp, LPARAM lp) {
  int in = StringIn(msg);
  if (in && lp && (in == 1 || ListHasStrings(h, in == 3))) {
    std::wstring s = Widen((LPCSTR)lp);
    return proc(h, msg, wp, (LPARAM)s.c_str());
  }
  switch (msg) {
    case WM_NCCREATE:
    case WM_CREATE: {
      CREATESTRUCTA* a = (CREATESTRUCTA*)lp;
      if (!a) break;
      CREATESTRUCTW c;
      memcpy(&c, a, sizeof c);
      std::wstring name = a->lpszName ? Widen(a->lpszName) : std::wstring();
      std::wstring cls = IS_INTRESOURCE(a->lpszClass) ? std::wstring() : Widen(a->lpszClass);
      if (a->lpszName) c.lpszName = name.c_str();
      if (!IS_INTRESOURCE(a->lpszClass)) c.lpszClass = cls.c_str();
      return proc(h, msg, wp, (LPARAM)&c);
    }
    case WM_MDICREATE: {
      MDICREATESTRUCTA* a = (MDICREATESTRUCTA*)lp;
      if (!a) break;
      MDICREATESTRUCTW c;
      memcpy(&c, a, sizeof c);
      std::wstring title = a->szTitle ? Widen(a->szTitle) : std::wstring();
      std::wstring cls = IS_INTRESOURCE(a->szClass) ? std::wstring() : Widen(a->szClass);
      if (a->szTitle) c.szTitle = title.c_str();
      if (!IS_INTRESOURCE(a->szClass)) c.szClass = cls.c_str();
      return proc(h, msg, wp, (LPARAM)&c);
    }
    case WM_GETTEXT:
    case WM_ASKCBFORMATNAME: {
      if (!lp || wp == 0) break;
      // A UTF-8 string is never shorter than its UTF-32 form.
      std::vector<wchar_t> buf(wp + 1);
      LRESULT n = proc(h, msg, wp, (LPARAM)buf.data());
      if (n < 0) n = 0;
      return CopyA(Narrow(buf.data(), (int)n), (LPSTR)lp, wp, true);
    }
    case LB_GETTEXT:
    case CB_GETLBTEXT: {
      bool combo = msg == CB_GETLBTEXT;
      if (!lp || !ListHasStrings(h, combo)) break;
      LRESULT len = proc(h, combo ? CB_GETLBTEXTLEN : LB_GETTEXTLEN, wp, 0);
      if (len < 0) return len;
      std::vector<wchar_t> buf(len + 1);
      LRESULT n = proc(h, msg, wp, (LPARAM)buf.data());
      if (n < 0) return n;
      std::string s = Narrow(buf.data(), (int)n);
      memcpy((LPSTR)lp, s.c_str(), s.size() + 1);
      return s.size();
    }
    case EM_GETLINE: {
      if (!lp) break;
      WORD cch = *(WORD*)lp;
      std::vector<wchar_t> buf(cch + 2);
      *(WORD*)buf.data() = cch;
      LRESULT n = proc(h, msg, wp, (LPARAM)buf.data());
      if (n <= 0) return n;
      return CopyA(Narrow(buf.data(), (int)n), (LPSTR)lp, cch, false);
    }
    case WM_GETTEXTLENGTH:
    case LB_GETTEXTLEN:
    case CB_GETLBTEXTLEN: {
      if (msg != WM_GETTEXTLENGTH && !ListHasStrings(h, msg == CB_GETLBTEXTLEN)) break;
      LRESULT len = proc(h, msg, wp, lp);
      if (len <= 0) return len;
      std::vector<wchar_t> buf(len + 1);
      LRESULT n;
      if (msg == WM_GETTEXTLENGTH)
        n = proc(h, WM_GETTEXT, len + 1, (LPARAM)buf.data());
      else
        n = proc(h, msg == LB_GETTEXTLEN ? LB_GETTEXT : CB_GETLBTEXT, wp, (LPARAM)buf.data());
      if (n < 0) return len;
      return Narrow(buf.data(), (int)n).size();
    }
    default:
      if (IsCharMsg(msg) && wp >= 0x80 && wp < 0x100) {
        // Gather the bytes of a UTF-8 character.
        Queue* q = CurrentQueue();
        BYTE b = (BYTE)wp;
        if ((b & 0xC0) == 0x80) {
          if (q->utf8Pending == 0) break;
          q->utf8Char = (q->utf8Char << 6) | (b & 0x3F);
          if (--q->utf8Pending > 0) return 0;
          return proc(h, msg, q->utf8Char, lp);
        }
        if ((b & 0xE0) == 0xC0) {
          q->utf8Pending = 1;
          q->utf8Char = b & 0x1F;
        } else if ((b & 0xF0) == 0xE0) {
          q->utf8Pending = 2;
          q->utf8Char = b & 0x0F;
        } else if ((b & 0xF8) == 0xF0) {
          q->utf8Pending = 3;
          q->utf8Char = b & 0x07;
        } else {
          break;
        }
        return 0;
      }
      break;
  }
  return proc(h, msg, wp, lp);
}

// A Unicode message to an ANSI procedure.
LRESULT CallWtoA(WNDPROC proc, HWND h, UINT msg, WPARAM wp, LPARAM lp) {
  int in = StringIn(msg);
  if (in && lp && (in == 1 || ListHasStrings(h, in == 3))) {
    std::string s = Narrow((LPCWSTR)lp);
    return proc(h, msg, wp, (LPARAM)s.c_str());
  }
  switch (msg) {
    case WM_NCCREATE:
    case WM_CREATE: {
      CREATESTRUCTW* w = (CREATESTRUCTW*)lp;
      if (!w) break;
      CREATESTRUCTA c;
      memcpy(&c, w, sizeof c);
      std::string name = w->lpszName ? Narrow(w->lpszName) : std::string();
      std::string cls = IS_INTRESOURCE(w->lpszClass) ? std::string() : Narrow(w->lpszClass);
      if (w->lpszName) c.lpszName = name.c_str();
      if (!IS_INTRESOURCE(w->lpszClass)) c.lpszClass = cls.c_str();
      return proc(h, msg, wp, (LPARAM)&c);
    }
    case WM_MDICREATE: {
      MDICREATESTRUCTW* w = (MDICREATESTRUCTW*)lp;
      if (!w) break;
      MDICREATESTRUCTA c;
      memcpy(&c, w, sizeof c);
      std::string title = w->szTitle ? Narrow(w->szTitle) : std::string();
      std::string cls = IS_INTRESOURCE(w->szClass) ? std::string() : Narrow(w->szClass);
      if (w->szTitle) c.szTitle = title.c_str();
      if (!IS_INTRESOURCE(w->szClass)) c.szClass = cls.c_str();
      return proc(h, msg, wp, (LPARAM)&c);
    }
    case WM_GETTEXT:
    case WM_ASKCBFORMATNAME: {
      if (!lp || wp == 0) break;
      std::vector<char> buf(wp * 4 + 1);
      LRESULT n = proc(h, msg, wp * 4 + 1, (LPARAM)buf.data());
      if (n < 0) n = 0;
      return CopyW(Widen(buf.data(), (int)n), (LPWSTR)lp, wp, true);
    }
    case LB_GETTEXT:
    case CB_GETLBTEXT: {
      bool combo = msg == CB_GETLBTEXT;
      if (!lp || !ListHasStrings(h, combo)) break;
      LRESULT len = proc(h, combo ? CB_GETLBTEXTLEN : LB_GETTEXTLEN, wp, 0);
      if (len < 0) return len;
      std::vector<char> buf(len + 1);
      LRESULT n = proc(h, msg, wp, (LPARAM)buf.data());
      if (n < 0) return n;
      std::wstring s = Widen(buf.data(), (int)n);
      memcpy((LPWSTR)lp, s.c_str(), (s.size() + 1) * sizeof(wchar_t));
      return s.size();
    }
    case EM_GETLINE: {
      if (!lp) break;
      WORD cch = *(WORD*)lp;
      size_t size = (size_t)cch * 4 < 0xFFFF ? (size_t)cch * 4 : 0xFFFF;
      std::vector<char> buf(size + 2);
      *(WORD*)buf.data() = (WORD)size;
      LRESULT n = proc(h, msg, wp, (LPARAM)buf.data());
      if (n <= 0) return n;
      return CopyW(Widen(buf.data(), (int)n), (LPWSTR)lp, cch, false);
    }
    case WM_GETTEXTLENGTH:
    case LB_GETTEXTLEN:
    case CB_GETLBTEXTLEN: {
      if (msg != WM_GETTEXTLENGTH && !ListHasStrings(h, msg == CB_GETLBTEXTLEN)) break;
      LRESULT len = proc(h, msg, wp, lp);
      if (len <= 0) return len;
      std::vector<char> buf(len + 1);
      LRESULT n;
      if (msg == WM_GETTEXTLENGTH)
        n = proc(h, WM_GETTEXT, len + 1, (LPARAM)buf.data());
      else
        n = proc(h, msg == LB_GETTEXTLEN ? LB_GETTEXT : CB_GETLBTEXT, wp, (LPARAM)buf.data());
      if (n < 0) return len;
      return Widen(buf.data(), (int)n).size();
    }
    default:
      if (IsCharMsg(msg) && wp >= 0x80) {
        std::string s = Narrow((const wchar_t*)&wp, 1);
        LRESULT r = 0;
        for (char c : s) r = proc(h, msg, (BYTE)c, lp);
        return r;
      }
      break;
  }
  return proc(h, msg, wp, lp);
}

}  // namespace

LRESULT CallProc(WNDPROC proc, bool procUnicode, HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam,
                 bool msgUnicode) {
  proc = ResolveProc(proc, &procUnicode);
  if (!proc) return 0;
  if (procUnicode == msgUnicode) return proc(hwnd, msg, wParam, lParam);
  return procUnicode ? CallAtoW(proc, hwnd, msg, wParam, lParam) : CallWtoA(proc, hwnd, msg, wParam, lParam);
}

// ---------------------------------------------------------------------
// Sending

namespace {

// Calls the window's procedure on its own thread, with the hooks.
LRESULT Deliver(Window* w, UINT msg, WPARAM wp, LPARAM lp, bool unicode, bool fromThisThread) {
  HWND h = w->hwnd;
  if (HaveHooks(WH_CALLWNDPROC)) {
    CWPSTRUCT cwp = {lp, wp, msg, h};
    CallHooks(WH_CALLWNDPROC, HC_ACTION, fromThisThread, (LPARAM)&cwp);
  }
  LRESULT r = CallProc(w->proc, w->unicode, h, msg, wp, lp, unicode);
  if (HaveHooks(WH_CALLWNDPROCRET)) {
    CWPRETSTRUCT ret = {r, lp, wp, msg, h};
    CallHooks(WH_CALLWNDPROCRET, HC_ACTION, fromThisThread, (LPARAM)&ret);
  }
  return r;
}

// Hands a reply to the sender. Call without the lock.
void Reply(SentMessage* sm, LRESULT r) {
  UserGuard g(UserLock());
  sm->result = r;
  sm->done = true;
  if (sm->callback) {
    sm->from->callbacks.push_back(sm);
    WakeQueue(sm->from);
  } else if (sm->noReply) {
    delete sm;
  } else {
    WakeQueue(sm->from);
  }
}

// Handles the messages other threads have sent to this one.
void ProcessSent(Queue* q) {
  for (;;) {
    SentMessage* sm;
    {
      UserGuard g(UserLock());
      if (q->sent.empty()) return;
      sm = q->sent.front();
      q->sent.pop_front();
    }
    SentMessage* outer = q->inSend;
    q->inSend = sm;
    Window* w = GetWindow(sm->hwnd);
    LRESULT r = w ? Deliver(w, sm->msg, sm->wParam, sm->lParam, sm->unicode, false) : 0;
    // ReplyMessage may have answered already.
    if (q->inSend == sm) Reply(sm, r);
    q->inSend = outer;
  }
}

// Calls the SendMessageCallback functions whose replies have come.
void DeliverCallbacks(Queue* q) {
  for (;;) {
    SentMessage* sm;
    {
      UserGuard g(UserLock());
      if (q->callbacks.empty()) return;
      sm = q->callbacks.front();
      q->callbacks.pop_front();
    }
    sm->callback(sm->hwnd, sm->msg, sm->callbackData, sm->result);
    delete sm;
  }
}

LRESULT SendCore(HWND h, UINT msg, WPARAM wp, LPARAM lp, bool unicode, DWORD timeout, bool* ok);

LRESULT Broadcast(UINT msg, WPARAM wp, LPARAM lp, bool unicode, DWORD timeout) {
  std::vector<HWND> all;
  {
    UserGuard g(UserLock());
    for (Window* t : TopLevels()) all.push_back(t->hwnd);
  }
  bool ok;
  for (HWND t : all) SendCore(t, msg, wp, lp, unicode, timeout, &ok);
  return 1;
}

// Sends to a window of another thread and waits for the reply, handling
// what is sent to this thread meanwhile.
LRESULT SendOther(Window* w, UINT msg, WPARAM wp, LPARAM lp, bool unicode, DWORD timeout, bool* ok) {
  Queue* me = CurrentQueue();
  Queue* target = QueueFor(w->tid);
  SentMessage* sm = new SentMessage{w->hwnd, msg, wp, lp, unicode};
  sm->from = me;
  auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout);
  UserGuard g(UserLock());
  target->sent.push_back(sm);
  WakeQueue(target);
  while (!sm->done) {
    if (!me->sent.empty()) {
      g.unlock();
      ProcessSent(me);
      g.lock();
      continue;
    }
    if (timeout == INFINITE) {
      me->wake.wait(g);
    } else if (me->wake.wait_until(g, deadline) == std::cv_status::timeout && !sm->done) {
      sm->noReply = true;  // the receiver frees it
      *ok = false;
      SetLastError(ERROR_TIMEOUT);
      return 0;
    }
  }
  LRESULT r = sm->result;
  delete sm;
  return r;
}

LRESULT SendCore(HWND h, UINT msg, WPARAM wp, LPARAM lp, bool unicode, DWORD timeout, bool* ok) {
  *ok = true;
  if (h == HWND_BROADCAST) return Broadcast(msg, wp, lp, unicode, timeout);
  Window* w = GetWindowOrFail(h);
  if (!w) {
    *ok = false;
    return 0;
  }
  if (w->tid == GetCurrentThreadId()) return Deliver(w, msg, wp, lp, unicode, true);
  return SendOther(w, msg, wp, lp, unicode, timeout, ok);
}

BOOL SendNotify(HWND h, UINT msg, WPARAM wp, LPARAM lp, bool unicode, SENDASYNCPROC cb, ULONG_PTR data) {
  if (h == HWND_BROADCAST) {
    std::vector<HWND> all;
    {
      UserGuard g(UserLock());
      for (Window* t : TopLevels()) all.push_back(t->hwnd);
    }
    for (HWND t : all) SendNotify(t, msg, wp, lp, unicode, cb, data);
    return TRUE;
  }
  Window* w = GetWindowOrFail(h);
  if (!w) return FALSE;
  if (w->tid == GetCurrentThreadId()) {
    LRESULT r = Deliver(w, msg, wp, lp, unicode, true);
    if (cb) cb(h, msg, data, r);
    return TRUE;
  }
  SentMessage* sm = new SentMessage{h, msg, wp, lp, unicode};
  sm->noReply = true;
  sm->from = CurrentQueue();
  sm->callback = cb;
  sm->callbackData = data;
  Queue* target = QueueFor(w->tid);
  UserGuard g(UserLock());
  target->sent.push_back(sm);
  WakeQueue(target);
  return TRUE;
}

}  // namespace

LRESULT SendTo(Window* w, UINT msg, WPARAM wParam, LPARAM lParam, bool unicode) {
  bool ok;
  return SendCore(w->hwnd, msg, wParam, lParam, unicode, INFINITE, &ok);
}

// ---------------------------------------------------------------------
// Posting and the message loop

namespace {

const LPARAM kTranslated = 0x02000000;  // WM_KEYDOWN bit 25: the backend sends the WM_CHAR itself
const UINT WM_SYSTIMER = 0x0118;        // the caret's blink timer

UINT_PTR g_nextTimerId = 0x100;

MSG NewMsg(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
  MSG m = {h, msg, wp, lp, GetTickCount(), CursorPos()};
  return m;
}

BOOL PostCore(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
  if (h == HWND_BROADCAST) {
    std::vector<HWND> all;
    {
      UserGuard g(UserLock());
      for (Window* t : TopLevels()) all.push_back(t->hwnd);
    }
    for (HWND t : all) PostCore(t, msg, wp, lp);
    return TRUE;
  }
  Queue* q;
  if (!h) {
    q = CurrentQueue();
  } else {
    Window* w = GetWindowOrFail(h);
    if (!w) return FALSE;
    q = QueueFor(w->tid);
  }
  UserGuard g(UserLock());
  q->posted.push_back(NewMsg(h, msg, wp, lp));
  WakeQueue(q);
  return TRUE;
}

bool InRange(UINT msg, UINT min, UINT max) {
  if (min == 0 && max == 0) return true;
  if (max == 0) max = ~0u;
  return msg >= min && msg <= max;
}

bool ForWindow(HWND msgWnd, HWND filter) {
  if (filter == NULL) return true;
  if (filter == (HWND)-1) return msgWnd == NULL;
  return msgWnd == filter || (msgWnd && IsChild(filter, msgWnd));
}

bool Matches(const MSG& m, HWND filter, UINT min, UINT max) {
  return ForWindow(m.hwnd, filter) && InRange(m.message, min, max);
}

ULONGLONG Now() { return GetTickCount64(); }

// The next message for GetMessage or PeekMessage, in Windows' order:
// sent, posted, input, quit, paint, timers.
bool PeekCore(MSG* out, HWND filter, UINT min, UINT max, UINT flags) {
  Queue* q = CurrentQueue();
  ProcessSent(q);
  DeliverCallbacks(q);
  bool remove = (flags & PM_REMOVE) != 0;
  MSG m;
  bool got = false;
  {
    UserGuard g(UserLock());
    for (std::deque<MSG>* d : {&q->posted, &q->input}) {
      for (auto it = d->begin(); it != d->end(); ++it) {
        if (Matches(*it, filter, min, max)) {
          m = *it;
          if (remove) d->erase(it);
          got = true;
          break;
        }
      }
      if (got) break;
    }
    if (!got && q->quit && InRange(WM_QUIT, min, max)) {
      m = NewMsg(NULL, WM_QUIT, (WPARAM)q->quitCode, 0);
      if (remove) q->quit = false;
      got = true;
    }
    if (!got && InRange(WM_PAINT, min, max) && filter != (HWND)-1) {
      if (Window* w = WindowToPaint(q->tid, filter)) {
        m = NewMsg(w->hwnd, WM_PAINT, 0, 0);
        got = true;
      }
    }
    if (!got && InRange(WM_TIMER, min, max)) {
      ULONGLONG now = Now();
      for (Timer& t : q->timers) {
        if (t.due <= now && ForWindow(t.hwnd, filter)) {
          m = NewMsg(t.hwnd, WM_TIMER, t.id, (LPARAM)t.proc);
          if (remove) t.due = now + t.elapse;
          got = true;
          break;
        }
      }
    }
    if (got && remove) q->last = m;
  }
  if (!got) return false;
  if (remove && HaveHooks(WH_GETMESSAGE)) CallHooks(WH_GETMESSAGE, HC_ACTION, PM_REMOVE, (LPARAM)&m);
  *out = m;
  return true;
}

// Whether anything other than paint and timers waits; with the lock.
bool HasWork(Queue* q) {
  return !q->posted.empty() || !q->input.empty() || !q->sent.empty() || !q->callbacks.empty() || q->quit;
}

DWORD QueueState(Queue* q) {
  DWORD s = 0;
  if (!q->posted.empty()) s |= QS_POSTMESSAGE | QS_ALLPOSTMESSAGE;
  for (const MSG& m : q->input) {
    if (m.message >= WM_KEYFIRST && m.message <= WM_KEYLAST) s |= QS_KEY;
    else if (m.message == WM_MOUSEMOVE) s |= QS_MOUSEMOVE;
    else s |= QS_MOUSEBUTTON;
  }
  if (!q->sent.empty()) s |= QS_SENDMESSAGE;
  if (q->quit) s |= QS_POSTMESSAGE;
  if (WindowToPaint(q->tid, NULL)) s |= QS_PAINT;
  ULONGLONG now = Now();
  for (const Timer& t : q->timers)
    if (t.due <= now) s |= QS_TIMER;
  return s;
}

// Waits until something comes for the queue, a timer falls due, or
// timeout ms pass.
void WaitForWork(Queue* q, DWORD timeout) {
  UserGuard g(UserLock());
  if (HasWork(q)) return;
  ULONGLONG now = Now();
  ULONGLONG wait = timeout == INFINITE ? ~0ULL : timeout;
  for (const Timer& t : q->timers) {
    ULONGLONG left = t.due > now ? t.due - now : 0;
    if (left < wait) wait = left;
  }
  if (wait == 0) return;
  if (g_backendWaiter == 0) {
    g_backendWaiter = q->tid;
    g.unlock();
    GetBackend()->Wait(wait == ~0ULL ? INFINITE : (DWORD)(wait < 0x7FFFFFFF ? wait : 0x7FFFFFFF));
    g.lock();
    g_backendWaiter = 0;
  } else if (wait == ~0ULL) {
    q->wake.wait(g);
  } else {
    q->wake.wait_for(g, std::chrono::milliseconds(wait));
  }
}

BOOL GetCore(LPMSG m, HWND h, UINT min, UINT max) {
  if (!m) {
    SetLastError(ERROR_INVALID_PARAMETER);
    return -1;
  }
  if (h && h != (HWND)-1 && !GetWindowOrFail(h)) return -1;
  for (;;) {
    if (PeekCore(m, h, min, max, PM_REMOVE)) return m->message != WM_QUIT;
    WaitForWork(CurrentQueue(), INFINITE);
  }
}

LRESULT DispatchCore(const MSG* m, bool unicode) {
  if (!m) return 0;
  if ((m->message == WM_TIMER || m->message == WM_SYSTIMER) && m->lParam) {
    ((TIMERPROC)m->lParam)(m->hwnd, m->message, (UINT_PTR)m->wParam, GetTickCount());
    return 0;
  }
  if (!m->hwnd) return 0;
  Window* w = GetWindowOrFail(m->hwnd);
  if (!w) return 0;
  if (w->tid != GetCurrentThreadId()) {
    SetLastError(ERROR_MESSAGE_SYNC_ONLY);
    return 0;
  }
  LRESULT r = CallProc(w->proc, w->unicode, m->hwnd, m->message, m->wParam, m->lParam, unicode);
  if (m->message == WM_PAINT) {
    UserGuard g(UserLock());
    if (GetWindow(m->hwnd)) w->internalPaint = false;
  }
  return r;
}

// The character a key makes on a US keyboard, or 0.
WCHAR KeyChar(UINT vk, const BYTE* keys) {
  bool shift = (keys[VK_SHIFT] & 0x80) != 0;
  bool ctrl = (keys[VK_CONTROL] & 0x80) != 0;
  bool caps = (keys[VK_CAPITAL] & 1) != 0;
  if (vk >= 'A' && vk <= 'Z') {
    if (ctrl) return (WCHAR)(vk - 'A' + 1);
    return (WCHAR)((shift != caps) ? vk : vk - 'A' + 'a');
  }
  if (ctrl) {
    switch (vk) {
      case VK_OEM_4: return 0x1B;
      case VK_OEM_5: return 0x1C;
      case VK_OEM_6: return 0x1D;
      case VK_RETURN: return '\n';
      case VK_BACK: return 0x7F;
    }
    return 0;
  }
  if (vk >= '0' && vk <= '9') return shift ? L")!@#$%^&*("[vk - '0'] : (WCHAR)vk;
  if (vk >= VK_NUMPAD0 && vk <= VK_NUMPAD9) return (WCHAR)('0' + vk - VK_NUMPAD0);
  switch (vk) {
    case VK_SPACE: return ' ';
    case VK_RETURN: return '\r';
    case VK_BACK: return '\b';
    case VK_TAB: return '\t';
    case VK_ESCAPE: return 0x1B;
    case VK_MULTIPLY: return '*';
    case VK_ADD: return '+';
    case VK_SUBTRACT: return '-';
    case VK_DECIMAL: return '.';
    case VK_DIVIDE: return '/';
    case VK_OEM_1: return shift ? ':' : ';';
    case VK_OEM_PLUS: return shift ? '+' : '=';
    case VK_OEM_COMMA: return shift ? '<' : ',';
    case VK_OEM_MINUS: return shift ? '_' : '-';
    case VK_OEM_PERIOD: return shift ? '>' : '.';
    case VK_OEM_2: return shift ? '?' : '/';
    case VK_OEM_3: return shift ? '~' : '`';
    case VK_OEM_4: return shift ? '{' : '[';
    case VK_OEM_5: return shift ? '|' : '\\';
    case VK_OEM_6: return shift ? '}' : ']';
    case VK_OEM_7: return shift ? '"' : '\'';
  }
  return 0;
}

}  // namespace

// Drops a destroyed window's timers and posted messages.
void ForgetWindowMessages(Window* w) {
  UserGuard g(UserLock());
  Queue* q = QueueFor(w->tid);
  for (size_t i = q->timers.size(); i-- > 0;)
    if (q->timers[i].hwnd == w->hwnd) q->timers.erase(q->timers.begin() + i);
  for (std::deque<MSG>* d : {&q->posted, &q->input})
    for (size_t i = d->size(); i-- > 0;)
      if ((*d)[i].hwnd == w->hwnd) d->erase(d->begin() + i);
}

// Puts input for a window in its thread's queue; a mouse move replaces
// one still waiting.
void QueueInput(Window* w, UINT msg, WPARAM wp, LPARAM lp) {
  UserGuard g(UserLock());
  Queue* q = QueueFor(w->tid);
  if (msg == WM_MOUSEMOVE && !q->input.empty() && q->input.back().message == WM_MOUSEMOVE &&
      q->input.back().hwnd == w->hwnd) {
    q->input.back() = NewMsg(w->hwnd, msg, wp, lp);
  } else {
    q->input.push_back(NewMsg(w->hwnd, msg, wp, lp));
  }
  WakeQueue(q);
}

}  // namespace winport

extern "C" {

// Hooks

HHOOK WINAPI SetWindowsHookExW(int idHook, HOOKPROC proc, HINSTANCE, DWORD tid) {
  if (proc == NULL) {
    SetLastError(ERROR_HOOK_NEEDS_HMOD);
    return NULL;
  }
  UserGuard g(UserLock());
  // A hook's procedure gets the same messages whichever function set it.
  winport::Hooks().push_back(new winport::Hook{idHook, proc, tid ? tid : GetCurrentThreadId(), false});
  return (HHOOK)winport::Hooks().back();
}
HHOOK WINAPI SetWindowsHookExA(int idHook, HOOKPROC proc, HINSTANCE hmod, DWORD tid) {
  return SetWindowsHookExW(idHook, proc, hmod, tid);
}

BOOL WINAPI UnhookWindowsHookEx(HHOOK hh) {
  UserGuard g(UserLock());
  for (winport::Hook* h : winport::Hooks()) {
    if ((HHOOK)h == hh && !h->dead) {
      h->dead = true;
      return TRUE;
    }
  }
  SetLastError(ERROR_INVALID_HOOK_HANDLE);
  return FALSE;
}

LRESULT WINAPI CallNextHookEx(HHOOK hh, int code, WPARAM wParam, LPARAM lParam) {
  winport::Hook* h = (winport::Hook*)hh;
  if (!h) return 0;
  winport::Hook* next;
  HOOKPROC proc = winport::NextHook(h->id, GetCurrentThreadId(), h, &next);
  return proc ? proc(code, wParam, lParam) : 0;
}

BOOL WINAPI CallMsgFilterW(LPMSG msg, int code) {
  if (HaveHooks(WH_SYSMSGFILTER) && CallHooks(WH_SYSMSGFILTER, code, 0, (LPARAM)msg)) return TRUE;
  return HaveHooks(WH_MSGFILTER) && CallHooks(WH_MSGFILTER, code, 0, (LPARAM)msg) != 0;
}
BOOL WINAPI CallMsgFilterA(LPMSG msg, int code) { return CallMsgFilterW(msg, code); }

// Window procedures

LRESULT WINAPI CallWindowProcW(WNDPROC proc, HWND h, UINT msg, WPARAM wp, LPARAM lp) {
  return CallProc(proc, true, h, msg, wp, lp, true);
}
LRESULT WINAPI CallWindowProcA(WNDPROC proc, HWND h, UINT msg, WPARAM wp, LPARAM lp) {
  return CallProc(proc, false, h, msg, wp, lp, false);
}

// Sending

LRESULT WINAPI SendMessageW(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
  bool ok;
  return SendCore(h, msg, wp, lp, true, INFINITE, &ok);
}
LRESULT WINAPI SendMessageA(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
  bool ok;
  return SendCore(h, msg, wp, lp, false, INFINITE, &ok);
}

LRESULT WINAPI SendMessageTimeoutW(HWND h, UINT msg, WPARAM wp, LPARAM lp, UINT, UINT timeout,
                                   PDWORD_PTR result) {
  bool ok;
  LRESULT r = SendCore(h, msg, wp, lp, true, timeout, &ok);
  if (result) *result = (DWORD_PTR)r;
  return ok;
}
LRESULT WINAPI SendMessageTimeoutA(HWND h, UINT msg, WPARAM wp, LPARAM lp, UINT, UINT timeout,
                                   PDWORD_PTR result) {
  bool ok;
  LRESULT r = SendCore(h, msg, wp, lp, false, timeout, &ok);
  if (result) *result = (DWORD_PTR)r;
  return ok;
}

BOOL WINAPI SendNotifyMessageW(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
  return SendNotify(h, msg, wp, lp, true, NULL, 0);
}
BOOL WINAPI SendNotifyMessageA(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
  return SendNotify(h, msg, wp, lp, false, NULL, 0);
}
BOOL WINAPI SendMessageCallbackW(HWND h, UINT msg, WPARAM wp, LPARAM lp, SENDASYNCPROC cb, ULONG_PTR data) {
  return SendNotify(h, msg, wp, lp, true, cb, data);
}
BOOL WINAPI SendMessageCallbackA(HWND h, UINT msg, WPARAM wp, LPARAM lp, SENDASYNCPROC cb, ULONG_PTR data) {
  return SendNotify(h, msg, wp, lp, false, cb, data);
}

BOOL WINAPI InSendMessage(void) { return CurrentQueue()->inSend != NULL; }
DWORD WINAPI InSendMessageEx(LPVOID) {
  SentMessage* sm = CurrentQueue()->inSend;
  if (!sm) return ISMEX_NOSEND;
  return sm->noReply ? ISMEX_NOTIFY : ISMEX_SEND;
}
BOOL WINAPI ReplyMessage(LRESULT r) {
  Queue* q = CurrentQueue();
  SentMessage* sm = q->inSend;
  if (!sm) return FALSE;
  q->inSend = NULL;
  Reply(sm, r);
  return TRUE;
}

// Posting

BOOL WINAPI PostMessageW(HWND h, UINT msg, WPARAM wp, LPARAM lp) { return PostCore(h, msg, wp, lp); }
BOOL WINAPI PostMessageA(HWND h, UINT msg, WPARAM wp, LPARAM lp) { return PostCore(h, msg, wp, lp); }

BOOL WINAPI PostThreadMessageW(DWORD tid, UINT msg, WPARAM wp, LPARAM lp) {
  Queue* q = QueueFor(tid);
  UserGuard g(UserLock());
  q->posted.push_back(NewMsg(NULL, msg, wp, lp));
  WakeQueue(q);
  return TRUE;
}
BOOL WINAPI PostThreadMessageA(DWORD tid, UINT msg, WPARAM wp, LPARAM lp) {
  return PostThreadMessageW(tid, msg, wp, lp);
}

void WINAPI PostQuitMessage(int code) {
  Queue* q = CurrentQueue();
  UserGuard g(UserLock());
  q->quit = true;
  q->quitCode = code;
  WakeQueue(q);
}

// The message loop

BOOL WINAPI GetMessageW(LPMSG m, HWND h, UINT min, UINT max) { return GetCore(m, h, min, max); }
BOOL WINAPI GetMessageA(LPMSG m, HWND h, UINT min, UINT max) { return GetCore(m, h, min, max); }

BOOL WINAPI PeekMessageW(LPMSG m, HWND h, UINT min, UINT max, UINT flags) {
  if (!m) return FALSE;
  return PeekCore(m, h, min, max, flags);
}
BOOL WINAPI PeekMessageA(LPMSG m, HWND h, UINT min, UINT max, UINT flags) {
  return PeekMessageW(m, h, min, max, flags);
}

LRESULT WINAPI DispatchMessageW(const MSG* m) { return DispatchCore(m, true); }
LRESULT WINAPI DispatchMessageA(const MSG* m) { return DispatchCore(m, false); }

BOOL WINAPI TranslateMessage(const MSG* m) {
  if (!m) return FALSE;
  if (m->message != WM_KEYDOWN && m->message != WM_SYSKEYDOWN) {
    return m->message == WM_KEYUP || m->message == WM_SYSKEYUP;
  }
  if (m->lParam & kTranslated) return TRUE;
  Queue* q = CurrentQueue();
  WCHAR ch = KeyChar((UINT)m->wParam, q->keys);
  if (ch) {
    UserGuard g(UserLock());
    q->posted.push_front(NewMsg(m->hwnd, m->message == WM_KEYDOWN ? WM_CHAR : WM_SYSCHAR, ch, m->lParam));
  }
  return TRUE;
}

BOOL WINAPI WaitMessage(void) {
  MSG m;
  if (PeekCore(&m, NULL, 0, 0, PM_NOREMOVE)) return TRUE;
  WaitForWork(CurrentQueue(), INFINITE);
  return TRUE;
}

DWORD WINAPI GetQueueStatus(UINT flags) {
  Queue* q = CurrentQueue();
  UserGuard g(UserLock());
  DWORD s = QueueState(q) & flags;
  return MAKELONG(s, s);
}

BOOL WINAPI GetInputState(void) {
  Queue* q = CurrentQueue();
  UserGuard g(UserLock());
  return !q->input.empty();
}

DWORD WINAPI MsgWaitForMultipleObjectsEx(DWORD n, const HANDLE* handles, DWORD timeout, DWORD mask, DWORD flags) {
  Queue* q = CurrentQueue();
  ULONGLONG start = Now();
  for (;;) {
    if (n) {
      DWORD r = WaitForMultipleObjects(n, handles, (flags & MWMO_WAITALL) != 0, 0);
      if (r != WAIT_TIMEOUT) return r;
    }
    {
      UserGuard g(UserLock());
      if (QueueState(q) & mask) return WAIT_OBJECT_0 + n;
    }
    ULONGLONG spent = Now() - start;
    if (timeout != INFINITE && spent >= timeout) return WAIT_TIMEOUT;
    // Objects have no way to wake a queue, so look at them again soon.
    DWORD wait = timeout == INFINITE ? INFINITE : (DWORD)(timeout - spent);
    if (n && (wait == INFINITE || wait > 10)) wait = 10;
    WaitForWork(q, wait);
  }
}
DWORD WINAPI MsgWaitForMultipleObjects(DWORD n, const HANDLE* handles, BOOL all, DWORD timeout, DWORD mask) {
  return MsgWaitForMultipleObjectsEx(n, handles, timeout, mask, all ? MWMO_WAITALL : 0);
}

LONG WINAPI GetMessageTime(void) { return (LONG)CurrentQueue()->last.time; }
DWORD WINAPI GetMessagePos(void) {
  POINT p = CurrentQueue()->last.pt;
  return MAKELONG((SHORT)p.x, (SHORT)p.y);
}
LPARAM WINAPI GetMessageExtraInfo(void) { return 0; }
BOOL WINAPI IsGUIThread(BOOL) { return TRUE; }

// Timers

UINT_PTR WINAPI SetTimer(HWND h, UINT_PTR id, UINT elapse, TIMERPROC proc) {
  Queue* q;
  if (h) {
    Window* w = GetWindowOrFail(h);
    if (!w) return 0;
    if (w->tid != GetCurrentThreadId()) {
      SetLastError(ERROR_ACCESS_DENIED);
      return 0;
    }
    q = QueueFor(w->tid);
  } else {
    q = CurrentQueue();
  }
  if (elapse < USER_TIMER_MINIMUM) elapse = USER_TIMER_MINIMUM;
  if (elapse > USER_TIMER_MAXIMUM) elapse = USER_TIMER_MAXIMUM;
  UserGuard g(UserLock());
  // A thread timer without a known ID gets a new one.
  if (!h) {
    bool known = false;
    for (const Timer& t : q->timers) known |= !t.hwnd && t.id == id;
    if (!known) id = g_nextTimerId++;
  }
  Timer t = {h, id, elapse, proc, Now() + elapse};
  bool replaced = false;
  for (Timer& old : q->timers) {
    if (old.hwnd == h && old.id == id) {
      old = t;
      replaced = true;
    }
  }
  if (!replaced) q->timers.push_back(t);
  WakeQueue(q);
  return h && id == 0 ? 1 : id;
}

BOOL WINAPI KillTimer(HWND h, UINT_PTR id) {
  Queue* q;
  if (h) {
    Window* w = GetWindowOrFail(h);
    if (!w) return FALSE;
    q = QueueFor(w->tid);
  } else {
    q = CurrentQueue();
  }
  UserGuard g(UserLock());
  for (size_t i = 0; i < q->timers.size(); i++) {
    if (q->timers[i].hwnd == h && q->timers[i].id == id) {
      q->timers.erase(q->timers.begin() + i);
      return TRUE;
    }
  }
  SetLastError(ERROR_INVALID_PARAMETER);
  return FALSE;
}

}  // extern "C"
