// afxsettingsstore.h - CSettingsStore, the Feature Pack's registry
// access: a key under HKEY_CURRENT_USER (or HKEY_LOCAL_MACHINE for an
// administrator's store) opened or created by path, and values read and
// written by type. Numbers are REG_DWORD, strings REG_SZ, and arrays,
// points, rectangles, lists and objects REG_BINARY, serialized with
// CArchive. CSettingsStoreSP makes the store of the class an application
// sets (SetRuntimeClass), so settings can go somewhere else.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#ifndef __AFXSETTINGSSTORE_H__
#define __AFXSETTINGSSTORE_H__

#ifndef __AFXWIN_H__
#include <afxwin.h>
#endif
#include <afxtempl.h>

class CSettingsStore : public CObject {
  DECLARE_DYNCREATE(CSettingsStore)
public:
  CSettingsStore(BOOL bAdmin, BOOL bReadOnly);
  virtual ~CSettingsStore();

  virtual BOOL CreateKey(LPCTSTR pszPath);
  virtual BOOL Open(LPCTSTR pszPath);
  virtual void Close();

  virtual BOOL DeleteValue(LPCTSTR pszValue);
  virtual BOOL DeleteKey(LPCTSTR pszPath, BOOL bAdmin = FALSE);

  virtual BOOL Write(LPCTSTR pszKey, int iVal);
  virtual BOOL Write(LPCTSTR pszKey, DWORD dwVal);
  virtual BOOL Write(LPCTSTR pszKey, LPCTSTR pszVal);
  virtual BOOL Write(LPCTSTR pszKey, const CRect& rect);
  virtual BOOL Write(LPCTSTR pszKey, LPBYTE pData, UINT nBytes);
  virtual BOOL Write(LPCTSTR pszKey, CObject& obj);
  virtual BOOL Write(LPCTSTR pszKey, CObject* pObj);
  virtual BOOL Write(LPCTSTR pszKey, CStringList& list);
  virtual BOOL Write(LPCTSTR pszKey, CObList& list);
  virtual BOOL Write(LPCTSTR pszKey, CStringArray& array);
  virtual BOOL Write(LPCTSTR pszKey, CByteArray& array);
  virtual BOOL Write(LPCTSTR pszKey, CWordArray& array);
  virtual BOOL Write(LPCTSTR pszKey, CDWordArray& array);
  virtual BOOL Write(LPCTSTR pszKey, CStringArray* pArray) { return pArray != NULL && Write(pszKey, *pArray); }
  virtual BOOL Write(LPCTSTR pszKey, CPoint& point);
  virtual BOOL Write(LPCTSTR pszKey, CArray<CRect, CRect>& array);
  virtual BOOL Write(LPCTSTR pszKey, CArray<CPoint, CPoint>& array);
  virtual BOOL Write(LPCTSTR pszKey, CList<DWORD, DWORD>& list);
  virtual BOOL Write(LPCTSTR pszKey, CMap<UINT, UINT, UINT, UINT>& map);
#ifndef _WINPORT  // (DWORD is UINT there)
  virtual BOOL Write(LPCTSTR pszKey, CMap<UINT, UINT, DWORD, DWORD>& map);
#endif

  virtual BOOL Read(LPCTSTR pszKey, int& iVal);
  virtual BOOL Read(LPCTSTR pszKey, DWORD& dwVal);
  virtual BOOL Read(LPCTSTR pszKey, CString& sVal);
  virtual BOOL Read(LPCTSTR pszKey, CRect& rect);
  virtual BOOL Read(LPCTSTR pszKey, BYTE** ppData, UINT* pBytes);
  virtual BOOL Read(LPCTSTR pszKey, CObject& obj);
  virtual BOOL Read(LPCTSTR pszKey, CObject*& pObj);
  virtual BOOL Read(LPCTSTR pszKey, CStringList& list);
  virtual BOOL Read(LPCTSTR pszKey, CObList& list);
  virtual BOOL Read(LPCTSTR pszKey, CStringArray& array);
  virtual BOOL Read(LPCTSTR pszKey, CByteArray& array);
  virtual BOOL Read(LPCTSTR pszKey, CWordArray& array);
  virtual BOOL Read(LPCTSTR pszKey, CDWordArray& array);
  virtual BOOL Read(LPCTSTR pszKey, CPoint& point);
  virtual BOOL Read(LPCTSTR pszKey, CArray<CRect, CRect>& array);
  virtual BOOL Read(LPCTSTR pszKey, CArray<CPoint, CPoint>& array);
  virtual BOOL Read(LPCTSTR pszKey, CList<DWORD, DWORD>& list);
  virtual BOOL Read(LPCTSTR pszKey, CMap<UINT, UINT, UINT, UINT>& map);
#ifndef _WINPORT  // (DWORD is UINT there)
  virtual BOOL Read(LPCTSTR pszKey, CMap<UINT, UINT, DWORD, DWORD>& map);
#endif

protected:
  friend class CSettingsStoreSP;
  CSettingsStore();

  HKEY m_hKey;
  HKEY m_hRoot;
  CString m_strPath;
  BOOL m_bReadOnly;
  BOOL m_bAdmin;

  // A value serialized by fn into a CArchive, as REG_BINARY.
  template <class F>
  BOOL _WriteArchived(LPCTSTR pszKey, F fn);
  template <class F>
  BOOL _ReadArchived(LPCTSTR pszKey, F fn);

private:
  CSettingsStore(const CSettingsStore&);
  CSettingsStore& operator=(const CSettingsStore&);
};

// The settings store an application chooses: SetRuntimeClass names a
// class derived from CSettingsStore, and Create makes one of it.
class CSettingsStoreSP {
public:
  explicit CSettingsStoreSP(DWORD dwUserData = 0) : m_pRegistry(NULL), m_dwUserData(dwUserData) {}
  ~CSettingsStoreSP() { delete m_pRegistry; }

  static BOOL AFXAPI SetRuntimeClass(CRuntimeClass* pRTI);
  CSettingsStore& Create(BOOL bAdmin, BOOL bReadOnly);

protected:
  CSettingsStore* m_pRegistry;
  DWORD m_dwUserData;
};

#endif  // __AFXSETTINGSSTORE_H__
