// oledata.cpp - COleDataSource and COleDataObject: data offered on the
// clipboard or in a drag, and data received from them.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
#include "afximpl.h"
#include <afxole.h>
#include <afxadv.h>

IMPLEMENT_DYNAMIC(COleDataSource, CCmdTarget)

BEGIN_INTERFACE_MAP(COleDataSource, CCmdTarget)
  INTERFACE_PART(COleDataSource, IID_IDataObject, DataObject)
END_INTERFACE_MAP()

static const DWORD TYMED_ANY = TYMED_HGLOBAL | TYMED_FILE | TYMED_ISTREAM | TYMED_ISTORAGE |
                               TYMED_GDI | TYMED_MFPICT | TYMED_ENHMF;

static void FillFormatEtc(LPFORMATETC lpFormatEtc, CLIPFORMAT cfFormat, DWORD tymed,
                          FORMATETC& formatEtc) {
  if (lpFormatEtc != NULL) {
    formatEtc = *lpFormatEtc;
    formatEtc.cfFormat = cfFormat;
    if (formatEtc.tymed == 0) formatEtc.tymed = tymed;
    return;
  }
  formatEtc.cfFormat = cfFormat;
  formatEtc.ptd = NULL;
  formatEtc.dwAspect = DVASPECT_CONTENT;
  formatEtc.lindex = -1;
  formatEtc.tymed = tymed;
}

// Copies a medium, so that each GetData caller owns what it gets.
static BOOL CopyStgMedium(CLIPFORMAT cfFormat, LPSTGMEDIUM lpDest, const STGMEDIUM* lpSource) {
  lpDest->tymed = lpSource->tymed;
  lpDest->pUnkForRelease = NULL;
  switch (lpSource->tymed) {
  case TYMED_HGLOBAL:
    lpDest->hGlobal = (HGLOBAL)::OleDuplicateData(lpSource->hGlobal, cfFormat, GMEM_MOVEABLE);
    return lpDest->hGlobal != NULL;
  case TYMED_GDI:
    lpDest->hBitmap = (HBITMAP)::OleDuplicateData(lpSource->hBitmap, cfFormat, 0);
    break;
  case TYMED_MFPICT: {
    // Copied by hand: Wine's OleDuplicateData does not copy a picture.
    lpDest->hMetaFilePict = NULL;
    const METAFILEPICT* pSrc = (const METAFILEPICT*)::GlobalLock(lpSource->hMetaFilePict);
    if (pSrc == NULL) break;
    HGLOBAL h = ::GlobalAlloc(GMEM_MOVEABLE | GMEM_SHARE, sizeof(METAFILEPICT));
    METAFILEPICT* pDest = h != NULL ? (METAFILEPICT*)::GlobalLock(h) : NULL;
    if (pDest != NULL) {
      *pDest = *pSrc;
      pDest->hMF = ::CopyMetaFile(pSrc->hMF, NULL);
      BOOL bOk = pDest->hMF != NULL;
      ::GlobalUnlock(h);
      if (bOk)
        lpDest->hMetaFilePict = h;
      else
        ::GlobalFree(h);
    } else if (h != NULL) {
      ::GlobalFree(h);
    }
    ::GlobalUnlock(lpSource->hMetaFilePict);
    break;
  }
  case TYMED_ENHMF:
    lpDest->hEnhMetaFile = ::CopyEnhMetaFile(lpSource->hEnhMetaFile, NULL);
    break;
  case TYMED_ISTREAM:
  case TYMED_ISTORAGE:
    lpDest->pstm = lpSource->pstm;
    lpDest->pstm->AddRef();
    return TRUE;
  case TYMED_FILE: {
    int n = lstrlenW(lpSource->lpszFileName) + 1;
    lpDest->lpszFileName = (LPOLESTR)::CoTaskMemAlloc(n * sizeof(OLECHAR));
    if (lpDest->lpszFileName == NULL) return FALSE;
    memcpy(lpDest->lpszFileName, lpSource->lpszFileName, n * sizeof(OLECHAR));
    return TRUE;
  }
  default:
    break;
  }
  // The handle forms end here.
  if (lpDest->hGlobal != NULL) return TRUE;
  lpDest->tymed = TYMED_NULL;
  return FALSE;
}

// ---------------------------------------------------------------------
// The format enumerator

class _AfxEnumFormatEtc final : public IEnumFORMATETC {
public:
  LONG m_nRef;
  CArray<FORMATETC, const FORMATETC&> m_formats;
  ULONG m_nPos;
  _AfxEnumFormatEtc() : m_nRef(1), m_nPos(0) {}

  STDMETHOD(QueryInterface)(REFIID iid, void** ppv) {
    if (::IsEqualIID(iid, IID_IUnknown) || ::IsEqualIID(iid, IID_IEnumFORMATETC)) {
      *ppv = this;
      AddRef();
      return S_OK;
    }
    *ppv = NULL;
    return E_NOINTERFACE;
  }
  STDMETHOD_(ULONG, AddRef)() { return (ULONG)::InterlockedIncrement(&m_nRef); }
  STDMETHOD_(ULONG, Release)() {
    LONG n = ::InterlockedDecrement(&m_nRef);
    if (n == 0) delete this;
    return (ULONG)n;
  }
  STDMETHOD(Next)(ULONG celt, FORMATETC* rgelt, ULONG* pceltFetched) {
    ULONG n = 0;
    while (n < celt && m_nPos < (ULONG)m_formats.GetSize()) rgelt[n++] = m_formats[(INT_PTR)m_nPos++];
    if (pceltFetched != NULL) *pceltFetched = n;
    return n == celt ? S_OK : S_FALSE;
  }
  STDMETHOD(Skip)(ULONG celt) {
    m_nPos += celt;
    if (m_nPos > (ULONG)m_formats.GetSize()) {
      m_nPos = (ULONG)m_formats.GetSize();
      return S_FALSE;
    }
    return S_OK;
  }
  STDMETHOD(Reset)() {
    m_nPos = 0;
    return S_OK;
  }
  STDMETHOD(Clone)(IEnumFORMATETC** ppenum) {
    _AfxEnumFormatEtc* p = new _AfxEnumFormatEtc;
    p->m_formats.Copy(m_formats);
    p->m_nPos = m_nPos;
    *ppenum = p;
    return S_OK;
  }
};

// ---------------------------------------------------------------------
// COleDataSource

static COleDataSource* g_pClipboardOwner = NULL;

COleDataSource::COleDataSource() {}

COleDataSource::~COleDataSource() {
  if (g_pClipboardOwner == this) g_pClipboardOwner = NULL;
  Empty();
}

void COleDataSource::Empty() {
  for (INT_PTR i = 0; i < m_cache.GetSize(); i++) {
    AFX_DATACACHE_ENTRY* pEntry = (AFX_DATACACHE_ENTRY*)m_cache[i];
    if (pEntry->m_stgMedium.tymed != TYMED_NULL) ::ReleaseStgMedium(&pEntry->m_stgMedium);
    if (pEntry->m_formatEtc.ptd != NULL) ::CoTaskMemFree(pEntry->m_formatEtc.ptd);
    delete pEntry;
  }
  m_cache.RemoveAll();
}

static BOOL Matches(const FORMATETC& entry, const FORMATETC* p) {
  return entry.cfFormat == p->cfFormat && (entry.tymed & p->tymed) != 0 &&
         entry.dwAspect == p->dwAspect && (entry.lindex == p->lindex || p->lindex == -1);
}

AFX_DATACACHE_ENTRY* COleDataSource::Lookup(LPFORMATETC lpFormatEtc, DATADIR nDataDir) const {
  for (INT_PTR i = 0; i < m_cache.GetSize(); i++) {
    AFX_DATACACHE_ENTRY* pEntry = (AFX_DATACACHE_ENTRY*)m_cache[i];
    if (pEntry->m_nDataDir == nDataDir && Matches(pEntry->m_formatEtc, lpFormatEtc)) return pEntry;
  }
  return NULL;
}

// The entry for a format, emptied, or a new one.
AFX_DATACACHE_ENTRY* COleDataSource::GetCacheEntry(LPFORMATETC lpFormatEtc, DATADIR nDataDir) {
  AFX_DATACACHE_ENTRY* pEntry = NULL;
  for (INT_PTR i = 0; i < m_cache.GetSize(); i++) {
    AFX_DATACACHE_ENTRY* p = (AFX_DATACACHE_ENTRY*)m_cache[i];
    if (p->m_nDataDir == nDataDir && p->m_formatEtc.cfFormat == lpFormatEtc->cfFormat &&
        p->m_formatEtc.dwAspect == lpFormatEtc->dwAspect &&
        p->m_formatEtc.lindex == lpFormatEtc->lindex) {
      pEntry = p;
      break;
    }
  }
  if (pEntry != NULL) {
    if (pEntry->m_stgMedium.tymed != TYMED_NULL) ::ReleaseStgMedium(&pEntry->m_stgMedium);
    if (pEntry->m_formatEtc.ptd != NULL) ::CoTaskMemFree(pEntry->m_formatEtc.ptd);
  } else {
    pEntry = new AFX_DATACACHE_ENTRY;
    m_cache.Add(pEntry);
  }
  pEntry->m_formatEtc = *lpFormatEtc;
  pEntry->m_formatEtc.ptd = NULL;  // devices are not kept
  memset(&pEntry->m_stgMedium, 0, sizeof(STGMEDIUM));
  pEntry->m_stgMedium.tymed = TYMED_NULL;
  pEntry->m_nDataDir = nDataDir;
  return pEntry;
}

void COleDataSource::CacheData(CLIPFORMAT cfFormat, LPSTGMEDIUM lpStgMedium,
                               LPFORMATETC lpFormatEtc) {
  FORMATETC formatEtc;
  FillFormatEtc(lpFormatEtc, cfFormat, lpStgMedium->tymed, formatEtc);
  formatEtc.tymed = lpStgMedium->tymed;
  AFX_DATACACHE_ENTRY* pEntry = GetCacheEntry(&formatEtc, DATADIR_GET);
  pEntry->m_stgMedium = *lpStgMedium;
}

void COleDataSource::CacheGlobalData(CLIPFORMAT cfFormat, HGLOBAL hGlobal,
                                     LPFORMATETC lpFormatEtc) {
  STGMEDIUM stgMedium;
  stgMedium.tymed = TYMED_HGLOBAL;
  stgMedium.hGlobal = hGlobal;
  stgMedium.pUnkForRelease = NULL;
  CacheData(cfFormat, &stgMedium, lpFormatEtc);
}

void COleDataSource::DelayRenderData(CLIPFORMAT cfFormat, LPFORMATETC lpFormatEtc) {
  FORMATETC formatEtc;
  FillFormatEtc(lpFormatEtc, cfFormat, TYMED_HGLOBAL, formatEtc);
  GetCacheEntry(&formatEtc, DATADIR_GET);
}

void COleDataSource::DelayRenderFileData(CLIPFORMAT cfFormat, LPFORMATETC lpFormatEtc) {
  FORMATETC formatEtc;
  FillFormatEtc(lpFormatEtc, cfFormat, TYMED_HGLOBAL | TYMED_ISTREAM, formatEtc);
  GetCacheEntry(&formatEtc, DATADIR_GET);
}

void COleDataSource::DelaySetData(CLIPFORMAT cfFormat, LPFORMATETC lpFormatEtc) {
  FORMATETC formatEtc;
  FillFormatEtc(lpFormatEtc, cfFormat, TYMED_HGLOBAL, formatEtc);
  GetCacheEntry(&formatEtc, DATADIR_SET);
}

BOOL COleDataSource::OnRenderGlobalData(LPFORMATETC, HGLOBAL*) { return FALSE; }
BOOL COleDataSource::OnRenderFileData(LPFORMATETC, CFile*) { return FALSE; }
BOOL COleDataSource::OnSetData(LPFORMATETC, LPSTGMEDIUM, BOOL) { return FALSE; }

// Renders by OnRenderGlobalData, or OnRenderFileData into memory. For
// GetDataHere the medium already holds the caller's memory or stream.
BOOL COleDataSource::OnRenderData(LPFORMATETC lpFormatEtc, LPSTGMEDIUM lpStgMedium) {
  BOOL bHere = lpStgMedium->tymed != TYMED_NULL;
  DWORD tymed = bHere ? lpStgMedium->tymed : lpFormatEtc->tymed;
  if (tymed & TYMED_HGLOBAL) {
    HGLOBAL hGlobal = bHere ? lpStgMedium->hGlobal : NULL;
    if (OnRenderGlobalData(lpFormatEtc, &hGlobal)) {
      lpStgMedium->tymed = TYMED_HGLOBAL;
      lpStgMedium->hGlobal = hGlobal;
      lpStgMedium->pUnkForRelease = NULL;
      return TRUE;
    }
    CSharedFile file;
    if (bHere) file.SetHandle(lpStgMedium->hGlobal, FALSE);
    if (OnRenderFileData(lpFormatEtc, &file)) {
      lpStgMedium->tymed = TYMED_HGLOBAL;
      HGLOBAL hRendered = file.Detach();  // the caller's memory for GetDataHere
      lpStgMedium->hGlobal = hRendered;
      lpStgMedium->pUnkForRelease = NULL;
      return TRUE;
    }
    if (bHere) file.Detach();
  }
  if (tymed & TYMED_ISTREAM) {
    CSharedFile file;
    if (OnRenderFileData(lpFormatEtc, &file)) {
      ULONGLONG nLen = file.GetLength();
      HGLOBAL hGlobal = file.Detach();
      if (bHere) {
        // Write into the caller's stream.
        void* p = ::GlobalLock(hGlobal);
        ULONG cb = 0;
        HRESULT hr = lpStgMedium->pstm->Write(p, (ULONG)nLen, &cb);
        ::GlobalUnlock(hGlobal);
        ::GlobalFree(hGlobal);
        return SUCCEEDED(hr);
      }
      LPSTREAM pStream = NULL;
      if (FAILED(::CreateStreamOnHGlobal(hGlobal, TRUE, &pStream))) {
        ::GlobalFree(hGlobal);
        return FALSE;
      }
      ULARGE_INTEGER size;
      size.QuadPart = nLen;
      pStream->SetSize(size);
      lpStgMedium->tymed = TYMED_ISTREAM;
      lpStgMedium->pstm = pStream;
      lpStgMedium->pUnkForRelease = NULL;
      return TRUE;
    }
  }
  return FALSE;
}

void COleDataSource::SetClipboard() {
  if (SUCCEEDED(::OleSetClipboard((LPDATAOBJECT)&m_xDataObject))) {
    g_pClipboardOwner = this;
    InternalRelease();  // the clipboard holds it now
  }
}

COleDataSource* PASCAL COleDataSource::GetClipboardOwner() {
  if (g_pClipboardOwner == NULL) return NULL;
  if (::OleIsCurrentClipboard((LPDATAOBJECT)&g_pClipboardOwner->m_xDataObject) != S_OK) {
    g_pClipboardOwner = NULL;
    return NULL;
  }
  return g_pClipboardOwner;
}

void PASCAL COleDataSource::FlushClipboard() {
  if (GetClipboardOwner() != NULL) ::OleFlushClipboard();
}

// ---------------------------------------------------------------------
// IDataObject

STDMETHODIMP_(ULONG) COleDataSource::XDataObject::AddRef() {
  METHOD_PROLOGUE(COleDataSource, DataObject)
  return pThis->ExternalAddRef();
}

STDMETHODIMP_(ULONG) COleDataSource::XDataObject::Release() {
  METHOD_PROLOGUE(COleDataSource, DataObject)
  return pThis->ExternalRelease();
}

STDMETHODIMP COleDataSource::XDataObject::QueryInterface(REFIID iid, LPVOID* ppvObj) {
  METHOD_PROLOGUE(COleDataSource, DataObject)
  return (HRESULT)pThis->ExternalQueryInterface(&iid, ppvObj);
}

STDMETHODIMP COleDataSource::XDataObject::GetData(LPFORMATETC pformatetcIn, LPSTGMEDIUM pmedium) {
  METHOD_PROLOGUE(COleDataSource, DataObject)
  if (pformatetcIn == NULL || pmedium == NULL) return E_INVALIDARG;
  memset(pmedium, 0, sizeof(STGMEDIUM));
  AFX_DATACACHE_ENTRY* pEntry = pThis->Lookup(pformatetcIn, DATADIR_GET);
  if (pEntry == NULL) return DV_E_FORMATETC;
  if (pEntry->m_stgMedium.tymed != TYMED_NULL)
    return CopyStgMedium(pformatetcIn->cfFormat, pmedium, &pEntry->m_stgMedium) ? S_OK : E_OUTOFMEMORY;
  // Rendered now, for the types both sides allow.
  FORMATETC formatEtc = *pformatetcIn;
  formatEtc.tymed &= pEntry->m_formatEtc.tymed;
  pmedium->tymed = TYMED_NULL;
  BOOL bOk = FALSE;
  try {
    bOk = pThis->OnRenderData(&formatEtc, pmedium);
  } catch (CException* e) {
    e->Delete();
  }
  return bOk ? S_OK : DV_E_FORMATETC;
}

STDMETHODIMP COleDataSource::XDataObject::GetDataHere(LPFORMATETC pformatetc, LPSTGMEDIUM pmedium) {
  METHOD_PROLOGUE(COleDataSource, DataObject)
  if (pformatetc == NULL || pmedium == NULL) return E_INVALIDARG;
  AFX_DATACACHE_ENTRY* pEntry = pThis->Lookup(pformatetc, DATADIR_GET);
  if (pEntry == NULL || (pmedium->tymed & pformatetc->tymed) == 0) return DV_E_FORMATETC;
  const STGMEDIUM& cached = pEntry->m_stgMedium;
  if (cached.tymed == TYMED_HGLOBAL && pmedium->tymed == TYMED_HGLOBAL) {
    SIZE_T nSize = ::GlobalSize(cached.hGlobal);
    if (::GlobalSize(pmedium->hGlobal) < nSize) return STG_E_MEDIUMFULL;
    void* pSrc = ::GlobalLock(cached.hGlobal);
    void* pDest = ::GlobalLock(pmedium->hGlobal);
    memcpy(pDest, pSrc, nSize);
    ::GlobalUnlock(pmedium->hGlobal);
    ::GlobalUnlock(cached.hGlobal);
    return S_OK;
  }
  if (cached.tymed != TYMED_NULL) return DV_E_TYMED;
  BOOL bOk = FALSE;
  try {
    bOk = pThis->OnRenderData(pformatetc, pmedium);
  } catch (CException* e) {
    e->Delete();
  }
  return bOk ? S_OK : DV_E_FORMATETC;
}

STDMETHODIMP COleDataSource::XDataObject::QueryGetData(LPFORMATETC pformatetc) {
  METHOD_PROLOGUE(COleDataSource, DataObject)
  if (pformatetc == NULL) return E_INVALIDARG;
  return pThis->Lookup(pformatetc, DATADIR_GET) != NULL ? S_OK : DV_E_FORMATETC;
}

STDMETHODIMP COleDataSource::XDataObject::GetCanonicalFormatEtc(LPFORMATETC pformatetcIn,
                                                                LPFORMATETC pformatetcOut) {
  if (pformatetcOut == NULL) return E_INVALIDARG;
  if (pformatetcIn != NULL) *pformatetcOut = *pformatetcIn;
  pformatetcOut->ptd = NULL;
  return DATA_S_SAMEFORMATETC;
}

STDMETHODIMP COleDataSource::XDataObject::SetData(LPFORMATETC pformatetc, LPSTGMEDIUM pmedium,
                                                  BOOL fRelease) {
  METHOD_PROLOGUE(COleDataSource, DataObject)
  if (pformatetc == NULL || pmedium == NULL) return E_INVALIDARG;
  if (pThis->Lookup(pformatetc, DATADIR_SET) == NULL) return DV_E_FORMATETC;
  BOOL bOk = FALSE;
  try {
    bOk = pThis->OnSetData(pformatetc, pmedium, fRelease);
  } catch (CException* e) {
    e->Delete();
  }
  return bOk ? S_OK : E_FAIL;
}

STDMETHODIMP COleDataSource::XDataObject::EnumFormatEtc(DWORD dwDirection,
                                                        LPENUMFORMATETC* ppenumFormatEtc) {
  METHOD_PROLOGUE(COleDataSource, DataObject)
  if (ppenumFormatEtc == NULL) return E_INVALIDARG;
  _AfxEnumFormatEtc* pEnum = new _AfxEnumFormatEtc;
  for (INT_PTR i = 0; i < pThis->m_cache.GetSize(); i++) {
    AFX_DATACACHE_ENTRY* pEntry = (AFX_DATACACHE_ENTRY*)pThis->m_cache[i];
    if ((DWORD)pEntry->m_nDataDir == dwDirection) pEnum->m_formats.Add(pEntry->m_formatEtc);
  }
  *ppenumFormatEtc = pEnum;
  return S_OK;
}

STDMETHODIMP COleDataSource::XDataObject::DAdvise(LPFORMATETC, DWORD, LPADVISESINK, DWORD*) {
  return OLE_E_ADVISENOTSUPPORTED;
}
STDMETHODIMP COleDataSource::XDataObject::DUnadvise(DWORD) { return OLE_E_ADVISENOTSUPPORTED; }
STDMETHODIMP COleDataSource::XDataObject::EnumDAdvise(LPENUMSTATDATA*) {
  return OLE_E_ADVISENOTSUPPORTED;
}

// ---------------------------------------------------------------------
// COleDataObject

COleDataObject::COleDataObject()
    : m_lpDataObject(NULL), m_lpEnumerator(NULL), m_bAutoRelease(TRUE), m_bClipboard(FALSE) {}

COleDataObject::~COleDataObject() { Release(); }

void COleDataObject::Attach(LPDATAOBJECT lpDataObject, BOOL bAutoRelease) {
  Release();
  m_lpDataObject = lpDataObject;
  m_bAutoRelease = bAutoRelease;
  m_bClipboard = FALSE;
}

LPDATAOBJECT COleDataObject::Detach() {
  if (m_lpEnumerator != NULL) {
    m_lpEnumerator->Release();
    m_lpEnumerator = NULL;
  }
  LPDATAOBJECT p = m_lpDataObject;
  m_lpDataObject = NULL;
  m_bClipboard = FALSE;
  return p;
}

void COleDataObject::Release() {
  LPDATAOBJECT p = Detach();
  if (p != NULL && m_bAutoRelease) p->Release();
}

BOOL COleDataObject::AttachClipboard() {
  LPDATAOBJECT p = NULL;
  if (FAILED(::OleGetClipboard(&p)) || p == NULL) return FALSE;
  Attach(p, TRUE);
  m_bClipboard = TRUE;
  return TRUE;
}

void COleDataObject::BeginEnumFormats() {
  if (m_lpEnumerator != NULL) {
    m_lpEnumerator->Release();
    m_lpEnumerator = NULL;
  }
  if (m_lpDataObject != NULL) m_lpDataObject->EnumFormatEtc(DATADIR_GET, &m_lpEnumerator);
}

BOOL COleDataObject::GetNextFormat(LPFORMATETC lpFormatEtc) {
  if (m_lpEnumerator == NULL) return FALSE;
  ULONG n = 0;
  if (m_lpEnumerator->Next(1, lpFormatEtc, &n) != S_OK || n != 1) return FALSE;
  if (lpFormatEtc->ptd != NULL) {
    ::CoTaskMemFree(lpFormatEtc->ptd);
    lpFormatEtc->ptd = NULL;
  }
  return TRUE;
}

BOOL COleDataObject::IsDataAvailable(CLIPFORMAT cfFormat, LPFORMATETC lpFormatEtc) {
  if (m_lpDataObject == NULL) return FALSE;
  FORMATETC formatEtc;
  FillFormatEtc(lpFormatEtc, cfFormat, TYMED_ANY, formatEtc);
  return m_lpDataObject->QueryGetData(&formatEtc) == S_OK;
}

BOOL COleDataObject::GetData(CLIPFORMAT cfFormat, LPSTGMEDIUM lpStgMedium,
                             LPFORMATETC lpFormatEtc) {
  if (m_lpDataObject == NULL) return FALSE;
  FORMATETC formatEtc;
  FillFormatEtc(lpFormatEtc, cfFormat, TYMED_ANY, formatEtc);
  return SUCCEEDED(m_lpDataObject->GetData(&formatEtc, lpStgMedium));
}

HGLOBAL COleDataObject::GetGlobalData(CLIPFORMAT cfFormat, LPFORMATETC lpFormatEtc) {
  FORMATETC formatEtc;
  FillFormatEtc(lpFormatEtc, cfFormat, TYMED_HGLOBAL | TYMED_ISTREAM, formatEtc);
  formatEtc.tymed &= TYMED_HGLOBAL | TYMED_ISTREAM;
  STGMEDIUM stgMedium;
  if (!GetData(cfFormat, &stgMedium, &formatEtc)) return NULL;
  HGLOBAL hResult = NULL;
  if (stgMedium.tymed == TYMED_HGLOBAL) {
    if (stgMedium.pUnkForRelease == NULL) return stgMedium.hGlobal;  // ours already
    hResult = (HGLOBAL)::OleDuplicateData(stgMedium.hGlobal, cfFormat, GMEM_MOVEABLE);
  } else if (stgMedium.tymed == TYMED_ISTREAM) {
    // Read the stream from its start into memory.
    CSharedFile file;
    LARGE_INTEGER zero;
    zero.QuadPart = 0;
    stgMedium.pstm->Seek(zero, STREAM_SEEK_SET, NULL);
    BYTE buf[4096];
    ULONG cb = 0;
    while (SUCCEEDED(stgMedium.pstm->Read(buf, sizeof(buf), &cb)) && cb != 0) file.Write(buf, cb);
    hResult = file.Detach();
  }
  ::ReleaseStgMedium(&stgMedium);
  return hResult;
}

CFile* COleDataObject::GetFileData(CLIPFORMAT cfFormat, LPFORMATETC lpFormatEtc) {
  HGLOBAL hGlobal = GetGlobalData(cfFormat, lpFormatEtc);
  if (hGlobal == NULL) return NULL;
  CSharedFile* pFile = new CSharedFile;
  SIZE_T nSize = ::GlobalSize(hGlobal);
  void* p = ::GlobalLock(hGlobal);
  pFile->Write(p, (UINT)nSize);
  ::GlobalUnlock(hGlobal);
  ::GlobalFree(hGlobal);
  pFile->SeekToBegin();
  return pFile;
}
