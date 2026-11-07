// ctlprop.cpp - a control's properties kept and restored: CPropExchange
// and its three kinds (archives for streams and storages, property bags,
// and resetting to the defaults), the PX_ functions, CFontHolder, and
// COleControl's IPersistStreamInit, IPersistStorage and
// IPersistPropertyBag.
//
// The stream format is this library's own: the properties in the order
// DoPropExchange names them, each in CArchive's form.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
#include "afximpl.h"
#include <afxctl.h>
#include <afxole.h>
#include "ctlimpl.h"

// ---------------------------------------------------------------------
// CFontHolder

CFontHolder::CFontHolder(LPPROPERTYNOTIFYSINK pNotify)
    : m_pFont(NULL), m_dwConnectCookie(0), m_pNotify(pNotify) {}

CFontHolder::~CFontHolder() { ReleaseFont(); }

void CFontHolder::InitializeFont(const FONTDESC* pFontDesc, LPDISPATCH pFontDispAmbient) {
  LPFONT pNew = NULL;
  if (pFontDispAmbient != NULL) {
    LPFONT pAmbient = NULL;
    if (SUCCEEDED(pFontDispAmbient->QueryInterface(IID_IFont, (void**)&pAmbient))) {
      if (FAILED(pAmbient->Clone(&pNew))) pNew = NULL;
      pAmbient->Release();
    }
  }
  if (pNew == NULL) {
    FONTDESC fd;
    if (pFontDesc == NULL) {
      memset(&fd, 0, sizeof(fd));
      fd.cbSizeofstruct = sizeof(fd);
      fd.lpstrName = (LPOLESTR)L"MS Sans Serif";
      fd.cySize.Lo = 80000;  // 8 points
      fd.sWeight = FW_NORMAL;
      fd.sCharset = DEFAULT_CHARSET;
      pFontDesc = &fd;
    }
    if (FAILED(::OleCreateFontIndirect((LPFONTDESC)pFontDesc, IID_IFont, (void**)&pNew)))
      pNew = NULL;
  }
  SetFont(pNew);
}

void CFontHolder::SetFont(LPFONT pNewFont) {
  ReleaseFont();
  m_pFont = pNewFont;
  if (m_pFont != NULL && m_pNotify != NULL)
    AfxConnectionAdvise(m_pFont, IID_IPropertyNotifySink, m_pNotify, FALSE, &m_dwConnectCookie);
}

void CFontHolder::ReleaseFont() {
  if (m_pFont == NULL) return;
  if (m_dwConnectCookie != 0 && m_pNotify != NULL)
    AfxConnectionUnadvise(m_pFont, IID_IPropertyNotifySink, m_pNotify, FALSE, m_dwConnectCookie);
  m_dwConnectCookie = 0;
  m_pFont->Release();
  m_pFont = NULL;
}

HFONT CFontHolder::GetFontHandle() {
  HFONT hFont = NULL;
  if (m_pFont != NULL && FAILED(m_pFont->get_hFont(&hFont))) hFont = NULL;
  return hFont;
}

HFONT CFontHolder::GetFontHandle(long cyLogical, long cyHimetric) {
  if (m_pFont != NULL && cyLogical != 0 && cyHimetric != 0)
    m_pFont->SetRatio(cyLogical, cyHimetric);
  return GetFontHandle();
}

CFont* CFontHolder::Select(CDC* pDC, long cyLogical, long cyHimetric) {
  HFONT hFont = GetFontHandle(cyLogical, cyHimetric);
  if (hFont == NULL) return NULL;
  return pDC->SelectObject(CFont::FromHandle(hFont));
}

BOOL CFontHolder::GetDisplayString(CString& strValue) {
  strValue.Empty();
  if (m_pFont == NULL) return FALSE;
  BSTR bstrName = NULL;
  if (FAILED(m_pFont->get_Name(&bstrName))) return FALSE;
  strValue = bstrName;
  ::SysFreeString(bstrName);
  return TRUE;
}

LPFONTDISP CFontHolder::GetFontDispatch() {
  LPFONTDISP pDisp = NULL;
  if (m_pFont != NULL && FAILED(m_pFont->QueryInterface(IID_IFontDisp, (void**)&pDisp)))
    pDisp = NULL;
  return pDisp;
}

void CFontHolder::QueryTextMetrics(LPTEXTMETRIC lptm) {
  memset(lptm, 0, sizeof(*lptm));
  HDC hdc = ::GetDC(NULL);
  HFONT hFont = GetFontHandle();
  HGDIOBJ hOld = hFont != NULL ? ::SelectObject(hdc, hFont) : NULL;
  ::GetTextMetrics(hdc, lptm);
  if (hOld != NULL) ::SelectObject(hdc, hOld);
  ::ReleaseDC(NULL, hdc);
}

// ---------------------------------------------------------------------
// CPropExchange

BOOL CPropExchange::ExchangeVersion(DWORD& dwVersionLoaded, DWORD dwVersionDefault,
                                    BOOL bConvert) {
  BOOL bResult;
  if (m_bLoading) {
    ULONG ul = dwVersionDefault;
    bResult = PX_ULong(this, _T("_Version"), ul, dwVersionDefault);
    dwVersionLoaded = ul;
    m_dwVersion = ul;
  } else {
    // Saving writes the new version, or keeps the one that was loaded.
    if (bConvert) m_dwVersion = dwVersionDefault;
    ULONG ul = m_dwVersion;
    bResult = PX_ULong(this, _T("_Version"), ul);
    dwVersionLoaded = m_dwVersion;
  }
  return bResult;
}

namespace {

// Loads a font object from the bytes of its IPersistStream.
LPFONT LoadFontFromStream(LPSTREAM pStm) {
  LPFONT pFont = NULL;
  FONTDESC fd;
  memset(&fd, 0, sizeof(fd));
  fd.cbSizeofstruct = sizeof(fd);
  fd.lpstrName = (LPOLESTR)L"MS Sans Serif";
  fd.cySize.Lo = 80000;
  fd.sWeight = FW_NORMAL;
  if (FAILED(::OleCreateFontIndirect(&fd, IID_IFont, (void**)&pFont))) return NULL;
  LPPERSISTSTREAM pPS = NULL;
  if (FAILED(pFont->QueryInterface(IID_IPersistStream, (void**)&pPS)) ||
      FAILED(pPS->Load(pStm))) {
    if (pPS != NULL) pPS->Release();
    pFont->Release();
    return NULL;
  }
  pPS->Release();
  return pFont;
}

// The bytes of an object's IPersistStream, in a global memory block.
HGLOBAL SaveToHGlobal(LPUNKNOWN pUnk) {
  LPPERSISTSTREAM pPS = NULL;
  if (pUnk == NULL || FAILED(pUnk->QueryInterface(IID_IPersistStream, (void**)&pPS))) return NULL;
  LPSTREAM pStm = NULL;
  HGLOBAL hGlobal = NULL;
  if (SUCCEEDED(::CreateStreamOnHGlobal(NULL, FALSE, &pStm))) {
    if (SUCCEEDED(pPS->Save(pStm, TRUE))) ::GetHGlobalFromStream(pStm, &hGlobal);
    pStm->Release();
  }
  pPS->Release();
  return hGlobal;
}

void WriteHGlobal(CArchive& ar, HGLOBAL hGlobal, SIZE_T cb) {
  ar << (DWORD)cb;
  if (cb == 0) return;
  void* p = ::GlobalLock(hGlobal);
  ar.Write(p, (UINT)cb);
  ::GlobalUnlock(hGlobal);
}

HGLOBAL ReadHGlobal(CArchive& ar, DWORD* pcb) {
  DWORD cb = 0;
  ar >> cb;
  *pcb = cb;
  HGLOBAL hGlobal = ::GlobalAlloc(GMEM_MOVEABLE, cb != 0 ? cb : 1);
  if (hGlobal == NULL) AfxThrowMemoryException();
  if (cb != 0) {
    void* p = ::GlobalLock(hGlobal);
    UINT n = ar.Read(p, cb);
    ::GlobalUnlock(hGlobal);
    if (n != cb) {
      ::GlobalFree(hGlobal);
      AfxThrowArchiveException(CArchiveException::endOfFile);
    }
  }
  return hGlobal;
}

// ---------------------------------------------------------------------
// Archives: streams and storages

class CArchivePropExchange : public CPropExchange {
public:
  explicit CArchivePropExchange(CArchive& ar) : m_ar(ar) { m_bLoading = ar.IsLoading(); }

  virtual BOOL ExchangeProp(LPCTSTR, VARTYPE vtProp, void* pvProp, const void*) {
    if (m_bLoading) {
      switch (vtProp) {
        case VT_I2: m_ar >> *(short*)pvProp; break;
        case VT_UI2: m_ar >> *(USHORT*)pvProp; break;
        case VT_I4: m_ar >> *(long*)pvProp; break;
        case VT_UI4: m_ar >> *(ULONG*)pvProp; break;
        case VT_BOOL: {
          BYTE b;
          m_ar >> b;
          *(BOOL*)pvProp = b != 0;
          break;
        }
        case VT_BSTR: m_ar >> *(CString*)pvProp; break;
        case VT_R4: m_ar >> *(float*)pvProp; break;
        case VT_R8: m_ar >> *(double*)pvProp; break;
        case VT_CY: m_ar >> ((CY*)pvProp)->int64; break;
        default: return FALSE;
      }
    } else {
      switch (vtProp) {
        case VT_I2: m_ar << *(short*)pvProp; break;
        case VT_UI2: m_ar << *(USHORT*)pvProp; break;
        case VT_I4: m_ar << *(long*)pvProp; break;
        case VT_UI4: m_ar << *(ULONG*)pvProp; break;
        case VT_BOOL: m_ar << (BYTE)(*(BOOL*)pvProp != 0); break;
        case VT_BSTR: m_ar << *(CString*)pvProp; break;
        case VT_R4: m_ar << *(float*)pvProp; break;
        case VT_R8: m_ar << *(double*)pvProp; break;
        case VT_CY: m_ar << ((CY*)pvProp)->int64; break;
        default: return FALSE;
      }
    }
    return TRUE;
  }

  virtual BOOL ExchangeBlobProp(LPCTSTR, HGLOBAL* phBlob, HGLOBAL) {
    if (m_bLoading) {
      if (*phBlob != NULL) ::GlobalFree(*phBlob);
      DWORD cb;
      *phBlob = ReadHGlobal(m_ar, &cb);
      if (cb == 0) {
        ::GlobalFree(*phBlob);
        *phBlob = NULL;
      }
    } else {
      SIZE_T cb = *phBlob != NULL ? ::GlobalSize(*phBlob) : 0;
      WriteHGlobal(m_ar, *phBlob, cb);
    }
    return TRUE;
  }

  virtual BOOL ExchangeFontProp(LPCTSTR, CFontHolder& font, const FONTDESC* pFontDesc,
                                LPFONTDISP pFontDispAmbient) {
    if (m_bLoading) {
      DWORD cb;
      HGLOBAL hGlobal = ReadHGlobal(m_ar, &cb);
      LPFONT pFont = NULL;
      if (cb != 0) {
        LPSTREAM pStm = NULL;
        if (SUCCEEDED(::CreateStreamOnHGlobal(hGlobal, TRUE, &pStm))) {
          hGlobal = NULL;
          pFont = LoadFontFromStream(pStm);
          pStm->Release();
        }
      }
      if (hGlobal != NULL) ::GlobalFree(hGlobal);
      if (pFont != NULL)
        font.SetFont(pFont);
      else
        font.InitializeFont(pFontDesc, pFontDispAmbient);
      return pFont != NULL;
    }
    HGLOBAL hGlobal = SaveToHGlobal(font.m_pFont);
    SIZE_T cb = hGlobal != NULL ? ::GlobalSize(hGlobal) : 0;
    WriteHGlobal(m_ar, hGlobal, cb);
    if (hGlobal != NULL) ::GlobalFree(hGlobal);
    return TRUE;
  }

  virtual BOOL ExchangePersistentProp(LPCTSTR, LPUNKNOWN* ppUnk, REFIID iid, LPUNKNOWN) {
    if (m_bLoading) {
      if (*ppUnk != NULL) {
        (*ppUnk)->Release();
        *ppUnk = NULL;
      }
      DWORD cb;
      HGLOBAL hGlobal = ReadHGlobal(m_ar, &cb);
      BOOL bOK = cb == 0;
      if (cb > sizeof(CLSID)) {
        LPSTREAM pStm = NULL;
        if (SUCCEEDED(::CreateStreamOnHGlobal(hGlobal, TRUE, &pStm))) {
          hGlobal = NULL;
          bOK = SUCCEEDED(::OleLoadFromStream(pStm, iid, (void**)ppUnk));
          pStm->Release();
        }
      }
      if (hGlobal != NULL) ::GlobalFree(hGlobal);
      return bOK;
    }
    // The class ID, then the object's own data.
    LPPERSISTSTREAM pPS = NULL;
    HGLOBAL hGlobal = NULL;
    if (*ppUnk != NULL && SUCCEEDED((*ppUnk)->QueryInterface(IID_IPersistStream, (void**)&pPS))) {
      LPSTREAM pStm = NULL;
      if (SUCCEEDED(::CreateStreamOnHGlobal(NULL, FALSE, &pStm))) {
        if (SUCCEEDED(::OleSaveToStream(pPS, pStm))) ::GetHGlobalFromStream(pStm, &hGlobal);
        pStm->Release();
      }
      pPS->Release();
    }
    ULARGE_INTEGER size = {};
    if (hGlobal != NULL) {
      // The stream's own size, not the block's.
      LPSTREAM pStm = NULL;
      if (SUCCEEDED(::CreateStreamOnHGlobal(hGlobal, FALSE, &pStm))) {
        STATSTG st;
        if (SUCCEEDED(pStm->Stat(&st, STATFLAG_NONAME))) size = st.cbSize;
        pStm->Release();
      }
    }
    WriteHGlobal(m_ar, hGlobal, (SIZE_T)size.QuadPart);
    if (hGlobal != NULL) ::GlobalFree(hGlobal);
    return TRUE;
  }

protected:
  CArchive& m_ar;
};

// ---------------------------------------------------------------------
// Resetting: every property takes its default.

class CResetPropExchange : public CPropExchange {
public:
  CResetPropExchange() { m_bLoading = TRUE; }

  virtual BOOL ExchangeProp(LPCTSTR, VARTYPE vtProp, void* pvProp, const void* pvDefault) {
    if (pvDefault == NULL) return FALSE;
    switch (vtProp) {
      case VT_I2: *(short*)pvProp = *(const short*)pvDefault; break;
      case VT_UI2: *(USHORT*)pvProp = *(const USHORT*)pvDefault; break;
      case VT_I4: *(long*)pvProp = *(const long*)pvDefault; break;
      case VT_UI4: *(ULONG*)pvProp = *(const ULONG*)pvDefault; break;
      case VT_BOOL: *(BOOL*)pvProp = *(const BOOL*)pvDefault; break;
      case VT_BSTR: *(CString*)pvProp = *(const CString*)pvDefault; break;
      case VT_R4: *(float*)pvProp = *(const float*)pvDefault; break;
      case VT_R8: *(double*)pvProp = *(const double*)pvDefault; break;
      case VT_CY: *(CY*)pvProp = *(const CY*)pvDefault; break;
      default: return FALSE;
    }
    return TRUE;
  }

  virtual BOOL ExchangeBlobProp(LPCTSTR, HGLOBAL* phBlob, HGLOBAL hBlobDefault) {
    if (*phBlob != NULL) ::GlobalFree(*phBlob);
    *phBlob = NULL;
    if (hBlobDefault != NULL) {
      SIZE_T cb = ::GlobalSize(hBlobDefault);
      *phBlob = ::GlobalAlloc(GMEM_MOVEABLE, cb);
      if (*phBlob != NULL) {
        memcpy(::GlobalLock(*phBlob), ::GlobalLock(hBlobDefault), cb);
        ::GlobalUnlock(*phBlob);
        ::GlobalUnlock(hBlobDefault);
      }
    }
    return TRUE;
  }

  virtual BOOL ExchangeFontProp(LPCTSTR, CFontHolder& font, const FONTDESC* pFontDesc,
                                LPFONTDISP pFontDispAmbient) {
    font.InitializeFont(pFontDesc, pFontDispAmbient);
    return TRUE;
  }

  virtual BOOL ExchangePersistentProp(LPCTSTR, LPUNKNOWN* ppUnk, REFIID iid,
                                      LPUNKNOWN pUnkDefault) {
    if (*ppUnk != NULL) (*ppUnk)->Release();
    *ppUnk = NULL;
    if (pUnkDefault != NULL && FAILED(pUnkDefault->QueryInterface(iid, (void**)ppUnk)))
      *ppUnk = NULL;
    return TRUE;
  }
};

// ---------------------------------------------------------------------
// Property bags: each property by name, as a VARIANT. Saving leaves out
// the properties at their defaults, unless the container asks for all.

class CPropbagPropExchange : public CPropExchange {
public:
  CPropbagPropExchange(LPPROPERTYBAG pPropBag, LPERRORLOG pErrorLog, BOOL bLoading,
                       BOOL bSaveAllProperties = FALSE)
      : m_pPropBag(pPropBag), m_pErrorLog(pErrorLog), m_bSaveAll(bSaveAllProperties) {
    m_bLoading = bLoading;
  }

  virtual BOOL ExchangeProp(LPCTSTR pszPropName, VARTYPE vtProp, void* pvProp,
                            const void* pvDefault) {
    CStringW strName(pszPropName);
    VARTYPE vt = vtProp == VT_BOOL ? VT_BOOL : vtProp;
    if (m_bLoading) {
      VARIANT var;
      ::VariantInit(&var);
      var.vt = vt;
      HRESULT hr = m_pPropBag->Read(strName, &var, m_pErrorLog);
      if (SUCCEEDED(hr) && var.vt != vt) hr = ::VariantChangeType(&var, &var, 0, vt);
      if (FAILED(hr)) {
        ::VariantClear(&var);
        if (pvDefault == NULL) return FALSE;
        CResetPropExchange reset;
        return reset.ExchangeProp(pszPropName, vtProp, pvProp, pvDefault);
      }
      switch (vtProp) {
        case VT_I2: *(short*)pvProp = var.iVal; break;
        case VT_UI2: *(USHORT*)pvProp = var.uiVal; break;
        case VT_I4: *(long*)pvProp = var.lVal; break;
        case VT_UI4: *(ULONG*)pvProp = var.ulVal; break;
        case VT_BOOL: *(BOOL*)pvProp = var.boolVal != VARIANT_FALSE; break;
        case VT_BSTR: *(CString*)pvProp = var.bstrVal; break;
        case VT_R4: *(float*)pvProp = var.fltVal; break;
        case VT_R8: *(double*)pvProp = var.dblVal; break;
        case VT_CY: *(CY*)pvProp = var.cyVal; break;
        default: ::VariantClear(&var); return FALSE;
      }
      ::VariantClear(&var);
      return TRUE;
    }
    if (!m_bSaveAll && pvDefault != NULL && IsDefault(vtProp, pvProp, pvDefault)) return TRUE;
    VARIANT var;
    ::VariantInit(&var);
    var.vt = vt;
    switch (vtProp) {
      case VT_I2: var.iVal = *(short*)pvProp; break;
      case VT_UI2: var.uiVal = *(USHORT*)pvProp; break;
      case VT_I4: var.lVal = *(long*)pvProp; break;
      case VT_UI4: var.ulVal = *(ULONG*)pvProp; break;
      case VT_BOOL: var.boolVal = *(BOOL*)pvProp ? VARIANT_TRUE : VARIANT_FALSE; break;
      case VT_BSTR: var.bstrVal = ((CString*)pvProp)->AllocSysString(); break;
      case VT_R4: var.fltVal = *(float*)pvProp; break;
      case VT_R8: var.dblVal = *(double*)pvProp; break;
      case VT_CY: var.cyVal = *(CY*)pvProp; break;
      default: return FALSE;
    }
    HRESULT hr = m_pPropBag->Write(strName, &var);
    ::VariantClear(&var);
    return SUCCEEDED(hr);
  }

  virtual BOOL ExchangeBlobProp(LPCTSTR pszPropName, HGLOBAL* phBlob, HGLOBAL hBlobDefault) {
    CStringW strName(pszPropName);
    if (m_bLoading) {
      VARIANT var;
      ::VariantInit(&var);
      var.vt = VT_ARRAY | VT_UI1;
      HRESULT hr = m_pPropBag->Read(strName, &var, m_pErrorLog);
      if (FAILED(hr) || var.vt != (VT_ARRAY | VT_UI1) || var.parray == NULL) {
        ::VariantClear(&var);
        CResetPropExchange reset;
        return reset.ExchangeBlobProp(pszPropName, phBlob, hBlobDefault);
      }
      if (*phBlob != NULL) ::GlobalFree(*phBlob);
      LONG lLower = 0, lUpper = -1;
      ::SafeArrayGetLBound(var.parray, 1, &lLower);
      ::SafeArrayGetUBound(var.parray, 1, &lUpper);
      SIZE_T cb = (SIZE_T)(lUpper - lLower + 1);
      *phBlob = cb != 0 ? ::GlobalAlloc(GMEM_MOVEABLE, cb) : NULL;
      if (*phBlob != NULL) {
        void* pData = NULL;
        ::SafeArrayAccessData(var.parray, &pData);
        memcpy(::GlobalLock(*phBlob), pData, cb);
        ::GlobalUnlock(*phBlob);
        ::SafeArrayUnaccessData(var.parray);
      }
      ::VariantClear(&var);
      return TRUE;
    }
    SIZE_T cb = *phBlob != NULL ? ::GlobalSize(*phBlob) : 0;
    VARIANT var;
    ::VariantInit(&var);
    var.vt = VT_ARRAY | VT_UI1;
    var.parray = ::SafeArrayCreateVector(VT_UI1, 0, (ULONG)cb);
    if (var.parray == NULL) return FALSE;
    if (cb != 0) {
      void* pData = NULL;
      ::SafeArrayAccessData(var.parray, &pData);
      memcpy(pData, ::GlobalLock(*phBlob), cb);
      ::GlobalUnlock(*phBlob);
      ::SafeArrayUnaccessData(var.parray);
    }
    HRESULT hr = m_pPropBag->Write(strName, &var);
    ::VariantClear(&var);
    return SUCCEEDED(hr);
  }

  // A font is an object in the bag.
  virtual BOOL ExchangeFontProp(LPCTSTR pszPropName, CFontHolder& font,
                                const FONTDESC* pFontDesc, LPFONTDISP pFontDispAmbient) {
    CStringW strName(pszPropName);
    if (m_bLoading) {
      VARIANT var;
      ::VariantInit(&var);
      var.vt = VT_UNKNOWN;
      HRESULT hr = m_pPropBag->Read(strName, &var, m_pErrorLog);
      LPFONT pFont = NULL;
      if (SUCCEEDED(hr) && (var.vt == VT_UNKNOWN || var.vt == VT_DISPATCH) && var.punkVal != NULL) {
        LPFONT pBagFont = NULL;
        if (SUCCEEDED(var.punkVal->QueryInterface(IID_IFont, (void**)&pBagFont))) {
          if (FAILED(pBagFont->Clone(&pFont))) pFont = NULL;
          pBagFont->Release();
        }
      }
      ::VariantClear(&var);
      if (pFont != NULL)
        font.SetFont(pFont);
      else
        font.InitializeFont(pFontDesc, pFontDispAmbient);
      return TRUE;
    }
    if (font.m_pFont == NULL) return TRUE;
    VARIANT var;
    ::VariantInit(&var);
    var.vt = VT_UNKNOWN;
    var.punkVal = font.m_pFont;
    return SUCCEEDED(m_pPropBag->Write(strName, &var));
  }

  virtual BOOL ExchangePersistentProp(LPCTSTR pszPropName, LPUNKNOWN* ppUnk, REFIID iid,
                                      LPUNKNOWN pUnkDefault) {
    CStringW strName(pszPropName);
    if (m_bLoading) {
      VARIANT var;
      ::VariantInit(&var);
      var.vt = VT_UNKNOWN;
      HRESULT hr = m_pPropBag->Read(strName, &var, m_pErrorLog);
      if (*ppUnk != NULL) (*ppUnk)->Release();
      *ppUnk = NULL;
      if (SUCCEEDED(hr) && (var.vt == VT_UNKNOWN || var.vt == VT_DISPATCH) && var.punkVal != NULL)
        var.punkVal->QueryInterface(iid, (void**)ppUnk);
      else if (pUnkDefault != NULL)
        pUnkDefault->QueryInterface(iid, (void**)ppUnk);
      ::VariantClear(&var);
      return TRUE;
    }
    if (*ppUnk == NULL) return TRUE;
    VARIANT var;
    ::VariantInit(&var);
    var.vt = VT_UNKNOWN;
    var.punkVal = *ppUnk;
    return SUCCEEDED(m_pPropBag->Write(strName, &var));
  }

protected:
  static BOOL IsDefault(VARTYPE vtProp, const void* pvProp, const void* pvDefault) {
    switch (vtProp) {
      case VT_I2: return *(const short*)pvProp == *(const short*)pvDefault;
      case VT_UI2: return *(const USHORT*)pvProp == *(const USHORT*)pvDefault;
      case VT_I4: return *(const long*)pvProp == *(const long*)pvDefault;
      case VT_UI4: return *(const ULONG*)pvProp == *(const ULONG*)pvDefault;
      case VT_BOOL: return (*(const BOOL*)pvProp != 0) == (*(const BOOL*)pvDefault != 0);
      case VT_BSTR: return *(const CString*)pvProp == *(const CString*)pvDefault;
      case VT_R4: return *(const float*)pvProp == *(const float*)pvDefault;
      case VT_R8: return *(const double*)pvProp == *(const double*)pvDefault;
      case VT_CY: return ((const CY*)pvProp)->int64 == ((const CY*)pvDefault)->int64;
    }
    return FALSE;
  }

  LPPROPERTYBAG m_pPropBag;
  LPERRORLOG m_pErrorLog;
  BOOL m_bSaveAll;
};

}  // namespace

// ---------------------------------------------------------------------
// The PX_ functions

#define PX_PAIR(name, type, vt)                                                          \
  BOOL AFXAPI name(CPropExchange* pPX, LPCTSTR pszPropName, type& value) {              \
    return pPX->ExchangeProp(pszPropName, vt, &value);                                    \
  }                                                                                       \
  BOOL AFXAPI name(CPropExchange* pPX, LPCTSTR pszPropName, type& value, type valueDefault) { \
    return pPX->ExchangeProp(pszPropName, vt, &value, &valueDefault);                     \
  }

PX_PAIR(PX_Short, short, VT_I2)
PX_PAIR(PX_UShort, USHORT, VT_UI2)
PX_PAIR(PX_Long, long, VT_I4)
PX_PAIR(PX_ULong, ULONG, VT_UI4)
PX_PAIR(PX_Bool, BOOL, VT_BOOL)
PX_PAIR(PX_Float, float, VT_R4)
PX_PAIR(PX_Double, double, VT_R8)
PX_PAIR(PX_Currency, CY, VT_CY)

BOOL AFXAPI PX_Color(CPropExchange* pPX, LPCTSTR pszPropName, OLE_COLOR& clrValue) {
  return pPX->ExchangeProp(pszPropName, VT_I4, &clrValue);
}

BOOL AFXAPI PX_Color(CPropExchange* pPX, LPCTSTR pszPropName, OLE_COLOR& clrValue,
                     OLE_COLOR clrDefault) {
  return pPX->ExchangeProp(pszPropName, VT_I4, &clrValue, &clrDefault);
}

BOOL AFXAPI PX_String(CPropExchange* pPX, LPCTSTR pszPropName, CString& strValue) {
  return pPX->ExchangeProp(pszPropName, VT_BSTR, &strValue);
}

BOOL AFXAPI PX_String(CPropExchange* pPX, LPCTSTR pszPropName, CString& strValue,
                      const CString& strDefault) {
  return pPX->ExchangeProp(pszPropName, VT_BSTR, &strValue, &strDefault);
}

BOOL AFXAPI PX_String(CPropExchange* pPX, LPCTSTR pszPropName, CString& strValue,
                      LPCTSTR lpszDefault) {
  CString strDefault(lpszDefault != NULL ? lpszDefault : _T(""));
  return pPX->ExchangeProp(pszPropName, VT_BSTR, &strValue, &strDefault);
}

BOOL AFXAPI PX_Blob(CPropExchange* pPX, LPCTSTR pszPropName, HGLOBAL& hBlob,
                    HGLOBAL hBlobDefault) {
  return pPX->ExchangeBlobProp(pszPropName, &hBlob, hBlobDefault);
}

BOOL AFXAPI PX_Font(CPropExchange* pPX, LPCTSTR pszPropName, CFontHolder& font,
                    const FONTDESC* pFontDesc, LPFONTDISP pFontDispAmbient) {
  return pPX->ExchangeFontProp(pszPropName, font, pFontDesc, pFontDispAmbient);
}

BOOL AFXAPI PX_IUnknown(CPropExchange* pPX, LPCTSTR pszPropName, LPUNKNOWN& pUnk, REFIID iid,
                        LPUNKNOWN pUnkDefault) {
  return pPX->ExchangePersistentProp(pszPropName, &pUnk, iid, pUnkDefault);
}

// ---------------------------------------------------------------------
// COleControl's properties

void COleControl::DoPropExchange(CPropExchange* pPX) {
  ExchangeExtent(pPX);
  ExchangeStockProps(pPX);
}

BOOL COleControl::ExchangeVersion(CPropExchange* pPX, DWORD dwVersionDefault, BOOL bConvert) {
  DWORD dwVersionLoaded = 0;
  return pPX->ExchangeVersion(dwVersionLoaded, dwVersionDefault, bConvert);
}

BOOL COleControl::ExchangeExtent(CPropExchange* pPX) {
  // Saved always, like the size of a control in an HTML page; a missing
  // one keeps the current size.
  long cx = m_cxExtent, cy = m_cyExtent;
  BOOL bOK = PX_Long(pPX, _T("_ExtentX"), cx);
  bOK = PX_Long(pPX, _T("_ExtentY"), cy) && bOK;
  if (pPX->IsLoading()) {
    m_cxExtent = cx;
    m_cyExtent = cy;
  }
  return bOK;
}

void COleControl::ExchangeStockProps(CPropExchange* pPX) {
  DWORD dwMask = GetStockPropMask();
  if (dwMask == 0) return;
  BOOL bLoading = pPX->IsLoading();
  if (dwMask & STOCKPROP_BACKCOLOR) PX_Color(pPX, _T("BackColor"), m_clrBackColor, AmbientBackColor());
  if (dwMask & STOCKPROP_FORECOLOR) PX_Color(pPX, _T("ForeColor"), m_clrForeColor, AmbientForeColor());
  if (dwMask & STOCKPROP_CAPTION)
    PX_String(pPX, _T("Caption"), m_strText, _T(""));
  else if (dwMask & STOCKPROP_TEXT)
    PX_String(pPX, _T("Text"), m_strText, _T(""));
  if (dwMask & STOCKPROP_BORDERSTYLE) PX_Short(pPX, _T("BorderStyle"), m_sBorderStyle, (short)0);
  if (dwMask & STOCKPROP_APPEARANCE) PX_Short(pPX, _T("Appearance"), m_sAppearance, (short)0);
  if (dwMask & STOCKPROP_ENABLED) PX_Bool(pPX, _T("Enabled"), m_bEnabled, TRUE);
  if (dwMask & STOCKPROP_FONT) {
    LPFONTDISP pAmbient = AmbientFont();
    PX_Font(pPX, _T("Font"), m_font, NULL, pAmbient);
    if (pAmbient != NULL) pAmbient->Release();
  }
  if (bLoading && m_hWnd != NULL) {
    // The window shows what was loaded.
    if (dwMask & (STOCKPROP_CAPTION | STOCKPROP_TEXT))
      CWnd::DefWindowProc(WM_SETTEXT, 0, (LPARAM)(LPCTSTR)m_strText);
    if (dwMask & STOCKPROP_ENABLED) ::EnableWindow(m_hWnd, m_bEnabled);
    if (dwMask & (STOCKPROP_BORDERSTYLE | STOCKPROP_APPEARANCE)) OnBorderStyleChanged();
    if (dwMask & STOCKPROP_FONT) OnFontChanged();
  }
}

void COleControl::OnResetState() {
  CResetPropExchange px;
  DoPropExchange(&px);
  InvalidateControl();
}

HRESULT COleControl::SaveToArchive(CArchive& ar) {
  try {
    CArchivePropExchange px(ar);
    DoPropExchange(&px);
    ar.Flush();
  } catch (CException* e) {
    e->Delete();
    return E_FAIL;
  }
  return S_OK;
}

HRESULT COleControl::LoadFromArchive(CArchive& ar) {
  try {
    CArchivePropExchange px(ar);
    DoPropExchange(&px);
  } catch (CException* e) {
    e->Delete();
    return E_FAIL;
  }
  return S_OK;
}

static HRESULT SaveToStream(COleControl* pCtl, LPSTREAM pStm) {
  COleStreamFile file;
  file.Attach(pStm);
  HRESULT hr;
  {
    CArchive ar(&file, CArchive::store);
    hr = pCtl->SaveToArchive(ar);
    try {
      ar.Close();
    } catch (CException* e) {
      e->Delete();
      hr = STG_E_WRITEFAULT;
    }
  }
  file.Detach();
  return hr;
}

static HRESULT LoadFromStream(COleControl* pCtl, LPSTREAM pStm) {
  COleStreamFile file;
  file.Attach(pStm);
  HRESULT hr;
  {
    CArchive ar(&file, CArchive::load);
    hr = pCtl->LoadFromArchive(ar);
    try {
      ar.Close();
    } catch (CException* e) {
      e->Delete();
    }
  }
  file.Detach();
  return hr;
}

// ---------------------------------------------------------------------
// IPersistStreamInit

CTL_IUNKNOWN(PersistStreamInit)

STDMETHODIMP COleControl::XPersistStreamInit::GetClassID(LPCLSID pClassID) {
  METHOD_PROLOGUE(COleControl, PersistStreamInit);
  return pThis->GetClassID(pClassID);
}

STDMETHODIMP COleControl::XPersistStreamInit::IsDirty() {
  METHOD_PROLOGUE(COleControl, PersistStreamInit);
  return pThis->m_bModified ? S_OK : S_FALSE;
}

STDMETHODIMP COleControl::XPersistStreamInit::Load(LPSTREAM pStm) {
  METHOD_PROLOGUE(COleControl, PersistStreamInit);
  if (pStm == NULL) return E_POINTER;
  HRESULT hr = LoadFromStream(pThis, pStm);
  if (FAILED(hr)) return hr;
  pThis->m_bInitialized = TRUE;
  pThis->m_bModified = FALSE;
  pThis->InvalidateControl();
  return S_OK;
}

STDMETHODIMP COleControl::XPersistStreamInit::Save(LPSTREAM pStm, BOOL fClearDirty) {
  METHOD_PROLOGUE(COleControl, PersistStreamInit);
  if (pStm == NULL) return E_POINTER;
  HRESULT hr = SaveToStream(pThis, pStm);
  if (SUCCEEDED(hr) && fClearDirty) pThis->m_bModified = FALSE;
  return hr;
}

STDMETHODIMP COleControl::XPersistStreamInit::GetSizeMax(ULARGE_INTEGER* pcbSize) {
  METHOD_PROLOGUE(COleControl, PersistStreamInit);
  if (pcbSize == NULL) return E_POINTER;
  LPSTREAM pStm = NULL;
  HRESULT hr = ::CreateStreamOnHGlobal(NULL, TRUE, &pStm);
  if (FAILED(hr)) return hr;
  hr = SaveToStream(pThis, pStm);
  STATSTG st;
  if (SUCCEEDED(hr)) hr = pStm->Stat(&st, STATFLAG_NONAME);
  if (SUCCEEDED(hr)) *pcbSize = st.cbSize;
  pStm->Release();
  return hr;
}

STDMETHODIMP COleControl::XPersistStreamInit::InitNew() {
  METHOD_PROLOGUE(COleControl, PersistStreamInit);
  pThis->OnResetState();
  pThis->m_bInitialized = TRUE;
  pThis->m_bModified = FALSE;
  return S_OK;
}

// ---------------------------------------------------------------------
// IPersistStorage: the properties in the storage's "Contents" stream

CTL_IUNKNOWN(PersistStorage)

static const WCHAR kContents[] = L"Contents";

STDMETHODIMP COleControl::XPersistStorage::GetClassID(LPCLSID pClassID) {
  METHOD_PROLOGUE(COleControl, PersistStorage);
  return pThis->GetClassID(pClassID);
}

STDMETHODIMP COleControl::XPersistStorage::IsDirty() {
  METHOD_PROLOGUE(COleControl, PersistStorage);
  return pThis->m_bModified ? S_OK : S_FALSE;
}

STDMETHODIMP COleControl::XPersistStorage::InitNew(LPSTORAGE) {
  METHOD_PROLOGUE(COleControl, PersistStorage);
  pThis->OnResetState();
  pThis->m_bInitialized = TRUE;
  pThis->m_bModified = FALSE;
  return S_OK;
}

STDMETHODIMP COleControl::XPersistStorage::Load(LPSTORAGE pStg) {
  METHOD_PROLOGUE(COleControl, PersistStorage);
  if (pStg == NULL) return E_POINTER;
  LPSTREAM pStm = NULL;
  HRESULT hr = pStg->OpenStream(kContents, NULL, STGM_READ | STGM_SHARE_EXCLUSIVE, 0, &pStm);
  if (FAILED(hr)) return hr;
  hr = LoadFromStream(pThis, pStm);
  pStm->Release();
  if (FAILED(hr)) return hr;
  pThis->m_bInitialized = TRUE;
  pThis->m_bModified = FALSE;
  pThis->InvalidateControl();
  return S_OK;
}

STDMETHODIMP COleControl::XPersistStorage::Save(LPSTORAGE pStgSave, BOOL) {
  METHOD_PROLOGUE(COleControl, PersistStorage);
  if (pStgSave == NULL) return E_POINTER;
  LPSTREAM pStm = NULL;
  HRESULT hr = pStgSave->CreateStream(
      kContents, STGM_CREATE | STGM_READWRITE | STGM_SHARE_EXCLUSIVE, 0, 0, &pStm);
  if (FAILED(hr)) return hr;
  hr = SaveToStream(pThis, pStm);
  pStm->Release();
  return hr;
}

STDMETHODIMP COleControl::XPersistStorage::SaveCompleted(LPSTORAGE) {
  METHOD_PROLOGUE(COleControl, PersistStorage);
  pThis->m_bModified = FALSE;
  return S_OK;
}

STDMETHODIMP COleControl::XPersistStorage::HandsOffStorage() { return S_OK; }

// ---------------------------------------------------------------------
// IPersistPropertyBag

CTL_IUNKNOWN(PersistPropertyBag)

STDMETHODIMP COleControl::XPersistPropertyBag::GetClassID(LPCLSID pClassID) {
  METHOD_PROLOGUE(COleControl, PersistPropertyBag);
  return pThis->GetClassID(pClassID);
}

STDMETHODIMP COleControl::XPersistPropertyBag::InitNew() {
  METHOD_PROLOGUE(COleControl, PersistPropertyBag);
  pThis->OnResetState();
  pThis->m_bInitialized = TRUE;
  pThis->m_bModified = FALSE;
  return S_OK;
}

STDMETHODIMP COleControl::XPersistPropertyBag::Load(LPPROPERTYBAG pPropBag,
                                                   LPERRORLOG pErrorLog) {
  METHOD_PROLOGUE(COleControl, PersistPropertyBag);
  if (pPropBag == NULL) return E_POINTER;
  try {
    CPropbagPropExchange px(pPropBag, pErrorLog, TRUE);
    pThis->DoPropExchange(&px);
  } catch (CException* e) {
    e->Delete();
    return E_FAIL;
  }
  pThis->m_bInitialized = TRUE;
  pThis->m_bModified = FALSE;
  pThis->InvalidateControl();
  return S_OK;
}

STDMETHODIMP COleControl::XPersistPropertyBag::Save(LPPROPERTYBAG pPropBag, BOOL fClearDirty,
                                                   BOOL fSaveAllProperties) {
  METHOD_PROLOGUE(COleControl, PersistPropertyBag);
  if (pPropBag == NULL) return E_POINTER;
  try {
    CPropbagPropExchange px(pPropBag, NULL, FALSE, fSaveAllProperties);
    pThis->DoPropExchange(&px);
  } catch (CException* e) {
    e->Delete();
    return E_FAIL;
  }
  if (fClearDirty) pThis->m_bModified = FALSE;
  return S_OK;
}
