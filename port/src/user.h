// user.h - the window manager's internals: window classes, windows, the
// threads' message queues, and the backends that show top-level windows.
//
// One lock guards all of it; it is released while a window procedure or
// hook runs. Every top-level window has a surface, an image of the whole
// window; its child windows draw into the same surface. A backend (X11,
// Wayland, Cocoa, or none at all) shows the surfaces and brings in input.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#ifndef _WINPORT_USER_H
#define _WINPORT_USER_H

#include "winport.h"
#include "region.h"

#include <condition_variable>
#include <deque>
#include <map>
#include <mutex>
#include <string>
#include <vector>

namespace winport {

// The window manager's lock. Window procedures and hooks run without it.
std::recursive_mutex& UserLock();
typedef std::unique_lock<std::recursive_mutex> UserGuard;

// A top-level window's image: 32-bit pixels, 0x00RRGGBB, top row first.
struct Surface {
  int width = 0, height = 0;
  std::vector<DWORD> pixels;
  void Resize(int w, int h);
  DWORD* Row(int y) { return &pixels[(size_t)y * width]; }
};

struct Class {
  ATOM atom = 0;
  std::wstring name;
  WNDCLASSEXW wc;  // lpszClassName and lpszMenuName point at the strings below
  std::wstring menuName;
  bool unicode = true;  // registered with the W function
  bool global = false;  // CS_GLOBALCLASS, or a system class
  std::vector<BYTE> extra;
};

struct ScrollBar {
  SCROLLINFO info = {sizeof(SCROLLINFO), 0, 0, 100, 0, 0, 0};
  bool enabled = true;
};

struct Window {
  HWND hwnd = NULL;
  Class* cls = NULL;
  WNDPROC proc = NULL;
  bool unicode = true;  // the procedure takes W messages
  bool ansiCreate = false;  // made with CreateWindowExA: an edit control's buffer is ANSI
  DWORD style = 0, exStyle = 0;
  Window* parent = NULL;  // NULL for a top-level window
  Window* owner = NULL;
  std::vector<Window*> children;  // top of the z-order first
  RECT rect = {0, 0, 0, 0};       // in the parent's client area (the screen for a top-level)
  RECT client = {0, 0, 0, 0};     // the client area, from the window's top left
  HMENU menu = NULL;              // a child window's ID, or a top-level's menu bar
  HMENU sysMenu = NULL;           // GetSystemMenu's copy for the window
  std::wstring text;
  HINSTANCE instance = NULL;
  std::vector<BYTE> extra;
  LONG_PTR userData = 0;
  std::vector<std::pair<std::wstring, HANDLE>> props;  // an atom's name is "#123"
  DWORD tid = 0;
  // Painting: what needs drawing, in client coordinates; whether its
  // background needs erasing and its frame drawing.
  Region update;
  bool erase = false, ncPaint = false, internalPaint = false;
  bool created = false;     // WM_CREATE returned
  bool destroying = false;  // DestroyWindow under way
  HWND lastActive = NULL;   // GetLastActivePopup
  ScrollBar hscroll, vscroll;
  DWORD contextHelpId = 0;
  HICON iconBig = NULL, iconSmall = NULL;
  // A top-level window's image and the backend's own data for it.
  Surface* surface = NULL;
  void* native = NULL;
  RECT normalRect = {0, 0, 0, 0};  // where the window goes when restored
  HDC ownDC = NULL;                // a CS_OWNDC window's DC
  bool ncActive = false;           // an MDI child's caption is drawn active

  bool Visible() const { return (style & WS_VISIBLE) != 0; }
  bool Child() const { return (style & WS_CHILD) != 0 && parent != NULL; }
  Window* TopLevel() {
    Window* w = this;
    while (w->parent) w = w->parent;
    return w;
  }
};

// Windows by handle; NULL when the handle is not a window.
Window* GetWindow(HWND h);
Window* GetWindowOrFail(HWND h);  // sets ERROR_INVALID_WINDOW_HANDLE
std::vector<Window*>& TopLevels();  // z-order, top first

// Geometry: a window's rectangle and client area in screen coordinates,
// and the offset of its top left within its top-level window's surface.
RECT ScreenRect(Window* w);
RECT ScreenClient(Window* w);
POINT SurfaceOrigin(Window* w);
// Whether the window and its parents are all visible.
bool ReallyVisible(Window* w);
// The part of the surface the window may draw in: client is false for
// the whole window. In surface coordinates.
Region VisibleRegion(Window* w, bool client);

// Classes
Class* FindClass(const wchar_t* name, HINSTANCE inst);
Class* FindClassAtom(ATOM atom);
void RegisterSystemClasses();
ATOM RegisterSystemClass(LPCWSTR name, WNDPROC proc, UINT style, int extra, HBRUSH brush, LPCWSTR cursor);

// Messages
struct SentMessage {
  HWND hwnd;
  UINT msg;
  WPARAM wParam;
  LPARAM lParam;
  bool unicode;
  LRESULT result = 0;
  bool done = false;
  bool noReply = false;  // SendNotifyMessage: the sender does not wait
  struct Queue* from = NULL;  // the sender's queue, woken on the reply
  SENDASYNCPROC callback = NULL;  // SendMessageCallback
  ULONG_PTR callbackData = 0;
};

struct Timer {
  HWND hwnd;
  UINT_PTR id;
  UINT elapse;
  TIMERPROC proc;
  ULONGLONG due;
};

struct Queue {
  DWORD tid = 0;
  std::deque<MSG> posted;
  std::deque<MSG> input;
  std::deque<SentMessage*> sent;
  std::vector<Timer> timers;
  bool quit = false;
  int quitCode = 0;
  SentMessage* inSend = NULL;  // the sent message being handled
  std::condition_variable_any wake;
  MSG last = {};  // GetMessagePos, GetMessageTime
  HWND focus = NULL, active = NULL, capture = NULL;
  BYTE keys[256] = {};
  int utf8Pending = 0;  // WM_CHAR to an ANSI procedure: bytes still to come
  DWORD utf8Char = 0;   // and the character so far
  std::deque<SentMessage*> callbacks;  // SendMessageCallback replies to deliver
};
Queue* QueueFor(DWORD tid);  // created on first use
Queue* CurrentQueue();
// Puts input for a window in its thread's queue.
void QueueInput(Window* w, UINT msg, WPARAM wParam, LPARAM lParam);
// Drops a destroyed window's timers and waiting messages.
void ForgetWindowMessages(Window* w);
void WakeQueue(Queue* q);  // call with the lock held

// Calls a window procedure with the message in the form the procedure
// wants, converting text between ANSI and Unicode.
LRESULT CallProc(WNDPROC proc, bool procUnicode, HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam,
                 bool msgUnicode);
LRESULT SendTo(Window* w, UINT msg, WPARAM wParam, LPARAM lParam, bool unicode = true);
// A window procedure handed to a caller of the other character set is a
// handle that CallWindowProc knows, as on Windows. ProcHandle makes one
// when needed; ResolveProc turns one back into the procedure and its set.
WNDPROC ProcHandle(WNDPROC proc, bool procUnicode, bool callerUnicode);
WNDPROC ResolveProc(WNDPROC proc, bool* unicode);
// Calls the hooks of a kind on the current thread; returns the first
// hook's result, or 0 when there are none.
LRESULT CallHooks(int idHook, int code, WPARAM wParam, LPARAM lParam);
bool HaveHooks(int idHook);

// Painting
void InvalidateWindow(Window* w, const Region* rgn, UINT flags);
// A visible window of the thread that needs WM_PAINT, within filter
// (NULL for any); NULL when there is none.
Window* WindowToPaint(DWORD tid, HWND filter);
void SurfaceChanged(Window* top, const RECT& r);  // tells the backend
HDC NewWindowDC(Window* w, bool client, const Region* clip);
void FreeWindowDC(HDC dc);

// Frame metrics: the non-client border widths for a style.
RECT FrameInsets(DWORD style, DWORD exStyle, bool menu);
void DrawFrame(Window* w);
// Where the menu bar and the system menu's box lie, in window coordinates.
RECT MenuBarRect(Window* w);
RECT SysMenuRect(Window* w);
// Scroll bars, for windows' own and the ScrollBar control: drawing one
// at r, and a click on one at r (in w's window coordinates), which sends
// WM_HSCROLL or WM_VSCROLL to notify with ctl as lParam.
void PaintScrollBar(HDC dc, const RECT& r, bool vertical, const ScrollBar* sb);
void ClickScrollBar(Window* w, ScrollBar* sb, RECT r, bool vertical, POINT screen, HWND notify, HWND ctl);

// Menus (user_menu.cpp): the menu window's class, drawing the menu bar,
// SC_KEYMENU and SC_MOUSEMENU, and a window's menus when it goes.
void RegisterMenuClass();
void RegisterDialogClass();  // user_dialog.cpp
size_t DialogTemplateSize(const void* tmpl);  // the bytes a dialog template takes
void RegisterButtonClasses();  // user_button.cpp: Button, Static, ScrollBar
void RegisterEditClass();  // user_edit.cpp
void RegisterListClasses();  // user_listbox.cpp: ListBox, ComboLBox, ComboBox
void RegisterMdiClass();  // user_mdi.cpp: MDIClient
void RegisterCommonControls();  // comctl_misc.cpp: the common control classes
void DrawMenuBarIn(Window* w, HDC dc, const RECT& bar);
void MenuCommand(Window* w, WPARAM wParam, LPARAM lParam);
void MenusOnDestroy(Window* w);

// Focus and activation
void ActivateWindow(Window* w, bool byMouse);
void WindowDestroyed(Window* w);

// Input from a backend: a position in the top-level window's surface.
void MouseInput(Window* top, UINT msg, int x, int y, WPARAM keys);
void KeyInput(Window* top, UINT msg, UINT vk, LPARAM lParam);
void CharInput(Window* top, wchar_t ch);
void CloseRequested(Window* top);
void ResizeRequested(Window* top, int width, int height);

// A cursor's picture for a backend: ARGB pixels, not premultiplied, top
// row first, and its IDC_ number when it is one of the system's.
struct CursorPicture {
  int systemId = 0;
  POINT hotspot = {0, 0};
  int width = 0, height = 0;
  std::vector<DWORD> argb;
};
bool CursorImage(HCURSOR c, CursorPicture* out);

// The backend: how top-level windows are shown.
struct Backend {
  virtual ~Backend() {}
  virtual const char* Name() = 0;
  virtual SIZE ScreenSize() = 0;
  virtual void Show(Window* /*w*/, bool /*show*/) {}
  virtual void Destroy(Window* /*w*/) {}
  virtual void Moved(Window* /*w*/) {}  // position or size
  virtual void TitleChanged(Window* /*w*/) {}
  virtual void Present(Window* /*w*/, const RECT& /*r*/) {}
  // Moving or sizing a top-level window with the mouse, for displays
  // where only the compositor can; hit is HTCAPTION or a frame edge.
  virtual bool BeginMoveSize(Window* /*w*/, LRESULT /*hit*/) { return false; }
  // The pointer's shape over the program's windows; NULL hides it.
  virtual void SetCursor(HCURSOR /*c*/) {}
  // Waits up to timeout ms for input, handing what comes to the input
  // functions above; returns early when WakeBackend is called. Called
  // without the lock.
  virtual void Wait(DWORD timeout) = 0;
  virtual void Wake() = 0;
};
Backend* GetBackend();
// The mouse position on the screen.
POINT& CursorPos();
Backend* NewHeadlessBackend();
Backend* NewX11Backend();  // NULL when there is no X display
Backend* NewWaylandBackend();  // NULL when there is no Wayland compositor
Backend* NewCocoaBackend();  // NULL except on macOS

}  // namespace winport

#endif
