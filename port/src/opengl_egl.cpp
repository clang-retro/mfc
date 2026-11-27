// opengl_egl.cpp - the system's OpenGL on Linux, through EGL, under the
// wgl functions of opengl.cpp.
//
// A Linux system with an OpenGL driver (Mesa or a vendor's) has the
// function of an installable client driver (ICD) of Windows: when its
// renderer is hardware, opengl_soft.cpp makes the contexts here instead
// of with the generic implementation. libEGL.so.1 is loaded when OpenGL
// is first used, so that a program needs no OpenGL library to start.
//
// Each context draws into an EGL pbuffer of the size of its window or
// bitmap, made on a display of no window system (EGL_MESA_platform_
// surfaceless, else EGL_EXT_platform_device, else the default display).
// Fetch reads the pbuffer back into the buffer that opengl.cpp gives,
// which opengl.cpp copies to the window with GDI, as for the other
// backends. So the picture goes through the same drawing path on X11,
// Wayland and the headless backend.
//
// The gl functions of the program are those of port/softgl (dispatch.c),
// which call through the thread's dispatch table, sgl_cur. A context
// here makes that table one that points at the driver's functions, found
// by name: the order of the entries comes from port/softgl/src/
// dispatch.h (gl_names.inc, made by the makefile).
//
// WINPORT_OPENGL chooses: "generic" (or "software") always draws with
// the generic implementation; "system" uses the driver even when its
// renderer is software (llvmpipe); otherwise the driver is used when its
// renderer is hardware.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#include "opengl_backend.h"

#ifdef WINPORT_EGL

#include <GL/gl.h>
#include <dlfcn.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <mutex>
#include <string>
#include <utility>
#include <vector>

extern "C" {
// A cell of a display list of port/softgl (sgl.h), which dispatch.h names.
typedef union sgl_cell sgl_cell;
#include "../softgl/src/dispatch.h"
}

#ifndef GL_BGRA
#define GL_BGRA 0x80E1
#endif
#ifndef GL_UNSIGNED_INT_8_8_8_8_REV
#define GL_UNSIGNED_INT_8_8_8_8_REV 0x8367
#endif

// The thread's dispatch table of port/softgl (context.c).
extern "C" __thread const struct sgl_dispatch* sgl_cur;

namespace winport {
namespace gl {
namespace egl {

namespace {

// The EGL types and values used here (the EGL 1.4 and 1.5 headers).
typedef void* Display;
typedef void* Config;
typedef void* Context;
typedef void* Surface;
typedef void* Device;
typedef int32_t Int;
typedef unsigned Bool;

const Int kNone = 0x3038;
const Int kAlphaSize = 0x3021;
const Int kBlueSize = 0x3022;
const Int kGreenSize = 0x3023;
const Int kRedSize = 0x3024;
const Int kDepthSize = 0x3025;
const Int kStencilSize = 0x3026;
const Int kSurfaceType = 0x3033;
const Int kRenderableType = 0x3040;
const Int kHeight = 0x3056;
const Int kWidth = 0x3057;
const Int kExtensions = 0x3055;
const Int kDraw = 0x3059;
const Int kRead = 0x305A;
const Int kPbufferBit = 0x0001;
const Int kOpenGLBit = 0x0008;
const unsigned kOpenGLApi = 0x30A2;
const Int kPlatformDevice = 0x313F;
const Int kPlatformSurfaceless = 0x31DD;

// The functions of libEGL.so.1.
struct Lib {
  void* (*GetProcAddress)(const char*);
  Display (*GetDisplay)(void*);
  Bool (*Initialize)(Display, Int*, Int*);
  const char* (*QueryString)(Display, Int);
  Bool (*ChooseConfig)(Display, const Int*, Config*, Int, Int*);
  Bool (*GetConfigAttrib)(Display, Config, Int, Int*);
  Bool (*BindAPI)(unsigned);
  Context (*CreateContext)(Display, Config, Context, const Int*);
  Bool (*DestroyContext)(Display, Context);
  Surface (*CreatePbufferSurface)(Display, Config, const Int*);
  Bool (*DestroySurface)(Display, Surface);
  Bool (*MakeCurrent)(Display, Surface, Surface, Context);
  Context (*GetCurrentContext)(void);
  Surface (*GetCurrentSurface)(Int);
  Display (*GetCurrentDisplay)(void);
  // The extensions of the client.
  Display (*GetPlatformDisplay)(Int, void*, const Int*);
  Bool (*QueryDevices)(Int, Device*, Int*);
};

// What was found when OpenGL was first used.
struct System {
  Lib egl;
  Display dpy = NULL;
  Config cfg = NULL;
  bool use = false;      // whether contexts are made here
  bool hardware = false; // whether the renderer is hardware
  int depth = 0, stencil = 0;
  sgl_dispatch table;    // the driver's functions
  std::string extensions;  // GL_EXTENSIONS of the driver
  // How Fetch reads: the framebuffer objects of OpenGL 3.0 (or of the
  // ARB extension), those of the EXT extension, and pixel buffer objects.
  bool fbo = false, fboExt = false, pbo = false;
  void(APIENTRY* bindFramebuffer)(GLenum, GLuint) = NULL;
  void(APIENTRY* bindBuffer)(GLenum, GLuint) = NULL;
};

// One context of the driver.
struct Ctx {
  Context ctx = NULL;
  Surface surf = NULL;
  int w = 0, h = 0;
  uint32_t* pixels = NULL;
  GLint readBuffer = 0;  // the buffer that the context reads at first
};

// The names of the entries of sgl_dispatch, in order.
const char* const kNames[] = {
#include "gl_names.inc"
};
static_assert(sizeof(kNames) / sizeof(kNames[0]) * sizeof(void*) == sizeof(sgl_dispatch),
              "gl_names.inc does not match port/softgl/src/dispatch.h");

template <class F>
bool Load(void* lib, F& f, const char* name) {
  f = (F)dlsym(lib, name);
  return f != NULL;
}

// Opens libEGL.so.1. Gives false when there is none.
bool OpenLib(Lib& l) {
  void* lib = dlopen("libEGL.so.1", RTLD_NOW | RTLD_LOCAL);
  if (!lib) return false;
  bool ok = Load(lib, l.GetProcAddress, "eglGetProcAddress") && Load(lib, l.GetDisplay, "eglGetDisplay") &&
            Load(lib, l.Initialize, "eglInitialize") && Load(lib, l.QueryString, "eglQueryString") &&
            Load(lib, l.ChooseConfig, "eglChooseConfig") &&
            Load(lib, l.GetConfigAttrib, "eglGetConfigAttrib") && Load(lib, l.BindAPI, "eglBindAPI") &&
            Load(lib, l.CreateContext, "eglCreateContext") &&
            Load(lib, l.DestroyContext, "eglDestroyContext") &&
            Load(lib, l.CreatePbufferSurface, "eglCreatePbufferSurface") &&
            Load(lib, l.DestroySurface, "eglDestroySurface") && Load(lib, l.MakeCurrent, "eglMakeCurrent") &&
            Load(lib, l.GetCurrentContext, "eglGetCurrentContext") &&
            Load(lib, l.GetCurrentSurface, "eglGetCurrentSurface") &&
            Load(lib, l.GetCurrentDisplay, "eglGetCurrentDisplay");
  if (!ok) {
    dlclose(lib);
    return false;
  }
  l.GetPlatformDisplay = (Display(*)(Int, void*, const Int*))l.GetProcAddress("eglGetPlatformDisplayEXT");
  l.QueryDevices = (Bool(*)(Int, Device*, Int*))l.GetProcAddress("eglQueryDevicesEXT");
  return true;
}

// Whether the space-separated list has word.
bool HasWord(const char* list, const char* word) {
  if (!list) return false;
  size_t n = strlen(word);
  for (const char* p = list; (p = strstr(p, word)) != NULL; p += n)
    if ((p == list || p[-1] == ' ') && (p[n] == ' ' || p[n] == 0)) return true;
  return false;
}

// Initializes dpy and chooses its config for pbuffers of desktop OpenGL:
// 8 bits of red, green, blue and alpha, a depth buffer and, if there is
// one, a stencil buffer.
bool Choose(System& s, Display dpy) {
  if (!dpy) return false;
  Int major = 0, minor = 0;
  if (!s.egl.Initialize(dpy, &major, &minor)) return false;
  if (!s.egl.BindAPI(kOpenGLApi)) return false;
  const Int depths[][2] = {{24, 8}, {24, 0}, {16, 0}};
  for (auto& d : depths) {
    Int attr[] = {kSurfaceType, kPbufferBit, kRenderableType, kOpenGLBit, kRedSize,    8, kGreenSize,
                  8,            kBlueSize,   8,               kAlphaSize, 8,           kDepthSize, d[0],
                  kStencilSize, d[1],        kNone};
    Config cfg = NULL;
    Int n = 0;
    if (s.egl.ChooseConfig(dpy, attr, &cfg, 1, &n) && n > 0) {
      s.dpy = dpy;
      s.cfg = cfg;
      s.egl.GetConfigAttrib(dpy, cfg, kDepthSize, &major);
      s.egl.GetConfigAttrib(dpy, cfg, kStencilSize, &minor);
      s.depth = major;
      s.stencil = minor;
      return true;
    }
  }
  return false;
}

// The displays to look for OpenGL on, best first: one of no window
// system (Mesa), each device (the vendors' drivers), and the default.
std::vector<Display> Displays(System& s) {
  std::vector<Display> out;
  const char* client = s.egl.QueryString(NULL, kExtensions);
  if (s.egl.GetPlatformDisplay && HasWord(client, "EGL_MESA_platform_surfaceless"))
    out.push_back(s.egl.GetPlatformDisplay(kPlatformSurfaceless, NULL, NULL));
  if (s.egl.GetPlatformDisplay && s.egl.QueryDevices && HasWord(client, "EGL_EXT_platform_device")) {
    Device devs[8];
    Int n = 0;
    if (s.egl.QueryDevices(8, devs, &n))
      for (Int i = 0; i < n && i < 8; i++) out.push_back(s.egl.GetPlatformDisplay(kPlatformDevice, devs[i], NULL));
  }
  out.push_back(s.egl.GetDisplay(NULL));
  return out;
}

// The thread's current EGL state, to put back after the probe.
struct Saved {
  Display dpy;
  Context ctx;
  Surface draw, read;
};

Saved Save(const Lib& l) {
  return {l.GetCurrentDisplay(), l.GetCurrentContext(), l.GetCurrentSurface(kDraw), l.GetCurrentSurface(kRead)};
}

void Restore(const Lib& l, const Saved& v, Display ours) {
  if (v.dpy)
    l.MakeCurrent(v.dpy, v.draw, v.read, v.ctx);
  else
    l.MakeCurrent(ours, NULL, NULL, NULL);
}

// The value of a state of the driver, as an integer.
GLint Int1(System& s, GLenum e) {
  GLint v = 0;
  s.table.GetIntegerv(e, &v);
  return v;
}

// Whether the driver's version is at least major.minor.
bool Version(System& s, int major, int minor) {
  const char* v = (const char*)s.table.GetString(GL_VERSION);
  int a = 0, b = 0;
  if (!v || sscanf(v, "%d.%d", &a, &b) < 1) return false;
  return a > major || (a == major && b >= minor);
}

bool HasExt(System& s, const char* name) { return HasWord(s.extensions.c_str(), name); }

// Whether a renderer name is that of a software renderer.
bool IsSoftware(const char* renderer) {
  static const char* const kSoft[] = {"llvmpipe", "softpipe", "Software", "software", "swrast", "SWR"};
  if (!renderer) return true;
  for (const char* k : kSoft)
    if (strstr(renderer, k)) return true;
  return false;
}

// Fills the dispatch table with the driver's functions. An entry that the
// driver does not have does nothing, as with no context.
void Fill(System& s) {
  void** out = (void**)&s.table;
  void* const* nop = (void* const*)&sgl_nop_table;
  for (size_t i = 0; i < sizeof(kNames) / sizeof(kNames[0]); i++) {
    void* f = s.egl.GetProcAddress(kNames[i]);
    out[i] = f ? f : nop[i];
  }
}

// Makes a context on the display that Choose chose and reads its
// renderer and extensions. Gives -1 when no context can be made, 0 for a
// software renderer and 1 for hardware.
int Look(System& s) {
  Int pb[] = {kWidth, 1, kHeight, 1, kNone};
  Surface surf = s.egl.CreatePbufferSurface(s.dpy, s.cfg, pb);
  Context ctx = s.egl.CreateContext(s.dpy, s.cfg, NULL, NULL);
  int kind = -1;
  if (surf && ctx) {
    Saved v = Save(s.egl);
    if (s.egl.MakeCurrent(s.dpy, surf, surf, ctx)) {
      const char* renderer = (const char*)s.table.GetString(GL_RENDERER);
      const char* ext = (const char*)s.table.GetString(GL_EXTENSIONS);
      s.extensions = ext ? ext : "";
      if (renderer) kind = IsSoftware(renderer) ? 0 : 1;
      s.fbo = Version(s, 3, 0) || HasExt(s, "GL_ARB_framebuffer_object");
      s.fboExt = !s.fbo && HasExt(s, "GL_EXT_framebuffer_object");
      s.pbo = Version(s, 2, 1) || HasExt(s, "GL_ARB_pixel_buffer_object");
      typedef void(APIENTRY * BindFn)(GLenum, GLuint);
      s.bindFramebuffer = NULL;
      s.bindBuffer = NULL;
      if (s.fbo || s.fboExt)
        s.bindFramebuffer = (BindFn)s.egl.GetProcAddress(s.fbo ? "glBindFramebuffer" : "glBindFramebufferEXT");
      if (s.pbo) s.bindBuffer = (BindFn)s.egl.GetProcAddress("glBindBuffer");
      Restore(s.egl, v, s.dpy);
    }
  }
  if (ctx) s.egl.DestroyContext(s.dpy, ctx);
  if (surf) s.egl.DestroySurface(s.dpy, surf);
  return kind;
}

// Looks at the driver once: the first display whose renderer is
// hardware, or, with WINPORT_OPENGL=system, the first one that draws.
System* Probe() {
  const char* want = getenv("WINPORT_OPENGL");
  std::string mode = want ? want : "";
  if (mode == "generic" || mode == "software") return NULL;
  System* s = new System;
  if (!OpenLib(s->egl)) {
    delete s;
    return NULL;
  }
  Fill(*s);
  Display soft = NULL;
  std::vector<Display> displays = Displays(*s);
  for (size_t i = 0; i < displays.size(); i++) {
    Display d = displays[i];
    // The default display (of X11 or Wayland) only when no other has
    // OpenGL: it finds the same drivers, and warns when it has no
    // hardware.
    if (i + 1 == displays.size() && i > 0 && soft) break;
    if (!Choose(*s, d)) continue;
    int kind = Look(*s);
    if (kind == 1) {
      s->hardware = s->use = true;
      return s;
    }
    if (kind == 0 && !soft) soft = d;
  }
  if (soft && mode == "system" && Choose(*s, soft) && Look(*s) == 0) s->use = true;
  return s;
}

// The driver, looked at on first use. NULL or !use: the generic
// implementation draws.
System* Sys() {
  static System* s = NULL;
  static std::once_flag once;
  std::call_once(once, [] { s = Probe(); });
  return s && s->use ? s : NULL;
}

// The context of this thread that is current here, or NULL.
thread_local Ctx* tCurrent = NULL;

// Makes c current with its pbuffer.
bool Bind(System& s, Ctx* c) {
  s.egl.BindAPI(kOpenGLApi);
  if (!s.egl.MakeCurrent(s.dpy, c->surf, c->surf, c->ctx)) return false;
  tCurrent = c;
  sgl_cur = &s.table;
  return true;
}

}  // namespace

bool Use() { return Sys() != NULL; }

bool Hardware() {
  System* s = Sys();
  return s && s->hardware;
}

int DepthBits() {
  System* s = Sys();
  return s ? s->depth : 0;
}

int StencilBits() {
  System* s = Sys();
  return s ? s->stencil : 0;
}

void* Make(void* share) {
  System* s = Sys();
  if (!s) return NULL;
  s->egl.BindAPI(kOpenGLApi);
  Context ctx = s->egl.CreateContext(s->dpy, s->cfg, share ? ((Ctx*)share)->ctx : NULL, NULL);
  if (!ctx) return NULL;
  Ctx* c = new Ctx;
  c->ctx = ctx;
  return c;
}

void Destroy(void* p) {
  System* s = Sys();
  Ctx* c = (Ctx*)p;
  if (!s || !c) return;
  if (tCurrent == c) SetCurrent(NULL);
  s->egl.DestroyContext(s->dpy, c->ctx);
  if (c->surf) s->egl.DestroySurface(s->dpy, c->surf);
  delete c;
}

bool SetCurrent(void* p) {
  System* s = Sys();
  Ctx* c = (Ctx*)p;
  if (!c) {
    if (s && tCurrent) {
      s->egl.BindAPI(kOpenGLApi);
      s->egl.MakeCurrent(s->dpy, NULL, NULL, NULL);
    }
    tCurrent = NULL;
    return true;
  }
  if (!s || !c->surf) return false;
  bool first = c->readBuffer == 0;
  if (!Bind(*s, c)) return false;
  if (first) c->readBuffer = Int1(*s, GL_READ_BUFFER);
  return true;
}

bool SetBuffer(void* p, int w, int h, void* pixels) {
  System* s = Sys();
  Ctx* c = (Ctx*)p;
  if (!s || !c) return false;
  c->pixels = (uint32_t*)pixels;
  if (c->surf && c->w == w && c->h == h) return true;
  Int pb[] = {kWidth, w, kHeight, h, kNone};
  Surface surf = s->egl.CreatePbufferSurface(s->dpy, s->cfg, pb);
  if (!surf) return false;
  Surface old = c->surf;
  c->surf = surf;
  c->w = w;
  c->h = h;
  // A context that is current draws into the new pbuffer from now on.
  if (tCurrent == c) Bind(*s, c);
  if (old) s->egl.DestroySurface(s->dpy, old);
  return true;
}

void Flush(bool finish) {
  System* s = Sys();
  if (!s || !tCurrent) return;
  if (finish)
    s->table.Finish();
  else
    s->table.Flush();
}

// Reads the pbuffer into the buffer, top row first. The states of the
// program that change how pixels are read are put back after.
void Fetch(void* p) {
  System* s = Sys();
  Ctx* c = (Ctx*)p;
  if (!s || !c || c != tCurrent || !c->pixels || c->w < 1 || c->h < 1) return;
  sgl_dispatch& t = s->table;
  // The framebuffer and pixel buffer object of the program.
  bool fbo = s->fbo;
  GLint readFb = 0, packBuf = 0;
  if ((s->fbo || s->fboExt) && s->bindFramebuffer) {
    readFb = Int1(*s, fbo ? 0x8CAA /* GL_READ_FRAMEBUFFER_BINDING */ : 0x8CA6 /* GL_FRAMEBUFFER_BINDING_EXT */);
    if (readFb) s->bindFramebuffer(fbo ? 0x8CA8 /* GL_READ_FRAMEBUFFER */ : 0x8D40 /* GL_FRAMEBUFFER_EXT */, 0);
  }
  if (s->pbo && s->bindBuffer) {
    packBuf = Int1(*s, 0x88ED /* GL_PIXEL_PACK_BUFFER_BINDING */);
    if (packBuf) s->bindBuffer(0x88EB /* GL_PIXEL_PACK_BUFFER */, 0);
  }
  static const GLenum kStore[] = {GL_PACK_SWAP_BYTES, GL_PACK_LSB_FIRST,  GL_PACK_ROW_LENGTH,
                                  GL_PACK_SKIP_ROWS,  GL_PACK_SKIP_PIXELS, GL_PACK_ALIGNMENT};
  static const GLint kStoreDefault[] = {0, 0, 0, 0, 0, 4};
  GLint store[6];
  for (int i = 0; i < 6; i++) {
    store[i] = Int1(*s, kStore[i]);
    if (store[i] != kStoreDefault[i]) t.PixelStorei(kStore[i], kStoreDefault[i]);
  }
  static const GLenum kTransfer[] = {GL_RED_SCALE, GL_GREEN_SCALE, GL_BLUE_SCALE, GL_ALPHA_SCALE,
                                     GL_RED_BIAS,  GL_GREEN_BIAS,  GL_BLUE_BIAS,  GL_ALPHA_BIAS};
  GLfloat transfer[8];
  bool changed[8];
  for (int i = 0; i < 8; i++) {
    t.GetFloatv(kTransfer[i], &transfer[i]);
    GLfloat def = i < 4 ? 1.0f : 0.0f;
    changed[i] = transfer[i] != def;
    if (changed[i]) t.PixelTransferf(kTransfer[i], def);
  }
  GLboolean mapColor = GL_FALSE;
  t.GetBooleanv(GL_MAP_COLOR, &mapColor);
  if (mapColor) t.PixelTransferi(GL_MAP_COLOR, GL_FALSE);
  GLint readBuffer = Int1(*s, GL_READ_BUFFER);
  if (c->readBuffer && readBuffer != c->readBuffer) t.ReadBuffer((GLenum)c->readBuffer);

  t.ReadPixels(0, 0, c->w, c->h, GL_BGRA, GL_UNSIGNED_INT_8_8_8_8_REV, c->pixels);

  if (c->readBuffer && readBuffer != c->readBuffer) t.ReadBuffer((GLenum)readBuffer);
  if (mapColor) t.PixelTransferi(GL_MAP_COLOR, GL_TRUE);
  for (int i = 0; i < 8; i++)
    if (changed[i]) t.PixelTransferf(kTransfer[i], transfer[i]);
  for (int i = 0; i < 6; i++)
    if (store[i] != kStoreDefault[i]) t.PixelStorei(kStore[i], store[i]);
  if (packBuf) s->bindBuffer(0x88EB, (GLuint)packBuf);
  if (readFb) s->bindFramebuffer(fbo ? 0x8CA8 : 0x8D40, (GLuint)readFb);

  // OpenGL gives the bottom row first.
  for (int y = 0; y < c->h / 2; y++) {
    uint32_t* a = c->pixels + (size_t)y * c->w;
    uint32_t* b = c->pixels + (size_t)(c->h - 1 - y) * c->w;
    for (int x = 0; x < c->w; x++) std::swap(a[x], b[x]);
  }
}

// Gives a function of the driver. EGL gives a function for any name that
// begins with gl, so a name with a vendor's suffix (glFooEXT) is given
// only when an extension of that vendor is there, and a name that ends
// in capitals of no vendor gives NULL, as wglGetProcAddress gives NULL
// for a function that the driver does not have.
void* FindProc(const char* name) {
  System* s = Sys();
  if (!s || !name || strncmp(name, "gl", 2) != 0) return NULL;
  static const char* const kVendors[] = {"ARB", "EXT", "KHR", "OES", "NV", "NVX", "AMD", "ATI", "APPLE",
                                         "SGI", "SGIS", "SGIX", "MESA", "MESAX", "INTEL", "IBM", "SUN",
                                         "HP", "WIN", "3DFX", "GREMEDY", "OVR", "OML", "S3", "PGI",
                                         "INGR", "REND", "SUNX", "EXTX", "QCOM", "ANGLE", "IMG", "NVIDIA"};
  size_t n = strlen(name);
  size_t caps = 0;
  while (caps < n - 2 && name[n - 1 - caps] >= 'A' && name[n - 1 - caps] <= 'Z') caps++;
  if (caps >= 2) {
    const char* vendor = NULL;
    for (const char* v : kVendors) {
      size_t k = strlen(v);
      if (k <= caps && strcmp(name + n - k, v) == 0 && (!vendor || k > strlen(vendor))) vendor = v;
    }
    if (!vendor) return NULL;
    std::string prefix = std::string("GL_") + vendor + "_";
    if (s->extensions.find(prefix) == std::string::npos) return NULL;
  }
  return s->egl.GetProcAddress(name);
}

}  // namespace egl
}  // namespace gl
}  // namespace winport

#endif  // WINPORT_EGL
