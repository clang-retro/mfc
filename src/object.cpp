// CObject, CRuntimeClass and the diagnostic helpers.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#include "stdafx.h"
#include "afximpl.h"

// ---------------------------------------------------------------------
// CObject

CRuntimeClass CObject::classCObject = {
    "CObject", sizeof(CObject), 0xffff, NULL, NULL, NULL, NULL, NULL};

CRuntimeClass* CObject::GetRuntimeClass() const { return RUNTIME_CLASS(CObject); }
CRuntimeClass* PASCAL CObject::GetThisClass() { return RUNTIME_CLASS(CObject); }

BOOL CObject::IsSerializable() const {
  return GetRuntimeClass()->m_wSchema != 0xffff;
}

BOOL CObject::IsKindOf(const CRuntimeClass* pClass) const {
  return GetRuntimeClass()->IsDerivedFrom(pClass);
}

void CObject::Serialize(CArchive&) {}
void CObject::AssertValid() const {}

void CObject::Dump(CDumpContext& dc) const {
  dc << "a " << GetRuntimeClass()->m_lpszClassName << " at " << (const void*)this << "\n";
}

CObject* AFXAPI AfxDynamicDownCast(CRuntimeClass* pClass, CObject* pObject) {
  if (pObject != NULL && pObject->IsKindOf(pClass)) return pObject;
  return NULL;
}

CObject* AFXAPI AfxStaticDownCast(CRuntimeClass* pClass, CObject* pObject) {
  ASSERT(pObject == NULL || pObject->IsKindOf(pClass));
  (void)pClass;
  return pObject;
}

// ---------------------------------------------------------------------
// CRuntimeClass

namespace {
CRuntimeClass* g_pFirstClass = NULL;
}

void AFXAPI _AfxRemoveClasses(const BYTE* pStart, const BYTE* pEnd) {
  for (CRuntimeClass** pp = &g_pFirstClass; *pp != NULL;) {
    if ((const BYTE*)*pp >= pStart && (const BYTE*)*pp < pEnd)
      *pp = (*pp)->m_pNextClass;
    else
      pp = &(*pp)->m_pNextClass;
  }
}

AFX_CLASSINIT::AFX_CLASSINIT(CRuntimeClass* pNewClass) {
  pNewClass->m_pNextClass = g_pFirstClass;
  g_pFirstClass = pNewClass;
}

CObject* CRuntimeClass::CreateObject() {
  if (m_pfnCreateObject == NULL) return NULL;
  return (*m_pfnCreateObject)();
}

BOOL CRuntimeClass::IsDerivedFrom(const CRuntimeClass* pBaseClass) const {
  const CRuntimeClass* p = this;
  while (p != NULL) {
    if (p == pBaseClass) return TRUE;
    p = p->m_pfnGetBaseClass ? (*p->m_pfnGetBaseClass)() : p->m_pBaseClass;
  }
  return FALSE;
}

CRuntimeClass* PASCAL CRuntimeClass::FromName(LPCSTR lpszClassName) {
  if (!lpszClassName) return NULL;
  for (CRuntimeClass* p = g_pFirstClass; p; p = p->m_pNextClass)
    if (lstrcmpA(lpszClassName, p->m_lpszClassName) == 0) return p;
  // Classes without IMPLEMENT_SERIAL are not in the list; CObject is
  // always known.
  if (lstrcmpA(lpszClassName, "CObject") == 0) return RUNTIME_CLASS(CObject);
  return NULL;
}

CRuntimeClass* PASCAL CRuntimeClass::FromName(LPCWSTR lpszClassName) {
  CStringA s(lpszClassName);
  return FromName((LPCSTR)s);
}

CObject* PASCAL CRuntimeClass::CreateObject(LPCSTR lpszClassName) {
  CRuntimeClass* p = FromName(lpszClassName);
  return p ? p->CreateObject() : NULL;
}

CObject* PASCAL CRuntimeClass::CreateObject(LPCWSTR lpszClassName) {
  CRuntimeClass* p = FromName(lpszClassName);
  return p ? p->CreateObject() : NULL;
}

void AFXAPI AfxDoForAllClasses(void(AFX_CDECL* pfn)(const CRuntimeClass*, void*),
                               void* pContext) {
  for (CRuntimeClass* p = g_pFirstClass; p; p = p->m_pNextClass) (*pfn)(p, pContext);
}

// ---------------------------------------------------------------------
// Diagnostics

BOOL afxTraceEnabled = TRUE;

void AFX_CDECL AfxTrace(LPCTSTR lpszFormat, ...) {
  if (!afxTraceEnabled) return;
  CString s;
  va_list a;
  va_start(a, lpszFormat);
  s.FormatV(lpszFormat, a);
  va_end(a);
  OutputDebugString(s);
}

BOOL AFXAPI AfxAssertFailedLine(LPCSTR lpszFileName, int nLine) {
  char buf[512];
  wsprintfA(buf, "Assertion failed: %s, line %d\n", lpszFileName, nLine);
  OutputDebugStringA(buf);
  int r = MessageBoxA(NULL, buf, "MFC assertion", MB_ABORTRETRYIGNORE | MB_ICONSTOP | MB_TASKMODAL);
  if (r == IDABORT) ExitProcess(3);
  return r == IDRETRY;
}

void AFXAPI AfxAssertValidObject(const CObject* pOb, LPCSTR lpszFileName, int nLine) {
  if (pOb == NULL) {
    if (AfxAssertFailedLine(lpszFileName, nLine)) AfxDebugBreak();
    return;
  }
  pOb->AssertValid();
}

BOOL AFXAPI AfxIsValidAddress(const void* lp, UINT_PTR nBytes, BOOL) {
  return lp != NULL || nBytes == 0;
}

BOOL AFXAPI AfxIsValidString(LPCWSTR lpsz, int) { return lpsz != NULL; }
BOOL AFXAPI AfxIsValidString(LPCSTR lpsz, int) { return lpsz != NULL; }

void AFXAPI AfxDebugBreak() { DebugBreak(); }

// ---------------------------------------------------------------------
// CDumpContext

CDumpContext afxDump;

CDumpContext::CDumpContext(CFile* pFile) : m_pFile(pFile), m_nDepth(0) {}

void CDumpContext::OutputString(LPCTSTR lpsz) {
  if (m_pFile) {
    CStringA s(lpsz);
    m_pFile->Write((LPCSTR)s, s.GetLength());
  } else {
    OutputDebugString(lpsz);
  }
}

CDumpContext& CDumpContext::operator<<(LPCSTR lpsz) {
  OutputString(CString(lpsz ? lpsz : "(NULL)"));
  return *this;
}
CDumpContext& CDumpContext::operator<<(LPCWSTR lpsz) {
  OutputString(CString(lpsz ? lpsz : L"(NULL)"));
  return *this;
}
#define DC_FMT(T, fmt) \
  CDumpContext& CDumpContext::operator<<(T v) { \
    CString s; s.Format(_T(fmt), v); OutputString(s); return *this; }
DC_FMT(const void*, "$%p")
DC_FMT(BYTE, "%u")
DC_FMT(WORD, "%u")
DC_FMT(UINT, "%u")
DC_FMT(LONG, "%ld")
DC_FMT(DWORD, "%lu")
DC_FMT(int, "%d")
DC_FMT(LONGLONG, "%I64d")
DC_FMT(ULONGLONG, "%I64u")
#undef DC_FMT
CDumpContext& CDumpContext::operator<<(float f) { return *this << (double)f; }
CDumpContext& CDumpContext::operator<<(double d) {
  CString s;
  s.Format(_T("%f"), d);
  OutputString(s);
  return *this;
}
CDumpContext& CDumpContext::operator<<(const CObject* pOb) {
  if (pOb == NULL) return *this << "NULL";
  pOb->Dump(*this);
  return *this;
}
CDumpContext& CDumpContext::operator<<(const CObject& ob) { return *this << &ob; }

void CDumpContext::HexDump(LPCTSTR lpszLine, BYTE* pby, int nBytes, int nWidth) {
  for (int i = 0; i < nBytes; i += nWidth) {
    CString s(lpszLine);
    for (int j = i; j < i + nWidth && j < nBytes; j++) s.AppendFormat(_T(" %02X"), pby[j]);
    s += _T("\n");
    OutputString(s);
  }
}

void CDumpContext::Flush() {
  if (m_pFile) m_pFile->Flush();
}
