// dbimpl.h - what the ODBC classes keep about fields and fetched rows.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "afximpl.h"
#include <afxdb.h>

// How a field's member variable holds its value.
enum {
  _AFX_RFX_STRING,      // CString
  _AFX_RFX_TEXTBUF,     // TCHAR array of nSize characters
  _AFX_RFX_BINARY,      // CByteArray
  _AFX_RFX_LONGBINARY,  // CLongBinary
  _AFX_RFX_POD,         // nSize bytes in nCType's C form
  _AFX_RFX_DATE,        // nSize bytes, converted through a TIMESTAMP_STRUCT
};

typedef void (*_AFX_FROM_TIMESTAMP)(void* pv, const TIMESTAMP_STRUCT& ts);
typedef void (*_AFX_TO_TIMESTAMP)(const void* pv, TIMESTAMP_STRUCT& ts);

struct _AFX_RFX_FIELD {
  CString strName;
  void* pv;
  UINT nKind;
  SQLSMALLINT nCType;
  SQLSMALLINT nSqlType;
  SQLULEN nColumnSize;
  SQLSMALLINT nScale;
  int nSize;
  UINT nParamType;  // a CFieldExchange::FieldType, for parameters
  BOOL bNull;
  BOOL bDirty;
  BOOL bNullable;
  BYTE rgPseudoNull[16];  // the member's bytes for a null value
  int nPseudoNull;
  _AFX_FROM_TIMESTAMP pfnFromTimestamp;
  _AFX_TO_TIMESTAMP pfnToTimestamp;
};

struct _AFX_DB_CELL {
  BOOL bNull;
  SQLSMALLINT nCType;  // for result columns no field covers
  CByteArray data;
};

struct _AFX_DB_ROW {
  explicit _AFX_DB_ROW(int nCells) : bDeleted(FALSE), nCells(nCells), pCells(new _AFX_DB_CELL[nCells]) {}
  ~_AFX_DB_ROW() { delete[] pCells; }
  BOOL bDeleted;
  int nCells;
  _AFX_DB_CELL* pCells;
};

// The member's value as bytes, and back.
void AFXAPI _AfxGetFieldBytes(const _AFX_RFX_FIELD& field, CByteArray& bytes);
void AFXAPI _AfxSetFieldBytes(const _AFX_RFX_FIELD& field, const BYTE* p, INT_PTR nBytes);
void AFXAPI _AfxSetFieldPseudoNull(const _AFX_RFX_FIELD& field);
BOOL AFXAPI _AfxGetParamOutput(const _AFX_RFX_FIELD& field, const CByteArray& buffer);

// Reads column nCol of the current row into the field's member.
void AFXAPI _AfxGetFieldData(CRecordset* pSet, HSTMT hstmt, SQLUSMALLINT nCol,
                             _AFX_RFX_FIELD& field);

// Binds a value, given as the field's member bytes, as parameter nParam.
// The data lives in a buffer added to rgBuffers.
void AFXAPI _AfxBindFieldBytes(CRecordset* pSet, HSTMT hstmt, SQLUSMALLINT nParam,
                               const _AFX_RFX_FIELD& field, BOOL bNull, const CByteArray& bytes,
                               CPtrArray& rgBuffers);

// Reads any column as text or binary, for GetFieldValue.
RETCODE AFXAPI _AfxGetLongData(HSTMT hstmt, SQLUSMALLINT nCol, SQLSMALLINT nCType,
                               CByteArray& data, BOOL& bNull);
