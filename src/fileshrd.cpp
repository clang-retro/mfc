// fileshrd.cpp - CSharedFile.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#include "afximpl.h"
#include <afxadv.h>

IMPLEMENT_DYNAMIC(CSharedFile, CMemFile)

CSharedFile::CSharedFile(UINT nAllocFlags, UINT nGrowBytes) : CMemFile(nGrowBytes) {
  m_nAllocFlags = nAllocFlags;
  m_hGlobalMemory = NULL;
  m_bAllowGrow = TRUE;
}

CSharedFile::~CSharedFile() {
  // Closed here: the base class's destructor would free with its own Free.
  if (m_lpBuffer != NULL) Close();
}

HGLOBAL CSharedFile::Detach() {
  HGLOBAL hMem = m_hGlobalMemory;
  if (hMem != NULL) ::GlobalUnlock(hMem);
  m_hGlobalMemory = NULL;
  m_lpBuffer = NULL;
  m_nBufferSize = 0;
  m_nFileSize = 0;
  m_nPosition = 0;
  return hMem;
}

void CSharedFile::SetHandle(HGLOBAL hGlobalMemory, BOOL bAllowGrow) {
  if (m_lpBuffer != NULL) Close();
  m_hGlobalMemory = hGlobalMemory;
  m_lpBuffer = (BYTE*)::GlobalLock(hGlobalMemory);
  m_nBufferSize = m_nFileSize = ::GlobalSize(hGlobalMemory);
  m_nPosition = 0;
  m_bAllowGrow = bAllowGrow;
  m_bAutoDelete = FALSE;
}

BYTE* CSharedFile::Alloc(SIZE_T nBytes) {
  m_hGlobalMemory = ::GlobalAlloc(m_nAllocFlags, nBytes);
  return m_hGlobalMemory != NULL ? (BYTE*)::GlobalLock(m_hGlobalMemory) : NULL;
}

BYTE* CSharedFile::Realloc(BYTE* lpMem, SIZE_T nBytes) {
  if (lpMem == NULL) return Alloc(nBytes);
  ::GlobalUnlock(m_hGlobalMemory);
  HGLOBAL hNew = ::GlobalReAlloc(m_hGlobalMemory, nBytes, m_nAllocFlags);
  if (hNew == NULL) {
    ::GlobalLock(m_hGlobalMemory);
    return NULL;
  }
  m_hGlobalMemory = hNew;
  return (BYTE*)::GlobalLock(hNew);
}

void CSharedFile::Free(BYTE*) {
  if (m_hGlobalMemory != NULL) {
    ::GlobalUnlock(m_hGlobalMemory);
    ::GlobalFree(m_hGlobalMemory);
  }
  m_hGlobalMemory = NULL;
}

void CSharedFile::GrowFile(SIZE_T dwNewLen) {
  if (!m_bAllowGrow && dwNewLen > m_nBufferSize) AfxThrowMemoryException();
  CMemFile::GrowFile(dwNewLen);
}
