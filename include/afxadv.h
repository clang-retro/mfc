// afxadv.h - advanced classes: CSharedFile (a memory file in global
// memory, for the clipboard and OLE), and CRecentFileList and CDockState
// (declared in afxwin.h and afxext.h).
//
// Part of clang-retro MFC, an independent implementation of the MFC API
// SPDX-License-Identifier: AGPL-3.0-only
// written from its public documentation.
#ifndef __AFXADV_H__
#define __AFXADV_H__

#ifndef __AFXWIN_H__
#include <afxwin.h>
#endif

class CSharedFile : public CMemFile {
  DECLARE_DYNAMIC(CSharedFile)
public:
  CSharedFile(UINT nAllocFlags = GMEM_DDESHARE | GMEM_MOVEABLE, UINT nGrowBytes = 4096);
  virtual ~CSharedFile();

  // Hands over the memory (unlocked); the file is then empty.
  HGLOBAL Detach();
  // Works on existing global memory, which stays the caller's.
  void SetHandle(HGLOBAL hGlobalMemory, BOOL bAllowGrow = TRUE);

protected:
  virtual BYTE* Alloc(SIZE_T nBytes);
  virtual BYTE* Realloc(BYTE* lpMem, SIZE_T nBytes);
  virtual void Free(BYTE* lpMem);
  virtual void GrowFile(SIZE_T dwNewLen);

  UINT m_nAllocFlags;
  HGLOBAL m_hGlobalMemory;
  BOOL m_bAllowGrow;
};

#endif  // __AFXADV_H__
