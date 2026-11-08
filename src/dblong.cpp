// dblong.cpp - CLongBinary, which the ODBC and DAO classes share.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#include "afximpl.h"
#include <afxdb_.h>

IMPLEMENT_DYNAMIC(CLongBinary, CObject)

CLongBinary::CLongBinary() : m_hData(NULL), m_dwDataLength(0) {}

CLongBinary::~CLongBinary() {
  if (m_hData != NULL) ::GlobalFree(m_hData);
}
