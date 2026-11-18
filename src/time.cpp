// CTime and CTimeSpan are in atltime.h; COleDateTime formats through the
// same strftime.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#include "stdafx.h"

CString AFXAPI _AfxFormatTm(LPCTSTR pszFormat, const struct tm& t) {
  return _AtlTime::FormatTm(pszFormat, t);
}
