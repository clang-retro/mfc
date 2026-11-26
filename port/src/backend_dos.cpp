// backend_dos.cpp - the backend of 32-bit DOS: it shows the top-level
// windows on a VESA screen, and reads the mouse (INT 33h) and the
// keyboard (INT 16h).
//
// DOS has no window system, so this backend is one. It keeps the shown
// windows in an order from back to front, and composes the screen from
// their surfaces over a plain desktop into a copy in memory. Each change
// goes from the copy to the linear frame buffer, in the screen's pixel
// format. The pointer is the cursor's picture, drawn over the frame
// buffer.
//
// The screen mode: WINPORT_SCREEN=WIDTHxHEIGHT selects a VESA mode of
// that size (the default is 640x480), with the most colors that the card
// has (32, 24, 16 or 15 bits). Without a VESA mode with a linear frame
// buffer, the backend uses VGA mode 13h (320x200, 256 colors). With
// WINPORT_BACKEND=headless, the windows stay in memory.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#include "user.h"

#if defined(__MSDOS__) && defined(__i386__)

#include <dos.h>
#include <dpmi.h>
#include <i86.h>
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <algorithm>
#include <vector>

namespace winport {

namespace {

// The desktop's color, where no window is.
const DWORD kDesktop = 0x00008080;

// One top-level window that the backend shows.
struct Native {
  Window* w = nullptr;
  bool shown = false;
  bool popup = false;
  RECT rect = {0, 0, 0, 0};  // where the screen shows it now
};

// The keys by their scan code (set 1), for the codes that the BIOS gives.
const BYTE kScanKeys[0x59] = {
    0, VK_ESCAPE, '1', '2', '3', '4', '5', '6',                                     // 0x00
    '7', '8', '9', '0', VK_OEM_MINUS, VK_OEM_PLUS, VK_BACK, VK_TAB,                // 0x08
    'Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I',                                         // 0x10
    'O', 'P', VK_OEM_4, VK_OEM_6, VK_RETURN, VK_LCONTROL, 'A', 'S',                 // 0x18
    'D', 'F', 'G', 'H', 'J', 'K', 'L', VK_OEM_1,                                    // 0x20
    VK_OEM_7, VK_OEM_3, VK_LSHIFT, VK_OEM_5, 'Z', 'X', 'C', 'V',                    // 0x28
    'B', 'N', 'M', VK_OEM_COMMA, VK_OEM_PERIOD, VK_OEM_2, VK_RSHIFT, VK_MULTIPLY,   // 0x30
    VK_LMENU, VK_SPACE, VK_CAPITAL, VK_F1, VK_F2, VK_F3, VK_F4, VK_F5,              // 0x38
    VK_F6, VK_F7, VK_F8, VK_F9, VK_F10, VK_NUMLOCK, VK_SCROLL, VK_HOME,             // 0x40
    VK_UP, VK_PRIOR, VK_SUBTRACT, VK_LEFT, VK_CLEAR, VK_RIGHT, VK_ADD, VK_END,      // 0x48
    VK_DOWN, VK_NEXT, VK_INSERT, VK_DELETE, 0, 0, VK_OEM_102, VK_F11,               // 0x50
    VK_F12,                                                                         // 0x58
};

// Gives the virtual key of a key that the BIOS read: AH is the scan
// code, AL the character (0xE0 for a key of the extended keypad).
UINT ScanToKey(unsigned scan, unsigned ch, bool* extended) {
  *extended = ch == 0xE0;
  switch (scan) {
    case 0x47: case 0x48: case 0x49: case 0x4B: case 0x4D: case 0x4F: case 0x50: case 0x51: case 0x52: case 0x53:
      if (ch == 0xE0 || ch == 0) *extended = true;
      break;
    // The keys that the BIOS gives only with their own scan codes.
    case 0x85: return VK_F11;
    case 0x86: return VK_F12;
    case 0x73: *extended = true; return VK_LEFT;   // Ctrl+Left
    case 0x74: *extended = true; return VK_RIGHT;  // Ctrl+Right
    case 0x8D: *extended = true; return VK_UP;     // Ctrl+Up
    case 0x91: *extended = true; return VK_DOWN;   // Ctrl+Down
    case 0x77: *extended = true; return VK_HOME;
    case 0x75: *extended = true; return VK_END;
    case 0x84: *extended = true; return VK_PRIOR;
    case 0x76: *extended = true; return VK_NEXT;
    case 0xE0: *extended = true; return ch == 0x0D ? VK_RETURN : VK_DIVIDE;
    case 0xA6: *extended = true; return VK_RETURN;
  }
  // Alt with a number key gives 0x78 to 0x83, Alt with F1 to F10 gives
  // 0x68 to 0x71, Shift and Ctrl with them 0x54 to 0x67.
  if (scan >= 0x78 && scan <= 0x83) return scan == 0x82 ? VK_OEM_MINUS : scan == 0x83 ? VK_OEM_PLUS : (UINT)(scan == 0x81 ? '0' : '1' + (scan - 0x78));
  if (scan >= 0x54 && scan <= 0x71) return VK_F1 + (scan - 0x54) % 10;
  if (scan >= 0x85 && scan <= 0x8C) return (scan & 1) ? VK_F11 : VK_F12;
  return scan < sizeof kScanKeys ? kScanKeys[scan] : 0;
}

// The video mode: its size, its pixel format and the frame buffer.
struct Video {
  int width = 0, height = 0;
  int bpp = 0;       // 8, 15, 16, 24 or 32
  int pitch = 0;     // bytes from one row to the next
  int rpos = 16, gpos = 8, bpos = 0;  // the position of each color's field
  int rbits = 8, gbits = 8, bbits = 8;
  unsigned char* fb = nullptr;  // the frame buffer, at its linear address
  int mode = 0;      // the VESA mode, or 0x13

  // Gives the pixel of the screen's format for a color 0x00RRGGBB.
  uint32_t Pixel(DWORD c) const {
    unsigned r = (c >> 16) & 255, g = (c >> 8) & 255, b = c & 255;
    if (bpp == 8) return (r & 0xE0) | ((g >> 3) & 0x1C) | (b >> 6);
    return (uint32_t)(r >> (8 - rbits)) << rpos | (uint32_t)(g >> (8 - gbits)) << gpos |
           (uint32_t)(b >> (8 - bbits)) << bpos;
  }

  // Writes n pixels of 0x00RRGGBB to the frame buffer at (x, y).
  void Write(int x, int y, const DWORD* src, int n) const {
    unsigned char* p = fb + (size_t)y * pitch;
    switch (bpp) {
      case 32: {
        uint32_t* d = (uint32_t*)p + x;
        if (rpos == 16 && gpos == 8 && bpos == 0) {
          memcpy(d, src, (size_t)n * 4);
        } else {
          for (int i = 0; i < n; i++) d[i] = Pixel(src[i]);
        }
        break;
      }
      case 24: {
        unsigned char* d = p + x * 3;
        for (int i = 0; i < n; i++, d += 3) {
          uint32_t v = Pixel(src[i]);
          d[0] = (unsigned char)v;
          d[1] = (unsigned char)(v >> 8);
          d[2] = (unsigned char)(v >> 16);
        }
        break;
      }
      case 15:
      case 16: {
        uint16_t* d = (uint16_t*)p + x;
        for (int i = 0; i < n; i++) d[i] = (uint16_t)Pixel(src[i]);
        break;
      }
      default: {
        unsigned char* d = p + x;
        for (int i = 0; i < n; i++) d[i] = (unsigned char)Pixel(src[i]);
        break;
      }
    }
  }
};

// Calls the video BIOS (INT 10h) in real mode with the registers r.
void VideoBios(__dpmi_regs* r) { __dpmi_int(0x10, r); }

// Gives the linear address of a real-mode far pointer (segment:offset).
const unsigned char* FarPtr(uint32_t p) { return (const unsigned char*)(uintptr_t)((p >> 16) * 16 + (p & 0xFFFF)); }

// Sets a VESA mode with a linear frame buffer of the size w x h (or the
// size of WINPORT_SCREEN), with the most colors. Gives false when the
// card has no such mode.
bool SetVesaMode(Video* v) {
  int want_w = 640, want_h = 480, want_bpp = 0;
  if (const char* s = getenv("WINPORT_SCREEN")) {
    int w, h, b;
    int n = sscanf(s, "%dx%dx%d", &w, &h, &b);
    if (n >= 2 && w > 0 && h > 0) {
      want_w = w;
      want_h = h;
      if (n == 3) want_bpp = b;
    }
  }
  int sel;
  int seg = __dpmi_allocate_dos_memory(64, &sel);  // 1 KiB
  if (seg < 0) return false;
  unsigned char* buf = (unsigned char*)(uintptr_t)(seg * 16);
  __dpmi_regs r;
  memset(&r, 0, sizeof r);
  memcpy(buf, "VBE2", 4);
  r.x.ax = 0x4F00;
  r.x.es = (unsigned short)seg;
  VideoBios(&r);
  if (r.x.ax != 0x004F || memcmp(buf, "VESA", 4) != 0 || buf[5] < 2) {
    __dpmi_free_dos_memory(sel);
    return false;
  }
  // Copies the list of modes: the next calls use the same buffer.
  std::vector<unsigned> modes;
  for (const uint16_t* m = (const uint16_t*)FarPtr(*(uint32_t*)(buf + 14)); *m != 0xFFFF && modes.size() < 512; m++)
    modes.push_back(*m);
  int best = -1, best_bpp = 0;
  Video found;
  for (unsigned mode : modes) {
    memset(buf, 0, 256);
    memset(&r, 0, sizeof r);
    r.x.ax = 0x4F01;
    r.x.cx = (unsigned short)mode;
    r.x.es = (unsigned short)seg;
    VideoBios(&r);
    if (r.x.ax != 0x004F) continue;
    unsigned attr = *(uint16_t*)buf;
    int w = *(uint16_t*)(buf + 0x12), h = *(uint16_t*)(buf + 0x14), bits = buf[0x19], model = buf[0x1B];
    // Supported, color, graphics, with a linear frame buffer.
    if ((attr & 0x99) != 0x99 || w != want_w || h != want_h) continue;
    if (model != 6 || (bits != 15 && bits != 16 && bits != 24 && bits != 32)) continue;
    int depth = bits == 16 && buf[0x1F] + buf[0x21] + buf[0x23] == 15 ? 15 : bits;
    if (want_bpp && depth != want_bpp) continue;
    if (depth <= best_bpp) continue;
    best = (int)mode;
    best_bpp = depth;
    found.width = w;
    found.height = h;
    found.bpp = depth;
    found.pitch = *(uint16_t*)(buf + 0x10);
    if (buf[5] >= 3 && *(uint16_t*)(buf + 0x32)) found.pitch = *(uint16_t*)(buf + 0x32);
    found.rbits = buf[0x1F];
    found.rpos = buf[0x20];
    found.gbits = buf[0x21];
    found.gpos = buf[0x22];
    found.bbits = buf[0x23];
    found.bpos = buf[0x24];
    found.fb = (unsigned char*)(uintptr_t) * (uint32_t*)(buf + 0x28);
  }
  __dpmi_free_dos_memory(sel);
  if (best < 0) return false;
  __dpmi_meminfo mi;
  mi.address = (unsigned long)(uintptr_t)found.fb;
  mi.size = (unsigned long)found.pitch * found.height;
  if (__dpmi_physical_address_mapping(&mi) != 0) return false;
  memset(&r, 0, sizeof r);
  r.x.ax = 0x4F02;
  r.x.bx = (unsigned short)(best | 0x4000);  // with the linear frame buffer
  VideoBios(&r);
  if (r.x.ax != 0x004F) return false;
  found.fb = (unsigned char*)(uintptr_t)mi.address;
  found.mode = best;
  *v = found;
  return true;
}

// Sets VGA mode 13h, with a palette of 3 bits of red, 3 of green and 2
// of blue.
void SetVgaMode(Video* v) {
  union REGS r;
  r.w.ax = 0x13;
  int386(0x10, &r, &r);
  outportb(0x3C8, 0);
  for (int i = 0; i < 256; i++) {
    outportb(0x3C9, (unsigned char)(((i >> 5) & 7) * 63 / 7));
    outportb(0x3C9, (unsigned char)(((i >> 2) & 7) * 63 / 7));
    outportb(0x3C9, (unsigned char)((i & 3) * 63 / 3));
  }
  v->width = 320;
  v->height = 200;
  v->bpp = 8;
  v->pitch = 320;
  v->fb = (unsigned char*)0xA0000;
  v->mode = 0x13;
}

// Sets the text mode again when the program ends.
void RestoreTextMode() {
  union REGS r;
  r.w.ax = 0x03;
  int386(0x10, &r, &r);
}

// Calls the mouse driver (INT 33h) with AX=ax, CX=cx and DX=dx.
void MouseCall(unsigned ax, unsigned cx, unsigned dx, union REGS* out) {
  union REGS r;
  memset(&r, 0, sizeof r);
  r.w.ax = (unsigned short)ax;
  r.w.cx = (unsigned short)cx;
  r.w.dx = (unsigned short)dx;
  int386(0x33, &r, out);
}

// Gives the time in milliseconds.
long long NowMs() {
  struct timespec t;
  clock_gettime(CLOCK_MONOTONIC, &t);
  return (long long)t.tv_sec * 1000 + t.tv_nsec / 1000000;
}

struct DosBackend : Backend {
  Video video;
  std::vector<DWORD> screen;     // the composed screen, 0x00RRGGBB
  std::vector<Native*> order;    // the shown windows, from back to front
  std::vector<Native*> natives;  // all windows that have a Native
  bool mouse = false;            // a mouse driver is there
  POINT pointer = {0, 0};
  unsigned buttons = 0;          // the buttons down, as INT 33h gives them
  Native* grab = nullptr;        // the window that the mouse went down in
  CursorPicture cursor;          // the pointer's picture
  bool cursorShown = true;
  RECT cursorRect = {0, 0, 0, 0};  // where the pointer is drawn now
  volatile bool woken = false;

  // Sets the screen mode and finds the mouse.
  bool Start() {
    if (!SetVesaMode(&video)) SetVgaMode(&video);
    atexit(RestoreTextMode);
    screen.assign((size_t)video.width * video.height, kDesktop);
    Flush(RECT{0, 0, video.width, video.height});
    union REGS r;
    MouseCall(0, 0, 0, &r);
    mouse = r.w.ax == 0xFFFF;
    if (mouse) {
      MouseCall(7, 0, video.width - 1, &r);
      MouseCall(8, 0, video.height - 1, &r);
      MouseCall(4, video.width / 2, video.height / 2, &r);
      pointer = POINT{video.width / 2, video.height / 2};
    }
    CursorImage(LoadCursorW(NULL, (LPCWSTR)IDC_ARROW), &cursor);
    DrawPointer();
    return true;
  }

  // Gives the backend's name.
  const char* Name() override { return "dos"; }

  // Gives the size of the screen mode.
  SIZE ScreenSize() override { return SIZE{video.width, video.height}; }

  // Gives the Native of a window, or NULL.
  static Native* NativeOf(Window* w) { return (Native*)w->native; }

  // Tells whether a window is a menu, a drop-down or a tooltip, which is
  // shown without becoming the active window.
  static bool IsPopup(Window* w) {
    if ((w->style & WS_CAPTION) == WS_CAPTION) return false;
    if (w->exStyle & WS_EX_APPWINDOW) return false;
    return (w->style & WS_POPUP) != 0;
  }

  // Composes the rectangle r of the screen from the desktop and the
  // shown windows, and writes it to the frame buffer.
  void Compose(RECT r) {
    if (r.left < 0) r.left = 0;
    if (r.top < 0) r.top = 0;
    if (r.right > video.width) r.right = video.width;
    if (r.bottom > video.height) r.bottom = video.height;
    if (r.right <= r.left || r.bottom <= r.top) return;
    for (int y = r.top; y < r.bottom; y++) std::fill_n(&screen[(size_t)y * video.width + r.left], r.right - r.left, kDesktop);
    std::vector<DWORD> composed;
    for (Native* n : order) {
      Surface* s = n->w->surface;
      if (!s || s->width <= 0 || s->height <= 0) continue;
      RECT c;
      c.left = std::max(r.left, n->rect.left);
      c.top = std::max(r.top, n->rect.top);
      c.right = std::min(r.right, std::min(n->rect.right, n->rect.left + s->width));
      c.bottom = std::min(r.bottom, std::min(n->rect.bottom, n->rect.top + s->height));
      if (c.right <= c.left || c.bottom <= c.top) continue;
      int cw = c.right - c.left;
      if (Translucent(n->w)) {
        // A layered window or one with a region: its premultiplied look
        // goes over what is behind it.
        composed.resize((size_t)cw * (c.bottom - c.top));
        RECT sr = {c.left - n->rect.left, c.top - n->rect.top, c.right - n->rect.left, c.bottom - n->rect.top};
        Composite(n->w, sr, composed.data(), cw);
        for (int y = c.top; y < c.bottom; y++) {
          DWORD* d = &screen[(size_t)y * video.width + c.left];
          const DWORD* p = &composed[(size_t)(y - c.top) * cw];
          for (int x = 0; x < cw; x++) {
            unsigned a = p[x] >> 24, ia = 255 - a;
            DWORD b = d[x];
            unsigned rr = ((p[x] >> 16) & 255) + ((b >> 16) & 255) * ia / 255;
            unsigned gg = ((p[x] >> 8) & 255) + ((b >> 8) & 255) * ia / 255;
            unsigned bb = (p[x] & 255) + (b & 255) * ia / 255;
            d[x] = std::min(rr, 255u) << 16 | std::min(gg, 255u) << 8 | std::min(bb, 255u);
          }
        }
      } else {
        for (int y = c.top; y < c.bottom; y++)
          memcpy(&screen[(size_t)y * video.width + c.left], &s->Row(y - n->rect.top)[c.left - n->rect.left], (size_t)cw * 4);
      }
    }
    Flush(r);
  }

  // Writes the rectangle r of the composed screen to the frame buffer,
  // and the pointer over it.
  void Flush(const RECT& r) {
    for (int y = r.top; y < r.bottom; y++) video.Write(r.left, y, &screen[(size_t)y * video.width + r.left], r.right - r.left);
    if (cursorShown && Overlaps(r, cursorRect)) DrawPointer();
  }

  // Tells whether two rectangles overlap.
  static bool Overlaps(const RECT& a, const RECT& b) {
    return a.left < b.right && b.left < a.right && a.top < b.bottom && b.top < a.bottom;
  }

  // Draws the pointer's picture over the frame buffer at the pointer.
  void DrawPointer() {
    if (!cursorShown || cursor.width <= 0) {
      cursorRect = RECT{0, 0, 0, 0};
      return;
    }
    int x0 = pointer.x - cursor.hotspot.x, y0 = pointer.y - cursor.hotspot.y;
    cursorRect = RECT{x0, y0, x0 + cursor.width, y0 + cursor.height};
    for (int y = 0; y < cursor.height; y++) {
      int sy = y0 + y;
      if (sy < 0 || sy >= video.height) continue;
      for (int x = 0; x < cursor.width; x++) {
        int sx = x0 + x;
        if (sx < 0 || sx >= video.width) continue;
        DWORD c = cursor.argb[(size_t)y * cursor.width + x];
        if ((c >> 24) < 128) continue;
        DWORD px = c & 0xFFFFFF;
        video.Write(sx, sy, &px, 1);
      }
    }
  }

  // Removes the pointer's picture: the screen under it goes back.
  void ErasePointer() {
    RECT r = cursorRect;
    cursorRect = RECT{0, 0, 0, 0};
    r.left = std::max(r.left, (LONG)0);
    r.top = std::max(r.top, (LONG)0);
    r.right = std::min(r.right, (LONG)video.width);
    r.bottom = std::min(r.bottom, (LONG)video.height);
    for (int y = r.top; y < r.bottom; y++) video.Write(r.left, y, &screen[(size_t)y * video.width + r.left], r.right - r.left);
  }

  // Shows the pointer's picture for a cursor, or hides it for none.
  void SetCursor(HCURSOR c) override {
    ErasePointer();
    cursorShown = c != NULL;
    if (c) CursorImage(c, &cursor);
    DrawPointer();
  }

  // Gives where the pointer is now.
  bool ReadCursor(POINT* p) override {
    *p = pointer;
    return true;
  }

  // Shows or hides a window: a shown window goes in front of the others.
  void Show(Window* w, bool show) override {
    Native* n = NativeOf(w);
    if (!n) {
      if (!show) return;
      n = new Native;
      n->w = w;
      n->popup = IsPopup(w);
      w->native = n;
      natives.push_back(n);
    }
    n->rect = w->rect;
    auto it = std::find(order.begin(), order.end(), n);
    if (it != order.end()) order.erase(it);
    n->shown = show;
    if (show) order.push_back(n);
    Compose(n->rect);
  }

  // Forgets a window, and shows what was behind it.
  void Destroy(Window* w) override {
    Native* n = NativeOf(w);
    if (!n) return;
    auto it = std::find(order.begin(), order.end(), n);
    if (it != order.end()) order.erase(it);
    natives.erase(std::find(natives.begin(), natives.end(), n));
    if (grab == n) grab = nullptr;
    w->native = nullptr;
    RECT r = n->rect;
    delete n;
    Compose(r);
  }

  // Shows a window at its new place or size.
  void Moved(Window* w) override {
    Native* n = NativeOf(w);
    if (!n) return;
    RECT old = n->rect;
    n->rect = w->rect;
    if (!n->shown) return;
    Compose(old);
    Compose(n->rect);
  }

  // Shows the whole window again in its new look.
  void TranslucencyChanged(Window* w) override {
    if (Native* n = NativeOf(w))
      if (n->shown) Compose(n->rect);
  }

  // Shows the changed part r of a window's surface.
  void Present(Window* w, const RECT& r) override {
    Native* n = NativeOf(w);
    if (!n || !n->shown) return;
    Compose(RECT{n->rect.left + r.left, n->rect.top + r.top, n->rect.left + r.right, n->rect.top + r.bottom});
  }

  // Gives the shown window at a point of the screen, the front one
  // first, or NULL for the desktop.
  Native* WindowAt(POINT p) {
    for (auto it = order.rbegin(); it != order.rend(); ++it) {
      Native* n = *it;
      if (p.x >= n->rect.left && p.x < n->rect.right && p.y >= n->rect.top && p.y < n->rect.bottom &&
          TakesMouse(n->w, p.x - n->rect.left, p.y - n->rect.top))
        return n;
    }
    return nullptr;
  }

  // Gives the active window's Native, or NULL.
  Native* ActiveNative() {
    HWND active = GetActiveWindow();
    for (Native* n : natives)
      if (n->w->hwnd == active) return n;
    return nullptr;
  }

  // Gives the MK_ flags for the buttons and the modifier keys.
  WPARAM Keys() {
    WPARAM k = 0;
    if (buttons & 1) k |= MK_LBUTTON;
    if (buttons & 2) k |= MK_RBUTTON;
    if (buttons & 4) k |= MK_MBUTTON;
    unsigned char shift = *(volatile unsigned char*)0x417;
    if (shift & 3) k |= MK_SHIFT;
    if (shift & 4) k |= MK_CONTROL;
    return k;
  }

  // Reads the mouse, moves the pointer and gives the layer the
  // movement and the buttons. Gives true when something changed.
  bool PollMouse() {
    if (!mouse) return false;
    union REGS r;
    MouseCall(3, 0, 0, &r);
    POINT p = {(short)r.w.cx, (short)r.w.dx};
    unsigned now = r.w.bx & 7;
    if (p.x == pointer.x && p.y == pointer.y && now == buttons) return false;
    if (p.x != pointer.x || p.y != pointer.y) {
      ErasePointer();
      pointer = p;
      DrawPointer();
      Native* n = grab ? grab : WindowAt(p);
      if (n) MouseInput(n->w, WM_MOUSEMOVE, p.x - n->rect.left, p.y - n->rect.top, Keys());
    }
    static const UINT kDown[3] = {WM_LBUTTONDOWN, WM_RBUTTONDOWN, WM_MBUTTONDOWN};
    static const UINT kUp[3] = {WM_LBUTTONUP, WM_RBUTTONUP, WM_MBUTTONUP};
    for (int b = 0; b < 3; b++) {
      unsigned bit = 1u << b;
      if ((now ^ buttons) & bit) {
        bool down = (now & bit) != 0;
        buttons ^= bit;
        Native* n = grab ? grab : WindowAt(p);
        if (down && !grab) {
          grab = n;
          if (n && !n->popup && order.back() != n) {
            // A click brings its window to the front.
            order.erase(std::find(order.begin(), order.end(), n));
            order.push_back(n);
            Compose(n->rect);
          }
        }
        if (n) MouseInput(n->w, down ? kDown[b] : kUp[b], p.x - n->rect.left, p.y - n->rect.top, Keys());
        if (!buttons) grab = nullptr;
      }
    }
    return true;
  }

  // Reads the keys that the BIOS has, and gives them to the active
  // window. The BIOS tells only that a key went down, so each key comes
  // up at once. Gives true when there was a key.
  bool PollKeys() {
    bool any = false;
    // The BIOS keyboard buffer is empty when its head is its tail.
    while (*(volatile uint16_t*)0x41A != *(volatile uint16_t*)0x41C) {
      union REGS r;
      r.w.ax = 0x1000;
      int386(0x16, &r, &r);
      any = true;
      unsigned scan = r.h.ah, ch = r.h.al;
      bool ext;
      UINT vk = ScanToKey(scan, ch, &ext);
      Native* n = ActiveNative();
      if (!n && !order.empty()) n = order.back();
      if (!n) continue;
      unsigned char shift = *(volatile unsigned char*)0x417;
      wchar_t c = 0;
      if (ch && ch != 0xE0) c = ch < 0x80 ? (wchar_t)ch : (wchar_t)0xFFFD;
      if (shift & 8) {
        // Alt with a letter is a menu's mnemonic.
        if (vk >= 'A' && vk <= 'Z') c = (wchar_t)((shift & 3) ? vk : vk - 'A' + 'a');
      }
      if (c == 0xFFFD) c = 0;
      LPARAM lp = 1 | ((LPARAM)(scan & 0x7F) << 16) | (ext ? (1 << 24) : 0) | ((shift & 8) ? (1 << 29) : 0);
      if (c) lp |= 0x02000000;  // the character comes from here, not TranslateMessage
      if (vk) {
        if (shift & 8) KeyInput(n->w, WM_KEYDOWN, VK_MENU, 1 | (0x38 << 16) | (1 << 29));
        KeyInput(n->w, (shift & 8) ? WM_SYSKEYDOWN : WM_KEYDOWN, vk, lp);
      }
      if (c) CharInput(n->w, c);
      if (vk) {
        KeyInput(n->w, (shift & 8) ? WM_SYSKEYUP : WM_KEYUP, vk, (lp & ~0x02000000) | 0xC0000000);
        if (shift & 8) KeyInput(n->w, WM_KEYUP, VK_MENU, 1 | (0x38 << 16) | 0xC0000000);
      }
    }
    return any;
  }

  // Reads the mouse and the keyboard for up to timeout ms, or until
  // Wake. The other threads run while it waits.
  void Wait(DWORD timeout) override {
    long long end = timeout == INFINITE ? -1 : NowMs() + timeout;
    for (;;) {
      if (woken) break;
      bool input = PollMouse();
      input |= PollKeys();
      if (input || woken) break;
      if (end >= 0 && NowMs() >= end) break;
      sched_yield();
    }
    woken = false;
  }

  // Makes Wait return.
  void Wake() override { woken = true; }
};

}  // namespace

// Gives the DOS backend: the screen in a VESA or VGA mode.
Backend* NewDosBackend() {
  DosBackend* b = new DosBackend;
  if (!b->Start()) {
    delete b;
    return nullptr;
  }
  return b;
}

}  // namespace winport

#endif
