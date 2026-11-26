// saver_host.cpp - runs the screen saver of a Windows program inside a
// host that owns the process and its event loop (see saver_host.h).
//
// The host calls the screen saver for each frame of its animation, and
// the program's WinMain has its own message loop that does not return.
// So WinMain runs in a coroutine: a second stack on the main thread. When
// the message loop waits for a message, the Cocoa backend calls Yield,
// which goes back to the host. The next frame goes on from that point.
// All the code stays on the main thread, as AppKit and the window manager
// require.
//
// The coroutine switch saves the registers that a called function must
// keep: the general and floating-point registers. It does not save the
// AltiVec registers of PowerPC: the layer is compiled without AltiVec,
// and the functions that it calls keep v20 to v31 themselves, so those
// registers do not change between two switches.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#include "saver_host.h"

#if defined(__APPLE__) && !defined(macintosh)

#include "cocoa.h"
#include "winport.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>

extern "C" int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int);

// Saves the registers of the running stack, stores its stack pointer in
// *from, and goes on with the stack whose stack pointer is to.
extern "C" void winport_coro_switch(void** from, void* to);
// The first code of a new coroutine: calls the function and argument
// that winport_coro_switch restored into two saved registers.
extern "C" void winport_coro_start(void);

#if defined(__ppc__)
// Darwin PowerPC: the frame is 464 bytes. r13 to r31 at 32, f14 to f31
// at 112, the condition register and the link register in the caller's
// linkage area (4 and 8 bytes above the frame).
asm(".text\n"
    ".align 2\n"
    ".private_extern _winport_coro_switch\n"
    "_winport_coro_switch:\n"
    "  mflr 0\n"
    "  stw 0, 8(1)\n"
    "  mfcr 0\n"
    "  stw 0, 4(1)\n"
    "  stwu 1, -464(1)\n"
    "  stmw 13, 32(1)\n"
    "  stfd 14, 112(1)\n  stfd 15, 120(1)\n  stfd 16, 128(1)\n  stfd 17, 136(1)\n"
    "  stfd 18, 144(1)\n  stfd 19, 152(1)\n  stfd 20, 160(1)\n  stfd 21, 168(1)\n"
    "  stfd 22, 176(1)\n  stfd 23, 184(1)\n  stfd 24, 192(1)\n  stfd 25, 200(1)\n"
    "  stfd 26, 208(1)\n  stfd 27, 216(1)\n  stfd 28, 224(1)\n  stfd 29, 232(1)\n"
    "  stfd 30, 240(1)\n  stfd 31, 248(1)\n"
    "  stw 1, 0(3)\n"
    "  mr 1, 4\n"
    "  lfd 14, 112(1)\n  lfd 15, 120(1)\n  lfd 16, 128(1)\n  lfd 17, 136(1)\n"
    "  lfd 18, 144(1)\n  lfd 19, 152(1)\n  lfd 20, 160(1)\n  lfd 21, 168(1)\n"
    "  lfd 22, 176(1)\n  lfd 23, 184(1)\n  lfd 24, 192(1)\n  lfd 25, 200(1)\n"
    "  lfd 26, 208(1)\n  lfd 27, 216(1)\n  lfd 28, 224(1)\n  lfd 29, 232(1)\n"
    "  lfd 30, 240(1)\n  lfd 31, 248(1)\n"
    "  lmw 13, 32(1)\n"
    "  addi 1, 1, 464\n"
    "  lwz 0, 4(1)\n"
    "  mtcrf 0x38, 0\n"
    "  lwz 0, 8(1)\n"
    "  mtlr 0\n"
    "  blr\n"
    ".private_extern _winport_coro_start\n"
    "_winport_coro_start:\n"
    "  mr 3, 15\n"
    "  mtctr 14\n"
    "  bctrl\n"
    "  trap\n");
enum { kFrame = 464, kAbove = 64 };
#elif defined(__i386__)
asm(".text\n"
    ".align 4\n"
    ".private_extern _winport_coro_switch\n"
    "_winport_coro_switch:\n"
    "  movl 4(%esp), %eax\n"
    "  movl 8(%esp), %edx\n"
    "  pushl %ebp\n  pushl %ebx\n  pushl %esi\n  pushl %edi\n"
    "  movl %esp, (%eax)\n"
    "  movl %edx, %esp\n"
    "  popl %edi\n  popl %esi\n  popl %ebx\n  popl %ebp\n"
    "  ret\n"
    ".private_extern _winport_coro_start\n"
    "_winport_coro_start:\n"
    "  andl $-16, %esp\n"
    "  subl $12, %esp\n"
    "  pushl %esi\n"
    "  call *%ebx\n"
    "  ud2\n");
#elif defined(__x86_64__)
asm(".text\n"
    ".align 4\n"
    ".private_extern _winport_coro_switch\n"
    "_winport_coro_switch:\n"
    "  pushq %rbp\n  pushq %rbx\n  pushq %r12\n  pushq %r13\n  pushq %r14\n  pushq %r15\n"
    "  movq %rsp, (%rdi)\n"
    "  movq %rsi, %rsp\n"
    "  popq %r15\n  popq %r14\n  popq %r13\n  popq %r12\n  popq %rbx\n  popq %rbp\n"
    "  ret\n"
    ".private_extern _winport_coro_start\n"
    "_winport_coro_start:\n"
    "  movq %r13, %rdi\n"
    "  andq $-16, %rsp\n"
    "  callq *%r12\n"
    "  ud2\n");
#elif defined(__aarch64__)
asm(".text\n"
    ".p2align 2\n"
    ".private_extern _winport_coro_switch\n"
    "_winport_coro_switch:\n"
    "  sub sp, sp, #176\n"
    "  stp x19, x20, [sp, #0]\n  stp x21, x22, [sp, #16]\n  stp x23, x24, [sp, #32]\n"
    "  stp x25, x26, [sp, #48]\n  stp x27, x28, [sp, #64]\n  stp x29, x30, [sp, #80]\n"
    "  stp d8, d9, [sp, #96]\n  stp d10, d11, [sp, #112]\n  stp d12, d13, [sp, #128]\n"
    "  stp d14, d15, [sp, #144]\n"
    "  mov x9, sp\n"
    "  str x9, [x0]\n"
    "  mov sp, x1\n"
    "  ldp x19, x20, [sp, #0]\n  ldp x21, x22, [sp, #16]\n  ldp x23, x24, [sp, #32]\n"
    "  ldp x25, x26, [sp, #48]\n  ldp x27, x28, [sp, #64]\n  ldp x29, x30, [sp, #80]\n"
    "  ldp d8, d9, [sp, #96]\n  ldp d10, d11, [sp, #112]\n  ldp d12, d13, [sp, #128]\n"
    "  ldp d14, d15, [sp, #144]\n"
    "  add sp, sp, #176\n"
    "  ret\n"
    ".private_extern _winport_coro_start\n"
    "_winport_coro_start:\n"
    "  mov x0, x20\n"
    "  blr x19\n"
    "  brk #1\n");
#else
#error "no coroutine switch for this processor"
#endif

namespace {

const size_t kStackSize = 4 << 20;  // OpenGL and the savers' recursion need room

char* g_stack;         // the coroutine's stack, with a guard page at the bottom
void* g_hostSp;        // the host's stack pointer, while the coroutine runs
void* g_coroSp;        // the coroutine's stack pointer, while the host runs
bool g_inCoroutine;    // the coroutine runs now
bool g_started;        // WinMain was called
bool g_finished;       // WinMain returned (or ExitProcess was called)
DWORD g_resumed;       // GetTickCount when the host last gave control
HWND g_hostWnd;        // the window that fills the host view
std::string g_command;

// Writes a line to the standard error when WINPORT_SAVER_DEBUG is set.
void Trace(const char* what) {
  static int on = -1;
  if (on < 0) on = getenv("WINPORT_SAVER_DEBUG") != NULL;
  if (on) fprintf(stderr, "saver: %s\n", what);
}

// Gives control back to the host, from the coroutine.
void SwitchToHost() {
  g_inCoroutine = false;
  winport_coro_switch(&g_coroSp, g_hostSp);
  g_inCoroutine = true;
}

// The Cocoa backend's Wait in a host: a wait for a message goes back to
// the host until its next frame. A poll (timeout 0) goes back only when
// the screen saver has run for 30 ms, so that a loop that only peeks for
// messages does not stop the host.
void Yield(long timeout) {
  if (!g_inCoroutine) return;
  if (timeout == 0 && GetTickCount() - g_resumed < 30) return;
  SwitchToHost();
}

// ExitProcess in the screen saver: the process is the host's. The
// coroutine stops here and is never continued.
void Finish(UINT) {
  g_finished = true;
  for (;;) SwitchToHost();
}

// The body of the coroutine: the program's WinMain.
void Body(void*) {
  Trace("coroutine runs");
  _WinportSetExitHook(Finish);
  _WinportSetCommandLine(g_command.c_str());
  LPSTR args = (LPSTR)strchr(g_command.c_str(), ' ');
  Trace(g_command.c_str());
  UINT code = (UINT)WinMain(GetModuleHandleA(NULL), NULL, args ? args + 1 : (LPSTR)"", SW_SHOW);
  Finish(code);
}

// Makes the coroutine's stack: a frame that winport_coro_switch restores
// into winport_coro_start, with Body and its argument in two of the saved
// registers.
void* NewStack(void (*fn)(void*), void* arg) {
  if (!g_stack) {
    void* p = mmap(NULL, kStackSize, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
    if (p == MAP_FAILED) return NULL;
    g_stack = (char*)p;
    mprotect(g_stack, 4096, PROT_NONE);
  }
  uintptr_t top = ((uintptr_t)g_stack + kStackSize) & ~(uintptr_t)15;
#if defined(__ppc__)
  // r14 and r15 are the 2nd and 3rd words of the saved registers (at 32);
  // the link register is 8 bytes above the frame.
  uint32_t* sp = (uint32_t*)(top - kAbove - kFrame);
  memset(sp, 0, kAbove + kFrame);
  sp[(32 + 4) / 4] = (uint32_t)(uintptr_t)fn;
  sp[(32 + 8) / 4] = (uint32_t)(uintptr_t)arg;
  sp[(kFrame + 8) / 4] = (uint32_t)(uintptr_t)winport_coro_start;
  return sp;
#elif defined(__i386__)
  // edi, esi (argument), ebx (function), ebp, then the return address.
  uint32_t* sp = (uint32_t*)(top - 64);
  memset(sp, 0, 64);
  sp[1] = (uint32_t)(uintptr_t)arg;
  sp[2] = (uint32_t)(uintptr_t)fn;
  sp[4] = (uint32_t)(uintptr_t)winport_coro_start;
  return sp;
#elif defined(__x86_64__)
  // r15, r14, r13 (argument), r12 (function), rbx, rbp, the return address.
  uint64_t* sp = (uint64_t*)(top - 128);
  memset(sp, 0, 128);
  sp[2] = (uint64_t)(uintptr_t)arg;
  sp[3] = (uint64_t)(uintptr_t)fn;
  sp[6] = (uint64_t)(uintptr_t)winport_coro_start;
  return sp;
#elif defined(__aarch64__)
  // x19 (function), x20 (argument), ..., x29, x30 (the return address).
  uint64_t* sp = (uint64_t*)(top - 256);
  memset(sp, 0, 256);
  sp[0] = (uint64_t)(uintptr_t)fn;
  sp[1] = (uint64_t)(uintptr_t)arg;
  sp[11] = (uint64_t)(uintptr_t)winport_coro_start;
  return sp;
#endif
}

// Runs the coroutine until it gives control back.
void Resume() {
  if (!g_started || g_finished || g_inCoroutine) return;
  g_resumed = GetTickCount();
  g_inCoroutine = true;
  winport_coro_switch(&g_hostSp, g_coroSp);
  g_inCoroutine = false;
}

// The window procedure of the window that fills the host view.
LRESULT CALLBACK HostProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
  if (msg == WM_ERASEBKGND) {
    RECT r;
    GetClientRect(h, &r);
    FillRect((HDC)wp, &r, (HBRUSH)GetStockObject(BLACK_BRUSH));
    return 1;
  }
  return DefWindowProcA(h, msg, wp, lp);
}

// Gives the host window, and the screen saver's windows in it, the size
// of the view.
void Resize(int width, int height) {
  if (!g_hostWnd) return;
  SetWindowPos(g_hostWnd, NULL, 0, 0, width, height, SWP_NOZORDER | SWP_NOACTIVATE);
  for (HWND c = GetWindow(g_hostWnd, GW_CHILD); c; c = GetWindow(c, GW_HWNDNEXT))
    SetWindowPos(c, NULL, 0, 0, width, height, SWP_NOZORDER | SWP_NOACTIVATE);
}

}  // namespace

extern "C" {

// WinportSaverStart: makes the host window and starts WinMain in the
// coroutine (see saver_host.h).
int WinportSaverStart(void* view, int width, int height, const char* program) {
  if (g_started) {
    WinportSaverMove(view, width, height);
    return !g_finished;
  }
  Trace("start");
  // The bundle is the program: its resources, and its file.
  HMODULE self = NULL;
  if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                         (LPCSTR)(void*)WinportSaverStart, &self))
    _WinportSetMainModule(self);
  winport::cocoa::SetHost(view, Yield);
  WNDCLASSA wc;
  memset(&wc, 0, sizeof wc);
  wc.lpfnWndProc = HostProc;
  wc.hInstance = GetModuleHandleA(NULL);
  wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
  wc.lpszClassName = "WinportSaverHost";
  RegisterClassA(&wc);
  g_hostWnd = CreateWindowExA(0, wc.lpszClassName, "", WS_POPUP | WS_CLIPCHILDREN, 0, 0, width, height, NULL, NULL,
                              wc.hInstance, NULL);
  if (!g_hostWnd) return 0;
  Trace("host window made");
  ShowWindow(g_hostWnd, SW_SHOWNA);
  char line[64];
  snprintf(line, sizeof line, " /p %lu", (unsigned long)(uintptr_t)g_hostWnd);
  g_command = std::string(program && *program ? program : "saver.scr") + line;
  g_coroSp = NewStack(Body, NULL);
  if (!g_coroSp) return 0;
  g_started = true;
  Trace("switch to the coroutine");
  Resume();
  Trace("back from the coroutine");
  return !g_finished;
}

// WinportSaverFrame: runs the screen saver until it waits.
void WinportSaverFrame(void) { Resume(); }

// WinportSaverMove: puts the screen saver in a view at its size.
void WinportSaverMove(void* view, int width, int height) {
  winport::cocoa::MoveHosted(view);
  Resize(width, height);
}

// WinportSaverDetach: takes the screen saver out of its view.
void WinportSaverDetach(void) { winport::cocoa::MoveHosted(NULL); }

// WinportSaverRunning: whether WinMain runs.
int WinportSaverRunning(void) { return g_started && !g_finished; }

}  // extern "C"

#endif
