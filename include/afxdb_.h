// afxdb_.h - what the ODBC and DAO database classes share: CLongBinary.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#ifndef __AFXDB__H__
#define __AFXDB__H__

#include "afx.h"

// A long binary value in global memory.
class CLongBinary : public CObject {
  DECLARE_DYNAMIC(CLongBinary)
public:
  CLongBinary();
  virtual ~CLongBinary();
  HGLOBAL m_hData;
  DWORD_PTR m_dwDataLength;
};

#endif  // __AFXDB__H__
