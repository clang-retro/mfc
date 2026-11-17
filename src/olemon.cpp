// olemon.cpp - CMonikerFile and CAsyncMonikerFile: files over the data a
// URL moniker names, read at once or while the program runs.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#include "afximpl.h"
#include <afxole.h>

IMPLEMENT_DYNAMIC(CMonikerFile, COleStreamFile)
IMPLEMENT_DYNAMIC(CAsyncMonikerFile, CMonikerFile)

// ---------------------------------------------------------------------
// CMonikerFile

CMonikerFile::CMonikerFile() : m_pMoniker(NULL) {}

CMonikerFile::~CMonikerFile() { CMonikerFile::Close(); }

void CMonikerFile::SetError(CFileException* pError, HRESULT hr, LPCTSTR lpszName) {
  if (pError == NULL) return;
  pError->m_cause = hr == INET_E_RESOURCE_NOT_FOUND || hr == INET_E_OBJECT_NOT_FOUND ||
                            hr == MK_E_SYNTAX || hr == INET_E_DATA_NOT_AVAILABLE
                        ? CFileException::fileNotFound
                        : CFileException::genericException;
  pError->m_lOsError = (LONG)hr;
  pError->m_strFileName = lpszName;
}

IBindCtx* CMonikerFile::CreateBindContext(CFileException* pError) {
  IBindCtx* pbc = NULL;
  HRESULT hr = ::CreateBindCtx(0, &pbc);
  if (FAILED(hr)) SetError(pError, hr, NULL);
  return pbc;
}

BOOL CMonikerFile::MakeMoniker(LPCTSTR lpszURL, IBindHost* pBindHost, CFileException* pError) {
  if (lpszURL == NULL || *lpszURL == 0) {
    SetError(pError, MK_E_SYNTAX, lpszURL);
    return FALSE;
  }
  CStringW strURL(lpszURL);
  HRESULT hr;
  if (pBindHost != NULL)
    hr = pBindHost->CreateMoniker((LPOLESTR)(LPCWSTR)strURL, NULL, &m_pMoniker, 0);
  else
    hr = ::CreateURLMoniker(NULL, strURL, &m_pMoniker);
  if (FAILED(hr)) {
    m_pMoniker = NULL;
    SetError(pError, hr, lpszURL);
    return FALSE;
  }
  m_strFileName = lpszURL;
  return TRUE;
}

BOOL CMonikerFile::Bind(IBindHost* pBindHost, CFileException* pError) {
  IStream* pStream = NULL;
  HRESULT hr;
  if (pBindHost != NULL) {
    hr = pBindHost->MonikerBindToStorage(m_pMoniker, NULL, NULL, IID_IStream, (void**)&pStream);
  } else {
    IBindCtx* pbc = CreateBindContext(pError);
    if (pbc == NULL) return FALSE;
    hr = m_pMoniker->BindToStorage(pbc, NULL, IID_IStream, (void**)&pStream);
    pbc->Release();
  }
  if (FAILED(hr) || pStream == NULL) {
    if (pStream != NULL) pStream->Release();
    SetError(pError, FAILED(hr) ? hr : E_FAIL, m_strFileName);
    return FALSE;
  }
  COleStreamFile::Attach(pStream);
  m_bCloseOnDelete = TRUE;
  return TRUE;
}

BOOL CMonikerFile::Open(LPCTSTR lpszURL, CFileException* pError) {
  return Open(lpszURL, (IBindHost*)NULL, pError);
}

BOOL CMonikerFile::Open(IMoniker* pMoniker, CFileException* pError) {
  return Open(pMoniker, (IBindHost*)NULL, pError);
}

BOOL CMonikerFile::Open(LPCTSTR lpszURL, IBindHost* pBindHost, CFileException* pError) {
  Close();
  if (!MakeMoniker(lpszURL, pBindHost, pError)) return FALSE;
  if (!Bind(pBindHost, pError)) {
    Close();
    return FALSE;
  }
  return TRUE;
}

BOOL CMonikerFile::Open(IMoniker* pMoniker, IBindHost* pBindHost, CFileException* pError) {
  Close();
  if (pMoniker == NULL) {
    SetError(pError, E_POINTER, NULL);
    return FALSE;
  }
  m_pMoniker = pMoniker;
  m_pMoniker->AddRef();
  if (!Bind(pBindHost, pError)) {
    Close();
    return FALSE;
  }
  return TRUE;
}

void CMonikerFile::Close() {
  // The data is only read, so nothing is committed.
  if (m_lpStream != NULL) m_lpStream->Release();
  COleStreamFile::Detach();
  m_strFileName.Empty();
  if (m_pMoniker != NULL) {
    m_pMoniker->Release();
    m_pMoniker = NULL;
  }
}

BOOL CMonikerFile::Detach(CFileException*) {
  COleStreamFile::Detach();
  if (m_pMoniker != NULL) {
    m_pMoniker->Release();
    m_pMoniker = NULL;
  }
  m_strFileName.Empty();
  return TRUE;
}

// ---------------------------------------------------------------------
// The bind status callback. It holds a pointer back to its file, which
// the file clears when it closes, since urlmon may keep the callback a
// little longer.
//
// It can be aggregated: made with a controlling IUnknown, it hands that
// object its own IUnknown (m_xInner, which alone counts the callback's
// references), and its IBindStatusCallback passes QueryInterface, AddRef
// and Release on to the controlling object. A file's override of
// CreateBindStatusCallback uses that to add interfaces of its own (such
// as IHttpNegotiate or IAuthenticate) to the callback urlmon sees.

class CAsyncMonikerFile::XBindStatusCallback final : public IBindStatusCallback {
public:
  XBindStatusCallback(CAsyncMonikerFile* pOwner, IUnknown* pUnkOuter)
      : m_cRef(1), m_pOwner(pOwner), m_pUnkOuter(pUnkOuter != NULL ? pUnkOuter : &m_xInner) {
    m_xInner.m_pThis = this;
  }

  // The callback's own IUnknown.
  struct XInner final : public IUnknown {
    XBindStatusCallback* m_pThis;
    STDMETHOD(QueryInterface)(REFIID iid, void** ppv) {
      if (ppv == NULL) return E_POINTER;
      if (::IsEqualIID(iid, IID_IUnknown)) {
        *ppv = static_cast<IUnknown*>(this);
      } else if (::IsEqualIID(iid, IID_IBindStatusCallback)) {
        *ppv = static_cast<IBindStatusCallback*>(m_pThis);
      } else {
        *ppv = NULL;
        return E_NOINTERFACE;
      }
      static_cast<IUnknown*>(*ppv)->AddRef();
      return S_OK;
    }
    STDMETHOD_(ULONG, AddRef)() { return (ULONG)::InterlockedIncrement(&m_pThis->m_cRef); }
    STDMETHOD_(ULONG, Release)() {
      LONG n = ::InterlockedDecrement(&m_pThis->m_cRef);
      if (n == 0) delete m_pThis;
      return (ULONG)n;
    }
  } m_xInner;

  STDMETHOD(QueryInterface)(REFIID iid, void** ppv) {
    return m_pUnkOuter->QueryInterface(iid, ppv);
  }
  STDMETHOD_(ULONG, AddRef)() { return m_pUnkOuter->AddRef(); }
  STDMETHOD_(ULONG, Release)() { return m_pUnkOuter->Release(); }

  STDMETHOD(OnStartBinding)(DWORD, IBinding* pib) {
    if (m_pOwner == NULL) return E_ABORT;
    if (m_pOwner->m_pBinding != NULL) m_pOwner->m_pBinding->Release();
    m_pOwner->m_pBinding = pib;
    if (pib != NULL) pib->AddRef();
    m_pOwner->OnStartBinding();
    return S_OK;
  }
  STDMETHOD(GetPriority)(LONG* pnPriority) {
    if (pnPriority == NULL) return E_POINTER;
    *pnPriority = m_pOwner != NULL ? m_pOwner->GetPriority() : THREAD_PRIORITY_NORMAL;
    return S_OK;
  }
  STDMETHOD(OnLowResource)(DWORD) {
    if (m_pOwner != NULL) m_pOwner->OnLowResource();
    return S_OK;
  }
  STDMETHOD(OnProgress)(ULONG ulProgress, ULONG ulProgressMax, ULONG ulStatusCode,
                        LPCWSTR szStatusText) {
    if (m_pOwner == NULL) return E_ABORT;
    CString strText(szStatusText != NULL ? szStatusText : L"");
    m_pOwner->OnProgress(ulProgress, ulProgressMax, ulStatusCode, strText);
    return S_OK;
  }
  STDMETHOD(OnStopBinding)(HRESULT hresult, LPCWSTR szError) {
    CAsyncMonikerFile* pOwner = m_pOwner;
    if (pOwner == NULL) return S_OK;
    CString strError(szError != NULL ? szError : L"");
    pOwner->OnStopBinding(hresult, strError.IsEmpty() ? NULL : (LPCTSTR)strError);
    // OnStopBinding may have closed the file, which lets go of us.
    if (m_pOwner != NULL && pOwner->m_pBinding != NULL) {
      pOwner->m_pBinding->Release();
      pOwner->m_pBinding = NULL;
    }
    return S_OK;
  }
  STDMETHOD(GetBindInfo)(DWORD* grfBINDF, BINDINFO* pbindinfo) {
    if (grfBINDF == NULL || pbindinfo == NULL) return E_POINTER;
    *grfBINDF = m_pOwner != NULL ? m_pOwner->GetBindInfo() : 0;
    DWORD cb = pbindinfo->cbSize;
    if (cb > sizeof(BINDINFO)) cb = sizeof(BINDINFO);
    memset(pbindinfo, 0, cb);
    pbindinfo->cbSize = cb;
    pbindinfo->dwBindVerb = BINDVERB_GET;
    return S_OK;
  }
  STDMETHOD(OnDataAvailable)(DWORD grfBSCF, DWORD dwSize, FORMATETC* pformatetc,
                             STGMEDIUM* pstgmed) {
    if (m_pOwner == NULL) return S_OK;
    // The first notice brings the stream that the data is read from.
    if (m_pOwner->m_lpStream == NULL && pstgmed != NULL && pstgmed->tymed == TYMED_ISTREAM &&
        pstgmed->pstm != NULL) {
      pstgmed->pstm->AddRef();
      m_pOwner->COleStreamFile::Attach(pstgmed->pstm);
      m_pOwner->m_bCloseOnDelete = TRUE;
    }
    if (pformatetc != NULL) {
      if (m_pOwner->m_pFormatEtc == NULL) m_pOwner->m_pFormatEtc = new FORMATETC;
      *m_pOwner->m_pFormatEtc = *pformatetc;
      m_pOwner->m_pFormatEtc->ptd = NULL;
    }
    if (m_pOwner->m_lpStream != NULL) m_pOwner->OnDataAvailable(dwSize, grfBSCF);
    return S_OK;
  }
  STDMETHOD(OnObjectAvailable)(REFIID, IUnknown*) { return S_OK; }

  LONG m_cRef;
  CAsyncMonikerFile* m_pOwner;
  IUnknown* m_pUnkOuter;  // the controlling object, or m_xInner
};

// ---------------------------------------------------------------------
// CAsyncMonikerFile

CAsyncMonikerFile::CAsyncMonikerFile()
    : m_pCallback(NULL), m_pBSC(NULL), m_pBinding(NULL), m_pFormatEtc(NULL) {}

CAsyncMonikerFile::~CAsyncMonikerFile() { CAsyncMonikerFile::Close(); }

IUnknown* CAsyncMonikerFile::CreateBindStatusCallback(IUnknown* pUnkControlling) {
  if (m_pCallback != NULL) {
    m_pCallback->m_pOwner = NULL;
    m_pCallback->m_xInner.Release();
  }
  // We keep one reference, so that Close can cut the callback off. The
  // caller gets the callback's own IUnknown, which is what a controlling
  // object keeps.
  m_pCallback = new XBindStatusCallback(this, pUnkControlling);
  m_pCallback->m_xInner.AddRef();
  return &m_pCallback->m_xInner;
}

DWORD CAsyncMonikerFile::GetBindInfo() const {
  return BINDF_ASYNCHRONOUS | BINDF_ASYNCSTORAGE | BINDF_PULLDATA;
}

LONG CAsyncMonikerFile::GetPriority() const { return THREAD_PRIORITY_NORMAL; }
void CAsyncMonikerFile::OnStartBinding() {}
void CAsyncMonikerFile::OnProgress(ULONG, ULONG, ULONG, LPCTSTR) {}
void CAsyncMonikerFile::OnDataAvailable(DWORD, DWORD) {}
void CAsyncMonikerFile::OnLowResource() {}
void CAsyncMonikerFile::OnStopBinding(HRESULT, LPCTSTR) {}

IBindCtx* CAsyncMonikerFile::CreateBindContext(CFileException* pError) {
  IBindCtx* pbc = NULL;
  HRESULT hr = ::CreateAsyncBindCtx(0, m_pBSC, NULL, &pbc);
  if (FAILED(hr)) SetError(pError, hr, NULL);
  return pbc;
}

IBindHost* CAsyncMonikerFile::GetBindHost(IUnknown* pUnknown) {
  if (pUnknown == NULL) return NULL;
  IBindHost* pBindHost = NULL;
  IServiceProvider* pSP = NULL;
  if (SUCCEEDED(pUnknown->QueryInterface(IID_IServiceProvider, (void**)&pSP))) {
    pSP->QueryService(SID_IBindHost, IID_IBindHost, (void**)&pBindHost);
    pSP->Release();
  }
  if (pBindHost == NULL) pUnknown->QueryInterface(IID_IBindHost, (void**)&pBindHost);
  return pBindHost;
}

BOOL CAsyncMonikerFile::Bind(IBindHost* pBindHost, CFileException* pError) {
  IUnknown* pUnk = CreateBindStatusCallback(NULL);
  if (pUnk == NULL) {
    SetError(pError, E_OUTOFMEMORY, m_strFileName);
    return FALSE;
  }
  IBindStatusCallback* pBSC = NULL;
  HRESULT hr = pUnk->QueryInterface(IID_IBindStatusCallback, (void**)&pBSC);
  pUnk->Release();
  if (FAILED(hr)) {
    SetError(pError, hr, m_strFileName);
    return FALSE;
  }
  m_pBSC = pBSC;

  IStream* pStream = NULL;
  if (pBindHost != NULL) {
    hr = pBindHost->MonikerBindToStorage(m_pMoniker, NULL, pBSC, IID_IStream, (void**)&pStream);
  } else {
    IBindCtx* pbc = CreateBindContext(pError);
    if (pbc == NULL) return FALSE;
    hr = m_pMoniker->BindToStorage(pbc, NULL, IID_IStream, (void**)&pStream);
    pbc->Release();
  }
  if (FAILED(hr)) {
    if (pStream != NULL) pStream->Release();
    SetError(pError, hr, m_strFileName);
    return FALSE;
  }
  // The data may be there already (MK_S_ASYNCHRONOUS when it is coming).
  if (pStream != NULL) {
    if (m_lpStream == NULL) {
      COleStreamFile::Attach(pStream);
      m_bCloseOnDelete = TRUE;
    } else {
      pStream->Release();
    }
  }
  return TRUE;
}

BOOL CAsyncMonikerFile::Open(LPCTSTR lpszURL, CFileException* pError) {
  return CMonikerFile::Open(lpszURL, (IBindHost*)NULL, pError);
}

BOOL CAsyncMonikerFile::Open(IMoniker* pMoniker, CFileException* pError) {
  return CMonikerFile::Open(pMoniker, (IBindHost*)NULL, pError);
}

BOOL CAsyncMonikerFile::Open(LPCTSTR lpszURL, IBindHost* pBindHost, CFileException* pError) {
  return CMonikerFile::Open(lpszURL, pBindHost, pError);
}

BOOL CAsyncMonikerFile::Open(IMoniker* pMoniker, IBindHost* pBindHost, CFileException* pError) {
  return CMonikerFile::Open(pMoniker, pBindHost, pError);
}

BOOL CAsyncMonikerFile::Open(LPCTSTR lpszURL, IServiceProvider* pServiceProvider,
                             CFileException* pError) {
  return Open(lpszURL, (IUnknown*)pServiceProvider, pError);
}

BOOL CAsyncMonikerFile::Open(IMoniker* pMoniker, IServiceProvider* pServiceProvider,
                             CFileException* pError) {
  return Open(pMoniker, (IUnknown*)pServiceProvider, pError);
}

BOOL CAsyncMonikerFile::Open(LPCTSTR lpszURL, IUnknown* pUnknown, CFileException* pError) {
  IBindHost* pBindHost = GetBindHost(pUnknown);
  BOOL bOK = CMonikerFile::Open(lpszURL, pBindHost, pError);
  if (pBindHost != NULL) pBindHost->Release();
  return bOK;
}

BOOL CAsyncMonikerFile::Open(IMoniker* pMoniker, IUnknown* pUnknown, CFileException* pError) {
  IBindHost* pBindHost = GetBindHost(pUnknown);
  BOOL bOK = CMonikerFile::Open(pMoniker, pBindHost, pError);
  if (pBindHost != NULL) pBindHost->Release();
  return bOK;
}

void CAsyncMonikerFile::Close() {
  if (m_pCallback != NULL) m_pCallback->m_pOwner = NULL;
  if (m_pBinding != NULL) m_pBinding->Abort();
  if (m_pCallback != NULL) {
    m_pCallback->m_xInner.Release();
    m_pCallback = NULL;
  }
  if (m_pBSC != NULL) {
    m_pBSC->Release();
    m_pBSC = NULL;
  }
  if (m_pBinding != NULL) {
    m_pBinding->Release();
    m_pBinding = NULL;
  }
  delete m_pFormatEtc;
  m_pFormatEtc = NULL;
  CMonikerFile::Close();
}
