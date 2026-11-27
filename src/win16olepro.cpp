// win16olepro.cpp - the parts of OLE control support that 16-bit OLE 2
// does not have, for 16-bit Windows: the stock font object
// (OleCreateFontIndirect), the stock picture object
// (OleCreatePictureIndirect, OleLoadPicture), OleTranslateColor and the
// property frame (OleCreatePropertyFrame). On Windows 3.1 these came
// from the OLE control runtime of the CDK; the library makes them
// itself. The file is empty in the other builds.
//
// The font tells IPropertyNotifySink sinks about each change. Both
// objects have a dispinterface (IFontDisp, IPictureDisp) with the stock
// DISPIDs and save themselves to a stream (IPersistStream): the font as
// its attributes, the picture as a picture file (a bitmap, an icon or a
// placeable metafile) after a small header. The stream formats are those
// of Win32, so a saved control loads in both.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#include "stdafx.h"

#ifdef _AFX_WIN16
#include "afximpl.h"
#include <afxcmn.h>  // CTabCtrl
#include <afxdisp.h>
#include <afxocc.h>  // COleControlSite
#include <ocidl.h>
#include <olectl.h>

namespace {

const long kHimetricPerInch = 2540;

/// Gives the pixels per inch of the screen, across (y FALSE) or down.
int ScreenDpi(BOOL bY) {
  HDC hdc = ::GetDC(NULL);
  int n = ::GetDeviceCaps(hdc, bY ? LOGPIXELSY : LOGPIXELSX);
  ::ReleaseDC(NULL, hdc);
  return n > 0 ? n : 96;
}

/// Changes pixels to HIMETRIC units, across (y FALSE) or down.
long PixelsToHimetric(int nPixels, BOOL bY) {
  return _AfxMulDiv(nPixels, kHimetricPerInch, ScreenDpi(bY));
}

// ---------------------------------------------------------------------
// A block of bytes in global memory, which can be larger than 64 KB.

class CW16Bytes {
public:
  /// Makes an empty block.
  CW16Bytes() : m_hMem(NULL), m_cb(0) {}
  /// Frees the block.
  ~CW16Bytes() { Free(); }

  /// Frees the memory and makes the block empty.
  void Free() {
    if (m_hMem != NULL) ::GlobalFree(m_hMem);
    m_hMem = NULL;
    m_cb = 0;
  }
  /// Sets the size of the block. Keeps the bytes that stay. Gives FALSE
  /// when there is no memory.
  BOOL SetSize(DWORD cb) {
    if (cb == 0) {
      Free();
      return TRUE;
    }
    HGLOBAL hNew = m_hMem == NULL ? ::GlobalAlloc(GMEM_MOVEABLE, cb)
                                  : ::GlobalReAlloc(m_hMem, cb, GMEM_MOVEABLE);
    if (hNew == NULL) return FALSE;
    m_hMem = hNew;
    m_cb = cb;
    return TRUE;
  }
  /// Adds cb bytes at the end. Gives FALSE when there is no memory.
  BOOL Append(const void HUGEP* pv, DWORD cb) {
    DWORD cbOld = m_cb;
    if (cb == 0) return TRUE;
    if (!SetSize(cbOld + cb)) return FALSE;
    BYTE HUGEP* p = Lock();
    ::hmemcpy(p + cbOld, pv, (long)cb);
    Unlock();
    return TRUE;
  }
  /// Makes this block a copy of another.
  BOOL CopyFrom(const CW16Bytes& other) {
    Free();
    if (other.m_cb == 0) return TRUE;
    BOOL bOK = Append(other.Lock(), other.m_cb);
    other.Unlock();
    return bOK;
  }
  /// Locks the block and gives a huge pointer to its first byte.
  BYTE HUGEP* Lock() const { return m_hMem != NULL ? (BYTE HUGEP*)::GlobalLock(m_hMem) : NULL; }
  /// Unlocks the block after Lock.
  void Unlock() const {
    if (m_hMem != NULL) ::GlobalUnlock(m_hMem);
  }
  /// Gives the size of the block in bytes.
  DWORD GetSize() const { return m_cb; }
  /// Tells if the block is empty.
  BOOL IsEmpty() const { return m_cb == 0; }

private:
  HGLOBAL m_hMem;
  DWORD m_cb;
};

/// Reads n bytes from a stream. Gives E_FAIL when the stream ends first.
HRESULT ReadAll(LPSTREAM pStm, void FAR* pv, ULONG cb) {
  ULONG cbGot = 0;
  HRESULT hr = pStm->Read(pv, cb, &cbGot);
  if (FAILED(hr)) return hr;
  return cbGot == cb ? S_OK : E_FAIL;
}

/// Writes n bytes to a stream. Gives STG_E_MEDIUMFULL when not all go.
HRESULT WriteAll(LPSTREAM pStm, const void HUGEP* pv, ULONG cb) {
  ULONG cbDone = 0;
  HRESULT hr = pStm->Write(pv, cb, &cbDone);
  if (FAILED(hr)) return hr;
  return cbDone == cb ? S_OK : STG_E_MEDIUMFULL;
}

/// Reads the rest of a stream, or cbMax bytes of it when cbMax is more
/// than 0, to the end of a block.
HRESULT ReadStream(LPSTREAM pStm, LONG cbMax, CW16Bytes* pOut) {
  BYTE buf[1024];
  for (;;) {
    ULONG cbWant = sizeof(buf);
    if (cbMax > 0) {
      if ((LONG)pOut->GetSize() >= cbMax) break;
      if ((ULONG)(cbMax - (LONG)pOut->GetSize()) < cbWant)
        cbWant = (ULONG)(cbMax - (LONG)pOut->GetSize());
    }
    ULONG cbGot = 0;
    HRESULT hr = pStm->Read(buf, cbWant, &cbGot);
    if (FAILED(hr)) return hr;
    if (cbGot == 0) break;
    if (!pOut->Append(buf, cbGot)) return E_OUTOFMEMORY;
  }
  return S_OK;
}

/// Gets the one argument of a property put as type vt.
HRESULT PutArg(DISPPARAMS FAR* pdp, VARTYPE vt, VARIANT* pOut) {
  if (pdp == NULL || pdp->cArgs != 1) return DISP_E_BADPARAMCOUNT;
  ::VariantInit(pOut);
  HRESULT hr = ::VariantChangeType(pOut, &pdp->rgvarg[0], 0, vt);
  return FAILED(hr) ? DISP_E_TYPEMISMATCH : S_OK;
}

// ---------------------------------------------------------------------
// The enumerators and the connection point of the font.

// An enumerator of a copy of the connections of a point.
class CW16EnumConnections : public IEnumConnections {
public:
  /// Takes the connections; each one is AddRef'd already.
  CW16EnumConnections(CONNECTDATA* pItems, ULONG nCount)
      : m_cRef(1), m_nPos(0), m_nCount(nCount), m_pItems(pItems) {}
  /// Releases the connections.
  virtual ~CW16EnumConnections() {
    for (ULONG i = 0; i < m_nCount; i++) m_pItems[i].pUnk->Release();
    delete[] m_pItems;
  }
  /// Gives IUnknown and IEnumConnections.
  STDMETHOD(QueryInterface)(REFIID riid, LPVOID FAR* ppv) {
    if (ppv == NULL) return E_POINTER;
    if (riid == IID_IUnknown || riid == IID_IEnumConnections) {
      *ppv = this;
      AddRef();
      return S_OK;
    }
    *ppv = NULL;
    return E_NOINTERFACE;
  }
  /// Adds a reference.
  STDMETHOD_(ULONG, AddRef)() { return ++m_cRef; }
  /// Removes a reference; the last one deletes the enumerator.
  STDMETHOD_(ULONG, Release)() {
    ULONG n = --m_cRef;
    if (n == 0) delete this;
    return n;
  }
  /// Gives the next connections, each one AddRef'd.
  STDMETHOD(Next)(ULONG cWant, LPCONNECTDATA rgcd, ULONG FAR* pcGot) {
    ULONG n = 0;
    while (n < cWant && m_nPos < m_nCount && rgcd != NULL) {
      rgcd[n] = m_pItems[m_nPos++];
      rgcd[n].pUnk->AddRef();
      n++;
    }
    if (pcGot != NULL) *pcGot = n;
    return n == cWant ? S_OK : S_FALSE;
  }
  /// Skips connections.
  STDMETHOD(Skip)(ULONG c) {
    m_nPos += c;
    if (m_nPos <= m_nCount) return S_OK;
    m_nPos = m_nCount;
    return S_FALSE;
  }
  /// Goes back to the first connection.
  STDMETHOD(Reset)() {
    m_nPos = 0;
    return S_OK;
  }
  /// Makes a copy of the enumerator at the same position.
  STDMETHOD(Clone)(IEnumConnections FAR* FAR* ppEnum) {
    if (ppEnum == NULL) return E_POINTER;
    CONNECTDATA* pItems = new CONNECTDATA[m_nCount > 0 ? m_nCount : 1];
    for (ULONG i = 0; i < m_nCount; i++) {
      pItems[i] = m_pItems[i];
      pItems[i].pUnk->AddRef();
    }
    CW16EnumConnections* pEnum = new CW16EnumConnections(pItems, m_nCount);
    pEnum->m_nPos = m_nPos;
    *ppEnum = pEnum;
    return S_OK;
  }

private:
  ULONG m_cRef;
  ULONG m_nPos;
  ULONG m_nCount;
  CONNECTDATA* m_pItems;
};

// One connection point, for the sinks of one interface. Its references
// are those of its owner.
class CW16ConnPoint : public IConnectionPoint {
public:
  /// Makes a point of the owner for the interface iid.
  CW16ConnPoint(IConnectionPointContainer* pOwner, REFIID iid)
      : m_pOwner(pOwner), m_iid(iid), m_dwNext(0) {}
  /// Releases the sinks.
  virtual ~CW16ConnPoint() {
    for (int i = 0; i < m_sinks.GetSize(); i++) ((LPUNKNOWN)m_sinks[i])->Release();
  }
  /// Gives IUnknown and IConnectionPoint.
  STDMETHOD(QueryInterface)(REFIID riid, LPVOID FAR* ppv) {
    if (ppv == NULL) return E_POINTER;
    if (riid == IID_IUnknown || riid == IID_IConnectionPoint) {
      *ppv = this;
      AddRef();
      return S_OK;
    }
    *ppv = NULL;
    return E_NOINTERFACE;
  }
  /// Adds a reference to the owner.
  STDMETHOD_(ULONG, AddRef)() { return m_pOwner->AddRef(); }
  /// Removes a reference from the owner.
  STDMETHOD_(ULONG, Release)() { return m_pOwner->Release(); }
  /// Gives the IID of the sinks.
  STDMETHOD(GetConnectionInterface)(IID FAR* piid) {
    if (piid == NULL) return E_POINTER;
    *piid = m_iid;
    return S_OK;
  }
  /// Gives the owner.
  STDMETHOD(GetConnectionPointContainer)(IConnectionPointContainer FAR* FAR* ppCPC) {
    if (ppCPC == NULL) return E_POINTER;
    *ppCPC = m_pOwner;
    m_pOwner->AddRef();
    return S_OK;
  }
  /// Connects a sink that has the interface of the point.
  STDMETHOD(Advise)(IUnknown FAR* pUnkSink, DWORD FAR* pdwCookie) {
    if (pUnkSink == NULL || pdwCookie == NULL) return E_POINTER;
    LPUNKNOWN pSink = NULL;
    if (FAILED(pUnkSink->QueryInterface(m_iid, (LPVOID FAR*)&pSink)) || pSink == NULL)
      return CONNECT_E_CANNOTCONNECT;
    m_sinks.Add(pSink);
    m_cookies.Add(++m_dwNext);
    *pdwCookie = m_dwNext;
    return S_OK;
  }
  /// Disconnects the sink of a cookie.
  STDMETHOD(Unadvise)(DWORD dwCookie) {
    for (int i = 0; i < m_cookies.GetSize(); i++)
      if (m_cookies[i] == dwCookie) {
        LPUNKNOWN pSink = (LPUNKNOWN)m_sinks[i];
        m_sinks.RemoveAt(i);
        m_cookies.RemoveAt(i);
        pSink->Release();
        return S_OK;
      }
    return CONNECT_E_NOCONNECTION;
  }
  /// Gives an enumerator of a copy of the connections.
  STDMETHOD(EnumConnections)(IEnumConnections FAR* FAR* ppEnum) {
    if (ppEnum == NULL) return E_POINTER;
    int n = (int)m_sinks.GetSize();
    CONNECTDATA* pItems = new CONNECTDATA[n > 0 ? n : 1];
    for (int i = 0; i < n; i++) {
      pItems[i].pUnk = (LPUNKNOWN)m_sinks[i];
      pItems[i].pUnk->AddRef();
      pItems[i].dwCookie = m_cookies[i];
    }
    *ppEnum = new CW16EnumConnections(pItems, (ULONG)n);
    return S_OK;
  }

  /// Tells each property notification sink that a property changed. A
  /// sink may unadvise while it is told, so the call uses a copy.
  void NotifyChanged(DISPID dispid) {
    CPtrArray copy;
    copy.Copy(m_sinks);
    for (int i = 0; i < copy.GetSize(); i++) ((LPUNKNOWN)copy[i])->AddRef();
    for (int i = 0; i < copy.GetSize(); i++) {
      ((IPropertyNotifySink*)copy[i])->OnChanged(dispid);
      ((LPUNKNOWN)copy[i])->Release();
    }
  }

private:
  IConnectionPointContainer* m_pOwner;
  IID m_iid;
  CPtrArray m_sinks;
  CDWordArray m_cookies;
  DWORD m_dwNext;
};

// An enumerator of the one connection point of an object.
class CW16EnumOnePoint : public IEnumConnectionPoints {
public:
  /// Makes an enumerator of the point.
  explicit CW16EnumOnePoint(IConnectionPoint* pPoint) : m_cRef(1), m_pPoint(pPoint), m_nPos(0) {
    m_pPoint->AddRef();
  }
  /// Releases the point.
  virtual ~CW16EnumOnePoint() { m_pPoint->Release(); }
  /// Gives IUnknown and IEnumConnectionPoints.
  STDMETHOD(QueryInterface)(REFIID riid, LPVOID FAR* ppv) {
    if (ppv == NULL) return E_POINTER;
    if (riid == IID_IUnknown || riid == IID_IEnumConnectionPoints) {
      *ppv = this;
      AddRef();
      return S_OK;
    }
    *ppv = NULL;
    return E_NOINTERFACE;
  }
  /// Adds a reference.
  STDMETHOD_(ULONG, AddRef)() { return ++m_cRef; }
  /// Removes a reference; the last one deletes the enumerator.
  STDMETHOD_(ULONG, Release)() {
    ULONG n = --m_cRef;
    if (n == 0) delete this;
    return n;
  }
  /// Gives the point, the first time.
  STDMETHOD(Next)(ULONG cWant, LPCONNECTIONPOINT FAR* ppCP, ULONG FAR* pcGot) {
    ULONG n = 0;
    if (cWant > 0 && m_nPos == 0 && ppCP != NULL) {
      ppCP[0] = m_pPoint;
      m_pPoint->AddRef();
      m_nPos = 1;
      n = 1;
    }
    if (pcGot != NULL) *pcGot = n;
    return n == cWant ? S_OK : S_FALSE;
  }
  /// Skips the point.
  STDMETHOD(Skip)(ULONG c) {
    if (c == 0) return S_OK;
    BOOL bOK = m_nPos == 0 && c == 1;
    m_nPos = 1;
    return bOK ? S_OK : S_FALSE;
  }
  /// Goes back to the point.
  STDMETHOD(Reset)() {
    m_nPos = 0;
    return S_OK;
  }
  /// Makes a copy of the enumerator at the same position.
  STDMETHOD(Clone)(IEnumConnectionPoints FAR* FAR* ppEnum) {
    if (ppEnum == NULL) return E_POINTER;
    CW16EnumOnePoint* pEnum = new CW16EnumOnePoint(m_pPoint);
    pEnum->m_nPos = m_nPos;
    *ppEnum = pEnum;
    return S_OK;
  }

private:
  ULONG m_cRef;
  IConnectionPoint* m_pPoint;
  int m_nPos;
};

// ---------------------------------------------------------------------
// The font

class CW16Font : public IFont,
                 public IFontDisp,
                 public IPersistStream,
                 public IConnectionPointContainer {
public:
  /// Makes the font: 12-point System, of normal weight.
  CW16Font()
      : m_cRef(1),
        m_notify(this, IID_IPropertyNotifySink),
        m_llSize(120000),
        m_sWeight(FW_NORMAL),
        m_sCharset(DEFAULT_CHARSET),
        m_bItalic(FALSE),
        m_bUnderline(FALSE),
        m_bStrike(FALSE),
        m_bDirty(FALSE),
        m_cyLogical(96),
        m_cyHimetric(kHimetricPerInch),
        m_hFont(NULL) {
    lstrcpy(m_szName, "System");
  }

  /// Takes the attributes of a font description.
  void Init(const FONTDESC FAR* pDesc) {
    lstrcpyn(m_szName, pDesc->lpstrName != NULL ? pDesc->lpstrName : "", LF_FACESIZE);
    m_llSize = pDesc->cySize.int64;
    m_sWeight = pDesc->sWeight;
    m_sCharset = pDesc->sCharset;
    m_bItalic = pDesc->fItalic != FALSE;
    m_bUnderline = pDesc->fUnderline != FALSE;
    m_bStrike = pDesc->fStrikethrough != FALSE;
  }

  // IUnknown

  /// Gives IFont, IFontDisp, IPersistStream and IConnectionPointContainer.
  STDMETHOD(QueryInterface)(REFIID riid, LPVOID FAR* ppv) {
    if (ppv == NULL) return E_POINTER;
    if (riid == IID_IUnknown || riid == IID_IFont)
      *ppv = (IFont*)this;
    else if (riid == IID_IDispatch || riid == IID_IFontDisp)
      *ppv = (IFontDisp*)this;
    else if (riid == IID_IPersist || riid == IID_IPersistStream)
      *ppv = (IPersistStream*)this;
    else if (riid == IID_IConnectionPointContainer)
      *ppv = (IConnectionPointContainer*)this;
    else {
      *ppv = NULL;
      return E_NOINTERFACE;
    }
    AddRef();
    return S_OK;
  }
  /// Adds a reference.
  STDMETHOD_(ULONG, AddRef)() { return ++m_cRef; }
  /// Removes a reference; the last one deletes the font.
  STDMETHOD_(ULONG, Release)() {
    ULONG n = --m_cRef;
    if (n == 0) delete this;
    return n;
  }

  // IFont

  /// Gives the face name.
  STDMETHOD(get_Name)(BSTR FAR* pName) {
    if (pName == NULL) return E_POINTER;
    *pName = ::SysAllocString(m_szName);
    return S_OK;
  }
  /// Sets the face name.
  STDMETHOD(put_Name)(BSTR name) {
    if (name == NULL) return CTL_E_INVALIDPROPERTYVALUE;
    lstrcpyn(m_szName, name, LF_FACESIZE);
    return Changed(DISPID_FONT_NAME);
  }
  /// Gives the size in points, as currency.
  STDMETHOD(get_Size)(CY FAR* pSize) {
    if (pSize == NULL) return E_POINTER;
    pSize->int64 = m_llSize;
    return S_OK;
  }
  /// Sets the size in points, as currency.
  STDMETHOD(put_Size)(CY size) {
    if (size.int64 < 0) return CTL_E_INVALIDPROPERTYVALUE;
    m_llSize = size.int64;
    return Changed(DISPID_FONT_SIZE);
  }
  /// Tells if the weight is bold or more.
  STDMETHOD(get_Bold)(BOOL FAR* pBold) {
    if (pBold == NULL) return E_POINTER;
    *pBold = m_sWeight >= FW_BOLD;
    return S_OK;
  }
  /// Sets the weight to bold or normal.
  STDMETHOD(put_Bold)(BOOL bold) {
    m_sWeight = bold ? FW_BOLD : FW_NORMAL;
    return Changed(DISPID_FONT_BOLD);
  }
  /// Tells if the font is italic.
  STDMETHOD(get_Italic)(BOOL FAR* p) { return GetFlag(m_bItalic, p); }
  /// Sets the italic flag.
  STDMETHOD(put_Italic)(BOOL b) { return SetFlag(&m_bItalic, b, DISPID_FONT_ITALIC); }
  /// Tells if the font is underlined.
  STDMETHOD(get_Underline)(BOOL FAR* p) { return GetFlag(m_bUnderline, p); }
  /// Sets the underline flag.
  STDMETHOD(put_Underline)(BOOL b) { return SetFlag(&m_bUnderline, b, DISPID_FONT_UNDER); }
  /// Tells if the font is struck through.
  STDMETHOD(get_Strikethrough)(BOOL FAR* p) { return GetFlag(m_bStrike, p); }
  /// Sets the strikethrough flag.
  STDMETHOD(put_Strikethrough)(BOOL b) { return SetFlag(&m_bStrike, b, DISPID_FONT_STRIKE); }
  /// Gives the weight.
  STDMETHOD(get_Weight)(SHORT FAR* pWeight) {
    if (pWeight == NULL) return E_POINTER;
    *pWeight = m_sWeight;
    return S_OK;
  }
  /// Sets the weight.
  STDMETHOD(put_Weight)(SHORT weight) {
    m_sWeight = weight;
    return Changed(DISPID_FONT_WEIGHT);
  }
  /// Gives the character set.
  STDMETHOD(get_Charset)(SHORT FAR* pCharset) {
    if (pCharset == NULL) return E_POINTER;
    *pCharset = m_sCharset;
    return S_OK;
  }
  /// Sets the character set.
  STDMETHOD(put_Charset)(SHORT charset) {
    m_sCharset = charset;
    return Changed(DISPID_FONT_CHARSET);
  }
  /// Gives a GDI font of the attributes, at the ratio of SetRatio. The
  /// font keeps the handle.
  STDMETHOD(get_hFont)(HFONT FAR* phFont) {
    if (phFont == NULL) return E_POINTER;
    if (m_hFont == NULL) {
      LOGFONT lf;
      memset(&lf, 0, sizeof(lf));
      // Points (in ten-thousandths) to HIMETRIC, then to logical units.
      long long llHimetric = m_llSize * kHimetricPerInch / 72 / 10000;
      long nHeight = (long)((llHimetric * m_cyLogical + m_cyHimetric / 2) / m_cyHimetric);
      if (nHeight == 0 && m_llSize > 0) nHeight = 1;
      if (nHeight > 32767) nHeight = 32767;
      lf.lfHeight = (int)-nHeight;
      lf.lfWeight = m_sWeight;
      lf.lfItalic = (BYTE)m_bItalic;
      lf.lfUnderline = (BYTE)m_bUnderline;
      lf.lfStrikeOut = (BYTE)m_bStrike;
      lf.lfCharSet = (BYTE)m_sCharset;
      lstrcpyn(lf.lfFaceName, m_szName, LF_FACESIZE);
      m_hFont = ::CreateFontIndirect(&lf);
    }
    *phFont = m_hFont;
    return m_hFont != NULL ? S_OK : E_FAIL;
  }
  /// Makes a new font with the same attributes and ratio.
  STDMETHOD(Clone)(IFont FAR* FAR* ppFont) {
    if (ppFont == NULL) return E_POINTER;
    CW16Font* pFont = new CW16Font;
    lstrcpy(pFont->m_szName, m_szName);
    pFont->m_llSize = m_llSize;
    pFont->m_sWeight = m_sWeight;
    pFont->m_sCharset = m_sCharset;
    pFont->m_bItalic = m_bItalic;
    pFont->m_bUnderline = m_bUnderline;
    pFont->m_bStrike = m_bStrike;
    pFont->m_cyLogical = m_cyLogical;
    pFont->m_cyHimetric = m_cyHimetric;
    *ppFont = pFont;
    return S_OK;
  }
  /// Tells if another font has the same attributes (S_OK) or not.
  STDMETHOD(IsEqual)(IFont FAR* pOther) {
    if (pOther == NULL) return E_POINTER;
    BSTR bstrName = NULL;
    CY cy;
    cy.int64 = 0;
    SHORT sWeight = 0, sCharset = 0;
    BOOL bItalic = FALSE, bUnder = FALSE, bStrike = FALSE;
    pOther->get_Name(&bstrName);
    pOther->get_Size(&cy);
    pOther->get_Weight(&sWeight);
    pOther->get_Charset(&sCharset);
    pOther->get_Italic(&bItalic);
    pOther->get_Underline(&bUnder);
    pOther->get_Strikethrough(&bStrike);
    BOOL bSame = bstrName != NULL && lstrcmp(m_szName, bstrName) == 0 && cy.int64 == m_llSize &&
                 sWeight == m_sWeight && sCharset == m_sCharset &&
                 (bItalic != FALSE) == m_bItalic && (bUnder != FALSE) == m_bUnderline &&
                 (bStrike != FALSE) == m_bStrike;
    ::SysFreeString(bstrName);
    return bSame ? S_OK : S_FALSE;
  }
  /// Sets the ratio of logical units to HIMETRIC units of the font height.
  STDMETHOD(SetRatio)(LONG cyLogical, LONG cyHimetric) {
    if (cyLogical == 0 || cyHimetric == 0) return E_INVALIDARG;
    if (cyLogical != m_cyLogical || cyHimetric != m_cyHimetric) {
      m_cyLogical = cyLogical;
      m_cyHimetric = cyHimetric;
      DropFont();
    }
    return S_OK;
  }
  /// Gives the text metrics of the GDI font on the screen.
  STDMETHOD(QueryTextMetrics)(TEXTMETRIC FAR* pTM) {
    if (pTM == NULL) return E_POINTER;
    HFONT hFont;
    if (FAILED(get_hFont(&hFont))) return E_FAIL;
    HDC hdc = ::GetDC(NULL);
    HGDIOBJ hOld = ::SelectObject(hdc, hFont);
    BOOL bOK = ::GetTextMetrics(hdc, pTM);
    ::SelectObject(hdc, hOld);
    ::ReleaseDC(NULL, hdc);
    return bOK ? S_OK : E_FAIL;
  }
  /// Accepts a reference to a GDI font of this object.
  STDMETHOD(AddRefHfont)(HFONT hFont) { return hFont != NULL ? S_OK : E_INVALIDARG; }
  /// Accepts the release of a GDI font of this object.
  STDMETHOD(ReleaseHfont)(HFONT hFont) { return hFont != NULL ? S_OK : E_INVALIDARG; }
  /// Accepts a device context; the font uses the ratio only.
  STDMETHOD(SetHdc)(HDC) { return S_OK; }

  // IFontDisp

  /// Gives 0: the font has no type information.
  STDMETHOD(GetTypeInfoCount)(UINT FAR* pctinfo) {
    if (pctinfo == NULL) return E_POINTER;
    *pctinfo = 0;
    return S_OK;
  }
  /// Gives no type information.
  STDMETHOD(GetTypeInfo)(UINT, LCID, LPTYPEINFO FAR* ppTI) {
    if (ppTI != NULL) *ppTI = NULL;
    return DISP_E_BADINDEX;
  }
  /// Gives the DISPID of one property name.
  STDMETHOD(GetIDsOfNames)(REFIID, char FAR* FAR* rgszNames, UINT cNames, LCID,
                           DISPID FAR* rgDispId) {
    static const struct {
      const char* pszName;
      DISPID dispid;
    } kNames[] = {{"Name", DISPID_FONT_NAME},       {"Size", DISPID_FONT_SIZE},
                  {"Bold", DISPID_FONT_BOLD},       {"Italic", DISPID_FONT_ITALIC},
                  {"Underline", DISPID_FONT_UNDER}, {"Strikethrough", DISPID_FONT_STRIKE},
                  {"Weight", DISPID_FONT_WEIGHT},   {"Charset", DISPID_FONT_CHARSET}};
    if (rgszNames == NULL || rgDispId == NULL || cNames == 0) return E_INVALIDARG;
    for (UINT i = 0; i < cNames; i++) rgDispId[i] = DISPID_UNKNOWN;
    for (int k = 0; k < (int)(sizeof(kNames) / sizeof(kNames[0])); k++)
      if (lstrcmpi(rgszNames[0], kNames[k].pszName) == 0) rgDispId[0] = kNames[k].dispid;
    return rgDispId[0] == DISPID_UNKNOWN || cNames > 1 ? DISP_E_UNKNOWNNAME : S_OK;
  }
  /// Gets or puts one property by its DISPID.
  STDMETHOD(Invoke)(DISPID dispid, REFIID, LCID, unsigned short wFlags, DISPPARAMS FAR* pdp,
                    VARIANT FAR* pResult, EXCEPINFO FAR*, UINT FAR*) {
    BOOL bPut = (wFlags & (DISPATCH_PROPERTYPUT | DISPATCH_PROPERTYPUTREF)) != 0;
    if (!bPut && !(wFlags & (DISPATCH_PROPERTYGET | DISPATCH_METHOD))) return DISP_E_MEMBERNOTFOUND;
    VARIANT v;
    ::VariantInit(&v);
    HRESULT hr = S_OK;
    switch (dispid) {
      case DISPID_FONT_NAME:
        if (bPut) {
          if (SUCCEEDED(hr = PutArg(pdp, VT_BSTR, &v))) hr = put_Name(v.bstrVal);
        } else {
          v.vt = VT_BSTR;
          hr = get_Name(&v.bstrVal);
        }
        break;
      case DISPID_FONT_SIZE:
        if (bPut) {
          if (SUCCEEDED(hr = PutArg(pdp, VT_CY, &v))) hr = put_Size(v.cyVal);
        } else {
          v.vt = VT_CY;
          hr = get_Size(&v.cyVal);
        }
        break;
      case DISPID_FONT_BOLD:
      case DISPID_FONT_ITALIC:
      case DISPID_FONT_UNDER:
      case DISPID_FONT_STRIKE: {
        if (bPut) {
          if (FAILED(hr = PutArg(pdp, VT_BOOL, &v))) break;
          BOOL b = v.boolVal != VARIANT_FALSE;
          hr = dispid == DISPID_FONT_BOLD     ? put_Bold(b)
               : dispid == DISPID_FONT_ITALIC ? put_Italic(b)
               : dispid == DISPID_FONT_UNDER  ? put_Underline(b)
                                              : put_Strikethrough(b);
        } else {
          BOOL b = FALSE;
          hr = dispid == DISPID_FONT_BOLD     ? get_Bold(&b)
               : dispid == DISPID_FONT_ITALIC ? get_Italic(&b)
               : dispid == DISPID_FONT_UNDER  ? get_Underline(&b)
                                              : get_Strikethrough(&b);
          v.vt = VT_BOOL;
          v.boolVal = b ? VARIANT_TRUE : VARIANT_FALSE;
        }
        break;
      }
      case DISPID_FONT_WEIGHT:
      case DISPID_FONT_CHARSET:
        if (bPut) {
          if (FAILED(hr = PutArg(pdp, VT_I2, &v))) break;
          hr = dispid == DISPID_FONT_WEIGHT ? put_Weight(v.iVal) : put_Charset(v.iVal);
        } else {
          v.vt = VT_I2;
          hr = dispid == DISPID_FONT_WEIGHT ? get_Weight(&v.iVal) : get_Charset(&v.iVal);
        }
        break;
      default:
        return DISP_E_MEMBERNOTFOUND;
    }
    if (SUCCEEDED(hr) && !bPut && pResult != NULL) {
      *pResult = v;
      return S_OK;
    }
    ::VariantClear(&v);
    return hr;
  }

  // IPersistStream: a version byte, the character set, the style bits,
  // the weight, the size and the name, as in Win32.

  /// Gives CLSID_StdFont.
  STDMETHOD(GetClassID)(LPCLSID pClassID) {
    if (pClassID == NULL) return E_POINTER;
    *pClassID = CLSID_StdFont;
    return S_OK;
  }
  /// Tells if the font changed since it was saved.
  STDMETHOD(IsDirty)() { return m_bDirty ? S_OK : S_FALSE; }
  /// Reads the attributes from a stream.
  STDMETHOD(Load)(LPSTREAM pStm) {
    if (pStm == NULL) return E_POINTER;
    BYTE bVersion, bStyle, bLen;
    SHORT sCharset, sWeight;
    ULONG ulSize;
    HRESULT hr;
    if (FAILED(hr = ReadAll(pStm, &bVersion, 1)) || FAILED(hr = ReadAll(pStm, &sCharset, 2)) ||
        FAILED(hr = ReadAll(pStm, &bStyle, 1)) || FAILED(hr = ReadAll(pStm, &sWeight, 2)) ||
        FAILED(hr = ReadAll(pStm, &ulSize, 4)) || FAILED(hr = ReadAll(pStm, &bLen, 1)))
      return hr;
    if (bVersion != 1) return E_FAIL;
    char szName[256];
    if (FAILED(hr = ReadAll(pStm, szName, bLen))) return hr;
    szName[bLen] = 0;
    lstrcpyn(m_szName, szName, LF_FACESIZE);
    m_sCharset = sCharset;
    m_bItalic = (bStyle & 0x02) != 0;
    m_bUnderline = (bStyle & 0x04) != 0;
    m_bStrike = (bStyle & 0x08) != 0;
    m_sWeight = sWeight;
    m_llSize = ulSize;
    m_bDirty = FALSE;
    DropFont();
    return S_OK;
  }
  /// Writes the attributes to a stream.
  STDMETHOD(Save)(LPSTREAM pStm, BOOL fClearDirty) {
    if (pStm == NULL) return E_POINTER;
    BYTE bVersion = 1;
    BYTE bStyle = (BYTE)((m_bItalic ? 0x02 : 0) | (m_bUnderline ? 0x04 : 0) | (m_bStrike ? 0x08 : 0));
    BYTE bLen = (BYTE)lstrlen(m_szName);
    SHORT sCharset = m_sCharset, sWeight = m_sWeight;
    ULONG ulSize = (ULONG)m_llSize;
    HRESULT hr;
    if (FAILED(hr = WriteAll(pStm, &bVersion, 1)) || FAILED(hr = WriteAll(pStm, &sCharset, 2)) ||
        FAILED(hr = WriteAll(pStm, &bStyle, 1)) || FAILED(hr = WriteAll(pStm, &sWeight, 2)) ||
        FAILED(hr = WriteAll(pStm, &ulSize, 4)) || FAILED(hr = WriteAll(pStm, &bLen, 1)) ||
        FAILED(hr = WriteAll(pStm, m_szName, bLen)))
      return hr;
    if (fClearDirty) m_bDirty = FALSE;
    return S_OK;
  }
  /// Gives the largest size of the saved font.
  STDMETHOD(GetSizeMax)(ULARGE_INTEGER FAR* pcbSize) {
    if (pcbSize == NULL) return E_POINTER;
    pcbSize->LowPart = 11 + 255;
    pcbSize->HighPart = 0;
    return S_OK;
  }

  // IConnectionPointContainer

  /// Gives an enumerator of the one point, for IPropertyNotifySink.
  STDMETHOD(EnumConnectionPoints)(IEnumConnectionPoints FAR* FAR* ppEnum) {
    if (ppEnum == NULL) return E_POINTER;
    *ppEnum = new CW16EnumOnePoint(&m_notify);
    return S_OK;
  }
  /// Gives the point of IPropertyNotifySink.
  STDMETHOD(FindConnectionPoint)(REFIID riid, IConnectionPoint FAR* FAR* ppCP) {
    if (ppCP == NULL) return E_POINTER;
    *ppCP = NULL;
    if (riid != IID_IPropertyNotifySink) return CONNECT_E_NOCONNECTION;
    *ppCP = &m_notify;
    m_notify.AddRef();
    return S_OK;
  }

private:
  /// Deletes the GDI fonts.
  virtual ~CW16Font() {
    DropFont();
    for (int i = 0; i < m_oldFonts.GetSize(); i++) ::DeleteObject((HGDIOBJ)m_oldFonts[i]);
  }
  /// Makes a new GDI font next time. The old one stays valid while the
  /// font lives, as a caller may still have it selected.
  void DropFont() {
    if (m_hFont != NULL) m_oldFonts.Add((WORD)(UINT)m_hFont);
    m_hFont = NULL;
  }
  /// Marks the font changed and tells the sinks.
  HRESULT Changed(DISPID dispid) {
    m_bDirty = TRUE;
    DropFont();
    m_notify.NotifyChanged(dispid);
    return S_OK;
  }
  /// Gives a flag.
  static HRESULT GetFlag(BOOL bFlag, BOOL FAR* p) {
    if (p == NULL) return E_POINTER;
    *p = bFlag;
    return S_OK;
  }
  /// Sets a flag and tells the sinks.
  HRESULT SetFlag(BOOL* pFlag, BOOL b, DISPID dispid) {
    *pFlag = b != FALSE;
    return Changed(dispid);
  }

  ULONG m_cRef;
  CW16ConnPoint m_notify;
  char m_szName[LF_FACESIZE];
  long long m_llSize;
  SHORT m_sWeight;
  SHORT m_sCharset;
  BOOL m_bItalic;
  BOOL m_bUnderline;
  BOOL m_bStrike;
  BOOL m_bDirty;
  LONG m_cyLogical;
  LONG m_cyHimetric;
  HFONT m_hFont;
  CWordArray m_oldFonts;
};

// ---------------------------------------------------------------------
// Picture files

const DWORD kPlaceableKey = 0x9AC6CDD7L;

/// Makes a device-independent bitmap file, 24 bits a pixel, of a bitmap.
BOOL BitmapFile(HBITMAP hbm, CW16Bytes* pOut) {
  BITMAP bm;
  if (!::GetObject(hbm, sizeof(bm), &bm)) return FALSE;
  BITMAPINFOHEADER bi;
  memset(&bi, 0, sizeof(bi));
  bi.biSize = sizeof(bi);
  bi.biWidth = bm.bmWidth;
  bi.biHeight = bm.bmHeight;
  bi.biPlanes = 1;
  bi.biBitCount = 24;
  bi.biCompression = BI_RGB;
  DWORD cbStride = ((DWORD)bm.bmWidth * 3 + 3) & ~3UL;
  bi.biSizeImage = cbStride * (DWORD)bm.bmHeight;
  BITMAPFILEHEADER fh;
  memset(&fh, 0, sizeof(fh));
  fh.bfType = 0x4D42;
  fh.bfOffBits = sizeof(fh) + sizeof(bi);
  fh.bfSize = fh.bfOffBits + bi.biSizeImage;
  pOut->Free();
  if (!pOut->Append(&fh, sizeof(fh)) || !pOut->Append(&bi, sizeof(bi)) ||
      !pOut->SetSize(fh.bfSize))
    return FALSE;
  BYTE HUGEP* p = pOut->Lock();
  HDC hdc = ::GetDC(NULL);
  int nLines = ::GetDIBits(hdc, hbm, 0, (UINT)bm.bmHeight, (void FAR*)(p + fh.bfOffBits),
                           (BITMAPINFO FAR*)&bi, DIB_RGB_COLORS);
  ::ReleaseDC(NULL, hdc);
  pOut->Unlock();
  return nLines == bm.bmHeight;
}

// The header of the global memory of a 16-bit icon: the masks follow it.
struct W16IconInfo {
  POINT ptHotSpot;
  WORD nWidth;
  WORD nHeight;
  WORD nWidthBytes;
  BYTE bPlanes;
  BYTE bBitsPixel;
};

/// Makes an icon file of a 16-bit icon: one image, 24 bits a pixel, with
/// its mask. A 16-bit HICON is global memory: a W16IconInfo, the AND mask
/// (one plane, word-aligned rows) and the XOR image (device bits).
BOOL IconFile(HICON hIcon, CW16Bytes* pOut) {
  const W16IconInfo FAR* pInfo = (const W16IconInfo FAR*)::GlobalLock((HGLOBAL)hIcon);
  if (pInfo == NULL) return FALSE;
  int cx = pInfo->nWidth, cy = pInfo->nHeight;
  UINT cbAndRow = (UINT)((cx + 15) / 16 * 2);
  const BYTE FAR* pAnd = (const BYTE FAR*)(pInfo + 1);
  const BYTE FAR* pXor = pAnd + cbAndRow * cy;
  HBITMAP hbmXor = ::CreateBitmap(cx, cy, pInfo->bPlanes, pInfo->bBitsPixel, pXor);
  HBITMAP hbmAnd = ::CreateBitmap(cx, cy, 1, 1, pAnd);
  ::GlobalUnlock((HGLOBAL)hIcon);
  BOOL bOK = hbmXor != NULL && hbmAnd != NULL && cx > 0 && cy > 0;
  if (bOK) {
    DWORD cbColorRow = ((DWORD)cx * 3 + 3) & ~3UL;
    DWORD cbMaskRow = ((DWORD)cx + 31) / 32 * 4;
    struct {
      BITMAPINFOHEADER h;
      RGBQUAD colors[2];
    } bi;
    memset(&bi, 0, sizeof(bi));
    bi.h.biSize = sizeof(bi.h);
    bi.h.biWidth = cx;
    bi.h.biHeight = 2L * cy;  // the image and its mask
    bi.h.biPlanes = 1;
    bi.h.biBitCount = 24;
    DWORD cbImage = sizeof(bi.h) + cbColorRow * cy + cbMaskRow * cy;
    BYTE head[22];
    memset(head, 0, sizeof(head));
    head[2] = 1;  // an icon file
    head[4] = 1;  // one image
    head[6] = (BYTE)(cx >= 256 ? 0 : cx);
    head[7] = (BYTE)(cy >= 256 ? 0 : cy);
    head[10] = 1;   // planes
    head[12] = 24;  // bits a pixel
    DWORD dwOffset = sizeof(head);
    memcpy(head + 14, &cbImage, 4);
    memcpy(head + 18, &dwOffset, 4);
    pOut->Free();
    bOK = pOut->Append(head, sizeof(head)) && pOut->Append(&bi.h, sizeof(bi.h)) &&
          pOut->SetSize(sizeof(head) + cbImage);
    if (bOK) {
      BYTE HUGEP* p = pOut->Lock() + sizeof(head) + sizeof(bi.h);
      HDC hdc = ::GetDC(NULL);
      bi.h.biHeight = cy;
      bOK = ::GetDIBits(hdc, hbmXor, 0, (UINT)cy, (void FAR*)p, (BITMAPINFO FAR*)&bi,
                        DIB_RGB_COLORS) == cy;
      bi.h.biBitCount = 1;
      bi.h.biSizeImage = 0;
      bOK = bOK && ::GetDIBits(hdc, hbmAnd, 0, (UINT)cy, (void FAR*)(p + cbColorRow * cy),
                               (BITMAPINFO FAR*)&bi, DIB_RGB_COLORS) == cy;
      ::ReleaseDC(NULL, hdc);
      pOut->Unlock();
    }
  }
  if (hbmXor != NULL) ::DeleteObject(hbmXor);
  if (hbmAnd != NULL) ::DeleteObject(hbmAnd);
  return bOK;
}

/// Makes a placeable metafile file of a metafile, cx by cy HIMETRIC. The
/// header gives the size in as fine a unit as fits 16 bits.
BOOL MetafileFile(HMETAFILE hmf, LONG cx, LONG cy, CW16Bytes* pOut) {
  // GetMetaFileBits takes the metafile, so it takes a copy.
  HMETAFILE hCopy = ::CopyMetaFile(hmf, NULL);
  if (hCopy == NULL) return FALSE;
  HGLOBAL hBits = ::GetMetaFileBits(hCopy);
  if (hBits == NULL) return FALSE;
  DWORD cbBits = ::GlobalSize(hBits);
  WORD head[11];
  memset(head, 0, sizeof(head));
  memcpy(head, &kPlaceableKey, 4);
  LONG w = cx < 0 ? -cx : cx, h = cy < 0 ? -cy : cy;
  long nInch = kHimetricPerInch;
  while (nInch > 1 && (_AfxMulDiv(w, nInch, kHimetricPerInch) > 32767 ||
                       _AfxMulDiv(h, nInch, kHimetricPerInch) > 32767))
    nInch /= 2;
  head[5] = (WORD)_AfxMulDiv(w, nInch, kHimetricPerInch);
  head[6] = (WORD)_AfxMulDiv(h, nInch, kHimetricPerInch);
  head[7] = (WORD)nInch;
  for (int i = 0; i < 10; i++) head[10] ^= head[i];
  pOut->Free();
  BOOL bOK = pOut->Append(head, sizeof(head)) &&
             pOut->Append((const BYTE HUGEP*)::GlobalLock(hBits), cbBits);
  ::GlobalUnlock(hBits);
  ::GlobalFree(hBits);
  return bOK;
}

/// Makes an icon of the first image of an icon file, which starts at p.
/// 16-bit Windows has no CreateIconFromResource: the image becomes a
/// device bitmap and a mask, then CreateIcon takes their bits.
HICON IconFromFile(const BYTE HUGEP* p, DWORD n) {
  const BYTE HUGEP* e = p + 6;
  DWORD cbImage, dwOffset;
  hmemcpy(&cbImage, e + 8, 4);
  hmemcpy(&dwOffset, e + 12, 4);
  if (dwOffset >= n || cbImage > n - dwOffset || cbImage < sizeof(BITMAPINFOHEADER)) return NULL;
  const BYTE HUGEP* pImage = p + dwOffset;
  BITMAPINFOHEADER bih;
  hmemcpy(&bih, pImage, sizeof(bih));
  int cx = (int)bih.biWidth, cy = (int)(bih.biHeight / 2);
  if (cx <= 0 || cy <= 0 || bih.biPlanes != 1) return NULL;
  DWORD nColors = bih.biClrUsed != 0 ? bih.biClrUsed : bih.biBitCount <= 8 ? 1UL << bih.biBitCount : 0;
  DWORD cbHeader = bih.biSize + nColors * sizeof(RGBQUAD);
  DWORD cbColorRow = ((DWORD)cx * bih.biBitCount + 31) / 32 * 4;
  DWORD cbMaskRow = ((DWORD)cx + 31) / 32 * 4;
  if (cbHeader + (cbColorRow + cbMaskRow) * cy > cbImage) return NULL;

  // The color table and header, with the height of one image.
  BITMAPINFO FAR* pbi = (BITMAPINFO FAR*)new BYTE[(UINT)cbHeader];
  hmemcpy(pbi, pImage, (long)cbHeader);
  pbi->bmiHeader.biHeight = cy;
  pbi->bmiHeader.biSizeImage = 0;
  struct {
    BITMAPINFOHEADER h;
    RGBQUAD colors[2];
  } mono;
  memset(&mono, 0, sizeof(mono));
  mono.h.biSize = sizeof(mono.h);
  mono.h.biWidth = cx;
  mono.h.biHeight = cy;
  mono.h.biPlanes = 1;
  mono.h.biBitCount = 1;
  mono.colors[1].rgbRed = mono.colors[1].rgbGreen = mono.colors[1].rgbBlue = 255;

  HDC hdc = ::GetDC(NULL);
  HBITMAP hbmXor = ::CreateDIBitmap(hdc, &pbi->bmiHeader, CBM_INIT,
                                    (const void FAR*)(pImage + cbHeader), pbi, DIB_RGB_COLORS);
  HBITMAP hbmAnd = ::CreateBitmap(cx, cy, 1, 1, NULL);
  if (hbmAnd != NULL)
    ::SetDIBits(hdc, hbmAnd, 0, (UINT)cy, (const void FAR*)(pImage + cbHeader + cbColorRow * cy),
                (BITMAPINFO FAR*)&mono, DIB_RGB_COLORS);
  ::ReleaseDC(NULL, hdc);
  delete[] (BYTE*)pbi;

  HICON hIcon = NULL;
  BITMAP bmXor, bmAnd;
  if (hbmXor != NULL && hbmAnd != NULL && ::GetObject(hbmXor, sizeof(bmXor), &bmXor) &&
      ::GetObject(hbmAnd, sizeof(bmAnd), &bmAnd)) {
    DWORD cbXor = (DWORD)bmXor.bmWidthBytes * bmXor.bmHeight * bmXor.bmPlanes;
    DWORD cbAnd = (DWORD)bmAnd.bmWidthBytes * bmAnd.bmHeight;
    if (cbXor < 0xFFF0UL && cbAnd < 0xFFF0UL) {
      BYTE* pXor = new BYTE[(UINT)cbXor];
      BYTE* pAnd = new BYTE[(UINT)cbAnd];
      ::GetBitmapBits(hbmXor, (LONG)cbXor, pXor);
      ::GetBitmapBits(hbmAnd, (LONG)cbAnd, pAnd);
      hIcon = ::CreateIcon(AfxGetInstanceHandle(), cx, cy, bmXor.bmPlanes, bmXor.bmBitsPixel, pAnd,
                           pXor);
      delete[] pXor;
      delete[] pAnd;
    }
  }
  if (hbmXor != NULL) ::DeleteObject(hbmXor);
  if (hbmAnd != NULL) ::DeleteObject(hbmAnd);
  return hIcon;
}

// ---------------------------------------------------------------------
// The picture

class CW16Picture : public IPicture, public IPictureDisp, public IPersistStream {
public:
  /// Makes an uninitialized picture.
  CW16Picture()
      : m_cRef(1),
        m_sType((SHORT)PICTYPE_UNINITIALIZED),
        m_hHandle(0),
        m_hPal(NULL),
        m_bOwn(FALSE),
        m_lWidth(0),
        m_lHeight(0),
        m_hCurDC(NULL),
        m_bKeep(TRUE) {}

  /// Takes the picture of a description. The picture owns the handle and
  /// destroys it when bOwn is TRUE.
  HRESULT Init(const PICTDESC FAR* pDesc, BOOL bOwn) {
    m_bOwn = bOwn != FALSE;
    if (pDesc == NULL) return S_OK;
    m_sType = (SHORT)pDesc->picType;
    switch ((int)pDesc->picType) {
      case PICTYPE_UNINITIALIZED:
      case PICTYPE_NONE:
        break;
      case PICTYPE_BITMAP: {
        m_hHandle = (UINT)pDesc->bmp.hbitmap;
        m_hPal = pDesc->bmp.hpal;
        BITMAP bm;
        if (!::GetObject(pDesc->bmp.hbitmap, sizeof(bm), &bm)) return E_INVALIDARG;
        m_lWidth = PixelsToHimetric(bm.bmWidth, FALSE);
        m_lHeight = PixelsToHimetric(bm.bmHeight, TRUE);
        break;
      }
      case PICTYPE_ICON: {
        m_hHandle = (UINT)pDesc->icon.hicon;
        int cx = ::GetSystemMetrics(SM_CXICON), cy = ::GetSystemMetrics(SM_CYICON);
        const W16IconInfo FAR* pInfo = (const W16IconInfo FAR*)::GlobalLock((HGLOBAL)pDesc->icon.hicon);
        if (pInfo != NULL) {
          cx = pInfo->nWidth;
          cy = pInfo->nHeight;
          ::GlobalUnlock((HGLOBAL)pDesc->icon.hicon);
        }
        m_lWidth = PixelsToHimetric(cx, FALSE);
        m_lHeight = PixelsToHimetric(cy, TRUE);
        break;
      }
      case PICTYPE_METAFILE:
        m_hHandle = (UINT)pDesc->wmf.hmeta;
        m_lWidth = pDesc->wmf.xExt;
        m_lHeight = pDesc->wmf.yExt;
        break;
      default:  // 16-bit Windows has no enhanced metafiles
        m_sType = (SHORT)PICTYPE_UNINITIALIZED;
        return E_INVALIDARG;
    }
    return S_OK;
  }

  /// Gives IPicture, IPictureDisp and IPersistStream.
  STDMETHOD(QueryInterface)(REFIID riid, LPVOID FAR* ppv) {
    if (ppv == NULL) return E_POINTER;
    if (riid == IID_IUnknown || riid == IID_IPicture)
      *ppv = (IPicture*)this;
    else if (riid == IID_IDispatch || riid == IID_IPictureDisp)
      *ppv = (IPictureDisp*)this;
    else if (riid == IID_IPersist || riid == IID_IPersistStream)
      *ppv = (IPersistStream*)this;
    else {
      *ppv = NULL;
      return E_NOINTERFACE;
    }
    AddRef();
    return S_OK;
  }
  /// Adds a reference.
  STDMETHOD_(ULONG, AddRef)() { return ++m_cRef; }
  /// Removes a reference; the last one deletes the picture.
  STDMETHOD_(ULONG, Release)() {
    ULONG n = --m_cRef;
    if (n == 0) delete this;
    return n;
  }

  // IPicture

  /// Gives the handle of the bitmap, icon or metafile.
  STDMETHOD(get_Handle)(OLE_HANDLE FAR* pHandle) {
    if (pHandle == NULL) return E_POINTER;
    *pHandle = (OLE_HANDLE)m_hHandle;
    return S_OK;
  }
  /// Gives the palette of a bitmap.
  STDMETHOD(get_hPal)(OLE_HANDLE FAR* phPal) {
    if (phPal == NULL) return E_POINTER;
    if (m_sType != PICTYPE_BITMAP) return E_FAIL;
    *phPal = (OLE_HANDLE)(UINT)m_hPal;
    return S_OK;
  }
  /// Gives the type of the picture (PICTYPE_).
  STDMETHOD(get_Type)(SHORT FAR* pType) {
    if (pType == NULL) return E_POINTER;
    *pType = m_sType;
    return S_OK;
  }
  /// Gives the width in HIMETRIC units.
  STDMETHOD(get_Width)(OLE_XSIZE_HIMETRIC FAR* pWidth) {
    if (pWidth == NULL) return E_POINTER;
    *pWidth = m_lWidth;
    return S_OK;
  }
  /// Gives the height in HIMETRIC units.
  STDMETHOD(get_Height)(OLE_YSIZE_HIMETRIC FAR* pHeight) {
    if (pHeight == NULL) return E_POINTER;
    *pHeight = m_lHeight;
    return S_OK;
  }
  /// Draws the part of the picture from (xSrc, ySrc) (HIMETRIC, from the
  /// bottom left), cxSrc by cySrc, into x, y, cx, cy. A 16-bit icon draws
  /// at its own size.
  STDMETHOD(Render)(HDC hdc, LONG x, LONG y, LONG cx, LONG cy, OLE_XPOS_HIMETRIC xSrc,
                    OLE_YPOS_HIMETRIC ySrc, OLE_XSIZE_HIMETRIC cxSrc, OLE_YSIZE_HIMETRIC cySrc,
                    LPCRECT) {
    if (hdc == NULL || cx == 0 || cy == 0) return E_INVALIDARG;
    switch (m_sType) {
      case PICTYPE_NONE:
      case PICTYPE_UNINITIALIZED:
        return S_OK;
      case PICTYPE_BITMAP: {
        BITMAP bm;
        if (!::GetObject((HBITMAP)m_hHandle, sizeof(bm), &bm) || m_lWidth == 0 || m_lHeight == 0)
          return E_FAIL;
        // The source rectangle in the pixels of the bitmap, from the top.
        LONG lTop = ySrc + (cySrc < 0 ? 0 : cySrc);
        int sx = (int)_AfxMulDiv(xSrc, bm.bmWidth, m_lWidth);
        int sy = (int)_AfxMulDiv(m_lHeight - lTop, bm.bmHeight, m_lHeight);
        int sw = (int)_AfxMulDiv(cxSrc < 0 ? -cxSrc : cxSrc, bm.bmWidth, m_lWidth);
        int sh = (int)_AfxMulDiv(cySrc < 0 ? -cySrc : cySrc, bm.bmHeight, m_lHeight);
        HDC hdcMem = ::CreateCompatibleDC(hdc);
        HGDIOBJ hOld = ::SelectObject(hdcMem, (HBITMAP)m_hHandle);
        int nMode = ::SetStretchBltMode(hdc, COLORONCOLOR);
        BOOL bOK = ::StretchBlt(hdc, (int)x, (int)y, (int)cx, (int)cy, hdcMem, sx, sy, sw, sh,
                                SRCCOPY);
        ::SetStretchBltMode(hdc, nMode);
        ::SelectObject(hdcMem, hOld);
        ::DeleteDC(hdcMem);
        return bOK ? S_OK : E_FAIL;
      }
      case PICTYPE_ICON:
        return ::DrawIcon(hdc, (int)x, (int)(cy < 0 ? y + cy : y), (HICON)m_hHandle) ? S_OK : E_FAIL;
      case PICTYPE_METAFILE: {
        // The extent in a unit that fits 16 bits.
        long lw = m_lWidth != 0 ? m_lWidth : 1, lh = m_lHeight != 0 ? m_lHeight : 1;
        while (lw > 32767 || lw < -32767 || lh > 32767 || lh < -32767) {
          lw /= 2;
          lh /= 2;
        }
        int nSaved = ::SaveDC(hdc);
        ::SetMapMode(hdc, MM_ANISOTROPIC);
        ::SetWindowOrgEx(hdc, 0, 0, NULL);
        ::SetWindowExtEx(hdc, (int)lw, (int)lh, NULL);
        ::SetViewportOrgEx(hdc, (int)x, (int)y, NULL);
        ::SetViewportExtEx(hdc, (int)cx, (int)cy, NULL);
        BOOL bOK = ::PlayMetaFile(hdc, (HMETAFILE)m_hHandle);
        ::RestoreDC(hdc, nSaved);
        return bOK ? S_OK : E_FAIL;
      }
    }
    return E_FAIL;
  }
  /// Sets the palette of a bitmap.
  STDMETHOD(set_hPal)(OLE_HANDLE hPal) {
    if (m_sType != PICTYPE_BITMAP) return E_FAIL;
    m_hPal = (HPALETTE)hPal;
    return S_OK;
  }
  /// Gives the device context that the bitmap is selected into.
  STDMETHOD(get_CurDC)(HDC FAR* phDC) {
    if (phDC == NULL) return E_POINTER;
    *phDC = m_hCurDC;
    return S_OK;
  }
  /// Selects the bitmap into a device context.
  STDMETHOD(SelectPicture)(HDC hdcIn, HDC FAR* phdcOut, OLE_HANDLE FAR* phbmpOut) {
    if (m_sType != PICTYPE_BITMAP) return E_FAIL;
    if (hdcIn != NULL) ::SelectObject(hdcIn, (HBITMAP)m_hHandle);
    if (phdcOut != NULL) *phdcOut = m_hCurDC;
    if (phbmpOut != NULL) *phbmpOut = (OLE_HANDLE)m_hHandle;
    m_hCurDC = hdcIn;
    return S_OK;
  }
  /// Tells if the picture saves the file it was loaded from.
  STDMETHOD(get_KeepOriginalFormat)(BOOL FAR* pKeep) {
    if (pKeep == NULL) return E_POINTER;
    *pKeep = m_bKeep;
    return S_OK;
  }
  /// Sets if the picture saves the file it was loaded from.
  STDMETHOD(put_KeepOriginalFormat)(BOOL keep) {
    m_bKeep = keep != FALSE;
    return S_OK;
  }
  /// Forgets the file it was loaded from: the handle changed.
  STDMETHOD(PictureChanged)() {
    m_original.Free();
    return S_OK;
  }
  /// Writes the picture file to a stream.
  STDMETHOD(SaveAsFile)(LPSTREAM pStm, BOOL, LONG FAR* pcbSize) {
    if (pStm == NULL) return E_POINTER;
    CW16Bytes file;
    HRESULT hr = FileBytes(&file);
    if (FAILED(hr)) return hr;
    hr = WriteAll(pStm, file.Lock(), file.GetSize());
    file.Unlock();
    if (SUCCEEDED(hr) && pcbSize != NULL) *pcbSize = (LONG)file.GetSize();
    return hr;
  }
  /// Gives PICTURE_SCALABLE for a metafile, PICTURE_TRANSPARENT for an icon.
  STDMETHOD(get_Attributes)(DWORD FAR* pdwAttr) {
    if (pdwAttr == NULL) return E_POINTER;
    *pdwAttr = m_sType == PICTYPE_METAFILE ? PICTURE_SCALABLE
               : m_sType == PICTYPE_ICON   ? PICTURE_TRANSPARENT
                                           : 0;
    return S_OK;
  }

  // IPictureDisp

  /// Gives 0: the picture has no type information.
  STDMETHOD(GetTypeInfoCount)(UINT FAR* pctinfo) {
    if (pctinfo == NULL) return E_POINTER;
    *pctinfo = 0;
    return S_OK;
  }
  /// Gives no type information.
  STDMETHOD(GetTypeInfo)(UINT, LCID, LPTYPEINFO FAR* ppTI) {
    if (ppTI != NULL) *ppTI = NULL;
    return DISP_E_BADINDEX;
  }
  /// Gives the DISPID of one property name.
  STDMETHOD(GetIDsOfNames)(REFIID, char FAR* FAR* rgszNames, UINT cNames, LCID,
                           DISPID FAR* rgDispId) {
    static const struct {
      const char* pszName;
      DISPID dispid;
    } kNames[] = {{"Handle", DISPID_PICT_HANDLE}, {"hPal", DISPID_PICT_HPAL},
                  {"Type", DISPID_PICT_TYPE},     {"Width", DISPID_PICT_WIDTH},
                  {"Height", DISPID_PICT_HEIGHT}, {"Render", DISPID_PICT_RENDER}};
    if (rgszNames == NULL || rgDispId == NULL || cNames == 0) return E_INVALIDARG;
    for (UINT i = 0; i < cNames; i++) rgDispId[i] = DISPID_UNKNOWN;
    for (int k = 0; k < (int)(sizeof(kNames) / sizeof(kNames[0])); k++)
      if (lstrcmpi(rgszNames[0], kNames[k].pszName) == 0) rgDispId[0] = kNames[k].dispid;
    return rgDispId[0] == DISPID_UNKNOWN || cNames > 1 ? DISP_E_UNKNOWNNAME : S_OK;
  }
  /// Gets a property, or puts hPal.
  STDMETHOD(Invoke)(DISPID dispid, REFIID, LCID, unsigned short wFlags, DISPPARAMS FAR* pdp,
                    VARIANT FAR* pResult, EXCEPINFO FAR*, UINT FAR*) {
    if (dispid == DISPID_PICT_HPAL && (wFlags & DISPATCH_PROPERTYPUT)) {
      VARIANT v;
      HRESULT hr = PutArg(pdp, VT_I4, &v);
      return FAILED(hr) ? hr : set_hPal((OLE_HANDLE)v.lVal);
    }
    if (!(wFlags & (DISPATCH_PROPERTYGET | DISPATCH_METHOD))) return DISP_E_MEMBERNOTFOUND;
    VARIANT v;
    ::VariantInit(&v);
    v.vt = VT_I4;
    switch (dispid) {
      case DISPID_PICT_HANDLE: v.lVal = (LONG)m_hHandle; break;
      case DISPID_PICT_HPAL: v.lVal = (LONG)(UINT)m_hPal; break;
      case DISPID_PICT_TYPE:
        v.vt = VT_I2;
        v.iVal = m_sType;
        break;
      case DISPID_PICT_WIDTH: v.lVal = m_lWidth; break;
      case DISPID_PICT_HEIGHT: v.lVal = m_lHeight; break;
      default: return DISP_E_MEMBERNOTFOUND;
    }
    if (pResult != NULL) *pResult = v;
    return S_OK;
  }

  // IPersistStream: "lt\0\0", the length, then the picture file.

  /// Gives CLSID_StdPicture.
  STDMETHOD(GetClassID)(LPCLSID pClassID) {
    if (pClassID == NULL) return E_POINTER;
    *pClassID = CLSID_StdPicture;
    return S_OK;
  }
  /// Gives S_FALSE: the picture does not change by itself.
  STDMETHOD(IsDirty)() { return S_FALSE; }
  /// Reads a picture from a stream: with the header, or a bare file.
  STDMETHOD(Load)(LPSTREAM pStm) {
    if (pStm == NULL) return E_POINTER;
    DWORD head[2];
    HRESULT hr = ReadAll(pStm, head, sizeof(head));
    if (FAILED(hr)) return hr;
    CW16Bytes file;
    if (head[0] == 0x0000746CL) {
      if (head[1] == 0) {
        Free();
        m_sType = PICTYPE_NONE;
        return S_OK;
      }
      hr = ReadStream(pStm, (LONG)head[1], &file);
    } else {
      // No header: the file itself.
      if (!file.Append(head, sizeof(head))) return E_OUTOFMEMORY;
      hr = ReadStream(pStm, 0, &file);
    }
    if (FAILED(hr)) return hr;
    hr = FromFile(file);
    return hr;
  }
  /// Writes the header and the picture file to a stream.
  STDMETHOD(Save)(LPSTREAM pStm, BOOL) {
    if (pStm == NULL) return E_POINTER;
    CW16Bytes file;
    if (m_sType != PICTYPE_NONE && m_sType != PICTYPE_UNINITIALIZED) {
      HRESULT hr = FileBytes(&file);
      if (FAILED(hr)) return hr;
    }
    DWORD head[2] = {0x0000746CL, file.GetSize()};
    HRESULT hr = WriteAll(pStm, head, sizeof(head));
    if (SUCCEEDED(hr) && !file.IsEmpty()) {
      hr = WriteAll(pStm, file.Lock(), file.GetSize());
      file.Unlock();
    }
    return hr;
  }
  /// Gives the size of the saved picture.
  STDMETHOD(GetSizeMax)(ULARGE_INTEGER FAR* pcbSize) {
    if (pcbSize == NULL) return E_POINTER;
    CW16Bytes file;
    if (m_sType != PICTYPE_NONE && m_sType != PICTYPE_UNINITIALIZED) FileBytes(&file);
    pcbSize->LowPart = 8 + file.GetSize();
    pcbSize->HighPart = 0;
    return S_OK;
  }

  /// Loads a picture file: a bitmap ("BM"), an icon or a placeable
  /// metafile, after an optional "lt" header.
  HRESULT FromFile(const CW16Bytes& file) {
    const BYTE HUGEP* p = file.Lock();
    DWORD n = file.GetSize();
    HRESULT hr = FromBytes(p, n);
    file.Unlock();
    return hr;
  }

private:
  /// Destroys the handle when the picture owns it.
  virtual ~CW16Picture() { Free(); }

  /// Destroys the handle when the picture owns it.
  void Free() {
    if (!m_bOwn || m_hHandle == 0) return;
    switch (m_sType) {
      case PICTYPE_BITMAP: ::DeleteObject((HBITMAP)m_hHandle); break;
      case PICTYPE_ICON: ::DestroyIcon((HICON)m_hHandle); break;
      case PICTYPE_METAFILE: ::DeleteMetaFile((HMETAFILE)m_hHandle); break;
    }
    m_hHandle = 0;
  }

  /// Takes a new picture of a description and keeps its file.
  HRESULT TakeNew(const PICTDESC& desc, const BYTE HUGEP* p, DWORD n) {
    Free();
    HRESULT hr = Init(&desc, TRUE);
    m_original.Free();
    if (SUCCEEDED(hr)) m_original.Append(p, n);
    return hr;
  }

  /// Loads the picture file of n bytes at p.
  HRESULT FromBytes(const BYTE HUGEP* p, DWORD n) {
    if (n >= 8 && p[0] == 'l' && p[1] == 't' && p[2] == 0 && p[3] == 0) {
      DWORD cb;
      hmemcpy(&cb, p + 4, 4);
      p += 8;
      n -= 8;
      if (cb < n) n = cb;
      if (n == 0) {
        Free();
        m_sType = PICTYPE_NONE;
        return S_OK;
      }
    }
    PICTDESC desc;
    memset(&desc, 0, sizeof(desc));
    desc.cbSizeofstruct = sizeof(desc);
    if (n >= sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER) && p[0] == 'B' && p[1] == 'M') {
      BITMAPFILEHEADER fh;
      hmemcpy(&fh, p, sizeof(fh));
      if (fh.bfOffBits >= n) return CTL_E_INVALIDPICTURE;
      // The header and color table, which can be at most 64 KB.
      DWORD cbInfo = fh.bfOffBits - sizeof(fh);
      if (cbInfo < sizeof(BITMAPINFOHEADER) || cbInfo > 0xFFF0UL) return CTL_E_INVALIDPICTURE;
      BITMAPINFO FAR* pbi = (BITMAPINFO FAR*)new BYTE[(UINT)cbInfo];
      hmemcpy(pbi, p + sizeof(fh), (long)cbInfo);
      HDC hdc = ::GetDC(NULL);
      HBITMAP hbm = ::CreateDIBitmap(hdc, &pbi->bmiHeader, CBM_INIT,
                                     (const void FAR*)(p + fh.bfOffBits), pbi, DIB_RGB_COLORS);
      ::ReleaseDC(NULL, hdc);
      delete[] (BYTE*)pbi;
      if (hbm == NULL) return CTL_E_INVALIDPICTURE;
      desc.picType = PICTYPE_BITMAP;
      desc.bmp.hbitmap = hbm;
      return TakeNew(desc, p, n);
    }
    if (n >= 6 + 16 && p[0] == 0 && p[1] == 0 && p[2] == 1 && p[3] == 0) {
      if (p[4] == 0 && p[5] == 0) return CTL_E_INVALIDPICTURE;
      HICON hIcon = IconFromFile(p, n);
      if (hIcon == NULL) return CTL_E_INVALIDPICTURE;
      desc.picType = PICTYPE_ICON;
      desc.icon.hicon = hIcon;
      return TakeNew(desc, p, n);
    }
    DWORD dwKey = 0;
    if (n >= 4) hmemcpy(&dwKey, p, 4);
    if (n > 22 && dwKey == kPlaceableKey) {
      WORD head[11];
      hmemcpy(head, p, sizeof(head));
      SHORT sLeft = (SHORT)head[3], sTop = (SHORT)head[4], sRight = (SHORT)head[5],
            sBottom = (SHORT)head[6];
      WORD wInch = head[7] != 0 ? head[7] : 1440;
      // SetMetaFileBits takes a global block of the metafile.
      HGLOBAL hBits = ::GlobalAlloc(GMEM_MOVEABLE, n - 22);
      if (hBits == NULL) return E_OUTOFMEMORY;
      hmemcpy(::GlobalLock(hBits), p + 22, (long)(n - 22));
      ::GlobalUnlock(hBits);
      HMETAFILE hmf = ::SetMetaFileBits(hBits);
      if (hmf == NULL) {
        ::GlobalFree(hBits);
        return CTL_E_INVALIDPICTURE;
      }
      desc.picType = PICTYPE_METAFILE;
      desc.wmf.hmeta = hmf;
      desc.wmf.xExt = (int)_AfxMulDiv(sRight - sLeft, kHimetricPerInch, wInch);
      desc.wmf.yExt = (int)_AfxMulDiv(sBottom - sTop, kHimetricPerInch, wInch);
      HRESULT hr = TakeNew(desc, p, n);
      // The extent can be more than an int holds.
      m_lWidth = _AfxMulDiv(sRight - sLeft, kHimetricPerInch, wInch);
      m_lHeight = _AfxMulDiv(sBottom - sTop, kHimetricPerInch, wInch);
      return hr;
    }
    // Enhanced metafiles, and everything else, are not 16-bit pictures.
    return CTL_E_INVALIDPICTURE;
  }

  /// Gives the picture file: the original one, or one made from the handle.
  HRESULT FileBytes(CW16Bytes* pOut) {
    if (m_bKeep && !m_original.IsEmpty()) return pOut->CopyFrom(m_original) ? S_OK : E_OUTOFMEMORY;
    BOOL bOK = FALSE;
    switch (m_sType) {
      case PICTYPE_BITMAP: bOK = BitmapFile((HBITMAP)m_hHandle, pOut); break;
      case PICTYPE_ICON: bOK = IconFile((HICON)m_hHandle, pOut); break;
      case PICTYPE_METAFILE:
        bOK = MetafileFile((HMETAFILE)m_hHandle, m_lWidth, m_lHeight, pOut);
        break;
      default: return E_UNEXPECTED;
    }
    return bOK ? S_OK : E_FAIL;
  }

  ULONG m_cRef;
  SHORT m_sType;
  UINT m_hHandle;  // an HBITMAP, an HICON or an HMETAFILE
  HPALETTE m_hPal;
  BOOL m_bOwn;
  LONG m_lWidth;   // HIMETRIC
  LONG m_lHeight;  // HIMETRIC
  HDC m_hCurDC;
  BOOL m_bKeep;
  CW16Bytes m_original;  // the file it was loaded from
};

// ---------------------------------------------------------------------
// The property frame: a modal dialog with a tab for each property page,
// and OK, Cancel and Apply.

enum { kIdApply = 0x3021, kIdTab = 0x3020 };
const int kMargin = 7, kButtonWidth = 75, kButtonHeight = 23;

class CW16PropFrame;

// The site that each page reports to.
class CW16PageSite : public IPropertyPageSite {
public:
  /// Makes the site of a page of a frame.
  CW16PageSite(CW16PropFrame* pFrame, LCID lcid) : m_cRef(1), m_pFrame(pFrame), m_lcid(lcid) {}
  /// Gives IUnknown and IPropertyPageSite.
  STDMETHOD(QueryInterface)(REFIID riid, LPVOID FAR* ppv) {
    if (ppv == NULL) return E_POINTER;
    if (riid == IID_IUnknown || riid == IID_IPropertyPageSite) {
      *ppv = this;
      AddRef();
      return S_OK;
    }
    *ppv = NULL;
    return E_NOINTERFACE;
  }
  /// Adds a reference.
  STDMETHOD_(ULONG, AddRef)() { return ++m_cRef; }
  /// Removes a reference; the last one deletes the site.
  STDMETHOD_(ULONG, Release)() {
    ULONG n = --m_cRef;
    if (n == 0) delete this;
    return n;
  }
  /// Enables Apply when the page becomes dirty.
  STDMETHOD(OnStatusChange)(DWORD dwFlags);
  /// Gives the locale of the frame.
  STDMETHOD(GetLocaleID)(LCID FAR* pLocaleID) {
    if (pLocaleID == NULL) return E_POINTER;
    *pLocaleID = m_lcid;
    return S_OK;
  }
  /// Gives no page container.
  STDMETHOD(GetPageContainer)(IUnknown FAR* FAR* ppUnk) {
    if (ppUnk != NULL) *ppUnk = NULL;
    return E_NOTIMPL;
  }
  /// Gives S_FALSE: the frame has no accelerators.
  STDMETHOD(TranslateAccelerator)(MSG FAR*) { return S_FALSE; }
  /// Disconnects the site from the frame, which goes.
  void Detach() { m_pFrame = NULL; }

private:
  virtual ~CW16PageSite() {}
  ULONG m_cRef;
  CW16PropFrame* m_pFrame;
  LCID m_lcid;
};

// One page of the frame.
struct W16FramePage {
  IPropertyPage* pPage;
  CW16PageSite* pSite;
  CString strTitle;
  SIZE size;
  BOOL bActive;
};

class CW16PropFrame : public CDialog {
public:
  /// Makes a frame of a caption.
  explicit CW16PropFrame(LPCTSTR lpszCaption) : m_strCaption(lpszCaption), m_nCurrent(-1) {}
  /// Deletes the pages.
  virtual ~CW16PropFrame() {
    for (int i = 0; i < m_pages.GetSize(); i++) delete (W16FramePage*)m_pages[i];
  }

  /// Gives page i.
  W16FramePage* Page(int i) const { return (W16FramePage*)m_pages[i]; }
  /// Gives the number of pages.
  int PageCount() const { return (int)m_pages.GetSize(); }
  /// Adds a page.
  void AddPage(W16FramePage* pPage) { m_pages.Add(pPage); }

  /// Enables Apply.
  void SetDirty() {
    if (m_hWnd != NULL) {
      CWnd* pApply = GetDlgItem(kIdApply);
      if (pApply != NULL) pApply->EnableWindow(TRUE);
    }
  }

protected:
  /// Makes the tab control and the buttons, sizes the frame around the
  /// largest page, and activates every page.
  virtual BOOL OnInitDialog() {
    CDialog::OnInitDialog();
    SetWindowText(m_strCaption);
    CFont* pFont = GetFont();

    // The page area is the largest page.
    int cx = 0, cy = 0;
    for (int i = 0; i < PageCount(); i++) {
      if (Page(i)->size.cx > cx) cx = Page(i)->size.cx;
      if (Page(i)->size.cy > cy) cy = Page(i)->size.cy;
    }
    if (cx < 3 * kButtonWidth + 2 * kMargin) cx = 3 * kButtonWidth + 2 * kMargin;

    m_tab.Create(WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_TABSTOP,
                 CRect(kMargin, kMargin, kMargin + cx, kMargin + cy), this, kIdTab);
    if (pFont != NULL) m_tab.SetFont(pFont, FALSE);
    for (int i = 0; i < PageCount(); i++) m_tab.InsertItem(i, Page(i)->strTitle);
    // Grow the tab control until its display area holds the pages.
    CRect rcDisplay(0, 0, cx, cy);
    m_tab.AdjustRect(TRUE, &rcDisplay);
    int cxTab = rcDisplay.Width(), cyTab = rcDisplay.Height();
    m_tab.SetWindowPos(NULL, 0, 0, cxTab, cyTab, SWP_NOMOVE | SWP_NOZORDER);
    CRect rcArea(0, 0, cxTab, cyTab);
    m_tab.AdjustRect(FALSE, &rcArea);
    rcArea.OffsetRect(kMargin, kMargin);

    int y = kMargin + cyTab + kMargin;
    int x = kMargin + cxTab - 3 * kButtonWidth - 2 * 6;
    MakeButton(m_ok, _T("OK"), BS_DEFPUSHBUTTON, x, y, IDOK, pFont);
    MakeButton(m_cancel, _T("Cancel"), BS_PUSHBUTTON, x + kButtonWidth + 6, y, IDCANCEL, pFont);
    MakeButton(m_apply, _T("&Apply"), BS_PUSHBUTTON, x + 2 * (kButtonWidth + 6), y, kIdApply,
               pFont);
    m_apply.EnableWindow(FALSE);

    // Size the frame around its client area, and center it.
    CRect rcWindow(0, 0, kMargin + cxTab + kMargin, y + kButtonHeight + kMargin);
    ::AdjustWindowRectEx(&rcWindow, GetStyle(), FALSE, GetExStyle());
    SetWindowPos(NULL, 0, 0, rcWindow.Width(), rcWindow.Height(), SWP_NOMOVE | SWP_NOZORDER);
    CenterWindow();

    // Every page stays active for the life of the frame; the tab shows one.
    for (int i = 0; i < PageCount(); i++) {
      W16FramePage* p = Page(i);
      RECT rc = rcArea;
      p->bActive = SUCCEEDED(p->pPage->Activate(m_hWnd, &rc, TRUE));
      if (p->bActive) p->pPage->Show(SW_HIDE);
    }
    Select(0);
    return TRUE;
  }

  /// Applies the dirty pages, then closes the frame.
  virtual void OnOK() {
    ApplyAll();
    DeactivateAll();
    EndDialog(IDOK);
  }
  /// Closes the frame without applying.
  virtual void OnCancel() {
    DeactivateAll();
    EndDialog(IDCANCEL);
  }

  /// Applies the dirty pages for the Apply button.
  virtual BOOL OnCommand(WPARAM wParam, LPARAM lParam) {
    if (LOWORD(wParam) == kIdApply) {
      ApplyAll();
      return TRUE;
    }
    return CDialog::OnCommand(wParam, lParam);
  }

  /// Shows the page of the tab that becomes selected.
  virtual BOOL OnNotify(WPARAM wParam, LPARAM lParam, LRESULT* pResult) {
    NMHDR* pnm = (NMHDR*)lParam;
    if (pnm->hwndFrom == m_tab.m_hWnd && pnm->code == TCN_SELCHANGE) {
      Select(m_tab.GetCurSel());
      *pResult = 0;
      return TRUE;
    }
    return CDialog::OnNotify(wParam, lParam, pResult);
  }

  /// Gives a message to the shown page first, for its accelerators and its
  /// dialog keys.
  virtual BOOL PreTranslateMessage(MSG* pMsg) {
    if (m_nCurrent >= 0 && Page(m_nCurrent)->bActive &&
        Page(m_nCurrent)->pPage->TranslateAccelerator(pMsg) == S_OK)
      return TRUE;
    return CDialog::PreTranslateMessage(pMsg);
  }

private:
  /// Makes a push button of the frame.
  void MakeButton(CButton& button, LPCTSTR lpszText, DWORD dwStyle, int x, int y, UINT nID,
                  CFont* pFont) {
    button.Create(lpszText, dwStyle | WS_CHILD | WS_VISIBLE | WS_TABSTOP,
                  CRect(x, y, x + kButtonWidth, y + kButtonHeight), this, nID);
    if (pFont != NULL) button.SetFont(pFont, FALSE);
  }
  /// Applies the pages that are dirty and disables Apply.
  void ApplyAll() {
    for (int i = 0; i < PageCount(); i++)
      if (Page(i)->bActive && Page(i)->pPage->IsPageDirty() == S_OK) Page(i)->pPage->Apply();
    if (m_apply.m_hWnd != NULL) m_apply.EnableWindow(FALSE);
  }
  /// Deactivates every page.
  void DeactivateAll() {
    for (int i = 0; i < PageCount(); i++) {
      if (Page(i)->bActive) Page(i)->pPage->Deactivate();
      Page(i)->bActive = FALSE;
    }
  }
  /// Hides the shown page and shows page i.
  void Select(int i) {
    if (i == m_nCurrent || i < 0 || i >= PageCount()) return;
    if (m_nCurrent >= 0 && Page(m_nCurrent)->bActive) Page(m_nCurrent)->pPage->Show(SW_HIDE);
    m_nCurrent = i;
    if (Page(i)->bActive) Page(i)->pPage->Show(SW_SHOWNORMAL);
  }

  CString m_strCaption;
  CPtrArray m_pages;
  CTabCtrl m_tab;
  CButton m_ok;
  CButton m_cancel;
  CButton m_apply;
  int m_nCurrent;
};

/// Enables Apply when the page becomes dirty.
STDMETHODIMP CW16PageSite::OnStatusChange(DWORD dwFlags) {
  if (m_pFrame != NULL && (dwFlags & PROPPAGESTATUS_DIRTY)) m_pFrame->SetDirty();
  return S_OK;
}

// ---------------------------------------------------------------------
// The class objects of the stock font and picture.

class CW16StdFactory : public IClassFactory {
public:
  /// Makes the class object of the font (bFont TRUE) or the picture.
  explicit CW16StdFactory(BOOL bFont) : m_bFont(bFont) {}
  /// Gives IUnknown and IClassFactory.
  STDMETHOD(QueryInterface)(REFIID riid, LPVOID FAR* ppv) {
    if (ppv == NULL) return E_POINTER;
    if (riid == IID_IUnknown || riid == IID_IClassFactory) {
      *ppv = this;
      return S_OK;
    }
    *ppv = NULL;
    return E_NOINTERFACE;
  }
  /// The object is static, so it does not count references.
  STDMETHOD_(ULONG, AddRef)() { return 2; }
  /// The object is static, so it does not count references.
  STDMETHOD_(ULONG, Release)() { return 1; }
  /// Makes a default font or an uninitialized picture.
  STDMETHOD(CreateInstance)(LPUNKNOWN pUnkOuter, REFIID riid, LPVOID FAR* ppv) {
    if (ppv == NULL) return E_POINTER;
    *ppv = NULL;
    if (pUnkOuter != NULL) return CLASS_E_NOAGGREGATION;
    if (m_bFont) return ::OleCreateFontIndirect(NULL, riid, ppv);
    PICTDESC desc;
    memset(&desc, 0, sizeof(desc));
    desc.cbSizeofstruct = sizeof(desc);
    desc.picType = (UINT)PICTYPE_UNINITIALIZED;
    return ::OleCreatePictureIndirect(&desc, riid, FALSE, ppv);
  }
  /// Does nothing: the object is static.
  STDMETHOD(LockServer)(BOOL) { return S_OK; }

private:
  BOOL m_bFont;
};

}  // namespace

// ---------------------------------------------------------------------
// The functions of olectl.h

/// Changes an OLE_COLOR to a COLORREF: an RGB value, a palette index, a
/// palette-relative RGB value, or a system color (0x80 in the high byte).
HRESULT WINAPI OleTranslateColor(OLE_COLOR clr, HPALETTE hpal, COLORREF FAR* lpcolorref) {
  COLORREF cr;
  switch ((BYTE)(clr >> 24)) {
    case 0x00:
    case 0x02:
      cr = clr;
      break;
    case 0x01:  // a palette index
      if (hpal != NULL) {
        UINT n = ::GetPaletteEntries(hpal, 0, 0, NULL);
        if ((clr & 0xFFFFL) >= n) return E_INVALIDARG;
      }
      cr = clr;
      break;
    case 0x80:
      if ((clr & 0xFFFFFFL) > COLOR_BTNHIGHLIGHT) return E_INVALIDARG;
      cr = ::GetSysColor((int)(clr & 0xFF));
      break;
    default:
      return E_INVALIDARG;
  }
  if (lpcolorref != NULL) *lpcolorref = cr;
  return S_OK;
}

/// Makes a stock font of a description, or a 12-point System font when
/// lpFontDesc is NULL, and gives its interface riid.
HRESULT WINAPI OleCreateFontIndirect(LPFONTDESC lpFontDesc, REFIID riid, LPVOID FAR* lplpvObj) {
  if (lplpvObj == NULL) return E_POINTER;
  *lplpvObj = NULL;
  CW16Font* pFont = new CW16Font;
  if (lpFontDesc != NULL) pFont->Init(lpFontDesc);
  HRESULT hr = pFont->QueryInterface(riid, lplpvObj);
  pFont->Release();
  return hr;
}

/// Makes a stock picture of a description and gives its interface riid.
/// The picture destroys the handle when fOwn is TRUE.
HRESULT WINAPI OleCreatePictureIndirect(LPPICTDESC lpPictDesc, REFIID riid, BOOL fOwn,
                                        LPVOID FAR* lplpvObj) {
  if (lplpvObj == NULL) return E_POINTER;
  *lplpvObj = NULL;
  CW16Picture* pPict = new CW16Picture;
  HRESULT hr = pPict->Init(lpPictDesc, fOwn);
  if (SUCCEEDED(hr)) hr = pPict->QueryInterface(riid, lplpvObj);
  pPict->Release();
  return hr;
}

/// Makes a stock picture of the picture file in a stream: lSize bytes of
/// it, or the rest of the stream when lSize is 0.
HRESULT WINAPI OleLoadPicture(LPSTREAM lpstream, LONG lSize, BOOL, REFIID riid,
                              LPVOID FAR* lplpvObj) {
  if (lpstream == NULL || lplpvObj == NULL) return E_POINTER;
  *lplpvObj = NULL;
  CW16Bytes file;
  HRESULT hr = ReadStream(lpstream, lSize, &file);
  if (FAILED(hr)) return hr;
  CW16Picture* pPict = new CW16Picture;
  hr = pPict->FromFile(file);
  if (SUCCEEDED(hr)) hr = pPict->QueryInterface(riid, lplpvObj);
  pPict->Release();
  return hr;
}

/// Shows a modal property frame of the pages of lpParams for its objects.
/// Gives S_OK when the frame closes, also when no page could be made.
HRESULT WINAPI OleCreatePropertyFrameIndirect(LPOCPFIPARAMS lpParams) {
  if (lpParams == NULL || lpParams->cbStructSize < sizeof(OCPFIPARAMS)) return E_INVALIDARG;
  if (lpParams->cObjects > 0 && lpParams->lplpUnk == NULL) return E_POINTER;
  if (lpParams->cPages > 0 && lpParams->lpPages == NULL) return E_POINTER;

  CString strCaption(lpParams->lpszCaption != NULL ? lpParams->lpszCaption : "");
  strCaption += strCaption.IsEmpty() ? _T("Properties") : _T(" Properties");
  CW16PropFrame frame(strCaption);
  for (ULONG i = 0; i < lpParams->cPages; i++) {
    IPropertyPage* pPage = NULL;
    if (FAILED(::CoCreateInstance(lpParams->lpPages[i], NULL, CLSCTX_INPROC_SERVER,
                                  IID_IPropertyPage, (LPVOID FAR*)&pPage)))
      continue;
    W16FramePage* p = new W16FramePage;
    p->pPage = pPage;
    p->pSite = new CW16PageSite(&frame, lpParams->lcid);
    p->size.cx = p->size.cy = 0;
    p->bActive = FALSE;
    pPage->SetPageSite(p->pSite);
    PROPPAGEINFO info;
    memset(&info, 0, sizeof(info));
    info.cb = sizeof(info);
    if (SUCCEEDED(pPage->GetPageInfo(&info))) {
      if (info.pszTitle != NULL) p->strTitle = info.pszTitle;
      p->size = info.size;
      CoTaskMemFree(info.pszTitle);
      CoTaskMemFree(info.pszDocString);
      CoTaskMemFree(info.pszHelpFile);
    }
    if (FAILED(pPage->SetObjects(lpParams->cObjects, lpParams->lplpUnk))) {
      pPage->SetPageSite(NULL);
      p->pSite->Detach();
      p->pSite->Release();
      pPage->Release();
      delete p;
      continue;
    }
    frame.AddPage(p);
  }

  HRESULT hr = S_OK;
  if (frame.PageCount() > 0) {
    CAfxDlgTemplate dlg(WS_POPUP | WS_CAPTION | WS_SYSMENU | DS_MODALFRAME, 100, 50, L"",
                        L"MS Sans Serif", 8);
    frame.InitModalIndirect(dlg.GetTemplate(), CWnd::FromHandle(lpParams->hWndOwner));
    if (frame.DoModal() == -1) hr = E_FAIL;
  }

  for (int i = 0; i < frame.PageCount(); i++) {
    W16FramePage* p = frame.Page(i);
    if (p->bActive) p->pPage->Deactivate();
    p->pPage->SetObjects(0, NULL);
    p->pPage->SetPageSite(NULL);
    p->pSite->Detach();
    p->pSite->Release();
    p->pPage->Release();
  }
  return hr;
}

/// Shows a modal property frame: the form of OleCreatePropertyFrameIndirect
/// with each parameter apart.
HRESULT WINAPI OleCreatePropertyFrame(HWND hwndOwner, UINT x, UINT y, LPCOLESTR lpszCaption,
                                      ULONG cObjects, LPUNKNOWN FAR* ppUnk, ULONG cPages,
                                      LPCLSID pPageClsID, LCID lcid, DWORD dwReserved,
                                      LPVOID pvReserved) {
  (void)dwReserved;
  (void)pvReserved;
  OCPFIPARAMS params;
  memset(&params, 0, sizeof(params));
  params.cbStructSize = sizeof(params);
  params.hWndOwner = hwndOwner;
  params.x = (int)x;
  params.y = (int)y;
  params.lpszCaption = lpszCaption;
  params.cObjects = cObjects;
  params.lplpUnk = ppUnk;
  params.cPages = cPages;
  params.lpPages = pPageClsID;
  params.lcid = lcid;
  params.dispidInitialProperty = DISPID_UNKNOWN;
  return OleCreatePropertyFrameIndirect(&params);
}

/// Loads an object from a stream: its CLSID, then its IPersistStream data.
/// The stock font and picture are made in the library: 16-bit OLE 2 has
/// no class for them.
HRESULT AFXAPI _AfxW16LoadFromStream(LPSTREAM pStm, REFIID iid, LPVOID FAR* ppv) {
  static CW16StdFactory s_font(TRUE), s_picture(FALSE);
  if (pStm == NULL || ppv == NULL) return E_POINTER;
  *ppv = NULL;
  CLSID clsid;
  HRESULT hr = ::ReadClassStm(pStm, &clsid);
  if (FAILED(hr)) return hr;
  LPPERSISTSTREAM pPS = NULL;
  if (clsid == CLSID_StdFont)
    hr = s_font.CreateInstance(NULL, IID_IPersistStream, (LPVOID FAR*)&pPS);
  else if (clsid == CLSID_StdPicture)
    hr = s_picture.CreateInstance(NULL, IID_IPersistStream, (LPVOID FAR*)&pPS);
  else
    hr = ::CoCreateInstance(clsid, NULL, CLSCTX_INPROC_SERVER | CLSCTX_LOCAL_SERVER,
                            IID_IPersistStream, (LPVOID FAR*)&pPS);
  if (FAILED(hr)) return hr;
  hr = pPS->Load(pStm);
  if (SUCCEEDED(hr)) hr = pPS->QueryInterface(iid, ppv);
  pPS->Release();
  return hr;
}

// ---------------------------------------------------------------------
// Data binding
//
// 16-bit Windows has no OLE DB and no data source controls, so occdb.cpp
// is not in the Win16 library. These functions keep the container API
// complete. A control site has no data source, so a binding request
// causes a CNotSupportedException.

// {DB526CC0-D188-11CD-AD48-00AA003C9CB6}: the sink that a control site
// gives to a bound control.
extern "C" const IID IID_INotifyDBEvents = {
    0xdb526cc0, 0xd188, 0x11cd, {0xad, 0x48, 0x00, 0xaa, 0x00, 0x3c, 0x9c, 0xb6}};

/// Binds a property to a field of a data source control. Win16 has no
/// data source controls, so this function causes a
/// CNotSupportedException.
void COleControlSite::BindDefaultProperty(DISPID, VARTYPE, LPCTSTR, CWnd*) {
  AfxThrowNotSupportedException();
}

/// Gives a data source to a property. Win16 has no data source
/// controls, so this function causes a CNotSupportedException.
void COleControlSite::BindProperty(DISPID, CWnd*) { AfxThrowNotSupportedException(); }

/// Gives the cursor of the data source control. Win16 has no data source
/// controls, so this function always gives NULL.
IUnknown* COleControlSite::GetDSCCursor() { return NULL; }

/// Binds a property of the control in this window to a field of a data
/// source control. This function causes a CNotSupportedException.
void CWnd::BindDefaultProperty(DISPID dwDispID, VARTYPE vtProp, LPCTSTR szFieldName,
                               CWnd* pDSCWnd) {
  if (m_pCtrlSite == NULL) AfxThrowNotSupportedException();
  m_pCtrlSite->BindDefaultProperty(dwDispID, vtProp, szFieldName, pDSCWnd);
}

/// Gives a data source to a property of the control in this window. This
/// function causes a CNotSupportedException.
void CWnd::BindProperty(DISPID dwDispId, CWnd* pWndDSC) {
  if (m_pCtrlSite == NULL) AfxThrowNotSupportedException();
  m_pCtrlSite->BindProperty(dwDispId, pWndDSC);
}

/// Gives the cursor of the data source control of the control in this
/// window. This function gives NULL.
IUnknown* CWnd::GetDSCCursor() {
  if (m_pCtrlSite == NULL) AfxThrowNotSupportedException();
  return m_pCtrlSite->GetDSCCursor();
}

#endif  // _AFX_WIN16
