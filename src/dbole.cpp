// dbole.cpp - the recordset functions for the afxdisp.h types, kept apart
// so that programs that do not use them do not link the OLE code.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#include "dbimpl.h"
#include <afxdisp.h>
#include "ddxfield.h"

void AFXAPI _AfxRfxDate(CFieldExchange* pFX, LPCTSTR szName, void* pv, int nSize,
                        const void* pPseudoNull, _AFX_FROM_TIMESTAMP pfnFrom,
                        _AFX_TO_TIMESTAMP pfnTo);

static void OleDateFromTimestamp(void* pv, const TIMESTAMP_STRUCT& ts) {
  ((COleDateTime*)pv)->SetDateTime(ts.year, ts.month, ts.day, ts.hour, ts.minute, ts.second);
}

static void OleDateToTimestamp(const void* pv, TIMESTAMP_STRUCT& ts) {
  COleDateTime dt;
  memcpy((void*)&dt, pv, sizeof(dt));
  memset(&ts, 0, sizeof(ts));
  if (dt.GetStatus() != COleDateTime::valid) return;
  ts.year = (SQLSMALLINT)dt.GetYear();
  ts.month = (SQLUSMALLINT)dt.GetMonth();
  ts.day = (SQLUSMALLINT)dt.GetDay();
  ts.hour = (SQLUSMALLINT)dt.GetHour();
  ts.minute = (SQLUSMALLINT)dt.GetMinute();
  ts.second = (SQLUSMALLINT)dt.GetSecond();
}

void AFXAPI RFX_Date(CFieldExchange* pFX, LPCTSTR szName, COleDateTime& value) {
  COleDateTime dtPseudo;
  dtPseudo.SetStatus(COleDateTime::null);
  _AfxRfxDate(pFX, szName, &value, sizeof(COleDateTime), &dtPseudo, OleDateFromTimestamp,
              OleDateToTimestamp);
}

void AFXAPI DDX_FieldText(CDataExchange* pDX, int nIDC, COleDateTime& value, CRecordset* pSet) {
  _AfxDdxFieldText(pDX, nIDC, value, pSet);
}

void AFXAPI DDX_FieldText(CDataExchange* pDX, int nIDC, COleCurrency& value, CRecordset* pSet) {
  _AfxDdxFieldText(pDX, nIDC, value, pSet);
}
