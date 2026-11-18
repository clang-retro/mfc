// dispcall.cpp - DispCallFunc: a call to a function, or to a method in
// an object's vtable, whose arguments are only known as VARIANTs.
//
// The arguments are sorted the way the platform's convention passes
// them (the System V convention on x86-64; AAPCS64, with Apple's packed
// stack, on arm64): integers and pointers into the integer registers,
// floating-point values into the vector registers, and what does not
// fit onto the stack, in order. The call then goes through one fixed
// prototype that fills every register and a run of stack slots, which a
// callee with fewer parameters ignores.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#include "winport.h"

#include <oleauto.h>
#include <stdint.h>
#include <string.h>

#include <vector>

namespace {

#if defined(__x86_64__)
const int kIntRegs = 6;
#elif defined(__aarch64__)
const int kIntRegs = 8;
#endif
const int kFpRegs = 8;
const int kStackSlots = 16;

#if defined(__x86_64__) || defined(__aarch64__)

struct Args {
  uint64_t ints[8] = {};
  int nInts = 0;
  uint64_t fps[kFpRegs] = {};
  int nFps = 0;
  std::vector<BYTE> stack;
  std::vector<std::vector<BYTE>> copies;  // arm64: large values passed by address
  bool bad = false;

  void OnStack(const void* p, size_t size, size_t align) {
#if defined(__APPLE__) && defined(__aarch64__)
    // Apple's arm64 packs arguments on the stack by their own size.
    (void)align;
    size_t a = size >= 8 ? 8 : size;
#else
    // Elsewhere each takes whole eight-byte slots.
    (void)size;
    size_t a = align < 8 ? 8 : align;
#endif
    while (stack.size() % a) stack.push_back(0);
    const BYTE* b = (const BYTE*)p;
    stack.insert(stack.end(), b, b + size);
#if !(defined(__APPLE__) && defined(__aarch64__))
    while (stack.size() % 8) stack.push_back(0);
#endif
    if (stack.size() > kStackSlots * 8) bad = true;
  }
  // An integer or pointer `size` bytes wide, already extended to 64 bits.
  void Int(uint64_t v, size_t size) {
    if (nInts < kIntRegs) {
      ints[nInts++] = v;
      return;
    }
    OnStack(&v, size, size);
  }
  void Float(float f) {
    uint64_t bits = 0;
    memcpy(&bits, &f, sizeof f);
    if (nFps < kFpRegs) {
      fps[nFps++] = bits;
      return;
    }
    OnStack(&f, sizeof f, sizeof f);
  }
  void Double(double d) {
    uint64_t bits;
    memcpy(&bits, &d, sizeof d);
    if (nFps < kFpRegs) {
      fps[nFps++] = bits;
      return;
    }
    OnStack(&d, sizeof d, sizeof d);
  }
  // A structure passed by value: a VARIANT or a DECIMAL.
  void Struct(const void* p, size_t size) {
#if defined(__x86_64__)
    if (size > 16) {
      OnStack(p, size, 8);  // the MEMORY class: a copy on the stack
      return;
    }
#else
    if (size > 16) {
      // AAPCS64: a copy made by the caller, passed by its address.
      copies.emplace_back((const BYTE*)p, (const BYTE*)p + size);
      Int((uint64_t)(uintptr_t)copies.back().data(), 8);
      return;
    }
#endif
    // Two eight-byte integer parts: both in registers or both on the stack.
    uint64_t parts[2] = {};
    memcpy(parts, p, size);
    int n = (int)((size + 7) / 8);
    if (nInts + n <= kIntRegs) {
      for (int i = 0; i < n; i++) ints[nInts++] = parts[i];
    } else {
#if defined(__aarch64__)
      nInts = kIntRegs;  // AAPCS64: no more integer registers after this
#endif
      OnStack(parts, (size_t)n * 8, 8);
    }
  }
};

double AsDouble(uint64_t bits) {
  double d;
  memcpy(&d, &bits, sizeof d);
  return d;
}

#define WINPORT_DC_ARGS(T)                                                                        \
  T, T, T, T, T, T, T, T, double, double, double, double, double, double, double, double, uint64_t, \
      uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t,     \
      uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t

#if defined(__x86_64__)
// Six integer registers, then the eight vector ones.
typedef uint64_t (*IntFn)(uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, double, double,
                          double, double, double, double, double, double, uint64_t, uint64_t, uint64_t,
                          uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t,
                          uint64_t, uint64_t, uint64_t, uint64_t, uint64_t);
typedef double (*FpFn)(uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, double, double, double,
                       double, double, double, double, double, uint64_t, uint64_t, uint64_t, uint64_t,
                       uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t,
                       uint64_t, uint64_t, uint64_t, uint64_t);
#define WINPORT_DC_CALL(f, a, s)                                                                      \
  f(a.ints[0], a.ints[1], a.ints[2], a.ints[3], a.ints[4], a.ints[5], AsDouble(a.fps[0]),           \
    AsDouble(a.fps[1]), AsDouble(a.fps[2]), AsDouble(a.fps[3]), AsDouble(a.fps[4]),                 \
    AsDouble(a.fps[5]), AsDouble(a.fps[6]), AsDouble(a.fps[7]), s[0], s[1], s[2], s[3], s[4], s[5], \
    s[6], s[7], s[8], s[9], s[10], s[11], s[12], s[13], s[14], s[15])
#else
typedef uint64_t (*IntFn)(WINPORT_DC_ARGS(uint64_t));
typedef double (*FpFn)(WINPORT_DC_ARGS(uint64_t));
#define WINPORT_DC_CALL(f, a, s)                                                                     \
  f(a.ints[0], a.ints[1], a.ints[2], a.ints[3], a.ints[4], a.ints[5], a.ints[6], a.ints[7],        \
    AsDouble(a.fps[0]), AsDouble(a.fps[1]), AsDouble(a.fps[2]), AsDouble(a.fps[3]),                \
    AsDouble(a.fps[4]), AsDouble(a.fps[5]), AsDouble(a.fps[6]), AsDouble(a.fps[7]), s[0], s[1],    \
    s[2], s[3], s[4], s[5], s[6], s[7], s[8], s[9], s[10], s[11], s[12], s[13], s[14], s[15])
#endif

#endif  // x86-64 or arm64

}  // namespace

extern "C" HRESULT WINAPI DispCallFunc(void* pvInstance, ULONG_PTR oVft, CALLCONV, VARTYPE vtReturn,
                                       UINT cActuals, VARTYPE* prgvt, VARIANTARG** prgpvarg,
                                       VARIANT* pvargResult) {
#if defined(__x86_64__) || defined(__aarch64__)
  if (cActuals > 0 && (prgvt == NULL || prgpvarg == NULL)) return E_INVALIDARG;
  void* fn = pvInstance ? (*(void***)pvInstance)[oVft / sizeof(void*)] : (void*)oVft;
  if (fn == NULL) return E_INVALIDARG;
  Args a;
  if (pvInstance) a.Int((uint64_t)(uintptr_t)pvInstance, 8);
  for (UINT i = 0; i < cActuals; i++) {
    VARTYPE vt = prgvt[i];
    const VARIANT* v = prgpvarg[i];
    if (v == NULL) return E_INVALIDARG;
    if (vt & (VT_BYREF | VT_ARRAY)) {
      a.Int((uint64_t)(uintptr_t)v->byref, 8);
      continue;
    }
    switch (vt) {
      case VT_I1: a.Int((uint64_t)(int64_t)v->cVal, 1); break;
      case VT_UI1: a.Int(v->bVal, 1); break;
      case VT_I2:
      case VT_BOOL: a.Int((uint64_t)(int64_t)v->iVal, 2); break;
      case VT_UI2: a.Int(v->uiVal, 2); break;
      case VT_I4:
      case VT_INT:
      case VT_ERROR:
      case VT_HRESULT: a.Int((uint64_t)(int64_t)v->lVal, 4); break;
      case VT_UI4:
      case VT_UINT: a.Int((uint32_t)v->lVal, 4); break;
      case VT_I8:
      case VT_UI8:
      case VT_CY: a.Int((uint64_t)v->llVal, 8); break;
      case VT_R4: a.Float(v->fltVal); break;
      case VT_R8:
      case VT_DATE: a.Double(v->dblVal); break;
      case VT_BSTR:
      case VT_UNKNOWN:
      case VT_DISPATCH:
      case VT_PTR:
      case VT_INT_PTR:
      case VT_UINT_PTR:
      case VT_LPSTR:
      case VT_LPWSTR:
      case VT_SAFEARRAY: a.Int((uint64_t)(uintptr_t)v->byref, 8); break;
      case VT_VARIANT: a.Struct(v, sizeof(VARIANT)); break;
      case VT_DECIMAL: a.Struct(v, sizeof(DECIMAL)); break;
      default: return DISP_E_BADVARTYPE;
    }
  }
  if (a.bad) return DISP_E_BADPARAMCOUNT;
  uint64_t s[kStackSlots] = {};
  memcpy(s, a.stack.data(), a.stack.size());

  VARIANT r;
  VariantInit(&r);
  switch (vtReturn) {
    case VT_R4: {
      double d = WINPORT_DC_CALL(((FpFn)fn), a, s);
      uint64_t bits;
      memcpy(&bits, &d, sizeof d);
      uint32_t lo = (uint32_t)bits;
      memcpy(&r.fltVal, &lo, sizeof lo);
      break;
    }
    case VT_R8:
    case VT_DATE: r.dblVal = WINPORT_DC_CALL(((FpFn)fn), a, s); break;
    case VT_VARIANT:
    case VT_DECIMAL:
    case VT_RECORD: return DISP_E_BADVARTYPE;  // returned through memory
    default: {
      uint64_t v = WINPORT_DC_CALL(((IntFn)fn), a, s);
      switch (vtReturn) {
        case VT_I1: r.cVal = (CHAR)v; break;
        case VT_UI1: r.bVal = (BYTE)v; break;
        case VT_I2:
        case VT_BOOL:
        case VT_UI2: r.iVal = (SHORT)v; break;
        case VT_I4:
        case VT_UI4:
        case VT_INT:
        case VT_UINT:
        case VT_ERROR:
        case VT_HRESULT: r.lVal = (LONG)v; break;
        default: r.llVal = (LONGLONG)v; break;
      }
    }
  }
  if (pvargResult && vtReturn != VT_EMPTY && vtReturn != VT_VOID) {
    r.vt = vtReturn == VT_HRESULT ? VT_ERROR : vtReturn;
    *pvargResult = r;
  }
  return S_OK;
#else
  (void)pvInstance, (void)oVft, (void)vtReturn, (void)cActuals, (void)prgvt, (void)prgpvarg,
      (void)pvargResult;
  return E_NOTIMPL;
#endif
}
