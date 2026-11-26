// opengl.cpp - pixel formats and the wgl functions.
//
// On Mac OS X, OpenGL draws through CGL into an off-screen buffer in
// memory (the software or hardware renderer, as the system chooses). On
// the classic Mac OS for PowerPC, it draws the same way through AGL
// (opengl_agl.cpp). opengl_backend.h is the interface to the two.
// SwapBuffers copies that buffer into the window with SetDIBitsToDevice,
// so the picture goes through the same drawing path as GDI. For a
// single-buffered format, glFlush and glFinish copy the buffer.
//
// On the other systems the generic implementation draws: the software
// renderer of port/softgl (opengl_soft.cpp), as GDI Generic does on
// Windows. Built with WINPORT_NO_SOFTGL, there is no OpenGL there: a DC
// has no pixel formats to choose from, so ChoosePixelFormat finds none
// and no rendering context can be made. Each call fails with
// ERROR_NOT_SUPPORTED.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#include "opengl_backend.h"

#ifdef WINPORT_CGL
#include <OpenGL/CGLContext.h>
#include <OpenGL/OpenGL.h>
#include <OpenGL/gl.h>
#include <OpenGL/glu.h>
#include <dlfcn.h>
#endif
#if defined(WINPORT_AGL) || defined(WINPORT_SOFTGL)
#include <GL/gl.h>
#include <GL/glu.h>
#endif

#include <windows.h>

#include "gdi.h"

#include <math.h>
#include <stdint.h>
#include <string.h>

#include <map>
#include <mutex>
#include <vector>

namespace {

// Sets ERROR_NOT_SUPPORTED and returns r.
template <class T>
T Unsupported(T r) {
  SetLastError(ERROR_NOT_SUPPORTED);
  return r;
}

}  // namespace

#ifndef WINPORT_GL

// Without OpenGL, each function below fails with ERROR_NOT_SUPPORTED and
// gives its failure value (Unsupported).
extern "C" {

int WINAPI ChoosePixelFormat(HDC, const PIXELFORMATDESCRIPTOR*) { return Unsupported(0); }
// No formats: DescribePixelFormat counts them, and there are none.
int WINAPI DescribePixelFormat(HDC, int, UINT, LPPIXELFORMATDESCRIPTOR) { return Unsupported(0); }
int WINAPI GetPixelFormat(HDC) { return Unsupported(0); }
BOOL WINAPI SetPixelFormat(HDC, int, const PIXELFORMATDESCRIPTOR*) { return Unsupported(FALSE); }
BOOL WINAPI SwapBuffers(HDC) { return Unsupported(FALSE); }

HGLRC WINAPI wglCreateContext(HDC) { return Unsupported((HGLRC)NULL); }
HGLRC WINAPI wglCreateLayerContext(HDC, int) { return Unsupported((HGLRC)NULL); }
BOOL WINAPI wglDeleteContext(HGLRC) { return Unsupported(FALSE); }
BOOL WINAPI wglCopyContext(HGLRC, HGLRC, UINT) { return Unsupported(FALSE); }
// Gives the current context of the thread.
HGLRC WINAPI wglGetCurrentContext(void) { return NULL; }
// Gives the DC of the current context of the thread.
HDC WINAPI wglGetCurrentDC(void) { return NULL; }
PROC WINAPI wglGetProcAddress(LPCSTR) { return Unsupported((PROC)NULL); }
// Making no context current succeeds; there is none to begin with.
BOOL WINAPI wglMakeCurrent(HDC, HGLRC rc) { return rc ? Unsupported(FALSE) : TRUE; }
BOOL WINAPI wglShareLists(HGLRC, HGLRC) { return Unsupported(FALSE); }
// Makes display lists of the glyph bitmaps of the font of dc.
BOOL WINAPI wglUseFontBitmapsA(HDC, DWORD, DWORD, DWORD) { return Unsupported(FALSE); }
// The Unicode form of wglUseFontBitmapsA.
BOOL WINAPI wglUseFontBitmapsW(HDC, DWORD, DWORD, DWORD) { return Unsupported(FALSE); }
BOOL WINAPI wglUseFontOutlinesA(HDC, DWORD, DWORD, DWORD, FLOAT, FLOAT, int, LPGLYPHMETRICSFLOAT) {
  return Unsupported(FALSE);
}
BOOL WINAPI wglUseFontOutlinesW(HDC, DWORD, DWORD, DWORD, FLOAT, FLOAT, int, LPGLYPHMETRICSFLOAT) {
  return Unsupported(FALSE);
}

}  // extern "C"

#else  // WINPORT_GL

#ifdef WINPORT_CGL

namespace winport {
namespace gl {

// Gives the type of the count that CGLChoosePixelFormat writes: long in
// the SDK of 10.4, GLint in later SDKs.
template <class F>
struct NpixArg;
template <class R, class A, class B, class C>
struct NpixArg<R (*)(A, B, C*)> {
  typedef C type;
};

// Makes an off-screen CGL context (see opengl_backend.h).
Native Make(Native share) {
  CGLPixelFormatAttribute attrs[] = {kCGLPFAOffScreen,
                                     kCGLPFAColorSize, (CGLPixelFormatAttribute)32,
                                     kCGLPFAAlphaSize, (CGLPixelFormatAttribute)8,
                                     kCGLPFADepthSize, (CGLPixelFormatAttribute)24,
                                     kCGLPFAStencilSize, (CGLPixelFormatAttribute)8,
                                     (CGLPixelFormatAttribute)0};
  CGLPixelFormatObj pf = NULL;
  NpixArg<decltype(&CGLChoosePixelFormat)>::type count = 0;
  if (CGLChoosePixelFormat(attrs, &pf, &count) != kCGLNoError || !pf) {
    // Without a stencil buffer, for renderers that have none.
    attrs[7] = (CGLPixelFormatAttribute)0;
    if (CGLChoosePixelFormat(attrs, &pf, &count) != kCGLNoError || !pf) return NULL;
  }
  CGLContextObj ctx = NULL;
  CGLError e = CGLCreateContext(pf, (CGLContextObj)share, &ctx);
  CGLDestroyPixelFormat(pf);
  return e == kCGLNoError ? ctx : NULL;
}

// Destroys a context (see opengl_backend.h).
void Destroy(Native ctx) {
  if (CGLGetCurrentContext() == (CGLContextObj)ctx) CGLSetCurrentContext(NULL);
  CGLDestroyContext((CGLContextObj)ctx);
}

// Makes ctx current (see opengl_backend.h).
bool SetCurrent(Native ctx) { return CGLSetCurrentContext((CGLContextObj)ctx) == kCGLNoError; }

// Points ctx at a buffer (see opengl_backend.h).
bool SetBuffer(Native ctx, int w, int h, void* pixels) {
  return CGLSetOffScreen((CGLContextObj)ctx, w, h, w * 4, pixels) == kCGLNoError;
}

// Copies context state (see opengl_backend.h).
bool Copy(Native from, Native to, unsigned mask) {
  return CGLCopyContext((CGLContextObj)from, (CGLContextObj)to, mask) == kCGLNoError;
}

// Calls the renderer's glFlush or glFinish for the current CGL context
// through its dispatch table, bypassing the glFlush and glFinish of
// opengl.cpp.
void RealFlush(bool finish) {
  CGLContextObj ctx = CGLGetCurrentContext();
  if (!ctx) return;
  if (finish)
    ctx->disp.finish(ctx->rend);
  else
    ctx->disp.flush(ctx->rend);
}

// Finds a function by name: the OpenGL framework exports the extension
// functions itself (see opengl_backend.h).
void* FindProc(const char* name) { return dlsym(RTLD_DEFAULT, name); }

}  // namespace gl
}  // namespace winport

#endif  // WINPORT_CGL

namespace {

// The pixel formats every DC offers, numbered from 1. All are drawn in
// memory with 8 bits for each of red, green, blue and alpha.
//   1: a window, double buffered
//   2: a window or a bitmap, single buffered
const int kFormats = 2;

// Fills a descriptor for format i (1 or 2).
void Describe(int i, PIXELFORMATDESCRIPTOR* p) {
  memset(p, 0, sizeof *p);
  p->nSize = sizeof *p;
  p->nVersion = 1;
  p->dwFlags = PFD_SUPPORT_OPENGL | PFD_DRAW_TO_WINDOW | PFD_GENERIC_FORMAT | PFD_SUPPORT_GDI;
  if (i == 1)
    p->dwFlags = (p->dwFlags & ~(DWORD)PFD_SUPPORT_GDI) | PFD_DOUBLEBUFFER;
  else
    p->dwFlags |= PFD_DRAW_TO_BITMAP;
  p->iPixelType = PFD_TYPE_RGBA;
  p->cColorBits = 32;
  p->cRedBits = 8;
  p->cRedShift = 16;
  p->cGreenBits = 8;
  p->cGreenShift = 8;
  p->cBlueBits = 8;
  p->cBlueShift = 0;
  p->cAlphaBits = 8;
  p->cAlphaShift = 24;
  p->cDepthBits = 32;
  p->cStencilBits = 8;
  p->iLayerType = PFD_MAIN_PLANE;
}

// One rendering context: a context of the system that draws into pixels, which are
// sized to the window (or bitmap) it was last made current with.
struct GLContext {
  winport::gl::Native native = NULL;  // the system's context
  int format = 1;
  HDC dc = NULL;      // the DC it was last made current with
  HWND hwnd = NULL;   // that DC's window, or NULL for a memory DC
  int w = 0, h = 0;   // the size of pixels
  std::vector<uint32_t> pixels;
  DWORD lastFlush = 0;   // when glFlush last copied the picture (GetTickCount)
  bool pending = false;  // whether a glFlush skipped the copy since
};

// The state shared by all threads: each DC's (or window's) format and the
// contexts that exist.
struct GLState {
  std::mutex lock;
  std::map<const void*, int> formats;
  std::map<HGLRC, GLContext*> contexts;
};

// The shared state, made on first use and never freed.
GLState& State() {
  static GLState* s = new GLState;
  return *s;
}

// Each thread's current context and DC.
thread_local GLContext* tCurrent = NULL;
thread_local HDC tCurrentDC = NULL;

// The key for a DC's pixel format: its window, or the DC itself for a
// memory DC. A window keeps its format for its whole life.
const void* FormatKey(HDC dc) {
  HWND w = WindowFromDC(dc);
  return w ? (const void*)w : (const void*)dc;
}

// The context for a handle, or NULL.
GLContext* ContextOf(HGLRC rc) {
  GLState& s = State();
  std::lock_guard<std::mutex> g(s.lock);
  auto it = s.contexts.find(rc);
  return it == s.contexts.end() ? NULL : it->second;
}

// The size, in pixels, of what dc draws on: the client area of its
// window, or its selected bitmap.
void TargetSize(HDC dc, HWND hwnd, int* w, int* h) {
  *w = *h = 0;
  if (hwnd) {
    RECT r;
    if (GetClientRect(hwnd, &r)) {
      *w = r.right - r.left;
      *h = r.bottom - r.top;
    }
  } else if (HGDIOBJ bmp = GetCurrentObject(dc, OBJ_BITMAP)) {
    BITMAP b;
    if (GetObjectW(bmp, sizeof b, &b)) {
      *w = b.bmWidth;
      *h = b.bmHeight;
    }
  }
  if (*w < 1) *w = 1;
  if (*h < 1) *h = 1;
}

// Sizes the context's pixels to its target and points the system's
// context at them. It keeps drawing into the old buffer until this is called, so a window that
// changed size gets the new size from its next frame.
void Resize(GLContext* c) {
  int w, h;
  TargetSize(c->dc, c->hwnd, &w, &h);
  if (w == c->w && h == c->h) return;
  c->w = w;
  c->h = h;
  c->pixels.assign((size_t)w * h, 0);
  winport::gl::SetBuffer(c->native, w, h, c->pixels.data());
}

// Copies the context's pixels to dc. CGL and AGL store each pixel as a
// 32-bit value 0xAARRGGBB in the machine's byte order, top row first; a 32-bit
// DIB wants the bytes blue, green, red, alpha.
void Present(GLContext* c, HDC dc) {
  if (c->pixels.empty()) return;
  // A color DC: the pixels go straight into its surface, in one pass. The
  // surface holds 0x00RRGGBB values in the machine's byte order, as CGL
  // and AGL do.
  winport::DCObj* d = winport::DCOf(dc);
  if (d && !winport::IsMetaDC(d) && !winport::IsMonoDC(d)) {
    winport::DrawScope ds(d);
    if (ds.ok) {
      POINT o = d->ToDevice(0, 0);
      RECT box = {o.x, o.y, o.x + c->w, o.y + c->h};
      for (const RECT& clip : ds.clip.Rects()) {
        RECT r = winport::Intersect(clip, box);
        if (winport::RectEmpty(r)) continue;
        for (int y = r.top; y < r.bottom; y++) {
          DWORD* row = ds.px.Row(y + ds.origin.y) + ds.origin.x;
          const uint32_t* src = c->pixels.data() + (size_t)(y - o.y) * c->w - o.x;
          for (int x = r.left; x < r.right; x++) row[x] = src[x] & 0xFFFFFF;
        }
        ds.Dirty(r);
      }
      return;
    }
  }
  BITMAPINFO bi;
  memset(&bi, 0, sizeof bi);
  bi.bmiHeader.biSize = sizeof bi.bmiHeader;
  bi.bmiHeader.biWidth = c->w;
  bi.bmiHeader.biHeight = -c->h;
  bi.bmiHeader.biPlanes = 1;
  bi.bmiHeader.biBitCount = 32;
  bi.bmiHeader.biCompression = BI_RGB;
#if defined(__BIG_ENDIAN__)
  std::vector<uint32_t> out(c->pixels.size());
  for (size_t i = 0; i < out.size(); i++) out[i] = __builtin_bswap32(c->pixels[i]);
  const void* bits = out.data();
#else
  const void* bits = c->pixels.data();
#endif
  SetDIBitsToDevice(dc, 0, 0, (DWORD)c->w, (DWORD)c->h, 0, 0, 0, (UINT)c->h, bits, &bi, DIB_RGB_COLORS);
}

// Copies a context's pixels to its window (through a DC of its own, as
// the DC it drew with can be released by now) or to its memory DC.
void PresentToTarget(GLContext* c) {
  if (c->hwnd) {
    if (!IsWindow(c->hwnd)) return;
    HDC dc = GetDC(c->hwnd);
    Present(c, dc);
    ReleaseDC(c->hwnd, dc);
  } else if (c->dc) {
    Present(c, c->dc);
  }
}

}  // namespace

extern "C" {

// Describes pixel format i of dc and gives the number of formats.
int WINAPI DescribePixelFormat(HDC dc, int i, UINT size, LPPIXELFORMATDESCRIPTOR p) {
  if (!dc) return Unsupported(0);
  if (p && size >= sizeof(PIXELFORMATDESCRIPTOR)) {
    if (i < 1 || i > kFormats) {
      SetLastError(ERROR_INVALID_PARAMETER);
      return 0;
    }
    Describe(i, p);
  }
  return kFormats;
}

// Gives the pixel format that matches want best.
int WINAPI ChoosePixelFormat(HDC dc, const PIXELFORMATDESCRIPTOR* want) {
  if (!dc || !want) {
    SetLastError(ERROR_INVALID_PARAMETER);
    return 0;
  }
  if (want->iPixelType != PFD_TYPE_RGBA) return Unsupported(0);
  // A bitmap or GDI drawing needs the single-buffered format; otherwise
  // double buffering is given unless single buffering is asked for.
  if (want->dwFlags & (PFD_DRAW_TO_BITMAP | PFD_SUPPORT_GDI)) return 2;
  if (want->dwFlags & (PFD_DOUBLEBUFFER | PFD_DOUBLEBUFFER_DONTCARE)) return 1;
  return 2;
}

// Gives the pixel format of dc, or 0 when it has none.
int WINAPI GetPixelFormat(HDC dc) {
  GLState& s = State();
  std::lock_guard<std::mutex> g(s.lock);
  auto it = s.formats.find(FormatKey(dc));
  if (it == s.formats.end()) {
    SetLastError(ERROR_INVALID_PIXEL_FORMAT);
    return 0;
  }
  return it->second;
}

// Sets the pixel format of dc (of its window). It can be set once.
BOOL WINAPI SetPixelFormat(HDC dc, int i, const PIXELFORMATDESCRIPTOR*) {
  if (!dc || i < 1 || i > kFormats) {
    SetLastError(ERROR_INVALID_PIXEL_FORMAT);
    return FALSE;
  }
  GLState& s = State();
  std::lock_guard<std::mutex> g(s.lock);
  int& f = s.formats[FormatKey(dc)];
  // A window's format can be set once only.
  if (f && f != i) {
    SetLastError(ERROR_INVALID_PIXEL_FORMAT);
    return FALSE;
  }
  f = i;
  return TRUE;
}

// Makes a rendering context for the pixel format of dc.
HGLRC WINAPI wglCreateContext(HDC dc) {
  int format = GetPixelFormat(dc);
  if (!format) return NULL;
  winport::gl::Native native = winport::gl::Make(NULL);
  if (!native) return Unsupported((HGLRC)NULL);
  GLContext* c = new GLContext;
  c->native = native;
  c->format = format;
  HGLRC rc = (HGLRC)c;
  GLState& s = State();
  std::lock_guard<std::mutex> g(s.lock);
  s.contexts[rc] = c;
  return rc;
}

// Makes a rendering context for a layer. Only the main plane (0) is there.
HGLRC WINAPI wglCreateLayerContext(HDC dc, int layer) {
  // Only the main plane exists.
  if (layer != 0) return Unsupported((HGLRC)NULL);
  return wglCreateContext(dc);
}

// Deletes a rendering context.
BOOL WINAPI wglDeleteContext(HGLRC rc) {
  GLContext* c = NULL;
  {
    GLState& s = State();
    std::lock_guard<std::mutex> g(s.lock);
    auto it = s.contexts.find(rc);
    if (it == s.contexts.end()) {
      SetLastError(ERROR_INVALID_HANDLE);
      return FALSE;
    }
    c = it->second;
    s.contexts.erase(it);
  }
  if (tCurrent == c) {
    winport::gl::SetCurrent(NULL);
    tCurrent = NULL;
    tCurrentDC = NULL;
  }
  winport::gl::Destroy(c->native);
  delete c;
  return TRUE;
}

// Makes rc the current context of the thread, drawing for dc.
BOOL WINAPI wglMakeCurrent(HDC dc, HGLRC rc) {
  if (!rc) {
    if (tCurrent) winport::gl::RealFlush(false);
    // What a skipped glFlush drew goes to the window now.
    if (tCurrent && tCurrent->pending) {
      tCurrent->pending = false;
      PresentToTarget(tCurrent);
    }
    winport::gl::SetCurrent(NULL);
    tCurrent = NULL;
    tCurrentDC = NULL;
    return TRUE;
  }
  GLContext* c = ContextOf(rc);
  if (!c || !dc) {
    SetLastError(ERROR_INVALID_HANDLE);
    return FALSE;
  }
  c->dc = dc;
  c->hwnd = WindowFromDC(dc);
  Resize(c);
  if (!winport::gl::SetCurrent(c->native)) return Unsupported(FALSE);
  tCurrent = c;
  tCurrentDC = dc;
  return TRUE;
}

// Gives the current context of the thread.
HGLRC WINAPI wglGetCurrentContext(void) { return (HGLRC)tCurrent; }
// Gives the DC of the current context of the thread.
HDC WINAPI wglGetCurrentDC(void) { return tCurrentDC; }

// Copies the state in mask from one context to another.
BOOL WINAPI wglCopyContext(HGLRC from, HGLRC to, UINT mask) {
  GLContext* a = ContextOf(from);
  GLContext* b = ContextOf(to);
  if (!a || !b) {
    SetLastError(ERROR_INVALID_HANDLE);
    return FALSE;
  }
  return winport::gl::Copy(a->native, b->native, mask);
}

// Lets context to use the display lists and textures of from.
BOOL WINAPI wglShareLists(HGLRC from, HGLRC to) {
  // CGL and AGL share lists only when a context is made, so the second
  // context is made again, sharing with the first. As on Windows, that is allowed
  // only before the second context has lists of its own.
  GLContext* a = ContextOf(from);
  GLContext* b = ContextOf(to);
  if (!a || !b || b == tCurrent) {
    SetLastError(ERROR_INVALID_HANDLE);
    return FALSE;
  }
  winport::gl::Native n = winport::gl::Make(a->native);
  if (!n) return Unsupported(FALSE);
  winport::gl::Destroy(b->native);
  b->native = n;
  b->w = b->h = 0;
  b->pixels.clear();
  return TRUE;
}

// Gives the address of an OpenGL function by its name.
PROC WINAPI wglGetProcAddress(LPCSTR name) {
  if (!name) return NULL;
  return (PROC)winport::gl::FindProc(name);
}

// Copies the picture of the context of dc to the window.
BOOL WINAPI SwapBuffers(HDC dc) {
  HWND hwnd = WindowFromDC(dc);
  GLContext* c = NULL;
  if (tCurrent && (tCurrent->dc == dc || (hwnd && tCurrent->hwnd == hwnd))) {
    c = tCurrent;
  } else {
    // A context of another thread, or one that is not current: the last
    // one made current with this window or DC.
    GLState& s = State();
    std::lock_guard<std::mutex> g(s.lock);
    for (auto& e : s.contexts)
      if (e.second->dc == dc || (hwnd && e.second->hwnd == hwnd)) c = e.second;
  }
  if (!c) {
    SetLastError(ERROR_INVALID_HANDLE);
    return FALSE;
  }
  if (c == tCurrent) winport::gl::RealFlush(true);
  Present(c, dc);
  // Follow a change in the window's size from the next frame on.
  if (c == tCurrent) {
    int w, h;
    TargetSize(c->dc, c->hwnd, &w, &h);
    if (w != c->w || h != c->h) {
      Resize(c);
      winport::gl::SetCurrent(c->native);
    }
  }
  return TRUE;
}

// glFlush and glFinish: the renderer's, then, for a single-buffered
// format, a copy of the picture to the window, as GDI's generic OpenGL
// does. A program linked with libwinport ahead of the OpenGL framework
// calls these.
void glFlush(void) {
  winport::gl::RealFlush(false);
  GLContext* c = tCurrent;
  if (!c || c->format == 1) return;
  // A program can flush many times for one picture (a screen saver that
  // clears the screen in small steps). The picture goes to the window at
  // most every 20 ms; glFinish, SwapBuffers and the next flush after that
  // time copy what a skipped flush drew.
  DWORD now = GetTickCount();
  if (now - c->lastFlush < 20) {
    c->pending = true;
    return;
  }
  c->lastFlush = now;
  c->pending = false;
  PresentToTarget(c);
}

// Waits for the renderer, then copies the picture to the window.
void glFinish(void) {
  winport::gl::RealFlush(true);
  GLContext* c = tCurrent;
  if (!c || c->format == 1) return;
  c->lastFlush = GetTickCount();
  c->pending = false;
  PresentToTarget(c);
}

}  // extern "C"

namespace {

// Makes display lists listBase... from the characters first... of dc's
// font, each a glBitmap of the character followed by its advance.
BOOL UseFontBitmaps(HDC dc, DWORD first, DWORD count, DWORD listBase, bool wide) {
  if (!tCurrent) {
    SetLastError(ERROR_INVALID_HANDLE);
    return FALSE;
  }
  TEXTMETRICW tm;
  if (!GetTextMetricsW(dc, &tm)) return FALSE;
  int h = tm.tmHeight;
  HDC mem = CreateCompatibleDC(dc);
  SelectObject(mem, GetCurrentObject(dc, OBJ_FONT));
  SetTextColor(mem, RGB(255, 255, 255));
  SetBkColor(mem, RGB(0, 0, 0));
  SetBkMode(mem, OPAQUE);
  GLint unpack;
  glGetIntegerv(GL_UNPACK_ALIGNMENT, &unpack);
  glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
  for (DWORD i = 0; i < count; i++) {
    UINT ch = first + i;
    INT adv = 0;
    if (wide ? !GetCharWidth32W(dc, ch, ch, &adv) : !GetCharWidth32A(dc, ch, ch, &adv)) adv = tm.tmAveCharWidth;
    int w = adv > 0 ? adv : 1;
    // The character in white on black, top row first.
    BITMAPINFO bi;
    memset(&bi, 0, sizeof bi);
    bi.bmiHeader.biSize = sizeof bi.bmiHeader;
    bi.bmiHeader.biWidth = w;
    bi.bmiHeader.biHeight = -h;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    void* bits = NULL;
    HBITMAP bmp = CreateDIBSection(mem, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
    if (!bmp) continue;
    HGDIOBJ old = SelectObject(mem, bmp);
    RECT r = {0, 0, w, h};
    if (wide) {
      WCHAR c = (WCHAR)ch;
      ExtTextOutW(mem, 0, 0, ETO_OPAQUE, &r, &c, 1, NULL);
    } else {
      char c = (char)ch;
      ExtTextOutA(mem, 0, 0, ETO_OPAQUE, &r, &c, 1, NULL);
    }
    GdiFlush();
    // glBitmap wants one bit for each pixel, bottom row first.
    int stride = (w + 7) / 8;
    std::vector<GLubyte> mask((size_t)stride * h, 0);
    const uint32_t* px = (const uint32_t*)bits;
    for (int y = 0; y < h; y++)
      for (int x = 0; x < w; x++)
        if ((px[(size_t)y * w + x] & 0xFF00) >= 0x8000)
          mask[(size_t)(h - 1 - y) * stride + x / 8] |= (GLubyte)(0x80 >> (x % 8));
    SelectObject(mem, old);
    DeleteObject(bmp);
    glNewList(listBase + i, GL_COMPILE);
    glBitmap(w, h, 0, (GLfloat)tm.tmDescent, (GLfloat)adv, 0, mask.data());
    glEndList();
  }
  glPixelStorei(GL_UNPACK_ALIGNMENT, unpack);
  DeleteDC(mem);
  return TRUE;
}

// A point of a flattened glyph contour, in em units.
struct Pt {
  GLdouble x, y, z;
};

// Converts 16.16 fixed point to a double.
double Fx(FIXED f) { return f.value + f.fract / 65536.0; }

// Reads a GGO_NATIVE outline into closed polylines, scaled by scale.
// Quadratic splines are cut into straight pieces no further than
// deviation (in em units) from the curve.
void Flatten(const BYTE* data, DWORD size, double scale, double deviation,
             std::vector<std::vector<Pt>>* out) {
  const BYTE* p = data;
  const BYTE* end = data + size;
  // Segments for one quadratic curve from its size and the tolerance.
  auto steps = [&](double ax, double ay, double bx, double by, double cx, double cy) {
    double dx = ax - 2 * bx + cx, dy = ay - 2 * by + cy;
    double d = sqrt(dx * dx + dy * dy) / 4;
    int n = 1;
    if (deviation > 0)
      while (n < 64 && d / ((double)n * n) > deviation) n *= 2;
    else
      n = 8;
    return n;
  };
  while (p + sizeof(TTPOLYGONHEADER) <= end) {
    TTPOLYGONHEADER h;
    memcpy(&h, p, sizeof h);
    if (h.cb < sizeof h || p + h.cb > end) break;
    const BYTE* q = p + sizeof h;
    const BYTE* cend = p + h.cb;
    out->emplace_back();
    std::vector<Pt>& c = out->back();
    Pt cur = {Fx(h.pfxStart.x) * scale, Fx(h.pfxStart.y) * scale, 0};
    c.push_back(cur);
    while (q + 4 <= cend) {
      WORD type, n;
      memcpy(&type, q, 2);
      memcpy(&n, q + 2, 2);
      const BYTE* pts = q + 4;
      if (pts + (size_t)n * sizeof(POINTFX) > cend) break;
      std::vector<Pt> v(n);
      for (WORD i = 0; i < n; i++) {
        POINTFX f;
        memcpy(&f, pts + (size_t)i * sizeof f, sizeof f);
        v[i].x = Fx(f.x) * scale;
        v[i].y = Fx(f.y) * scale;
        v[i].z = 0;
      }
      if (type == TT_PRIM_LINE) {
        for (const Pt& t : v) c.push_back(t);
        if (n) cur = v[n - 1];
      } else if (type == TT_PRIM_QSPLINE && n) {
        // Control points; between two of them lies an implied end point.
        for (WORD i = 0; i + 1 < n; i++) {
          Pt b = v[i];
          Pt e = i + 2 < n ? Pt{(v[i].x + v[i + 1].x) / 2, (v[i].y + v[i + 1].y) / 2, 0} : v[i + 1];
          int k = steps(cur.x, cur.y, b.x, b.y, e.x, e.y);
          for (int j = 1; j <= k; j++) {
            double t = (double)j / k, u = 1 - t;
            Pt r = {u * u * cur.x + 2 * u * t * b.x + t * t * e.x, u * u * cur.y + 2 * u * t * b.y + t * t * e.y, 0};
            c.push_back(r);
          }
          cur = e;
        }
        if (n == 1) {
          c.push_back(v[0]);
          cur = v[0];
        }
      }
      q = pts + (size_t)n * sizeof(POINTFX);
    }
    // The contour closes on its start.
    if (c.size() > 1 && c.back().x == c.front().x && c.back().y == c.front().y) c.pop_back();
    if (c.size() < 3) out->pop_back();
    p += h.cb;
  }
}

// The GLU tessellator callbacks: they pass the triangles straight to GL.
// Vertices made at crossings are kept until the glyph is done.
std::vector<Pt*>* gCombined = NULL;
GLdouble gFaceZ = 0;

// Begins a primitive of the tessellator.
void CALLBACK TessBegin(GLenum type) { glBegin(type); }
// Ends a primitive of the tessellator.
void CALLBACK TessEnd() { glEnd(); }
// Gives a vertex of the tessellator to OpenGL.
void CALLBACK TessVertex(void* v) {
  const Pt* p = (const Pt*)v;
  glVertex3d(p->x, p->y, gFaceZ);
}
// Makes a new vertex where the edges of the tessellator cross.
void CALLBACK TessCombine(GLdouble c[3], void*[4], GLfloat[4], void** out) {
  Pt* p = new Pt{c[0], c[1], c[2]};
  gCombined->push_back(p);
  *out = p;
}

// Draws the inside of the contours at depth z, facing the way of normalZ.
void Face(GLUtesselator* tess, std::vector<std::vector<Pt>>& contours, GLdouble z, GLdouble normalZ) {
  gFaceZ = z;
  glNormal3d(0, 0, normalZ);
  gluTessNormal(tess, 0, 0, normalZ);
  gluTessBeginPolygon(tess, NULL);
  for (auto& c : contours) {
    gluTessBeginContour(tess);
    for (Pt& p : c) gluTessVertex(tess, &p.x, &p);
    gluTessEndContour(tess);
  }
  gluTessEndPolygon(tess);
}

// Makes display lists of 3-D characters from the outlines of dc's font:
// the front face at z = 0, and, with extrusion, the back face at
// z = -extrusion and the sides. Units are ems: the font's em height is 1.
// Each list ends by moving the pen by the character's advance.
BOOL UseFontOutlines(HDC dc, DWORD first, DWORD count, DWORD listBase, FLOAT deviation, FLOAT extrusion,
                     int format, LPGLYPHMETRICSFLOAT gmf, bool wide) {
  if (!tCurrent) {
    SetLastError(ERROR_INVALID_HANDLE);
    return FALSE;
  }
  OUTLINETEXTMETRICW otm;
  if (!GetOutlineTextMetricsW(dc, sizeof otm, &otm)) {
    SetLastError(ERROR_NOT_SUPPORTED);
    return FALSE;
  }
  TEXTMETRICW& tm = otm.otmTextMetrics;
  double em = tm.tmHeight - tm.tmInternalLeading;
  if (em <= 0) em = tm.tmHeight > 0 ? tm.tmHeight : 1;
  double scale = 1.0 / em;
  MAT2 ident = {{0, 1}, {0, 0}, {0, 0}, {0, 1}};
  GLUtesselator* tess = format == WGL_FONT_POLYGONS ? gluNewTess() : NULL;
  std::vector<Pt*> combined;
  gCombined = &combined;
  if (tess) {
    typedef GLvoid (*TessFn)();
    gluTessCallback(tess, GLU_TESS_BEGIN, (TessFn)TessBegin);
    gluTessCallback(tess, GLU_TESS_END, (TessFn)TessEnd);
    gluTessCallback(tess, GLU_TESS_VERTEX, (TessFn)TessVertex);
    gluTessCallback(tess, GLU_TESS_COMBINE, (TessFn)TessCombine);
    gluTessProperty(tess, GLU_TESS_WINDING_RULE, GLU_TESS_WINDING_NONZERO);
  }
  for (DWORD i = 0; i < count; i++) {
    UINT ch = first + i;
    GLYPHMETRICS gm;
    DWORD size = wide ? GetGlyphOutlineW(dc, ch, GGO_NATIVE, &gm, 0, NULL, &ident)
                      : GetGlyphOutlineA(dc, ch, GGO_NATIVE, &gm, 0, NULL, &ident);
    std::vector<std::vector<Pt>> contours;
    if (size != GDI_ERROR && size > 0) {
      std::vector<BYTE> data(size);
      DWORD got = wide ? GetGlyphOutlineW(dc, ch, GGO_NATIVE, &gm, size, data.data(), &ident)
                       : GetGlyphOutlineA(dc, ch, GGO_NATIVE, &gm, size, data.data(), &ident);
      if (got != GDI_ERROR) Flatten(data.data(), got, scale, deviation, &contours);
    } else if (size == GDI_ERROR) {
      if (wide ? GetGlyphOutlineW(dc, ch, GGO_METRICS, &gm, 0, NULL, &ident) == GDI_ERROR
               : GetGlyphOutlineA(dc, ch, GGO_METRICS, &gm, 0, NULL, &ident) == GDI_ERROR)
        memset(&gm, 0, sizeof gm);
    }
    if (gmf) {
      gmf[i].gmfBlackBoxX = (FLOAT)(gm.gmBlackBoxX * scale);
      gmf[i].gmfBlackBoxY = (FLOAT)(gm.gmBlackBoxY * scale);
      gmf[i].gmfptGlyphOrigin.x = (FLOAT)(gm.gmptGlyphOrigin.x * scale);
      gmf[i].gmfptGlyphOrigin.y = (FLOAT)(gm.gmptGlyphOrigin.y * scale);
      gmf[i].gmfCellIncX = (FLOAT)(gm.gmCellIncX * scale);
      gmf[i].gmfCellIncY = (FLOAT)(gm.gmCellIncY * scale);
    }
    glNewList(listBase + i, GL_COMPILE);
    if (format == WGL_FONT_POLYGONS) {
      Face(tess, contours, 0, 1);
      if (extrusion != 0) {
        Face(tess, contours, -extrusion, -1);
        // The sides: a quad for each edge, facing outward.
        for (auto& c : contours) {
          glBegin(GL_QUADS);
          for (size_t k = 0; k < c.size(); k++) {
            const Pt& a = c[k];
            const Pt& b = c[(k + 1) % c.size()];
            double nx = b.y - a.y, ny = -(b.x - a.x);
            double len = sqrt(nx * nx + ny * ny);
            if (len > 0) glNormal3d(nx / len, ny / len, 0);
            glVertex3d(a.x, a.y, 0);
            glVertex3d(a.x, a.y, -extrusion);
            glVertex3d(b.x, b.y, -extrusion);
            glVertex3d(b.x, b.y, 0);
          }
          glEnd();
        }
      }
    } else {
      // WGL_FONT_LINES: the outlines, front and back.
      for (int side = 0; side < (extrusion != 0 ? 2 : 1); side++)
        for (auto& c : contours) {
          glBegin(GL_LINE_LOOP);
          for (const Pt& p : c) glVertex3d(p.x, p.y, side ? -extrusion : 0);
          glEnd();
        }
    }
    glTranslated(gm.gmCellIncX * scale, gm.gmCellIncY * scale, 0);
    glEndList();
  }
  if (tess) gluDeleteTess(tess);
  for (Pt* p : combined) delete p;
  gCombined = NULL;
  return TRUE;
}

}  // namespace

extern "C" {

// Makes display lists of the glyph bitmaps of the font of dc.
BOOL WINAPI wglUseFontBitmapsA(HDC dc, DWORD first, DWORD count, DWORD listBase) {
  return UseFontBitmaps(dc, first, count, listBase, false);
}
// The Unicode form of wglUseFontBitmapsA.
BOOL WINAPI wglUseFontBitmapsW(HDC dc, DWORD first, DWORD count, DWORD listBase) {
  return UseFontBitmaps(dc, first, count, listBase, true);
}
BOOL WINAPI wglUseFontOutlinesA(HDC dc, DWORD first, DWORD count, DWORD listBase, FLOAT deviation,
                                FLOAT extrusion, int format, LPGLYPHMETRICSFLOAT gmf) {
  return UseFontOutlines(dc, first, count, listBase, deviation, extrusion, format, gmf, false);
}
BOOL WINAPI wglUseFontOutlinesW(HDC dc, DWORD first, DWORD count, DWORD listBase, FLOAT deviation,
                                FLOAT extrusion, int format, LPGLYPHMETRICSFLOAT gmf) {
  return UseFontOutlines(dc, first, count, listBase, deviation, extrusion, format, gmf, true);
}

}  // extern "C"

#endif  // WINPORT_GL

extern "C" {

// Layer planes (overlays and underlays) are not offered on any system.
BOOL WINAPI wglDescribeLayerPlane(HDC, int, int, UINT, LPLAYERPLANEDESCRIPTOR) { return Unsupported(FALSE); }
int WINAPI wglSetLayerPaletteEntries(HDC, int, int, int, const COLORREF*) { return Unsupported(0); }
int WINAPI wglGetLayerPaletteEntries(HDC, int, int, int, COLORREF*) { return Unsupported(0); }
BOOL WINAPI wglRealizeLayerPalette(HDC, int, BOOL) { return Unsupported(FALSE); }
// Swaps the buffers of the layers in planes; only the main plane is there.
BOOL WINAPI wglSwapLayerBuffers(HDC dc, UINT planes) {
  // The main plane alone is SwapBuffers.
  if (planes & 1) return SwapBuffers(dc);
  return Unsupported(FALSE);
}

}  // extern "C"
