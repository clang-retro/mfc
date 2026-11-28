// backend_haiku_be.cpp - the Haiku API half of the Haiku backend, which
// shows top-level windows on Haiku (see haiku.h and backend_haiku.cpp).
//
// Each top-level window is a BWindow with no border and one BView that
// shows a BBitmap: a copy of the window's pixels. The window manager
// draws the frame and the caption in those pixels, as it does on X11 and
// Wayland. The BWindow threads do not touch the window manager. They put
// the input in a queue, and the thread that waits for input (Take) takes
// it from there. The BApplication runs on a thread of its own.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#if defined(__HAIKU__)

#include "haiku.h"

#include <Application.h>
#include <Bitmap.h>
#include <Clipboard.h>
#include <Cursor.h>
#include <Message.h>
#include <OS.h>
#include <Screen.h>
#include <View.h>
#include <Window.h>
#include <image.h>

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#include <chrono>
#include <condition_variable>
#include <deque>
#include <mutex>

namespace winport {
namespace haiku {

namespace {

// The message that asks a window to show the part of its bitmap that
// Update changed.
const uint32 kShowDirty = 'wpDy';

std::mutex g_queueLock;
std::condition_variable g_queueReady;
std::deque<Event> g_events;
bool g_woken = false;
bool g_exiting = false;  // the program is ending: windows may close
thread_id g_appThread = -1;

// Puts one input event in the queue and wakes the waiting thread.
void Post(const Event& e) {
  std::lock_guard<std::mutex> g(g_queueLock);
  g_events.push_back(e);
  g_queueReady.notify_all();
}

// The k flags of haiku.h for Haiku's modifier flags.
unsigned ModsOf(uint32 m) {
  unsigned r = 0;
  if (m & B_SHIFT_KEY) r |= kShift;
  if (m & B_CONTROL_KEY) r |= kControl;
  if (m & B_COMMAND_KEY) r |= kAlt;
  if (m & B_OPTION_KEY) r |= kWin;
  if (m & B_CAPS_LOCK) r |= kCapsLock;
  return r;
}

// The first character of a UTF-8 string, or 0.
uint32_t FirstChar(const char* s) {
  if (!s || !*s) return 0;
  const unsigned char* p = (const unsigned char*)s;
  if (p[0] < 0x80) return p[0];
  int n = p[0] >= 0xF0 ? 3 : p[0] >= 0xE0 ? 2 : 1;
  uint32_t c = p[0] & (0x3F >> n);
  for (int i = 1; i <= n; i++) {
    if ((p[i] & 0xC0) != 0x80) return 0;
    c = (c << 6) | (p[i] & 0x3F);
  }
  return c;
}

// The Haiku key codes of the modifier keys, by the modifier flag that
// each one sets, for B_MODIFIERS_CHANGED.
struct ModifierKey {
  uint32 flag;
  unsigned key;
};
const ModifierKey kModifierKeys[] = {
    {B_LEFT_SHIFT_KEY, 0x4b},   {B_RIGHT_SHIFT_KEY, 0x56},   {B_LEFT_CONTROL_KEY, 0x5c},
    {B_RIGHT_CONTROL_KEY, 0x60}, {B_LEFT_COMMAND_KEY, 0x5d},  {B_RIGHT_COMMAND_KEY, 0x5f},
    {B_LEFT_OPTION_KEY, 0x66},  {B_RIGHT_OPTION_KEY, 0x67},
};

// Tells if a key code is one of the modifier keys or Caps Lock.
bool IsModifierKey(unsigned key) {
  if (key == 0x3b) return true;
  for (const ModifierKey& m : kModifierKeys)
    if (m.key == key) return true;
  return false;
}

class PixelView;

}  // namespace

// A window: the BWindow, its view, and the bitmap the view shows. The
// lock keeps the bitmap and the dirty rectangle.
struct Win {
  void* owner = NULL;
  BWindow* window = NULL;
  PixelView* view = NULL;
  std::mutex lock;
  BBitmap* bitmap = NULL;
  BRect dirty;
  bool dirtyPosted = false;
  bool shown = false;
  uint32 buttons = 0;  // the mouse buttons down, as Haiku gives them
};

namespace {

// The view that fills the window: it draws the bitmap and gives the
// mouse to the queue.
class PixelView : public BView {
 public:
  // Makes the view for a window, as big as the window.
  PixelView(BRect frame, Win* w) : BView(frame, "winport", B_FOLLOW_ALL, B_WILL_DRAW), win(w) {
    SetViewColor(B_TRANSPARENT_COLOR);
  }

  // Draws the part of the bitmap that the update rectangle covers.
  void Draw(BRect r) override {
    std::lock_guard<std::mutex> g(win->lock);
    if (win->bitmap) DrawBitmap(win->bitmap, r, r);
  }

  // Sends a mouse event with the current message's modifiers.
  void Send(Event::Kind kind, BPoint where, int button) {
    Event e;
    e.kind = kind;
    e.owner = win->owner;
    e.x = (int)where.x;
    e.y = (int)where.y;
    e.button = button;
    e.mods = ModsOf(modifiers());
    Post(e);
  }

  // Gives the buttons that were pressed. The view keeps the mouse until
  // all of them are let go, also outside the window.
  void MouseDown(BPoint where) override {
    int32 buttons = 0;
    if (BMessage* m = Window()->CurrentMessage()) m->FindInt32("buttons", &buttons);
    if (!buttons) buttons = B_PRIMARY_MOUSE_BUTTON;
    SetMouseEventMask(B_POINTER_EVENTS, B_NO_POINTER_HISTORY);
    Buttons(where, (uint32)buttons);
  }

  // Gives the buttons that were let go.
  void MouseUp(BPoint where) override {
    int32 buttons = 0;
    if (BMessage* m = Window()->CurrentMessage()) m->FindInt32("buttons", &buttons);
    Buttons(where, (uint32)buttons);
  }

  // Gives a mouse move, also when it leaves the window.
  void MouseMoved(BPoint where, uint32, const BMessage*) override { Send(Event::kMouseMove, where, 0); }

  // Compares the buttons now down with those before, and sends a press
  // or a release for each one that changed.
  void Buttons(BPoint where, uint32 now) {
    static const uint32 kFlags[3] = {B_PRIMARY_MOUSE_BUTTON, B_SECONDARY_MOUSE_BUTTON, B_TERTIARY_MOUSE_BUTTON};
    for (int i = 0; i < 3; i++) {
      bool was = (win->buttons & kFlags[i]) != 0, is = (now & kFlags[i]) != 0;
      if (was != is) Send(is ? Event::kMouseDown : Event::kMouseUp, where, i);
    }
    win->buttons = now;
  }

  Win* win;
};

// A window: it sends the keys, the wheel, activation and closing to the
// queue, and shows the bitmap when Update asks it to.
class PixelWindow : public BWindow {
 public:
  // Makes a window with no border: floating and without the focus for a
  // popup, an ordinary one otherwise.
  PixelWindow(BRect frame, Win* w, bool popup)
      : BWindow(frame, "", B_NO_BORDER_WINDOW_LOOK, popup ? B_FLOATING_ALL_WINDOW_FEEL : B_NORMAL_WINDOW_FEEL,
                B_WILL_ACCEPT_FIRST_CLICK | B_NO_SERVER_SIDE_WINDOW_MODIFIERS | B_ASYNCHRONOUS_CONTROLS |
                    (popup ? B_AVOID_FOCUS : 0)),
        win(w) {}

  // Takes the keys before BWindow can use them as shortcuts, and the
  // wheel; gives the other messages to BWindow.
  void DispatchMessage(BMessage* m, BHandler* target) override {
    switch (m->what) {
      case B_KEY_DOWN:
      case B_UNMAPPED_KEY_DOWN:
      case B_KEY_UP:
      case B_UNMAPPED_KEY_UP: {
        int32 key = 0, mods = 0, raw = 0;
        m->FindInt32("key", &key);
        m->FindInt32("modifiers", &mods);
        m->FindInt32("raw_char", &raw);
        // The modifier keys come with B_MODIFIERS_CHANGED.
        if (IsModifierKey((unsigned)key)) return;
        Event e;
        e.kind = (m->what == B_KEY_DOWN || m->what == B_UNMAPPED_KEY_DOWN) ? Event::kKeyDown : Event::kKeyUp;
        e.owner = win->owner;
        e.key = (unsigned)key;
        e.mods = ModsOf((uint32)mods);
        const char* bytes = NULL;
        if (m->what == B_KEY_DOWN && m->FindString("bytes", &bytes) == B_OK) e.ch = FirstChar(bytes);
        e.raw = (uint32_t)raw & 0xFF;
        Post(e);
        return;
      }
      case B_MODIFIERS_CHANGED: {
        int32 now = 0, old = 0;
        m->FindInt32("modifiers", &now);
        m->FindInt32("be:old_modifiers", &old);
        for (const ModifierKey& k : kModifierKeys) {
          bool was = (old & k.flag) != 0, is = (now & k.flag) != 0;
          if (was == is) continue;
          Event e;
          e.kind = is ? Event::kKeyDown : Event::kKeyUp;
          e.owner = win->owner;
          e.key = k.key;
          e.mods = ModsOf((uint32)now);
          Post(e);
        }
        if ((old ^ now) & B_CAPS_LOCK) {
          // Caps Lock is pressed and let go at once, as its state says.
          Event e;
          e.kind = Event::kKeyDown;
          e.owner = win->owner;
          e.key = 0x3b;
          e.mods = ModsOf((uint32)now);
          Post(e);
          e.kind = Event::kKeyUp;
          Post(e);
        }
        return;
      }
      case B_MOUSE_WHEEL_CHANGED: {
        float dx = 0, dy = 0;
        m->FindFloat("be:wheel_delta_x", &dx);
        m->FindFloat("be:wheel_delta_y", &dy);
        BPoint where;
        uint32 buttons;
        win->view->GetMouse(&where, &buttons, false);
        Event e;
        e.kind = Event::kWheel;
        e.owner = win->owner;
        e.x = (int)where.x;
        e.y = (int)where.y;
        e.wheelX = dx;
        e.wheelY = dy;
        e.mods = ModsOf(modifiers());
        Post(e);
        return;
      }
    }
    BWindow::DispatchMessage(m, target);
  }

  // Shows the part of the bitmap that changed since the last time.
  void MessageReceived(BMessage* m) override {
    if (m->what == kShowDirty) {
      BRect r;
      {
        std::lock_guard<std::mutex> g(win->lock);
        r = win->dirty;
        win->dirtyPosted = false;
        win->dirty = BRect();
      }
      if (r.IsValid()) win->view->Invalidate(r);
      return;
    }
    BWindow::MessageReceived(m);
  }

  // Tells the window manager that the window became active or not.
  void WindowActivated(bool active) override {
    Event e;
    e.kind = Event::kActivated;
    e.owner = win->owner;
    e.active = active;
    Post(e);
  }

  // Asks the window manager to close the window (Alt+F4 does the same
  // on Windows); the window stays until the program destroys it.
  bool QuitRequested() override {
    if (g_exiting) return true;
    Event e;
    e.kind = Event::kClose;
    e.owner = win->owner;
    Post(e);
    return false;
  }

  Win* win;
};

// The application: Quit from the Deskbar asks the program to end.
class PixelApp : public BApplication {
 public:
  // Makes the application with a signature from the program's name.
  explicit PixelApp(const char* signature) : BApplication(signature) {}

  // Gives the program a quit event instead of quitting, unless the
  // program is ending.
  bool QuitRequested() override {
    if (g_exiting) return BApplication::QuitRequested();
    Event e;
    e.kind = Event::kQuit;
    Post(e);
    return false;
  }
};

// Runs the application's message loop. The thread made the BApplication,
// so it is the application's thread.
int32 AppThread(void* data) {
  PixelApp* app = new PixelApp((const char*)data);
  free(data);
  app->Run();
  delete app;
  return 0;
}

// Ends the application when the program ends: the windows may close
// now, and the application thread finishes.
void StopApp() {
  if (g_appThread < 0 || !be_app) return;
  g_exiting = true;
  be_app->PostMessage(B_QUIT_REQUESTED);
  status_t ignored;
  wait_for_thread(g_appThread, &ignored);
  g_appThread = -1;
}

// The application signature: application/x-vnd.winport-NAME, from the
// file name of the program, with the characters a MIME type allows.
char* Signature() {
  std::string path;
  const char* name = "program";
  if (ImagePath(&path)) {
    size_t slash = path.rfind('/');
    name = path.c_str() + (slash == std::string::npos ? 0 : slash + 1);
  }
  std::string sig = "application/x-vnd.winport-";
  for (const char* p = name; *p && sig.size() < 200; p++) {
    char c = *p;
    sig += (isalnum((unsigned char)c) || c == '-' || c == '.') ? c : '-';
  }
  return strdup(sig.c_str());
}

}  // namespace

bool ImagePath(std::string* path) {
  image_info info;
  int32 cookie = 0;
  while (get_next_image_info(B_CURRENT_TEAM, &cookie, &info) == B_OK)
    if (info.type == B_APP_IMAGE) {
      *path = info.name;
      return true;
    }
  return false;
}

bool Start() {
  if (be_app) return true;
  // The application thread makes the BApplication; the program waits
  // until it is there.
  g_appThread = spawn_thread(AppThread, "winport app", B_NORMAL_PRIORITY, Signature());
  if (g_appThread < 0) return false;
  resume_thread(g_appThread);
  for (int i = 0; i < 500 && (!be_app || be_app->IsLaunching()); i++) snooze(10000);
  if (!be_app) return false;
  atexit(StopApp);
  return true;
}

void ScreenSize(int* width, int* height) {
  BScreen s(B_MAIN_SCREEN_ID);
  BRect f = s.Frame();
  *width = (int)f.Width() + 1;
  *height = (int)f.Height() + 1;
}

Win* Create(void* owner, int x, int y, int width, int height, bool popup) {
  Win* w = new Win;
  w->owner = owner;
  BRect frame(x, y, x + (width > 0 ? width : 1) - 1, y + (height > 0 ? height : 1) - 1);
  PixelWindow* pw = new PixelWindow(frame, w, popup);
  w->window = pw;
  w->view = new PixelView(pw->Bounds(), w);
  pw->AddChild(w->view);
  return w;
}

void Destroy(Win* w) {
  if (!w) return;
  if (w->window->Lock()) w->window->Quit();
  delete w->bitmap;
  delete w;
}

void Show(Win* w, bool show, bool activate) {
  if (!w->window->Lock()) return;
  if (show && !w->shown) {
    w->window->Show();
    w->shown = true;
  } else if (!show && w->shown) {
    w->window->Hide();
    w->shown = false;
  }
  if (show && activate) {
    w->window->Activate(true);
    w->view->MakeFocus(true);
  }
  w->window->Unlock();
}

void SetFrame(Win* w, int x, int y, int width, int height) {
  if (!w->window->Lock()) return;
  w->window->MoveTo(x, y);
  w->window->ResizeTo((width > 0 ? width : 1) - 1, (height > 0 ? height : 1) - 1);
  w->window->Unlock();
}

void SetTitle(Win* w, const std::string& utf8) {
  if (!w->window->Lock()) return;
  w->window->SetTitle(utf8.c_str());
  w->window->Unlock();
}

void Update(Win* w, const uint32_t* pixels, int width, int height, int stride, int x, int y, int cx, int cy) {
  if (width <= 0 || height <= 0) return;
  bool post = false;
  {
    std::lock_guard<std::mutex> g(w->lock);
    BRect size(0, 0, width - 1, height - 1);
    if (!w->bitmap || w->bitmap->Bounds() != size) {
      // A new size: the whole bitmap is new.
      delete w->bitmap;
      w->bitmap = new BBitmap(size, B_RGB32);
      x = 0;
      y = 0;
      cx = width;
      cy = height;
    }
    if (x < 0) cx += x, x = 0;
    if (y < 0) cy += y, y = 0;
    if (x + cx > width) cx = width - x;
    if (y + cy > height) cy = height - y;
    if (cx <= 0 || cy <= 0) return;
    // B_RGB32 is 0x00RRGGBB in the machine's byte order, as the pixels are.
    uint8* bits = (uint8*)w->bitmap->Bits();
    int32 bpr = w->bitmap->BytesPerRow();
    for (int row = y; row < y + cy; row++)
      memcpy(bits + (size_t)row * bpr + (size_t)x * 4, pixels + (size_t)row * stride + x, (size_t)cx * 4);
    BRect r(x, y, x + cx - 1, y + cy - 1);
    w->dirty = w->dirty.IsValid() ? (w->dirty | r) : r;
    if (!w->dirtyPosted) post = w->dirtyPosted = true;
  }
  if (post) w->window->PostMessage(kShowDirty);
}

void SetCursor(bool hidden, int systemId, const uint32_t* argb, int width, int height, int hotX, int hotY) {
  static bool s_hidden = false;
  if (!be_app) return;
  if (hidden != s_hidden) {
    if (hidden)
      be_app->HideCursor();
    else
      be_app->ShowCursor();
    s_hidden = hidden;
  }
  if (hidden) return;
  BCursorID id = B_CURSOR_ID_SYSTEM_DEFAULT;
  bool system = true;
  switch (systemId) {
    case 32512: id = B_CURSOR_ID_SYSTEM_DEFAULT; break;   // IDC_ARROW
    case 32513: id = B_CURSOR_ID_I_BEAM; break;           // IDC_IBEAM
    case 32514: id = B_CURSOR_ID_PROGRESS; break;         // IDC_WAIT
    case 32515: id = B_CURSOR_ID_CROSS_HAIR; break;       // IDC_CROSS
    case 32516: id = B_CURSOR_ID_RESIZE_NORTH; break;     // IDC_UPARROW
    case 32642: id = B_CURSOR_ID_RESIZE_NORTH_WEST_SOUTH_EAST; break;  // IDC_SIZENWSE
    case 32643: id = B_CURSOR_ID_RESIZE_NORTH_EAST_SOUTH_WEST; break;  // IDC_SIZENESW
    case 32644: id = B_CURSOR_ID_RESIZE_EAST_WEST; break;    // IDC_SIZEWE
    case 32645: id = B_CURSOR_ID_RESIZE_NORTH_SOUTH; break;  // IDC_SIZENS
    case 32646: id = B_CURSOR_ID_MOVE; break;             // IDC_SIZEALL
    case 32648: id = B_CURSOR_ID_NOT_ALLOWED; break;      // IDC_NO
    case 32649: id = B_CURSOR_ID_FOLLOW_LINK; break;      // IDC_HAND
    case 32650: id = B_CURSOR_ID_PROGRESS; break;         // IDC_APPSTARTING
    case 32651: id = B_CURSOR_ID_HELP; break;             // IDC_HELP
    default: system = false;
  }
  if (system || !argb || width <= 0 || height <= 0) {
    BCursor c(id);
    be_app->SetCursor(&c, true);
    return;
  }
  // B_RGBA32 is 0xAARRGGBB in the machine's byte order, not premultiplied.
  BBitmap bitmap(BRect(0, 0, width - 1, height - 1), B_RGBA32);
  uint8* bits = (uint8*)bitmap.Bits();
  for (int row = 0; row < height; row++)
    memcpy(bits + (size_t)row * bitmap.BytesPerRow(), argb + (size_t)row * width, (size_t)width * 4);
  BCursor c(&bitmap, BPoint(hotX, hotY));
  be_app->SetCursor(&c, true);
}

long ClipboardCount() { return be_clipboard ? (long)be_clipboard->SystemCount() : 0; }

bool ClipboardText(std::string* utf8) {
  if (!be_clipboard || !be_clipboard->Lock()) return false;
  bool ok = false;
  const char* text = NULL;
  ssize_t length = 0;
  if (BMessage* data = be_clipboard->Data())
    if (data->FindData("text/plain", B_MIME_TYPE, (const void**)&text, &length) == B_OK && text) {
      utf8->assign(text, (size_t)length);
      ok = true;
    }
  be_clipboard->Unlock();
  return ok;
}

long SetClipboardText(const std::string& utf8) {
  if (!be_clipboard || !be_clipboard->Lock()) return 0;
  be_clipboard->Clear();
  if (BMessage* data = be_clipboard->Data()) data->AddData("text/plain", B_MIME_TYPE, utf8.data(), utf8.size());
  be_clipboard->Commit();
  be_clipboard->Unlock();
  return ClipboardCount();
}

std::vector<Event> Take(long timeout) {
  std::unique_lock<std::mutex> g(g_queueLock);
  auto ready = [] { return g_woken || !g_events.empty(); };
  if (timeout < 0)
    g_queueReady.wait(g, ready);
  else
    g_queueReady.wait_for(g, std::chrono::milliseconds(timeout), ready);
  g_woken = false;
  std::vector<Event> out(g_events.begin(), g_events.end());
  g_events.clear();
  return out;
}

void Wake() {
  std::lock_guard<std::mutex> g(g_queueLock);
  g_woken = true;
  g_queueReady.notify_all();
}

}  // namespace haiku
}  // namespace winport

#endif
