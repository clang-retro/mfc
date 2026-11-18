// errorinfo.cpp - rich errors: the error object a method leaves for its
// caller on the thread (SetErrorInfo, GetErrorInfo), and the one
// CreateErrorInfo makes.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#include "winport.h"

#include <oleauto.h>

#include <atomic>

namespace {

class ErrorInfo final : public IErrorInfo, public ICreateErrorInfo {
public:
  ErrorInfo() : refs_(1), guid_(IID_NULL), source_(NULL), desc_(NULL), helpFile_(NULL), helpContext_(0) {}

  STDMETHOD(QueryInterface)(REFIID riid, void** out) override {
    if (out == NULL) return E_POINTER;
    if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, IID_IErrorInfo)) {
      *out = static_cast<IErrorInfo*>(this);
    } else if (IsEqualIID(riid, IID_ICreateErrorInfo)) {
      *out = static_cast<ICreateErrorInfo*>(this);
    } else {
      *out = NULL;
      return E_NOINTERFACE;
    }
    AddRef();
    return S_OK;
  }
  STDMETHOD_(ULONG, AddRef)() override { return ++refs_; }
  STDMETHOD_(ULONG, Release)() override {
    ULONG n = --refs_;
    if (n == 0) delete this;
    return n;
  }

  // IErrorInfo
  STDMETHOD(GetGUID)(GUID* p) override {
    if (p == NULL) return E_INVALIDARG;
    *p = guid_;
    return S_OK;
  }
  STDMETHOD(GetSource)(BSTR* p) override { return Copy(source_, p); }
  STDMETHOD(GetDescription)(BSTR* p) override { return Copy(desc_, p); }
  STDMETHOD(GetHelpFile)(BSTR* p) override { return Copy(helpFile_, p); }
  STDMETHOD(GetHelpContext)(DWORD* p) override {
    if (p == NULL) return E_INVALIDARG;
    *p = helpContext_;
    return S_OK;
  }

  // ICreateErrorInfo
  STDMETHOD(SetGUID)(REFGUID g) override {
    guid_ = g;
    return S_OK;
  }
  STDMETHOD(SetSource)(LPOLESTR s) override { return Set(&source_, s); }
  STDMETHOD(SetDescription)(LPOLESTR s) override { return Set(&desc_, s); }
  STDMETHOD(SetHelpFile)(LPOLESTR s) override { return Set(&helpFile_, s); }
  STDMETHOD(SetHelpContext)(DWORD n) override {
    helpContext_ = n;
    return S_OK;
  }

private:
  ~ErrorInfo() {
    SysFreeString(source_);
    SysFreeString(desc_);
    SysFreeString(helpFile_);
  }
  static HRESULT Copy(BSTR s, BSTR* out) {
    if (out == NULL) return E_INVALIDARG;
    *out = s ? SysAllocStringLen(s, SysStringLen(s)) : NULL;
    return s && *out == NULL ? E_OUTOFMEMORY : S_OK;
  }
  static HRESULT Set(BSTR* field, LPCOLESTR s) {
    BSTR b = s ? SysAllocString(s) : NULL;
    if (s && b == NULL) return E_OUTOFMEMORY;
    SysFreeString(*field);
    *field = b;
    return S_OK;
  }

  std::atomic<ULONG> refs_;
  GUID guid_;
  BSTR source_, desc_, helpFile_;
  DWORD helpContext_;
};

// The thread's error object, until someone takes it.
thread_local IErrorInfo* t_error = NULL;

}  // namespace

extern "C" {

HRESULT WINAPI CreateErrorInfo(ICreateErrorInfo** out) {
  if (out == NULL) return E_INVALIDARG;
  *out = new ErrorInfo;
  return S_OK;
}

HRESULT WINAPI SetErrorInfo(ULONG reserved, IErrorInfo* p) {
  if (reserved != 0) return E_INVALIDARG;
  if (p) p->AddRef();
  IErrorInfo* old = t_error;
  t_error = p;
  if (old) old->Release();
  return S_OK;
}

HRESULT WINAPI GetErrorInfo(ULONG reserved, IErrorInfo** out) {
  if (reserved != 0 || out == NULL) return E_INVALIDARG;
  *out = t_error;  // the caller's now
  t_error = NULL;
  return *out ? S_OK : S_FALSE;
}

}  // extern "C"
