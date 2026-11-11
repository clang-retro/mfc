// afxdb.h - ODBC database classes: CDatabase, CRecordset and its record
// field exchange (RFX), CRecordView, and the DDX_Field functions.
//
// Recordsets keep the rows they have fetched, so every type scrolls even
// over forward-only drivers, and updates are made with UPDATE, INSERT and
// DELETE statements rather than positioned cursors (see the README).
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#ifndef __AFXDB_H__
#define __AFXDB_H__

#include "afxext.h"
#include <sql.h>
#include <sqlext.h>

#if !defined(_AFX_NOFORCE_LIBS) && defined(_WIN32)
__asm__(".section .drectve,\"yn\"\n"
        ".ascii \" -defaultlib:odbc32\"\n"
        ".text");
#endif

class CDatabase;
class CRecordset;
class CFieldExchange;
class CRecordView;
class CDBException;
class CLongBinary;
class CDBVariant;
class COleDateTime;
class COleCurrency;

// ---------------------------------------------------------------------
// Error codes in CDBException::m_nRetCode, beyond ODBC's own.

#define AFX_SQL_ERROR 1000
#define AFX_SQL_ERROR_API_CONFORMANCE (AFX_SQL_ERROR + 1)
#define AFX_SQL_ERROR_CONNECT_FAIL (AFX_SQL_ERROR + 2)
#define AFX_SQL_ERROR_DATA_TRUNCATED (AFX_SQL_ERROR + 3)
#define AFX_SQL_ERROR_DYNASET_NOT_SUPPORTED (AFX_SQL_ERROR + 4)
#define AFX_SQL_ERROR_EMPTY_COLUMN_LIST (AFX_SQL_ERROR + 5)
#define AFX_SQL_ERROR_FIELD_SCHEMA_MISMATCH (AFX_SQL_ERROR + 6)
#define AFX_SQL_ERROR_ILLEGAL_MODE (AFX_SQL_ERROR + 7)
#define AFX_SQL_ERROR_MULTIPLE_ROWS_AFFECTED (AFX_SQL_ERROR + 8)
#define AFX_SQL_ERROR_NO_CURRENT_RECORD (AFX_SQL_ERROR + 9)
#define AFX_SQL_ERROR_NO_ROWS_AFFECTED (AFX_SQL_ERROR + 10)
#define AFX_SQL_ERROR_RECORDSET_READONLY (AFX_SQL_ERROR + 11)
#define AFX_SQL_ERROR_SQL_CONFORMANCE (AFX_SQL_ERROR + 12)
#define AFX_SQL_ERROR_SQL_NO_TOTAL (AFX_SQL_ERROR + 13)
#define AFX_SQL_ERROR_RECORDSET_FORWARD_ONLY (AFX_SQL_ERROR + 14)
#define AFX_SQL_ERROR_SNAPSHOT_NOT_SUPPORTED (AFX_SQL_ERROR + 15)
#define AFX_SQL_ERROR_TX_NOT_SUPPORTED (AFX_SQL_ERROR + 16)
#define AFX_SQL_ERROR_ROW_FETCH (AFX_SQL_ERROR + 17)
#define AFX_SQL_ERROR_INCORRECT_ODBC (AFX_SQL_ERROR + 18)
#define AFX_SQL_ERROR_UPDATE_DELETE_FAILED (AFX_SQL_ERROR + 19)
#define AFX_SQL_ERROR_DYNAMIC_CURSOR_NOT_SUPPORTED (AFX_SQL_ERROR + 20)
#define AFX_SQL_ERROR_FIELD_NOT_FOUND (AFX_SQL_ERROR + 21)
#define AFX_SQL_ERROR_BOOKMARKS_NOT_SUPPORTED (AFX_SQL_ERROR + 22)
#define AFX_SQL_ERROR_BOOKMARKS_NOT_ENABLED (AFX_SQL_ERROR + 23)
#define AFX_SQL_ERROR_NO_DATA_FOUND (AFX_SQL_ERROR + 24)
#define AFX_SQL_ERROR_MAX (AFX_SQL_ERROR + 25)

// Values MFC's RFX uses to mark null fields of each type. This library
// keeps a null flag per field instead, and sets new fields to these.
#define AFX_RFX_SHORT_PSEUDO_NULL (0x7EE4)
#define AFX_RFX_INT_PSEUDO_NULL (0x7EE4)
#define AFX_RFX_LONG_PSEUDO_NULL (0x4a4d4120L)
#define AFX_RFX_BYTE_PSEUDO_NULL 255
#define AFX_RFX_SINGLE_PSEUDO_NULL (-9.123e19f)
#define AFX_RFX_DOUBLE_PSEUDO_NULL (-9.123e19)
#define AFX_RFX_BOOL_PSEUDO_NULL 2
#define AFX_RFX_TIMESTAMP_PSEUDO_NULL 99
#define AFX_RFX_BIGINT_PSEUDO_NULL (0x4a4d4120L)

#define AFX_DB_USE_DEFAULT_TYPE (0xFFFFFFFF)
#define DEFAULT_FIELD_TYPE SQL_TYPE_NULL
#define AFX_RFX_TEXT_MAX 255  // nMaxLength default

// ---------------------------------------------------------------------
// CDBException

class CDBException : public CException {
  DECLARE_DYNAMIC(CDBException)
public:
  CDBException(RETCODE nRetCode = SQL_SUCCESS);
  virtual ~CDBException();

  RETCODE m_nRetCode;
  CString m_strError;
  CString m_strStateNativeOrigin;

  virtual BOOL GetErrorMessage(LPTSTR lpszError, UINT nMaxError,
                               PUINT pnHelpContext = NULL) const;
  virtual BOOL GetErrorMessage(LPTSTR lpszError, UINT nMaxError, PUINT pnHelpContext = NULL);

  // Fills in the messages from ODBC's diagnostics for a statement or
  // connection, or from the library's own error code.
  void BuildErrorString(CDatabase* pdb, HSTMT hstmt, BOOL bTrace = TRUE);
};

void AFXAPI AfxThrowDBException(RETCODE nRetCode, CDatabase* pdb, HSTMT hstmt);

// ---------------------------------------------------------------------
// CLongBinary and CDBVariant

#include "afxdb_.h"  // CLongBinary, shared with afxdao.h

#define DBVT_NULL 0
#define DBVT_BOOL 1
#define DBVT_UCHAR 2
#define DBVT_SHORT 3
#define DBVT_LONG 4
#define DBVT_SINGLE 5
#define DBVT_DOUBLE 6
#define DBVT_DATE 7
#define DBVT_STRING 8
#define DBVT_BINARY 9
#define DBVT_ASTRING 10
#define DBVT_WSTRING 11

class CDBVariant {
public:
  CDBVariant();
  virtual ~CDBVariant();
  void Clear();

  DWORD m_dwType;
  union {
    BOOL m_boolVal;
    unsigned char m_chVal;
    short m_iVal;
    long m_lVal;
    float m_fltVal;
    double m_dblVal;
    TIMESTAMP_STRUCT* m_pdate;
    CString* m_pstring;
    CLongBinary* m_pbinary;
    CStringA* m_pstringA;
    CStringW* m_pstringW;
  };

private:
  CDBVariant(const CDBVariant&);
  void operator=(const CDBVariant&);
};

// ---------------------------------------------------------------------
// CDatabase

class CDatabase : public CObject {
  DECLARE_DYNAMIC(CDatabase)
public:
  CDatabase();
  virtual ~CDatabase();

  enum DbOpenOptions {
    openExclusive = 0x0001,
    openReadOnly = 0x0002,
    useCursorLib = 0x0004,
    noOdbcDialog = 0x0008,
    forceOdbcDialog = 0x0010,
  };

  virtual BOOL Open(LPCTSTR lpszDSN, BOOL bExclusive = FALSE, BOOL bReadOnly = FALSE,
                    LPCTSTR lpszConnect = _T("ODBC;"), BOOL bUseCursorLib = TRUE);
  virtual BOOL OpenEx(LPCTSTR lpszConnectString, DWORD dwOptions = 0);
  virtual void Close();

  HDBC m_hdbc;

  BOOL IsOpen() const { return m_hdbc != SQL_NULL_HDBC; }
  BOOL CanUpdate() const { return m_bUpdatable; }
  BOOL CanTransact() const { return m_bTransactions; }
  CString GetDatabaseName() const;
  const CString& GetConnect() const { return m_strConnect; }
  DWORD GetBookmarkPersistence() const;
  int GetCursorCommitBehavior() const { return m_nCursorCommitBehavior; }
  int GetCursorRollbackBehavior() const { return m_nCursorRollbackBehavior; }

  void SetLoginTimeout(DWORD dwSeconds) { m_dwLoginTimeout = dwSeconds; }
  void SetQueryTimeout(DWORD dwSeconds) { m_dwQueryTimeout = dwSeconds; }

  BOOL BeginTrans();
  BOOL CommitTrans();
  BOOL Rollback();
  void ExecuteSQL(LPCTSTR lpszSQL);
  void Cancel();

  virtual void OnSetOptions(HSTMT hstmt);
  virtual void BindParameters(HSTMT hstmt);

  // The environment handle all CDatabase objects share.
  static HENV AFXAPI GetHENV();

  // Whether a call's result is success; otherwise FALSE.
  BOOL Check(RETCODE nRetCode) const;

  // Replaces [name] brackets with the driver's identifier quote.
  void ReplaceBrackets(CString& strSQL) const;

protected:
  CString m_strConnect;
  DWORD m_dwLoginTimeout;
  DWORD m_dwQueryTimeout;
  BOOL m_bUpdatable;
  BOOL m_bTransactions;
  BOOL m_bTransactionPending;
  int m_nCursorCommitBehavior;
  int m_nCursorRollbackBehavior;
  TCHAR m_chIDQuoteChar;
  CPtrList m_listRecordsets;

  void Free();
  void GetConnectInfo();

  friend class CRecordset;
};

// ---------------------------------------------------------------------
// CFieldExchange: carries one operation through DoFieldExchange.

class CFieldExchange {
public:
  enum RFX_Operation {
    BindParam,
    RebindParam,
    BindFieldToColumn,
    BindFieldForUpdate,
    UnbindFieldForUpdate,
    Fixup,
    MarkForAddNew,
    MarkForUpdate,
    Name,
    NameValue,
    Value,
    SetFieldNull,
    StoreField,
    LoadField,
    AllocCache,
    AllocMultiRowBuffer,
    DeleteMultiRowBuffer,
  };

  enum FieldType {
    noFieldType = -1,
    outputColumn = 0,
    param = SQL_PARAM_INPUT,
    inputParam = param,
    outputParam = SQL_PARAM_OUTPUT,
    inoutParam = SQL_PARAM_INPUT_OUTPUT,
  };

  CFieldExchange(UINT nOperation, CRecordset* prs, void* pvField = NULL);

  UINT m_nOperation;
  CRecordset* m_prs;
  UINT m_nFieldType;
  UINT m_nFields;  // output columns seen in this pass
  UINT m_nParams;  // parameters seen in this pass

  void SetFieldType(UINT nFieldType) { m_nFieldType = nFieldType; }
  BOOL IsFieldType(UINT* pnField);

  // Used by the RFX functions.
  void* m_pvField;
  CString* m_pstr;
};

// The record field exchange functions, for DoFieldExchange.
void AFXAPI RFX_Text(CFieldExchange* pFX, LPCTSTR szName, CString& value,
                     int nMaxLength = AFX_RFX_TEXT_MAX, int nColumnType = SQL_VARCHAR,
                     short nScale = 0);
void AFXAPI RFX_Text(CFieldExchange* pFX, LPCTSTR szName, LPTSTR value, int nMaxLength,
                     int nColumnType = SQL_VARCHAR, short nScale = 0);
void AFXAPI RFX_Bool(CFieldExchange* pFX, LPCTSTR szName, BOOL& value);
void AFXAPI RFX_Byte(CFieldExchange* pFX, LPCTSTR szName, BYTE& value);
void AFXAPI RFX_Int(CFieldExchange* pFX, LPCTSTR szName, int& value);
void AFXAPI RFX_Long(CFieldExchange* pFX, LPCTSTR szName, long& value);
void AFXAPI RFX_BigInt(CFieldExchange* pFX, LPCTSTR szName, LONGLONG& value);
void AFXAPI RFX_Single(CFieldExchange* pFX, LPCTSTR szName, float& value);
void AFXAPI RFX_Double(CFieldExchange* pFX, LPCTSTR szName, double& value);
void AFXAPI RFX_Date(CFieldExchange* pFX, LPCTSTR szName, CTime& value);
void AFXAPI RFX_Date(CFieldExchange* pFX, LPCTSTR szName, TIMESTAMP_STRUCT& value);
void AFXAPI RFX_Date(CFieldExchange* pFX, LPCTSTR szName, COleDateTime& value);
void AFXAPI RFX_Binary(CFieldExchange* pFX, LPCTSTR szName, CByteArray& value,
                       int nMaxLength = 255);
void AFXAPI RFX_LongBinary(CFieldExchange* pFX, LPCTSTR szName, CLongBinary& value);

// ---------------------------------------------------------------------
// CRecordset

struct CRecordsetStatus {
  long m_lCurrentRecord;  // -1 when unknown
  BOOL m_bRecordCountFinal;
};

struct CODBCFieldInfo {
  CString m_strName;
  SWORD m_nSQLType;
  UDWORD m_nPrecision;
  SWORD m_nScale;
  SWORD m_nNullability;
};

struct _AFX_RFX_FIELD;  // the library's record of one field
struct _AFX_DB_ROW;     // one fetched row

class CRecordset : public CObject {
  DECLARE_DYNAMIC(CRecordset)
public:
  CRecordset(CDatabase* pDatabase = NULL);
  virtual ~CRecordset();

  enum OpenType { dynaset, snapshot, forwardOnly, dynamic };
  enum OpenOptions {
    none = 0x0,
    readOnly = 0x0004,
    appendOnly = 0x0008,
    skipDeletedRecords = 0x0010,
    noDirtyFieldCheck = 0x0020,
    useBookmarks = 0x0100,
    useMultiRowFetch = 0x0200,
    userAllocMultiRowBuffers = 0x0400,
    useExtendedFetch = 0x0800,
    executeDirect = 0x2000,
    optimizeBulkAdd = 0x4000,
    firstBulkAdd = 0x8000,
  };
  enum EditMode { noMode, edit, addnew };
  enum LockMode { optimistic, pessimistic };

  virtual BOOL Open(UINT nOpenType = AFX_DB_USE_DEFAULT_TYPE, LPCTSTR lpszSQL = NULL,
                    DWORD dwOptions = none);
  virtual void Close();

  HSTMT m_hstmt;
  CDatabase* m_pDatabase;
  CString m_strFilter;
  CString m_strSort;
  UINT m_nFields;
  UINT m_nParams;

  BOOL IsOpen() const { return m_hstmt != SQL_NULL_HSTMT; }
  BOOL IsBOF() const { return m_bBOF; }
  BOOL IsEOF() const { return m_bEOF; }
  BOOL IsDeleted() const;
  BOOL CanAppend() const;
  BOOL CanBookmark() const { return (m_dwOptions & useBookmarks) != 0; }
  BOOL CanRestart() const { return TRUE; }
  BOOL CanScroll() const { return m_nOpenType != forwardOnly; }
  BOOL CanTransact() const { return m_pDatabase != NULL && m_pDatabase->CanTransact(); }
  BOOL CanUpdate() const;

  long GetRecordCount() const { return m_lRecordCount; }
  void GetStatus(CRecordsetStatus& rStatus) const;
  CString GetTableName() const { return m_strTableName; }
  const CString& GetSQL() const { return m_strSQL; }
  short GetODBCFieldCount() const { return (short)m_nResultCols; }
  void GetODBCFieldInfo(short nIndex, CODBCFieldInfo& fieldinfo);
  void GetODBCFieldInfo(LPCTSTR lpszName, CODBCFieldInfo& fieldinfo);
  void GetFieldValue(LPCTSTR lpszName, CDBVariant& varValue,
                     short nFieldType = DEFAULT_FIELD_TYPE);
  void GetFieldValue(short nIndex, CDBVariant& varValue, short nFieldType = DEFAULT_FIELD_TYPE);
  void GetFieldValue(LPCTSTR lpszName, CString& strValue);
  void GetFieldValue(short nIndex, CString& strValue);
  short GetFieldIndexByName(LPCTSTR lpszFieldName);
  DWORD GetRowsetSize() const { return 1; }
  DWORD GetRowsFetched() const { return 1; }

  virtual void Move(long nRows, WORD wFetchType = SQL_FETCH_RELATIVE);
  void MoveNext() { Move(1); }
  void MovePrev() { Move(-1); }
  void MoveFirst() { Move(1, SQL_FETCH_FIRST); }
  void MoveLast() { Move(-1, SQL_FETCH_LAST); }
  void SetAbsolutePosition(long nRows);
  void GetBookmark(CDBVariant& varBookmark);
  void SetBookmark(const CDBVariant& varBookmark);

  virtual void AddNew();
  virtual void Edit();
  virtual BOOL Update();
  virtual void Delete();
  void CancelUpdate();
  virtual BOOL Requery();
  BOOL FlushResultSet();
  void Cancel();

  BOOL IsFieldDirty(void* pv);
  BOOL IsFieldNull(void* pv);
  BOOL IsFieldNullable(void* pv);
  void SetFieldDirty(void* pv, BOOL bDirty = TRUE);
  void SetFieldNull(void* pv, BOOL bNull = TRUE);
  void SetParamNull(int nIndex, BOOL bNull = TRUE);
  void SetLockingMode(UINT nMode) { m_nLockMode = nMode; }

  virtual CString GetDefaultConnect();
  virtual CString GetDefaultSQL();
  virtual void DoFieldExchange(CFieldExchange* pFX);
  virtual void OnSetOptions(HSTMT hstmt);
  virtual void OnSetUpdateOptions(HSTMT hstmt);

  BOOL Check(RETCODE nRetCode) const;
  void ThrowDBException(RETCODE nRetCode, HSTMT hstmt = SQL_NULL_HSTMT);

  UINT m_nEditMode;  // an EditMode value

  // Used by the RFX functions.
  void _AddField(const _AFX_RFX_FIELD& field, BOOL bParam);

protected:
  UINT m_nDefaultType;
  UINT m_nOpenType;
  DWORD m_dwOptions;
  UINT m_nLockMode;
  CString m_strSQL;
  CString m_strRequerySQL;  // the SQL as given, before the filter and sort
  CString m_strTableName;
  BOOL m_bBOF;
  BOOL m_bEOF;
  long m_lRecordCount;
  UINT m_nResultCols;
  BOOL m_bDatabaseOwned;  // m_pDatabase was made by Open

  // Fields and parameters, as DoFieldExchange lists them.
  CPtrArray m_rgFields;  // _AFX_RFX_FIELD*
  CPtrArray m_rgParams;  // _AFX_RFX_FIELD*
  CByteArray m_rgParamNull;
  CPtrArray m_rgParamBuffers;  // CByteArray*, alive until the next execute

  // The rows fetched so far; for forward-only recordsets, just the
  // current one.
  CPtrArray m_rgRows;  // _AFX_DB_ROW*, from record m_nRowBase on
  long m_nRowBase;     // records a forward-only recordset has let go of
  long m_nCurRow;      // the current record, or -1 before the first
  CDWordArray m_rgColCType;  // the C type each result column is kept as
  BOOL m_bAllFetched;
  _AFX_DB_ROW* m_pEditBackup;

  void BuildSQL(LPCTSTR lpszSQL);
  void ExecuteQuery();
  void PrepareFields();
  BOOL FetchRow();
  void LoadRow(long nRow);
  void ClearRows();
  void FreeFields();
  void FreeParamBuffers();
  void BindParams(HSTMT hstmt);
  // The output parameters' values, into their members: once the
  // statement has no more result sets.
  void GetOutputParams();
  void SetCurrentToNull();
  _AFX_DB_ROW* SaveMembers() const;
  void RestoreMembers(const _AFX_DB_ROW* pRow);
  int FindField(void* pv) const;
  void AppendWhereCurrent(CString& strSQL, CPtrArray& rgBind, CByteArray& rgBindRow) const;
  void ExecuteUpdateSQL(const CString& strSQL, const CPtrArray& rgBind,
                        const CByteArray& rgBindRow, const _AFX_DB_ROW* pRow);
  void CheckCurrentRecord() const;
  void ThrowIfReadOnly() const;
  BOOL MarkDirtyFields(const _AFX_DB_ROW* pOriginal);

  friend class CDatabase;
};

// ---------------------------------------------------------------------
// CRecordView: a form view that shows the current record of a recordset.

class CRecordView : public CFormView {
  DECLARE_DYNAMIC(CRecordView)
protected:
  CRecordView(LPCTSTR lpszTemplateName);
  CRecordView(UINT nIDTemplate);

public:
  virtual ~CRecordView();
  virtual CRecordset* OnGetRecordset() = 0;

  BOOL IsOnLastRecord();
  BOOL IsOnFirstRecord();

  virtual BOOL OnMove(UINT nIDMoveCommand);
  virtual void OnInitialUpdate();

  afx_msg void OnUpdateRecordFirst(CCmdUI* pCmdUI);
  afx_msg void OnUpdateRecordPrev(CCmdUI* pCmdUI);
  afx_msg void OnUpdateRecordNext(CCmdUI* pCmdUI);
  afx_msg void OnUpdateRecordLast(CCmdUI* pCmdUI);
  afx_msg BOOL OnMoveCommand(UINT nID) { return OnMove(nID); }

protected:
  BOOL m_bOnFirstRecord;
  BOOL m_bOnLastRecord;
  DECLARE_MESSAGE_MAP()
};

// ---------------------------------------------------------------------
// Dialog data exchange with recordset fields: like DDX_Text and the
// others, but a null field shows as empty and an empty entry stores null.

void AFXAPI DDX_FieldText(CDataExchange* pDX, int nIDC, BYTE& value, CRecordset* pRecordset);
void AFXAPI DDX_FieldText(CDataExchange* pDX, int nIDC, int& value, CRecordset* pRecordset);
void AFXAPI DDX_FieldText(CDataExchange* pDX, int nIDC, UINT& value, CRecordset* pRecordset);
void AFXAPI DDX_FieldText(CDataExchange* pDX, int nIDC, long& value, CRecordset* pRecordset);
#ifndef _WINPORT
void AFXAPI DDX_FieldText(CDataExchange* pDX, int nIDC, DWORD& value, CRecordset* pRecordset);
#else
// DWORD is UINT here, so the extra overload takes unsigned long.
void AFXAPI DDX_FieldText(CDataExchange* pDX, int nIDC, unsigned long& value, CRecordset* pRecordset);
#endif
void AFXAPI DDX_FieldText(CDataExchange* pDX, int nIDC, CString& value, CRecordset* pRecordset);
void AFXAPI DDX_FieldText(CDataExchange* pDX, int nIDC, LPTSTR pstrValue, int nMaxLen,
                          CRecordset* pRecordset);
void AFXAPI DDX_FieldText(CDataExchange* pDX, int nIDC, float& value, CRecordset* pRecordset);
void AFXAPI DDX_FieldText(CDataExchange* pDX, int nIDC, double& value, CRecordset* pRecordset);
void AFXAPI DDX_FieldText(CDataExchange* pDX, int nIDC, short& value, CRecordset* pRecordset);
void AFXAPI DDX_FieldText(CDataExchange* pDX, int nIDC, COleDateTime& value,
                          CRecordset* pRecordset);
void AFXAPI DDX_FieldText(CDataExchange* pDX, int nIDC, COleCurrency& value,
                          CRecordset* pRecordset);
void AFXAPI DDX_FieldCheck(CDataExchange* pDX, int nIDC, int& value, CRecordset* pRecordset);
void AFXAPI DDX_FieldRadio(CDataExchange* pDX, int nIDC, int& value, CRecordset* pRecordset);
void AFXAPI DDX_FieldLBString(CDataExchange* pDX, int nIDC, CString& value,
                              CRecordset* pRecordset);
void AFXAPI DDX_FieldCBString(CDataExchange* pDX, int nIDC, CString& value,
                              CRecordset* pRecordset);
void AFXAPI DDX_FieldLBStringExact(CDataExchange* pDX, int nIDC, CString& value,
                                   CRecordset* pRecordset);
void AFXAPI DDX_FieldCBStringExact(CDataExchange* pDX, int nIDC, CString& value,
                                   CRecordset* pRecordset);
void AFXAPI DDX_FieldLBIndex(CDataExchange* pDX, int nIDC, int& index, CRecordset* pRecordset);
void AFXAPI DDX_FieldCBIndex(CDataExchange* pDX, int nIDC, int& index, CRecordset* pRecordset);
void AFXAPI DDX_FieldScroll(CDataExchange* pDX, int nIDC, int& value, CRecordset* pRecordset);
void AFXAPI DDX_FieldSlider(CDataExchange* pDX, int nIDC, int& value, CRecordset* pRecordset);

#endif  // __AFXDB_H__
