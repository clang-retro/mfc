// ctldata.cpp - data path properties of controls: CDataPathProperty,
// CCachedDataPathProperty, PX_DataPath and COleControl::Load.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#include "afximpl.h"
#include <afxctl.h>

IMPLEMENT_DYNAMIC(CDataPathProperty, CAsyncMonikerFile)
IMPLEMENT_DYNAMIC(CCachedDataPathProperty, CDataPathProperty)

CDataPathProperty::CDataPathProperty(COleControl* pControl) : m_pControl(pControl) {}

CDataPathProperty::CDataPathProperty(LPCTSTR lpszPath, COleControl* pControl)
    : m_pControl(pControl), m_strPath(lpszPath) {}

CDataPathProperty::~CDataPathProperty() {}

void CDataPathProperty::ResetData() {}

BOOL CDataPathProperty::Open(LPCTSTR lpszPath, COleControl* pControl, CFileException* pError) {
  m_pControl = pControl;
  m_strPath = lpszPath;
  // The container's bind host comes through the client site.
  LPUNKNOWN pSite = m_pControl != NULL ? (LPUNKNOWN)m_pControl->GetClientSite() : NULL;
  return CAsyncMonikerFile::Open(m_strPath, pSite, pError);
}

BOOL CDataPathProperty::Open(COleControl* pControl, CFileException* pError) {
  return Open(m_strPath, pControl, pError);
}

BOOL CDataPathProperty::Open(LPCTSTR lpszPath, CFileException* pError) {
  return Open(lpszPath, m_pControl, pError);
}

BOOL CDataPathProperty::Open(CFileException* pError) { return Open(m_strPath, m_pControl, pError); }

CCachedDataPathProperty::CCachedDataPathProperty(COleControl* pControl)
    : CDataPathProperty(pControl) {}

CCachedDataPathProperty::CCachedDataPathProperty(LPCTSTR lpszPath, COleControl* pControl)
    : CDataPathProperty(lpszPath, pControl) {}

void CCachedDataPathProperty::ResetData() { m_Cache.SetLength(0); }

void CCachedDataPathProperty::OnDataAvailable(DWORD dwSize, DWORD) {
  // dwSize counts all the data so far; read what is new. With pulled data
  // the stream says E_PENDING when it has no more for now.
  BYTE buf[4096];
  DWORD dwHave = (DWORD)m_Cache.GetLength();
  while (m_lpStream != NULL && dwHave < dwSize) {
    ULONG nWant = dwSize - dwHave < sizeof(buf) ? dwSize - dwHave : (ULONG)sizeof(buf);
    ULONG nRead = 0;
    HRESULT hr = m_lpStream->Read(buf, nWant, &nRead);
    if (nRead > 0) {
      m_Cache.SeekToEnd();
      m_Cache.Write(buf, nRead);
      dwHave += nRead;
    }
    if (FAILED(hr) || hr == S_FALSE || nRead == 0) break;
  }
}

BOOL COleControl::Load(LPCTSTR strNewPath, CDataPathProperty& prop) {
  prop.SetControl(this);
  prop.Close();
  prop.ResetData();
  return prop.Open(strNewPath, this);
}

BOOL AFXAPI PX_DataPath(CPropExchange* pPX, LPCTSTR pszPropName,
                        CDataPathProperty& dataPathProperty) {
  CString strPath = dataPathProperty.GetPath();
  if (!PX_String(pPX, pszPropName, strPath, _T(""))) return FALSE;
  if (pPX->IsLoading()) {
    COleControl* pControl = dataPathProperty.GetControl();
    if (!strPath.IsEmpty() && pControl != NULL) {
      pControl->Load(strPath, dataPathProperty);
    } else {
      dataPathProperty.Close();
      dataPathProperty.ResetData();
      dataPathProperty.SetPath(strPath);
    }
  }
  return TRUE;
}
