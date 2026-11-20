// olepro.cpp - what controls use from OLE Automation's control support:
// the standard font object (OleCreateFontIndirect), the standard picture
// object (OleCreatePictureIndirect, OleLoadPicture) and OleTranslateColor.
//
// The font tells IPropertyNotifySink sinks about each change. Both
// objects have a dispinterface (IFontDisp, IPictureDisp) with the
// standard DISPIDs and save themselves to a stream (IPersistStream): the
// font as its attributes, the picture as a picture file (a bitmap or an
// icon) after a small header.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#include "winport.h"
#include "ole.h"

#include <ocidl.h>
#include <olectl.h>
#include <oleauto.h>
#include <string.h>
#include <wctype.h>

#include <atomic>
#include <string>
#include <vector>

namespace {

// ---------------------------------------------------------------------
// One connection point, for the sinks of one interface.

class ConnPoint final : public IConnectionPoint {
public:
  ConnPoint(IConnectionPointContainer* owner, REFIID iid) : owner_(owner), iid_(iid) {}
  ~ConnPoint() {
    for (Sink& s : sinks_) s.unk->Release();
  }
  // The owner's lifetime is the point's.
  STDMETHOD(QueryInterface)(REFIID riid, void** out) override {
    if (out == NULL) return E_POINTER;
    if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, IID_IConnectionPoint)) {
      *out = static_cast<IConnectionPoint*>(this);
      AddRef();
      return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
  }
  STDMETHOD_(ULONG, AddRef)() override { return owner_->AddRef(); }
  STDMETHOD_(ULONG, Release)() override { return owner_->Release(); }
  STDMETHOD(GetConnectionInterface)(IID* p) override {
    if (p == NULL) return E_POINTER;
    *p = iid_;
    return S_OK;
  }
  STDMETHOD(GetConnectionPointContainer)(IConnectionPointContainer** p) override {
    if (p == NULL) return E_POINTER;
    *p = owner_;
    owner_->AddRef();
    return S_OK;
  }
  STDMETHOD(Advise)(IUnknown* sink, DWORD* cookie) override {
    if (sink == NULL || cookie == NULL) return E_POINTER;
    IUnknown* p = NULL;
    if (FAILED(sink->QueryInterface(iid_, (void**)&p)) || p == NULL) return CONNECT_E_CANNOTCONNECT;
    sinks_.push_back(Sink{p, ++next_});
    *cookie = next_;
    return S_OK;
  }
  STDMETHOD(Unadvise)(DWORD cookie) override {
    for (size_t i = 0; i < sinks_.size(); i++)
      if (sinks_[i].cookie == cookie) {
        IUnknown* p = sinks_[i].unk;
        sinks_.erase(sinks_.begin() + i);
        p->Release();
        return S_OK;
      }
    return CONNECT_E_NOCONNECTION;
  }
  STDMETHOD(EnumConnections)(IEnumConnections** p) override {
    if (p == NULL) return E_POINTER;
    std::vector<CONNECTDATA> v;
    for (Sink& s : sinks_) v.push_back(CONNECTDATA{s.unk, s.cookie});
    *p = new winport::ole::ConnectionEnum(v);
    return S_OK;
  }

  // The sinks, AddRef'd, so that one may unadvise while being told.
  std::vector<IUnknown*> Sinks() {
    std::vector<IUnknown*> v;
    for (Sink& s : sinks_) {
      s.unk->AddRef();
      v.push_back(s.unk);
    }
    return v;
  }

private:
  struct Sink {
    IUnknown* unk;
    DWORD cookie;
  };
  IConnectionPointContainer* owner_;
  IID iid_;
  std::vector<Sink> sinks_;
  DWORD next_ = 0;
};

class EnumOnePoint final : public IEnumConnectionPoints {
public:
  explicit EnumOnePoint(IConnectionPoint* cp) : cp_(cp) { cp_->AddRef(); }
  ~EnumOnePoint() { cp_->Release(); }
  STDMETHOD(QueryInterface)(REFIID riid, void** out) override {
    if (out == NULL) return E_POINTER;
    if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, IID_IEnumConnectionPoints)) {
      *out = this;
      AddRef();
      return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
  }
  STDMETHOD_(ULONG, AddRef)() override { return ++refs_; }
  STDMETHOD_(ULONG, Release)() override {
    ULONG n = --refs_;
    if (n == 0) delete this;
    return n;
  }
  STDMETHOD(Next)(ULONG c, LPCONNECTIONPOINT* out, ULONG* got) override {
    ULONG n = 0;
    if (c > 0 && pos_ == 0 && out) {
      out[0] = cp_;
      cp_->AddRef();
      pos_ = 1;
      n = 1;
    }
    if (got) *got = n;
    return n == c ? S_OK : S_FALSE;
  }
  STDMETHOD(Skip)(ULONG c) override {
    if (c == 0) return S_OK;
    bool ok = pos_ == 0 && c == 1;
    pos_ = 1;
    return ok ? S_OK : S_FALSE;
  }
  STDMETHOD(Reset)() override {
    pos_ = 0;
    return S_OK;
  }
  STDMETHOD(Clone)(IEnumConnectionPoints** out) override {
    if (out == NULL) return E_POINTER;
    EnumOnePoint* e = new EnumOnePoint(cp_);
    e->pos_ = pos_;
    *out = e;
    return S_OK;
  }

private:
  IConnectionPoint* cp_;
  int pos_ = 0;
  std::atomic<ULONG> refs_{1};
};

HRESULT ReadAll(IStream* s, void* p, ULONG n) {
  ULONG got = 0;
  HRESULT hr = s->Read(p, n, &got);
  if (FAILED(hr)) return hr;
  return got == n ? S_OK : E_FAIL;
}
HRESULT WriteAll(IStream* s, const void* p, ULONG n) {
  ULONG done = 0;
  HRESULT hr = s->Write(p, n, &done);
  if (FAILED(hr)) return hr;
  return done == n ? S_OK : STG_E_MEDIUMFULL;
}

// A DISPPARAMS' one argument for a property put, as type vt.
HRESULT PutArg(DISPPARAMS* dp, VARTYPE vt, VARIANT* out) {
  if (dp == NULL || dp->cArgs != 1) return DISP_E_BADPARAMCOUNT;
  VariantInit(out);
  HRESULT hr = VariantChangeType(out, &dp->rgvarg[0], 0, vt);
  return FAILED(hr) ? DISP_E_TYPEMISMATCH : S_OK;
}

// ---------------------------------------------------------------------
// The font.

const int kHimetricPerInch = 2540;

class Font final : public IFont, public IFontDisp, public IPersistStream, public IConnectionPointContainer {
public:
  Font() : notify_(this, IID_IPropertyNotifySink) {}

  void Init(const FONTDESC* d) {
    name_ = d->lpstrName ? d->lpstrName : L"";
    size_ = d->cySize.int64;
    weight_ = d->sWeight;
    charset_ = d->sCharset;
    italic_ = d->fItalic != FALSE;
    underline_ = d->fUnderline != FALSE;
    strike_ = d->fStrikethrough != FALSE;
  }

  // IUnknown
  STDMETHOD(QueryInterface)(REFIID riid, void** out) override {
    if (out == NULL) return E_POINTER;
    if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, IID_IFont)) {
      *out = static_cast<IFont*>(this);
    } else if (IsEqualIID(riid, IID_IDispatch) || IsEqualIID(riid, IID_IFontDisp)) {
      *out = static_cast<IFontDisp*>(this);
    } else if (IsEqualIID(riid, IID_IPersist) || IsEqualIID(riid, IID_IPersistStream)) {
      *out = static_cast<IPersistStream*>(this);
    } else if (IsEqualIID(riid, IID_IConnectionPointContainer)) {
      *out = static_cast<IConnectionPointContainer*>(this);
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

  // IFont
  STDMETHOD(get_Name)(BSTR* p) override {
    if (p == NULL) return E_POINTER;
    *p = SysAllocStringLen(name_.data(), (UINT)name_.size());
    return S_OK;
  }
  STDMETHOD(put_Name)(BSTR s) override {
    if (s == NULL) return CTL_E_INVALIDPROPERTYVALUE;
    name_ = s;
    return Changed(DISPID_FONT_NAME);
  }
  STDMETHOD(get_Size)(CY* p) override {
    if (p == NULL) return E_POINTER;
    p->int64 = size_;
    return S_OK;
  }
  STDMETHOD(put_Size)(CY cy) override {
    if (cy.int64 < 0) return CTL_E_INVALIDPROPERTYVALUE;
    size_ = cy.int64;
    return Changed(DISPID_FONT_SIZE);
  }
  STDMETHOD(get_Bold)(BOOL* p) override {
    if (p == NULL) return E_POINTER;
    *p = weight_ >= FW_BOLD;
    return S_OK;
  }
  STDMETHOD(put_Bold)(BOOL b) override {
    weight_ = b ? FW_BOLD : FW_NORMAL;
    return Changed(DISPID_FONT_BOLD);
  }
  STDMETHOD(get_Italic)(BOOL* p) override { return GetFlag(italic_, p); }
  STDMETHOD(put_Italic)(BOOL b) override { return SetFlag(&italic_, b, DISPID_FONT_ITALIC); }
  STDMETHOD(get_Underline)(BOOL* p) override { return GetFlag(underline_, p); }
  STDMETHOD(put_Underline)(BOOL b) override { return SetFlag(&underline_, b, DISPID_FONT_UNDER); }
  STDMETHOD(get_Strikethrough)(BOOL* p) override { return GetFlag(strike_, p); }
  STDMETHOD(put_Strikethrough)(BOOL b) override { return SetFlag(&strike_, b, DISPID_FONT_STRIKE); }
  STDMETHOD(get_Weight)(SHORT* p) override {
    if (p == NULL) return E_POINTER;
    *p = weight_;
    return S_OK;
  }
  STDMETHOD(put_Weight)(SHORT w) override {
    weight_ = w;
    return Changed(DISPID_FONT_WEIGHT);
  }
  STDMETHOD(get_Charset)(SHORT* p) override {
    if (p == NULL) return E_POINTER;
    *p = charset_;
    return S_OK;
  }
  STDMETHOD(put_Charset)(SHORT c) override {
    charset_ = c;
    return Changed(DISPID_FONT_CHARSET);
  }
  STDMETHOD(get_hFont)(HFONT* p) override {
    if (p == NULL) return E_POINTER;
    if (hfont_ == NULL) {
      LOGFONTW lf;
      memset(&lf, 0, sizeof lf);
      // Points (in ten-thousandths) to logical units by the ratio.
      long long himetric = size_ * kHimetricPerInch / 72 / 10000;
      lf.lfHeight = -(LONG)((himetric * cyLogical_ + cyHimetric_ / 2) / cyHimetric_);
      if (lf.lfHeight == 0 && size_ > 0) lf.lfHeight = -1;
      lf.lfWeight = weight_;
      lf.lfItalic = italic_;
      lf.lfUnderline = underline_;
      lf.lfStrikeOut = strike_;
      lf.lfCharSet = (BYTE)charset_;
      lstrcpynW(lf.lfFaceName, name_.c_str(), LF_FACESIZE);
      hfont_ = CreateFontIndirectW(&lf);
    }
    *p = hfont_;
    return hfont_ ? S_OK : E_FAIL;
  }
  STDMETHOD(Clone)(IFont** p) override {
    if (p == NULL) return E_POINTER;
    Font* f = new Font;
    f->name_ = name_;
    f->size_ = size_;
    f->weight_ = weight_;
    f->charset_ = charset_;
    f->italic_ = italic_;
    f->underline_ = underline_;
    f->strike_ = strike_;
    f->cyLogical_ = cyLogical_;
    f->cyHimetric_ = cyHimetric_;
    *p = f;
    return S_OK;
  }
  STDMETHOD(IsEqual)(IFont* other) override {
    if (other == NULL) return E_POINTER;
    BSTR n = NULL;
    CY cy;
    SHORT w = 0, cs = 0;
    BOOL i = 0, u = 0, s = 0;
    other->get_Name(&n);
    other->get_Size(&cy);
    other->get_Weight(&w);
    other->get_Charset(&cs);
    other->get_Italic(&i);
    other->get_Underline(&u);
    other->get_Strikethrough(&s);
    bool same = n && name_ == n && cy.int64 == size_ && w == weight_ && cs == charset_ &&
                (i != 0) == italic_ && (u != 0) == underline_ && (s != 0) == strike_;
    SysFreeString(n);
    return same ? S_OK : S_FALSE;
  }
  STDMETHOD(SetRatio)(LONG cyLogical, LONG cyHimetric) override {
    if (cyLogical == 0 || cyHimetric == 0) return E_INVALIDARG;
    if (cyLogical != cyLogical_ || cyHimetric != cyHimetric_) {
      cyLogical_ = cyLogical;
      cyHimetric_ = cyHimetric;
      DropFont();
    }
    return S_OK;
  }
  STDMETHOD(QueryTextMetrics)(TEXTMETRICW* tm) override {
    if (tm == NULL) return E_POINTER;
    HFONT h;
    if (FAILED(get_hFont(&h))) return E_FAIL;
    HDC dc = GetDC(NULL);
    HGDIOBJ old = SelectObject(dc, h);
    BOOL ok = GetTextMetricsW(dc, tm);
    SelectObject(dc, old);
    ReleaseDC(NULL, dc);
    return ok ? S_OK : E_FAIL;
  }
  STDMETHOD(AddRefHfont)(HFONT h) override { return h ? S_OK : E_INVALIDARG; }
  STDMETHOD(ReleaseHfont)(HFONT h) override { return h ? S_OK : E_INVALIDARG; }
  STDMETHOD(SetHdc)(HDC) override { return S_OK; }

  // IFontDisp
  STDMETHOD(GetTypeInfoCount)(UINT* p) override {
    if (p == NULL) return E_POINTER;
    *p = 0;
    return S_OK;
  }
  STDMETHOD(GetTypeInfo)(UINT, LCID, ITypeInfo** p) override {
    if (p) *p = NULL;
    return DISP_E_BADINDEX;
  }
  STDMETHOD(GetIDsOfNames)(REFIID, LPOLESTR* names, UINT c, LCID, DISPID* ids) override {
    static const struct {
      const wchar_t* name;
      DISPID id;
    } kNames[] = {{L"Name", DISPID_FONT_NAME},       {L"Size", DISPID_FONT_SIZE},
                  {L"Bold", DISPID_FONT_BOLD},       {L"Italic", DISPID_FONT_ITALIC},
                  {L"Underline", DISPID_FONT_UNDER}, {L"Strikethrough", DISPID_FONT_STRIKE},
                  {L"Weight", DISPID_FONT_WEIGHT},   {L"Charset", DISPID_FONT_CHARSET}};
    if (names == NULL || ids == NULL || c == 0) return E_INVALIDARG;
    for (UINT i = 0; i < c; i++) ids[i] = DISPID_UNKNOWN;
    for (const auto& k : kNames)
      if (lstrcmpiW(names[0], k.name) == 0) ids[0] = k.id;
    return ids[0] == DISPID_UNKNOWN || c > 1 ? DISP_E_UNKNOWNNAME : S_OK;
  }
  STDMETHOD(Invoke)(DISPID id, REFIID, LCID, WORD flags, DISPPARAMS* dp, VARIANT* res, EXCEPINFO*,
                    UINT*) override {
    bool put = (flags & (DISPATCH_PROPERTYPUT | DISPATCH_PROPERTYPUTREF)) != 0;
    if (!put && !(flags & (DISPATCH_PROPERTYGET | DISPATCH_METHOD))) return DISP_E_MEMBERNOTFOUND;
    VARIANT v;
    VariantInit(&v);
    HRESULT hr = S_OK;
    switch (id) {
      case DISPID_FONT_NAME:
        if (put) {
          if (SUCCEEDED(hr = PutArg(dp, VT_BSTR, &v))) hr = put_Name(v.bstrVal);
        } else {
          v.vt = VT_BSTR;
          hr = get_Name(&v.bstrVal);
        }
        break;
      case DISPID_FONT_SIZE:
        if (put) {
          if (SUCCEEDED(hr = PutArg(dp, VT_CY, &v))) hr = put_Size(v.cyVal);
        } else {
          v.vt = VT_CY;
          hr = get_Size(&v.cyVal);
        }
        break;
      case DISPID_FONT_BOLD:
      case DISPID_FONT_ITALIC:
      case DISPID_FONT_UNDER:
      case DISPID_FONT_STRIKE: {
        if (put) {
          if (FAILED(hr = PutArg(dp, VT_BOOL, &v))) break;
          BOOL b = v.boolVal != VARIANT_FALSE;
          hr = id == DISPID_FONT_BOLD     ? put_Bold(b)
               : id == DISPID_FONT_ITALIC ? put_Italic(b)
               : id == DISPID_FONT_UNDER  ? put_Underline(b)
                                          : put_Strikethrough(b);
        } else {
          BOOL b = FALSE;
          hr = id == DISPID_FONT_BOLD     ? get_Bold(&b)
               : id == DISPID_FONT_ITALIC ? get_Italic(&b)
               : id == DISPID_FONT_UNDER  ? get_Underline(&b)
                                          : get_Strikethrough(&b);
          v.vt = VT_BOOL;
          v.boolVal = b ? VARIANT_TRUE : VARIANT_FALSE;
        }
        break;
      }
      case DISPID_FONT_WEIGHT:
      case DISPID_FONT_CHARSET:
        if (put) {
          if (FAILED(hr = PutArg(dp, VT_I2, &v))) break;
          hr = id == DISPID_FONT_WEIGHT ? put_Weight(v.iVal) : put_Charset(v.iVal);
        } else {
          v.vt = VT_I2;
          hr = id == DISPID_FONT_WEIGHT ? get_Weight(&v.iVal) : get_Charset(&v.iVal);
        }
        break;
      default: return DISP_E_MEMBERNOTFOUND;
    }
    if (SUCCEEDED(hr) && !put && res) {
      *res = v;
      return S_OK;
    }
    VariantClear(&v);
    return hr;
  }

  // IPersistStream: a version byte, the character set, the style bits,
  // the weight, the size and the name (in the system's code page).
  STDMETHOD(GetClassID)(CLSID* p) override {
    if (p == NULL) return E_POINTER;
    *p = CLSID_StdFont;
    return S_OK;
  }
  STDMETHOD(IsDirty)() override { return dirty_ ? S_OK : S_FALSE; }
  STDMETHOD(Load)(IStream* s) override {
    if (s == NULL) return E_POINTER;
    BYTE version, style, len;
    SHORT charset, weight;
    ULONG size;
    HRESULT hr;
    if (FAILED(hr = ReadAll(s, &version, 1)) || FAILED(hr = ReadAll(s, &charset, 2)) ||
        FAILED(hr = ReadAll(s, &style, 1)) || FAILED(hr = ReadAll(s, &weight, 2)) ||
        FAILED(hr = ReadAll(s, &size, 4)) || FAILED(hr = ReadAll(s, &len, 1)))
      return hr;
    if (version != 1) return E_FAIL;
    char name[256];
    if (FAILED(hr = ReadAll(s, name, len))) return hr;
    name[len] = 0;
    WCHAR wname[256];
    int n = MultiByteToWideChar(CP_ACP, 0, name, len, wname, 255);
    wname[n > 0 ? n : 0] = 0;
    name_ = wname;
    charset_ = charset;
    italic_ = (style & 0x02) != 0;
    underline_ = (style & 0x04) != 0;
    strike_ = (style & 0x08) != 0;
    weight_ = weight;
    size_ = size;
    dirty_ = false;
    DropFont();
    return S_OK;
  }
  STDMETHOD(Save)(IStream* s, BOOL clearDirty) override {
    if (s == NULL) return E_POINTER;
    char name[256];
    int len = WideCharToMultiByte(CP_ACP, 0, name_.c_str(), (int)name_.size(), name, 255, NULL, NULL);
    if (len < 0) len = 0;
    BYTE version = 1, style = (BYTE)((italic_ ? 0x02 : 0) | (underline_ ? 0x04 : 0) | (strike_ ? 0x08 : 0)),
         blen = (BYTE)len;
    SHORT charset = charset_, weight = weight_;
    ULONG size = (ULONG)size_;
    HRESULT hr;
    if (FAILED(hr = WriteAll(s, &version, 1)) || FAILED(hr = WriteAll(s, &charset, 2)) ||
        FAILED(hr = WriteAll(s, &style, 1)) || FAILED(hr = WriteAll(s, &weight, 2)) ||
        FAILED(hr = WriteAll(s, &size, 4)) || FAILED(hr = WriteAll(s, &blen, 1)) ||
        FAILED(hr = WriteAll(s, name, blen)))
      return hr;
    if (clearDirty) dirty_ = false;
    return S_OK;
  }
  STDMETHOD(GetSizeMax)(ULARGE_INTEGER* p) override {
    if (p == NULL) return E_POINTER;
    p->QuadPart = 11 + 255;
    return S_OK;
  }

  // IConnectionPointContainer
  STDMETHOD(EnumConnectionPoints)(IEnumConnectionPoints** p) override {
    if (p == NULL) return E_POINTER;
    *p = new EnumOnePoint(&notify_);
    return S_OK;
  }
  STDMETHOD(FindConnectionPoint)(REFIID riid, IConnectionPoint** p) override {
    if (p == NULL) return E_POINTER;
    *p = NULL;
    if (!IsEqualIID(riid, IID_IPropertyNotifySink)) return CONNECT_E_NOCONNECTION;
    *p = &notify_;
    notify_.AddRef();
    return S_OK;
  }

private:
  ~Font() {
    DropFont();
    for (HFONT h : old_) DeleteObject(h);
  }
  // A new handle next time; the old one stays valid while the font lives,
  // as a caller may still have it selected.
  void DropFont() {
    if (hfont_) old_.push_back(hfont_);
    hfont_ = NULL;
  }
  HRESULT Changed(DISPID id) {
    dirty_ = true;
    DropFont();
    for (IUnknown* u : notify_.Sinks()) {
      ((IPropertyNotifySink*)u)->OnChanged(id);
      u->Release();
    }
    return S_OK;
  }
  static HRESULT GetFlag(bool f, BOOL* p) {
    if (p == NULL) return E_POINTER;
    *p = f;
    return S_OK;
  }
  HRESULT SetFlag(bool* f, BOOL b, DISPID id) {
    *f = b != FALSE;
    return Changed(id);
  }

  std::atomic<ULONG> refs_{1};
  ConnPoint notify_;
  std::wstring name_;
  LONGLONG size_ = 0;
  SHORT weight_ = FW_NORMAL, charset_ = DEFAULT_CHARSET;
  bool italic_ = false, underline_ = false, strike_ = false, dirty_ = false;
  LONG cyLogical_ = 96, cyHimetric_ = kHimetricPerInch;
  HFONT hfont_ = NULL;
  std::vector<HFONT> old_;
};

// ---------------------------------------------------------------------
// The picture.

int ScreenDpi(bool y) {
  HDC dc = GetDC(NULL);
  int n = GetDeviceCaps(dc, y ? LOGPIXELSY : LOGPIXELSX);
  ReleaseDC(NULL, dc);
  return n > 0 ? n : 96;
}
LONG PixelsToHimetric(int px, bool y) { return MulDiv(px, kHimetricPerInch, ScreenDpi(y)); }

// A device-independent bitmap file of a bitmap.
bool BitmapFile(HBITMAP hbm, std::vector<BYTE>* out) {
  BITMAP bm;
  if (!GetObjectW(hbm, sizeof bm, &bm)) return false;
  BITMAPINFOHEADER bi;
  memset(&bi, 0, sizeof bi);
  bi.biSize = sizeof bi;
  bi.biWidth = bm.bmWidth;
  bi.biHeight = bm.bmHeight;
  bi.biPlanes = 1;
  bi.biBitCount = 24;
  bi.biCompression = BI_RGB;
  DWORD stride = ((DWORD)bm.bmWidth * 3 + 3) & ~3u;
  bi.biSizeImage = stride * (DWORD)bm.bmHeight;
  BITMAPFILEHEADER fh;
  memset(&fh, 0, sizeof fh);
  fh.bfType = 0x4d42;
  fh.bfOffBits = sizeof fh + sizeof bi;
  fh.bfSize = fh.bfOffBits + bi.biSizeImage;
  out->resize(fh.bfSize);
  memcpy(out->data(), &fh, sizeof fh);
  memcpy(out->data() + sizeof fh, &bi, sizeof bi);
  HDC dc = GetDC(NULL);
  int lines = GetDIBits(dc, hbm, 0, (UINT)bm.bmHeight, out->data() + fh.bfOffBits, (BITMAPINFO*)&bi,
                        DIB_RGB_COLORS);
  ReleaseDC(NULL, dc);
  return lines == bm.bmHeight;
}

// An icon file of an icon: its one image, 32 bits a pixel with its mask,
// or a monochrome icon's two halves.
bool IconFile(HICON icon, std::vector<BYTE>* out) {
  ICONINFO ii;
  if (!GetIconInfo(icon, &ii)) return false;
  BITMAP bm;
  bool ok = ii.hbmMask && GetObjectW(ii.hbmMask, sizeof bm, &bm);
  int w = ok ? bm.bmWidth : 0, h = ok ? (ii.hbmColor ? bm.bmHeight : bm.bmHeight / 2) : 0;
  ok = ok && w > 0 && h > 0;
  std::vector<BYTE> image;
  WORD bits = ii.hbmColor ? 32 : 1;
  if (ok) {
    HDC dc = GetDC(NULL);
    DWORD maskStride = ((DWORD)w + 31) / 32 * 4;
    struct {
      BITMAPINFOHEADER h;
      RGBQUAD colors[2];
    } bi;
    memset(&bi, 0, sizeof bi);
    bi.h.biSize = sizeof bi.h;
    bi.h.biWidth = w;
    bi.h.biPlanes = 1;
    bi.h.biCompression = BI_RGB;
    if (ii.hbmColor) {
      // The colors, then the mask.
      DWORD stride = (DWORD)w * 4;
      image.resize(sizeof bi.h + stride * h + maskStride * h);
      bi.h.biHeight = h;
      bi.h.biBitCount = 32;
      ok = GetDIBits(dc, ii.hbmColor, 0, (UINT)h, image.data() + sizeof bi.h, (BITMAPINFO*)&bi, DIB_RGB_COLORS) == h;
      bi.h.biBitCount = 1;
      ok = ok && GetDIBits(dc, ii.hbmMask, 0, (UINT)h, image.data() + sizeof bi.h + stride * h, (BITMAPINFO*)&bi,
                           DIB_RGB_COLORS) == h;
      memset(&bi, 0, sizeof bi);
      bi.h.biSize = sizeof bi.h;
      bi.h.biWidth = w;
      bi.h.biHeight = 2 * h;
      bi.h.biPlanes = 1;
      bi.h.biBitCount = 32;
      memcpy(image.data(), &bi.h, sizeof bi.h);
    } else {
      // The mask is both halves: bottom up, the colors' and then the mask's.
      image.resize(sizeof bi + maskStride * 2 * h);
      bi.h.biHeight = 2 * h;
      bi.h.biBitCount = 1;
      ok = GetDIBits(dc, ii.hbmMask, 0, (UINT)(2 * h), image.data() + sizeof bi, (BITMAPINFO*)&bi,
                     DIB_RGB_COLORS) == 2 * h;
      bi.h.biClrUsed = 2;
      bi.colors[1].rgbRed = bi.colors[1].rgbGreen = bi.colors[1].rgbBlue = 255;
      memcpy(image.data(), &bi, sizeof bi);
    }
    ReleaseDC(NULL, dc);
  }
  if (ii.hbmColor) DeleteObject(ii.hbmColor);
  if (ii.hbmMask) DeleteObject(ii.hbmMask);
  if (!ok) return false;
  BYTE head[22] = {0, 0, 1, 0, 1, 0};
  head[6] = (BYTE)(w >= 256 ? 0 : w);
  head[7] = (BYTE)(h >= 256 ? 0 : h);
  head[10] = 1;
  head[12] = (BYTE)bits;
  DWORD size = (DWORD)image.size(), offset = sizeof head;
  memcpy(head + 14, &size, 4);
  memcpy(head + 18, &offset, 4);
  out->assign(head, head + sizeof head);
  out->insert(out->end(), image.begin(), image.end());
  return true;
}

// A placeable metafile file: its header gives the size in HIMETRIC.
const DWORD kPlaceableKey = 0x9AC6CDD7;
bool MetafileFile(HMETAFILE mf, LONG cx, LONG cy, std::vector<BYTE>* out) {
  UINT n = GetMetaFileBitsEx(mf, 0, NULL);
  if (n == 0) return false;
  WORD head[11] = {};
  memcpy(head, &kPlaceableKey, 4);
  // The bounding box (from 0, 0) in as fine a unit as fits 16 bits.
  LONG w = cx < 0 ? -cx : cx, h = cy < 0 ? -cy : cy;
  int inch = kHimetricPerInch;
  while (inch > 1 && (MulDiv(w, inch, kHimetricPerInch) > 32767 || MulDiv(h, inch, kHimetricPerInch) > 32767))
    inch /= 2;
  head[5] = (WORD)MulDiv(w, inch, kHimetricPerInch);
  head[6] = (WORD)MulDiv(h, inch, kHimetricPerInch);
  head[7] = (WORD)inch;
  for (int i = 0; i < 10; i++) head[10] ^= head[i];
  out->assign((BYTE*)head, (BYTE*)head + sizeof head);
  out->resize(sizeof head + n);
  return GetMetaFileBitsEx(mf, n, out->data() + sizeof head) == n;
}

bool EnhMetafileFile(HENHMETAFILE emf, std::vector<BYTE>* out) {
  UINT n = GetEnhMetaFileBits(emf, 0, NULL);
  if (n == 0) return false;
  out->resize(n);
  return GetEnhMetaFileBits(emf, n, out->data()) == n;
}

class Picture final : public IPicture, public IPictureDisp, public IPersistStream {
public:
  SHORT type_ = (SHORT)PICTYPE_UNINITIALIZED;
  HGDIOBJ handle_ = NULL;  // HBITMAP, HICON, HMETAFILE or HENHMETAFILE
  HPALETTE hpal_ = NULL;
  bool own_ = false;
  LONG width_ = 0, height_ = 0;  // HIMETRIC
  HDC curDC_ = NULL;
  bool keep_ = true;
  std::vector<BYTE> original_;  // the file it was loaded from

  HRESULT Init(const PICTDESC* d, BOOL own) {
    own_ = own != FALSE;
    if (d == NULL) return S_OK;
    type_ = (SHORT)d->picType;
    switch (d->picType) {
      case (UINT)PICTYPE_UNINITIALIZED:
      case PICTYPE_NONE: break;
      case PICTYPE_BITMAP: {
        handle_ = d->bmp.hbitmap;
        hpal_ = d->bmp.hpal;
        BITMAP bm;
        if (!GetObjectW(handle_, sizeof bm, &bm)) return E_INVALIDARG;
        width_ = PixelsToHimetric(bm.bmWidth, false);
        height_ = PixelsToHimetric(bm.bmHeight, true);
        break;
      }
      case PICTYPE_ICON: {
        handle_ = d->icon.hicon;
        ICONINFO ii;
        int cx = GetSystemMetrics(SM_CXICON), cy = GetSystemMetrics(SM_CYICON);
        if (GetIconInfo((HICON)handle_, &ii)) {
          BITMAP bm;
          HBITMAP sized = ii.hbmColor ? ii.hbmColor : ii.hbmMask;
          if (sized && GetObjectW(sized, sizeof bm, &bm)) {
            cx = bm.bmWidth;
            cy = ii.hbmColor ? bm.bmHeight : bm.bmHeight / 2;
          }
          if (ii.hbmColor) DeleteObject(ii.hbmColor);
          if (ii.hbmMask) DeleteObject(ii.hbmMask);
        }
        width_ = PixelsToHimetric(cx, false);
        height_ = PixelsToHimetric(cy, true);
        break;
      }
      case PICTYPE_METAFILE:
        handle_ = d->wmf.hmeta;
        width_ = d->wmf.xExt;
        height_ = d->wmf.yExt;
        break;
      case PICTYPE_ENHMETAFILE: {
        handle_ = d->emf.hemf;
        ENHMETAHEADER h;
        if (GetEnhMetaFileHeader(d->emf.hemf, sizeof h, &h)) {
          width_ = h.rclFrame.right - h.rclFrame.left;
          height_ = h.rclFrame.bottom - h.rclFrame.top;
        }
        break;
      }
      default: return E_INVALIDARG;
    }
    return S_OK;
  }

  STDMETHOD(QueryInterface)(REFIID riid, void** out) override {
    if (out == NULL) return E_POINTER;
    if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, IID_IPicture)) {
      *out = static_cast<IPicture*>(this);
    } else if (IsEqualIID(riid, IID_IDispatch) || IsEqualIID(riid, IID_IPictureDisp)) {
      *out = static_cast<IPictureDisp*>(this);
    } else if (IsEqualIID(riid, IID_IPersist) || IsEqualIID(riid, IID_IPersistStream)) {
      *out = static_cast<IPersistStream*>(this);
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

  // IPicture
  STDMETHOD(get_Handle)(OLE_HANDLE* p) override {
    if (p == NULL) return E_POINTER;
    *p = (OLE_HANDLE)(UINT_PTR)handle_;
    return S_OK;
  }
  STDMETHOD(get_hPal)(OLE_HANDLE* p) override {
    if (p == NULL) return E_POINTER;
    if (type_ != PICTYPE_BITMAP) return E_FAIL;
    *p = (OLE_HANDLE)(UINT_PTR)hpal_;
    return S_OK;
  }
  STDMETHOD(get_Type)(SHORT* p) override {
    if (p == NULL) return E_POINTER;
    *p = type_;
    return S_OK;
  }
  STDMETHOD(get_Width)(OLE_XSIZE_HIMETRIC* p) override {
    if (p == NULL) return E_POINTER;
    *p = width_;
    return S_OK;
  }
  STDMETHOD(get_Height)(OLE_YSIZE_HIMETRIC* p) override {
    if (p == NULL) return E_POINTER;
    *p = height_;
    return S_OK;
  }
  // The part of the picture from (xSrc, ySrc) (HIMETRIC, from the bottom
  // left), cxSrc by cySrc (negative: upwards), into x, y, cx, cy.
  STDMETHOD(Render)(HDC dc, LONG x, LONG y, LONG cx, LONG cy, OLE_XPOS_HIMETRIC xSrc, OLE_YPOS_HIMETRIC ySrc,
                    OLE_XSIZE_HIMETRIC cxSrc, OLE_YSIZE_HIMETRIC cySrc, LPCRECT) override {
    if (dc == NULL) return E_INVALIDARG;
    if (cx == 0 || cy == 0) return E_INVALIDARG;
    switch (type_) {
      case PICTYPE_NONE:
      case PICTYPE_UNINITIALIZED: return S_OK;
      case PICTYPE_BITMAP: {
        BITMAP bm;
        if (!GetObjectW(handle_, sizeof bm, &bm) || width_ == 0 || height_ == 0) return E_FAIL;
        // The source rectangle in the bitmap's pixels, from the top.
        LONG top = ySrc + (cySrc < 0 ? 0 : cySrc);
        LONG sx = MulDiv(xSrc, bm.bmWidth, width_);
        LONG sy = MulDiv(height_ - top, bm.bmHeight, height_);
        LONG sw = MulDiv(cxSrc < 0 ? -cxSrc : cxSrc, bm.bmWidth, width_);
        LONG sh = MulDiv(cySrc < 0 ? -cySrc : cySrc, bm.bmHeight, height_);
        HDC mem = CreateCompatibleDC(dc);
        HGDIOBJ old = SelectObject(mem, handle_);
        int mode = SetStretchBltMode(dc, COLORONCOLOR);
        BOOL ok = StretchBlt(dc, x, y, cx, cy, mem, sx, sy, sw, sh, SRCCOPY);
        SetStretchBltMode(dc, mode);
        SelectObject(mem, old);
        DeleteDC(mem);
        return ok ? S_OK : E_FAIL;
      }
      case PICTYPE_ICON:
        return DrawIconEx(dc, x, y, (HICON)handle_, cx, cy, 0, NULL, DI_NORMAL) ? S_OK : E_FAIL;
      case PICTYPE_METAFILE: {
        int saved = SaveDC(dc);
        SetMapMode(dc, MM_ANISOTROPIC);
        SetWindowOrgEx(dc, 0, 0, NULL);
        SetWindowExtEx(dc, width_ ? width_ : 1, height_ ? height_ : 1, NULL);
        SetViewportOrgEx(dc, x, y, NULL);
        SetViewportExtEx(dc, cx, cy, NULL);
        BOOL ok = PlayMetaFile(dc, (HMETAFILE)handle_);
        RestoreDC(dc, saved);
        return ok ? S_OK : E_FAIL;
      }
      case PICTYPE_ENHMETAFILE: {
        RECT rc = {x, y, x + cx, y + cy};
        return PlayEnhMetaFile(dc, (HENHMETAFILE)handle_, &rc) ? S_OK : E_FAIL;
      }
    }
    return E_FAIL;
  }
  STDMETHOD(set_hPal)(OLE_HANDLE h) override {
    if (type_ != PICTYPE_BITMAP) return E_FAIL;
    hpal_ = (HPALETTE)(UINT_PTR)h;
    return S_OK;
  }
  STDMETHOD(get_CurDC)(HDC* p) override {
    if (p == NULL) return E_POINTER;
    *p = curDC_;
    return S_OK;
  }
  STDMETHOD(SelectPicture)(HDC in, HDC* pOut, OLE_HANDLE* pBmp) override {
    if (type_ != PICTYPE_BITMAP) return E_FAIL;
    if (in) SelectObject(in, handle_);
    if (pOut) *pOut = curDC_;
    if (pBmp) *pBmp = (OLE_HANDLE)(UINT_PTR)handle_;
    curDC_ = in;
    return S_OK;
  }
  STDMETHOD(get_KeepOriginalFormat)(BOOL* p) override {
    if (p == NULL) return E_POINTER;
    *p = keep_;
    return S_OK;
  }
  STDMETHOD(put_KeepOriginalFormat)(BOOL b) override {
    keep_ = b != FALSE;
    return S_OK;
  }
  STDMETHOD(PictureChanged)() override {
    original_.clear();
    return S_OK;
  }
  STDMETHOD(SaveAsFile)(IStream* s, BOOL, LONG* pcb) override {
    if (s == NULL) return E_POINTER;
    std::vector<BYTE> file;
    HRESULT hr = FileBytes(&file);
    if (FAILED(hr)) return hr;
    hr = WriteAll(s, file.data(), (ULONG)file.size());
    if (SUCCEEDED(hr) && pcb) *pcb = (LONG)file.size();
    return hr;
  }
  STDMETHOD(get_Attributes)(DWORD* p) override {
    if (p == NULL) return E_POINTER;
    *p = type_ == PICTYPE_METAFILE || type_ == PICTYPE_ENHMETAFILE ? PICTURE_SCALABLE
         : type_ == PICTYPE_ICON                                   ? PICTURE_TRANSPARENT
                                                                   : 0;
    return S_OK;
  }

  // IPictureDisp
  STDMETHOD(GetTypeInfoCount)(UINT* p) override {
    if (p == NULL) return E_POINTER;
    *p = 0;
    return S_OK;
  }
  STDMETHOD(GetTypeInfo)(UINT, LCID, ITypeInfo** p) override {
    if (p) *p = NULL;
    return DISP_E_BADINDEX;
  }
  STDMETHOD(GetIDsOfNames)(REFIID, LPOLESTR* names, UINT c, LCID, DISPID* ids) override {
    static const struct {
      const wchar_t* name;
      DISPID id;
    } kNames[] = {{L"Handle", DISPID_PICT_HANDLE}, {L"hPal", DISPID_PICT_HPAL},
                  {L"Type", DISPID_PICT_TYPE},     {L"Width", DISPID_PICT_WIDTH},
                  {L"Height", DISPID_PICT_HEIGHT}, {L"Render", DISPID_PICT_RENDER}};
    if (names == NULL || ids == NULL || c == 0) return E_INVALIDARG;
    for (UINT i = 0; i < c; i++) ids[i] = DISPID_UNKNOWN;
    for (const auto& k : kNames)
      if (lstrcmpiW(names[0], k.name) == 0) ids[0] = k.id;
    return ids[0] == DISPID_UNKNOWN || c > 1 ? DISP_E_UNKNOWNNAME : S_OK;
  }
  STDMETHOD(Invoke)(DISPID id, REFIID, LCID, WORD flags, DISPPARAMS* dp, VARIANT* res, EXCEPINFO*,
                    UINT*) override {
    if (id == DISPID_PICT_HPAL && (flags & DISPATCH_PROPERTYPUT)) {
      VARIANT v;
      HRESULT hr = PutArg(dp, VT_I4, &v);
      return FAILED(hr) ? hr : set_hPal((OLE_HANDLE)v.lVal);
    }
    if (!(flags & (DISPATCH_PROPERTYGET | DISPATCH_METHOD))) return DISP_E_MEMBERNOTFOUND;
    VARIANT v;
    VariantInit(&v);
    v.vt = VT_I4;
    switch (id) {
      case DISPID_PICT_HANDLE: v.lVal = (LONG)(UINT_PTR)handle_; break;
      case DISPID_PICT_HPAL: v.lVal = (LONG)(UINT_PTR)hpal_; break;
      case DISPID_PICT_TYPE:
        v.vt = VT_I2;
        v.iVal = type_;
        break;
      case DISPID_PICT_WIDTH: v.lVal = width_; break;
      case DISPID_PICT_HEIGHT: v.lVal = height_; break;
      default: return DISP_E_MEMBERNOTFOUND;
    }
    if (res) *res = v;
    return S_OK;
  }

  // IPersistStream: "lt\0\0", the length, then the picture file.
  STDMETHOD(GetClassID)(CLSID* p) override {
    if (p == NULL) return E_POINTER;
    *p = CLSID_StdPicture;
    return S_OK;
  }
  STDMETHOD(IsDirty)() override { return S_FALSE; }
  STDMETHOD(Load)(IStream* s) override;
  STDMETHOD(Save)(IStream* s, BOOL) override {
    if (s == NULL) return E_POINTER;
    std::vector<BYTE> file;
    if (type_ != PICTYPE_NONE && type_ != PICTYPE_UNINITIALIZED) {
      HRESULT hr = FileBytes(&file);
      if (FAILED(hr)) return hr;
    }
    DWORD head[2] = {0x0000746c, (DWORD)file.size()};
    HRESULT hr = WriteAll(s, head, sizeof head);
    if (SUCCEEDED(hr) && !file.empty()) hr = WriteAll(s, file.data(), (ULONG)file.size());
    return hr;
  }
  STDMETHOD(GetSizeMax)(ULARGE_INTEGER* p) override {
    if (p == NULL) return E_POINTER;
    std::vector<BYTE> file;
    if (type_ != PICTYPE_NONE && type_ != PICTYPE_UNINITIALIZED) FileBytes(&file);
    p->QuadPart = 8 + file.size();
    return S_OK;
  }

  HRESULT FromFile(const BYTE* p, size_t n);

private:
  ~Picture() { Free(); }
  void Free() {
    if (!own_ || handle_ == NULL) return;
    switch (type_) {
      case PICTYPE_BITMAP: DeleteObject(handle_); break;
      case PICTYPE_ICON: DestroyIcon((HICON)handle_); break;
      case PICTYPE_METAFILE: DeleteMetaFile((HMETAFILE)handle_); break;
      case PICTYPE_ENHMETAFILE: DeleteEnhMetaFile((HENHMETAFILE)handle_); break;
    }
    handle_ = NULL;
  }
  HRESULT FileBytes(std::vector<BYTE>* out) {
    if (keep_ && !original_.empty()) {
      *out = original_;
      return S_OK;
    }
    bool ok = false;
    switch (type_) {
      case PICTYPE_BITMAP: ok = BitmapFile((HBITMAP)handle_, out); break;
      case PICTYPE_ICON: ok = IconFile((HICON)handle_, out); break;
      case PICTYPE_METAFILE: ok = MetafileFile((HMETAFILE)handle_, width_, height_, out); break;
      case PICTYPE_ENHMETAFILE: ok = EnhMetafileFile((HENHMETAFILE)handle_, out); break;
      default: return E_UNEXPECTED;
    }
    return ok ? S_OK : E_FAIL;
  }

  std::atomic<ULONG> refs_{1};
};

// A picture file: a bitmap ("BM"), an icon, a placeable metafile or an
// enhanced one.
HRESULT Picture::FromFile(const BYTE* p, size_t n) {
  if (n >= 8 && p[0] == 'l' && p[1] == 't' && p[2] == 0 && p[3] == 0) {
    DWORD len;
    memcpy(&len, p + 4, 4);
    p += 8;
    n -= 8;
    if (len < n) n = len;
    if (n == 0) {
      type_ = PICTYPE_NONE;
      return S_OK;
    }
  }
  if (n >= sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER) && p[0] == 'B' && p[1] == 'M') {
    BITMAPFILEHEADER fh;
    memcpy(&fh, p, sizeof fh);
    const BITMAPINFO* bi = (const BITMAPINFO*)(p + sizeof fh);
    if (fh.bfOffBits >= n) return CTL_E_INVALIDPICTURE;
    HDC dc = GetDC(NULL);
    HBITMAP hbm = CreateDIBitmap(dc, &bi->bmiHeader, CBM_INIT, p + fh.bfOffBits, bi, DIB_RGB_COLORS);
    ReleaseDC(NULL, dc);
    if (hbm == NULL) return CTL_E_INVALIDPICTURE;
    PICTDESC d;
    memset(&d, 0, sizeof d);
    d.cbSizeofstruct = sizeof d;
    d.picType = PICTYPE_BITMAP;
    d.bmp.hbitmap = hbm;
    Free();
    HRESULT hr = Init(&d, TRUE);
    original_.assign(p, p + n);
    return hr;
  }
  if (n >= 6 + 16 && p[0] == 0 && p[1] == 0 && p[2] == 1 && p[3] == 0) {
    // An icon file: the first image.
    WORD count = (WORD)(p[4] | (p[5] << 8));
    if (count == 0) return CTL_E_INVALIDPICTURE;
    const BYTE* e = p + 6;
    DWORD size, offset;
    memcpy(&size, e + 8, 4);
    memcpy(&offset, e + 12, 4);
    if (offset >= n || size > n - offset) return CTL_E_INVALIDPICTURE;
    int cx = e[0] ? e[0] : 256, cy = e[1] ? e[1] : 256;
    HICON h = CreateIconFromResourceEx((PBYTE)p + offset, size, TRUE, 0x00030000, cx, cy, 0);
    if (h == NULL) return CTL_E_INVALIDPICTURE;
    PICTDESC d;
    memset(&d, 0, sizeof d);
    d.cbSizeofstruct = sizeof d;
    d.picType = PICTYPE_ICON;
    d.icon.hicon = h;
    Free();
    HRESULT hr = Init(&d, TRUE);
    original_.assign(p, p + n);
    return hr;
  }
  DWORD key = 0;
  if (n >= 4) memcpy(&key, p, 4);
  if (n > 22 && key == kPlaceableKey) {
    WORD head[11];
    memcpy(head, p, sizeof head);
    SHORT left = (SHORT)head[3], top = (SHORT)head[4], right = (SHORT)head[5], bottom = (SHORT)head[6];
    WORD inch = head[7] ? head[7] : 1440;
    HMETAFILE mf = SetMetaFileBitsEx((UINT)(n - 22), p + 22);
    if (mf == NULL) return CTL_E_INVALIDPICTURE;
    PICTDESC d;
    memset(&d, 0, sizeof d);
    d.cbSizeofstruct = sizeof d;
    d.picType = PICTYPE_METAFILE;
    d.wmf.hmeta = mf;
    d.wmf.xExt = MulDiv(right - left, kHimetricPerInch, inch);
    d.wmf.yExt = MulDiv(bottom - top, kHimetricPerInch, inch);
    Free();
    HRESULT hr = Init(&d, TRUE);
    original_.assign(p, p + n);
    return hr;
  }
  DWORD type = 0, sig = 0;
  if (n >= sizeof(ENHMETAHEADER)) {
    memcpy(&type, p, 4);
    memcpy(&sig, p + 40, 4);
  }
  if (type == EMR_HEADER && sig == ENHMETA_SIGNATURE) {
    HENHMETAFILE emf = SetEnhMetaFileBits((UINT)n, p);
    if (emf == NULL) return CTL_E_INVALIDPICTURE;
    PICTDESC d;
    memset(&d, 0, sizeof d);
    d.cbSizeofstruct = sizeof d;
    d.picType = PICTYPE_ENHMETAFILE;
    d.emf.hemf = emf;
    Free();
    HRESULT hr = Init(&d, TRUE);
    original_.assign(p, p + n);
    return hr;
  }
  return CTL_E_INVALIDPICTURE;
}

// The rest of a stream, or n bytes of it.
HRESULT ReadStream(IStream* s, LONG n, std::vector<BYTE>* out) {
  out->clear();
  BYTE buf[4096];
  for (;;) {
    ULONG want = sizeof buf;
    if (n > 0) {
      if ((LONG)out->size() >= n) break;
      if ((ULONG)(n - (LONG)out->size()) < want) want = (ULONG)(n - (LONG)out->size());
    }
    ULONG got = 0;
    HRESULT hr = s->Read(buf, want, &got);
    if (FAILED(hr)) return hr;
    if (got == 0) break;
    out->insert(out->end(), buf, buf + got);
  }
  return S_OK;
}

HRESULT Picture::Load(IStream* s) {
  if (s == NULL) return E_POINTER;
  DWORD head[2];
  HRESULT hr = ReadAll(s, head, sizeof head);
  if (FAILED(hr)) return hr;
  std::vector<BYTE> file;
  if (head[0] == 0x0000746c) {
    if (head[1] == 0) {
      Free();
      type_ = PICTYPE_NONE;
      return S_OK;
    }
    hr = ReadStream(s, (LONG)head[1], &file);
  } else {
    // No header: the file itself.
    file.assign((BYTE*)head, (BYTE*)head + sizeof head);
    std::vector<BYTE> rest;
    hr = ReadStream(s, 0, &rest);
    file.insert(file.end(), rest.begin(), rest.end());
  }
  if (FAILED(hr)) return hr;
  return FromFile(file.data(), file.size());
}

}  // namespace

extern "C" {

HRESULT WINAPI OleTranslateColor(OLE_COLOR clr, HPALETTE hpal, COLORREF* out) {
  COLORREF c;
  switch (clr >> 24) {
    case 0x00: c = clr; break;
    case 0x01:  // a palette index
      if (hpal) {
        UINT n = GetPaletteEntries(hpal, 0, 0, NULL);
        if ((clr & 0xffff) >= n) return E_INVALIDARG;
      }
      c = clr;
      break;
    case 0x02: c = clr; break;  // a palette-relative RGB
    case 0x80:
      if ((clr & 0xffffff) > COLOR_MENUBAR) return E_INVALIDARG;
      c = GetSysColor((int)(clr & 0xff));
      break;
    default: return E_INVALIDARG;
  }
  if (out) *out = c;
  return S_OK;
}

HRESULT WINAPI OleCreateFontIndirect(LPFONTDESC desc, REFIID riid, LPVOID* out) {
  if (out == NULL) return E_POINTER;
  *out = NULL;
  Font* f = new Font;
  if (desc) {
    f->Init(desc);
  } else {
    FONTDESC d;
    memset(&d, 0, sizeof d);
    d.lpstrName = (LPOLESTR)L"System";
    d.cySize.int64 = 120000;
    d.sWeight = FW_NORMAL;
    d.sCharset = DEFAULT_CHARSET;
    f->Init(&d);
  }
  HRESULT hr = f->QueryInterface(riid, out);
  f->Release();
  return hr;
}

HRESULT WINAPI OleCreatePictureIndirect(LPPICTDESC desc, REFIID riid, BOOL own, LPVOID* out) {
  if (out == NULL) return E_POINTER;
  *out = NULL;
  Picture* p = new Picture;
  HRESULT hr = p->Init(desc, own);
  if (SUCCEEDED(hr)) hr = p->QueryInterface(riid, out);
  p->Release();
  return hr;
}

HRESULT WINAPI OleLoadPicture(LPSTREAM s, LONG size, BOOL, REFIID riid, LPVOID* out) {
  if (s == NULL || out == NULL) return E_POINTER;
  *out = NULL;
  std::vector<BYTE> file;
  HRESULT hr = ReadStream(s, size, &file);
  if (FAILED(hr)) return hr;
  Picture* p = new Picture;
  hr = p->FromFile(file.data(), file.size());
  if (SUCCEEDED(hr)) hr = p->QueryInterface(riid, out);
  p->Release();
  return hr;
}

HRESULT WINAPI OleLoadPictureEx(LPSTREAM s, LONG size, BOOL runMode, REFIID riid, DWORD, DWORD, DWORD,
                                LPVOID* out) {
  return OleLoadPicture(s, size, runMode, riid, out);
}

}  // extern "C"

// ---------------------------------------------------------------------
// The class objects of the standard font and picture, which a saved
// control's font or picture is loaded through (OleLoadFromStream).

namespace {

class StdFactory final : public IClassFactory {
 public:
  explicit StdFactory(bool font) : font_(font) {}
  STDMETHOD(QueryInterface)(REFIID riid, void** out) override {
    if (out == NULL) return E_POINTER;
    if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, IID_IClassFactory)) {
      *out = static_cast<IClassFactory*>(this);
      return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
  }
  // Static, so not counted.
  STDMETHOD_(ULONG, AddRef)() override { return 2; }
  STDMETHOD_(ULONG, Release)() override { return 1; }
  STDMETHOD(CreateInstance)(IUnknown* outer, REFIID riid, void** out) override {
    if (out == NULL) return E_POINTER;
    *out = NULL;
    if (outer != NULL) return CLASS_E_NOAGGREGATION;
    if (font_) return OleCreateFontIndirect(NULL, riid, out);
    PICTDESC d = {sizeof d, (UINT)PICTYPE_UNINITIALIZED};
    return OleCreatePictureIndirect(&d, riid, FALSE, out);
  }
  STDMETHOD(LockServer)(BOOL) override { return S_OK; }

 private:
  bool font_;
};

}  // namespace

namespace winport {
namespace ole {

HRESULT GetBuiltinClassObject(REFCLSID clsid, REFIID riid, void** out) {
  static StdFactory font(true), picture(false);
  *out = NULL;
  if (IsEqualCLSID(clsid, CLSID_StdFont)) return font.QueryInterface(riid, out);
  if (IsEqualCLSID(clsid, CLSID_StdPicture)) return picture.QueryInterface(riid, out);
  return REGDB_E_CLASSNOTREG;
}

}  // namespace ole
}  // namespace winport
