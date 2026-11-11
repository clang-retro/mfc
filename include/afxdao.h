// afxdao.h - the DAO database classes: CDaoWorkspace, CDaoDatabase,
// CDaoTableDef, CDaoQueryDef, CDaoRecordset and its field exchange (DFX),
// CDaoException, and the information structures.
//
// The classes call the DAO engine (DAO 3.6, or 3.5 where that is all
// there is) through its Unicode interfaces. A recordset's bound fields
// are read and written through its Fields collection.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#ifndef __AFXDAO_H__
#define __AFXDAO_H__

#include "afxdisp.h"
#include "afxext.h"
#include "afxdb_.h"
#include <dbdaoint.h>
#include <dbdaoerr.h>

class CDaoException;
class CDaoErrorInfo;
class CDaoWorkspace;
class CDaoDatabase;
class CDaoTableDef;
class CDaoQueryDef;
class CDaoRecordset;
class CDaoFieldExchange;

// ---------------------------------------------------------------------
// The engine

void AFXAPI AfxDaoInit();
void AFXAPI AfxDaoTerm();
DAODBEngine* AFXAPI AfxDaoGetEngine();

// Error codes in CDaoException::m_nAfxDaoError.
#define NO_AFX_DAO_ERROR 0
#define AFX_DAO_ERROR_MIN 2000
#define AFX_DAO_ERROR_ENGINE_INITIALIZATION (AFX_DAO_ERROR_MIN + 0)
#define AFX_DAO_ERROR_DFX_BIND (AFX_DAO_ERROR_MIN + 1)
#define AFX_DAO_ERROR_OBJECT_NOT_OPEN (AFX_DAO_ERROR_MIN + 2)
#define AFX_DAO_ERROR_MAX (AFX_DAO_ERROR_MIN + 2)

// How much the Get...Info functions fill in.
#define AFX_DAO_PRIMARY_INFO 0x00000001
#define AFX_DAO_SECONDARY_INFO 0x00000002
#define AFX_DAO_ALL_INFO 0x00000003

#define AFX_DAO_USE_DEFAULT_TYPE (-1)

// Checks a DAO call, throwing a CDaoException when it fails.
void AFXAPI AfxDaoCheck(SCODE scode, LPCSTR lpszDaoCall, LPCSTR lpszFile, int nLine,
                        int nError = NO_AFX_DAO_ERROR, BOOL bMemOnly = FALSE);
#define DAO_CHECK(f) AfxDaoCheck(f, #f, __FILE__, __LINE__)
#define DAO_CHECK_ERROR(f, err) AfxDaoCheck(f, #f, __FILE__, __LINE__, err)
#define DAO_CHECK_MEM(f) AfxDaoCheck(f, #f, __FILE__, __LINE__, NO_AFX_DAO_ERROR, TRUE)
#define DAO_TRACE(f) f

// ---------------------------------------------------------------------
// Information structures

class CDaoErrorInfo {
public:
  long m_lErrorCode;
  CString m_strSource;
  CString m_strDescription;
  CString m_strHelpFile;
  long m_lHelpContext;
};

class CDaoWorkspaceInfo {
public:
  CString m_strName;           // primary
  CString m_strUserName;       // secondary
  BOOL m_bIsolateODBCTrans;    // all
};

class CDaoDatabaseInfo {
public:
  CString m_strName;           // primary
  BOOL m_bUpdatable;           // primary
  BOOL m_bTransactions;        // primary
  CString m_strVersion;        // secondary
  long m_lCollatingOrder;      // secondary
  short m_nQueryTimeout;       // secondary
  CString m_strConnect;        // all
};

class CDaoTableDefInfo {
public:
  CString m_strName;           // primary
  BOOL m_bUpdatable;           // primary
  long m_lAttributes;          // primary
  COleDateTime m_dateCreated;  // secondary
  COleDateTime m_dateLastUpdated;
  CString m_strSrcTableName;
  CString m_strConnect;
  CString m_strValidationRule;
  CString m_strValidationText;
  long m_lRecordCount;         // all
};

class CDaoFieldInfo {
public:
  CString m_strName;           // primary
  short m_nType;               // primary
  long m_lSize;                // primary
  long m_lAttributes;          // primary
  short m_nOrdinalPosition;    // secondary
  BOOL m_bRequired;
  BOOL m_bAllowZeroLength;
  long m_lCollatingOrder;
  CString m_strForeignName;
  CString m_strSourceField;
  CString m_strSourceTable;
  CString m_strValidationRule; // all
  CString m_strValidationText;
  CString m_strDefaultValue;
};

class CDaoIndexFieldInfo {
public:
  CString m_strName;
  BOOL m_bDescending;
};

class CDaoIndexInfo {
public:
  CDaoIndexInfo();
  ~CDaoIndexInfo();

  CString m_strName;                   // primary
  CDaoIndexFieldInfo* m_pFieldInfos;   // primary
  short m_nFields;                     // primary
  BOOL m_bPrimary;                     // secondary
  BOOL m_bUnique;
  BOOL m_bClustered;
  BOOL m_bIgnoreNulls;
  BOOL m_bRequired;
  BOOL m_bForeign;
  long m_lDistinctCount;               // all

  BOOL m_bCleanupFieldInfo;  // m_pFieldInfos is the structure's own
};

class CDaoRelationFieldInfo {
public:
  CString m_strName;
  CString m_strForeignName;
};

class CDaoRelationInfo {
public:
  CDaoRelationInfo();
  ~CDaoRelationInfo();

  CString m_strName;                       // primary
  CString m_strTable;
  CString m_strForeignTable;
  long m_lAttributes;                      // secondary
  CDaoRelationFieldInfo* m_pFieldInfos;
  short m_nFields;

  BOOL m_bCleanupFieldInfo;
};

class CDaoQueryDefInfo {
public:
  CString m_strName;           // primary
  short m_nType;               // primary
  COleDateTime m_dateCreated;  // secondary
  COleDateTime m_dateLastUpdated;
  BOOL m_bUpdatable;
  BOOL m_bReturnsRecords;
  CString m_strSQL;            // all
  CString m_strConnect;
  short m_nODBCTimeout;
};

class CDaoParameterInfo {
public:
  CString m_strName;
  short m_nType;
  COleVariant m_varValue;
};

// ---------------------------------------------------------------------
// CDaoException

class CDaoException : public CException {
  DECLARE_DYNAMIC(CDaoException)
public:
  CDaoException();
  virtual ~CDaoException();

  CDaoErrorInfo* m_pErrorInfo;  // the error GetErrorInfo last read
  SCODE m_scode;
  int m_nAfxDaoError;

  short GetErrorCount();
  void GetErrorInfo(int nIndex);

  virtual BOOL GetErrorMessage(LPTSTR lpszError, UINT nMaxError, PUINT pnHelpContext = NULL);

  // ----- Implementation
  DAOError* m_pDAOError;
  DAOErrors* m_pDAOErrors;
  void InitErrorsCollection();
  void FillErrorInfo();
};

void AFXAPI AfxThrowDaoException(int nAfxDaoError = NO_AFX_DAO_ERROR, SCODE scode = S_OK);

// ---------------------------------------------------------------------
// CDaoWorkspace

class CDaoWorkspace : public CObject {
  DECLARE_DYNAMIC(CDaoWorkspace)
public:
  CDaoWorkspace();
  virtual ~CDaoWorkspace();

  DAOWorkspace* m_pDAOWorkspace;
  DAOWorkspaces* m_pDAOWorkspaces;
  DAODatabases* m_pDAODatabases;
  int m_nStatus;

  virtual void Create(LPCTSTR lpszName, LPCTSTR lpszUserName, LPCTSTR lpszPassword);
  virtual void Append();
  // NULL opens the default workspace.
  virtual void Open(LPCTSTR lpszName = NULL);
  virtual void Close();

  BOOL IsOpen() const { return m_bOpen; }
  CString GetName();
  CString GetUserName();
  void SetIsolateODBCTrans(BOOL bIsolateODBCTrans);
  BOOL GetIsolateODBCTrans();
  short GetWorkspaceCount();
  void GetWorkspaceInfo(int nIndex, CDaoWorkspaceInfo& wsinfo, DWORD dwInfoOptions = AFX_DAO_PRIMARY_INFO);
  void GetWorkspaceInfo(LPCTSTR lpszName, CDaoWorkspaceInfo& wsinfo,
                        DWORD dwInfoOptions = AFX_DAO_PRIMARY_INFO);
  short GetDatabaseCount();
  void GetDatabaseInfo(int nIndex, CDaoDatabaseInfo& dbinfo, DWORD dwInfoOptions = AFX_DAO_PRIMARY_INFO);
  void GetDatabaseInfo(LPCTSTR lpszName, CDaoDatabaseInfo& dbinfo,
                       DWORD dwInfoOptions = AFX_DAO_PRIMARY_INFO);

  void BeginTrans();
  void CommitTrans();
  void Rollback();

  // The engine's settings.
  static CString PASCAL GetVersion();
  static CString PASCAL GetIniPath();
  static void PASCAL SetIniPath(LPCTSTR lpszRegistrySubkey);
  static void PASCAL SetDefaultUser(LPCTSTR lpszDefaultUser);
  static void PASCAL SetDefaultPassword(LPCTSTR lpszPassword);
  static short PASCAL GetLoginTimeout();
  static void PASCAL SetLoginTimeout(short nSeconds);
  static void PASCAL CompactDatabase(LPCTSTR lpszSrcName, LPCTSTR lpszDestName,
                                     LPCTSTR lpszLocale = dbLangGeneral, int nOptions = 0);
  static void PASCAL CompactDatabase(LPCTSTR lpszSrcName, LPCTSTR lpszDestName, LPCTSTR lpszLocale,
                                     int nOptions, LPCTSTR lpszPassword);
  static void PASCAL RepairDatabase(LPCTSTR lpszName);
  static void PASCAL Idle(int nAction = dbFreeLocks);

  // ----- Implementation
  BOOL m_bOpen;
  BOOL m_bNew;
  CMapPtrToPtr m_mapDatabases;  // the CDaoDatabase objects open in it

  void InitWorkspacesCollection();
  void InitDatabasesCollection();
  void GetWorkspaceInfo(DAOWorkspace* pDAOWorkspace, CDaoWorkspaceInfo& wsinfo,
                        DWORD dwInfoOptions);
};

// ---------------------------------------------------------------------
// CDaoDatabase

class CDaoDatabase : public CObject {
  DECLARE_DYNAMIC(CDaoDatabase)
public:
  CDaoDatabase(CDaoWorkspace* pWorkspace = NULL);
  virtual ~CDaoDatabase();

  CDaoWorkspace* m_pWorkspace;
  DAODatabase* m_pDAODatabase;
  DAOTableDefs* m_pDAOTableDefs;
  DAOQueryDefs* m_pDAOQueryDefs;
  DAORelations* m_pDAORelations;
  int m_nStatus;

  virtual void Create(LPCTSTR lpszName, LPCTSTR lpszLocale = dbLangGeneral, int dwOptions = 0);
  virtual void Open(LPCTSTR lpszName, BOOL bExclusive = FALSE, BOOL bReadOnly = FALSE,
                    LPCTSTR lpszConnect = _T(""));
  virtual void Close();

  BOOL IsOpen() const { return m_bOpen; }
  BOOL CanUpdate();
  BOOL CanTransact();
  CString GetName();
  CString GetConnect();
  CString GetVersion();
  short GetQueryTimeout();
  void SetQueryTimeout(short nSeconds);
  long GetRecordsAffected();

  void Execute(LPCTSTR lpszSQL, int nOptions = dbFailOnError);

  void DeleteTableDef(LPCTSTR lpszName);
  void DeleteQueryDef(LPCTSTR lpszName);
  void CreateRelation(LPCTSTR lpszName, LPCTSTR lpszTable, LPCTSTR lpszForeignTable,
                      long lAttributes, LPCTSTR lpszField, LPCTSTR lpszForeignField);
  void CreateRelation(CDaoRelationInfo& relinfo);
  void DeleteRelation(LPCTSTR lpszName);

  short GetTableDefCount();
  void GetTableDefInfo(int nIndex, CDaoTableDefInfo& tabledefinfo,
                       DWORD dwInfoOptions = AFX_DAO_PRIMARY_INFO);
  void GetTableDefInfo(LPCTSTR lpszName, CDaoTableDefInfo& tabledefinfo,
                       DWORD dwInfoOptions = AFX_DAO_PRIMARY_INFO);
  short GetQueryDefCount();
  void GetQueryDefInfo(int nIndex, CDaoQueryDefInfo& querydefinfo,
                       DWORD dwInfoOptions = AFX_DAO_PRIMARY_INFO);
  void GetQueryDefInfo(LPCTSTR lpszName, CDaoQueryDefInfo& querydefinfo,
                       DWORD dwInfoOptions = AFX_DAO_PRIMARY_INFO);
  short GetRelationCount();
  void GetRelationInfo(int nIndex, CDaoRelationInfo& relinfo,
                       DWORD dwInfoOptions = AFX_DAO_PRIMARY_INFO);
  void GetRelationInfo(LPCTSTR lpszName, CDaoRelationInfo& relinfo,
                       DWORD dwInfoOptions = AFX_DAO_PRIMARY_INFO);

  // ----- Implementation
  BOOL m_bOpen;
  BOOL m_bOwnWorkspace;  // the default workspace, opened for this database
  CMapPtrToPtr m_mapTableDefs;
  CMapPtrToPtr m_mapQueryDefs;
  CMapPtrToPtr m_mapRecordsets;

  void InitWorkspace();
  void InitCollections();
};

// ---------------------------------------------------------------------
// CDaoTableDef

class CDaoTableDef : public CObject {
  DECLARE_DYNAMIC(CDaoTableDef)
public:
  CDaoTableDef(CDaoDatabase* pDatabase);
  virtual ~CDaoTableDef();

  CDaoDatabase* m_pDatabase;
  DAOTableDef* m_pDAOTableDef;
  DAOFields* m_pDAOFields;
  DAOIndexes* m_pDAOIndexes;

  virtual void Create(LPCTSTR lpszName, long lAttributes = 0, LPCTSTR lpszSrcTable = NULL,
                      LPCTSTR lpszConnect = NULL);
  virtual void Append();
  virtual void Open(LPCTSTR lpszName);
  virtual void Close();

  BOOL IsOpen() const { return m_bOpen; }
  BOOL CanUpdate();
  CString GetName();
  void SetName(LPCTSTR lpszName);
  long GetAttributes();
  void SetAttributes(long lAttributes);
  CString GetConnect();
  void SetConnect(LPCTSTR lpszConnect);
  CString GetSourceTableName();
  void SetSourceTableName(LPCTSTR lpszSrcTableName);
  COleDateTime GetDateCreated();
  COleDateTime GetDateLastUpdated();
  CString GetValidationRule();
  void SetValidationRule(LPCTSTR lpszValidationRule);
  CString GetValidationText();
  void SetValidationText(LPCTSTR lpszValidationText);
  long GetRecordCount();
  void RefreshLink();

  void CreateField(LPCTSTR lpszName, short nType, long lSize, long lAttributes = 0);
  void CreateField(CDaoFieldInfo& fieldinfo);
  void DeleteField(LPCTSTR lpszName);
  void DeleteField(int nIndex);
  void CreateIndex(CDaoIndexInfo& indexinfo);
  void DeleteIndex(LPCTSTR lpszName);
  void DeleteIndex(int nIndex);

  short GetFieldCount();
  void GetFieldInfo(int nIndex, CDaoFieldInfo& fieldinfo, DWORD dwInfoOptions = AFX_DAO_PRIMARY_INFO);
  void GetFieldInfo(LPCTSTR lpszName, CDaoFieldInfo& fieldinfo,
                    DWORD dwInfoOptions = AFX_DAO_PRIMARY_INFO);
  short GetIndexCount();
  void GetIndexInfo(int nIndex, CDaoIndexInfo& indexinfo, DWORD dwInfoOptions = AFX_DAO_PRIMARY_INFO);
  void GetIndexInfo(LPCTSTR lpszName, CDaoIndexInfo& indexinfo,
                    DWORD dwInfoOptions = AFX_DAO_PRIMARY_INFO);

  // ----- Implementation
  BOOL m_bOpen;
  BOOL m_bNew;
  void InitFieldsCollection();
  void InitIndexesCollection();
};

// ---------------------------------------------------------------------
// CDaoQueryDef

class CDaoQueryDef : public CObject {
  DECLARE_DYNAMIC(CDaoQueryDef)
public:
  CDaoQueryDef(CDaoDatabase* pDatabase);
  virtual ~CDaoQueryDef();

  CDaoDatabase* m_pDatabase;
  DAOQueryDef* m_pDAOQueryDef;
  DAOParameters* m_pDAOParameters;
  DAOFields* m_pDAOFields;

  // A querydef with no name is temporary: it can't be appended.
  virtual void Create(LPCTSTR lpszName = NULL, LPCTSTR lpszSQL = NULL);
  virtual void Append();
  virtual void Open(LPCTSTR lpszName = NULL);
  virtual void Close();

  BOOL IsOpen() const { return m_bOpen; }
  BOOL CanUpdate();
  CString GetName();
  void SetName(LPCTSTR lpszName);
  CString GetSQL();
  void SetSQL(LPCTSTR lpszSQL);
  short GetType();
  COleDateTime GetDateCreated();
  COleDateTime GetDateLastUpdated();
  CString GetConnect();
  void SetConnect(LPCTSTR lpszConnect);
  short GetODBCTimeout();
  void SetODBCTimeout(short nODBCTimeout);
  BOOL GetReturnsRecords();
  void SetReturnsRecords(BOOL bReturnsRecords);
  long GetRecordsAffected();

  virtual void Execute(int nOptions = dbFailOnError);

  virtual COleVariant GetParameterValue(LPCTSTR lpszName);
  virtual COleVariant GetParameterValue(int nIndex);
  virtual void SetParameterValue(LPCTSTR lpszName, const COleVariant& varValue);
  virtual void SetParameterValue(int nIndex, const COleVariant& varValue);
  void SetParameterValueNull(LPCTSTR lpszName);
  void SetParameterValueNull(int nIndex);

  short GetFieldCount();
  void GetFieldInfo(int nIndex, CDaoFieldInfo& fieldinfo, DWORD dwInfoOptions = AFX_DAO_PRIMARY_INFO);
  void GetFieldInfo(LPCTSTR lpszName, CDaoFieldInfo& fieldinfo,
                    DWORD dwInfoOptions = AFX_DAO_PRIMARY_INFO);
  short GetParameterCount();
  void GetParameterInfo(int nIndex, CDaoParameterInfo& paraminfo,
                        DWORD dwInfoOptions = AFX_DAO_PRIMARY_INFO);
  void GetParameterInfo(LPCTSTR lpszName, CDaoParameterInfo& paraminfo,
                        DWORD dwInfoOptions = AFX_DAO_PRIMARY_INFO);

  // ----- Implementation
  BOOL m_bOpen;
  BOOL m_bNew;
  void InitParametersCollection();
  void InitFieldsCollection();
};

// ---------------------------------------------------------------------
// CDaoFieldExchange and the DFX functions

#define AFX_DAO_TEXT_DEFAULT_SIZE 255
#define AFX_DAO_BINARY_DEFAULT_SIZE 2048
#define AFX_DAO_LONGBINARY_DEFAULT_SIZE 32768

// dwBindOptions: whether a field's value is kept to find out whether it
// changed (else it is written whenever it is marked dirty).
#define AFX_DAO_ENABLE_FIELD_CACHE 0x01
#define AFX_DAO_DISABLE_FIELD_CACHE 0
#define AFX_DAO_CACHE_BY_VALUE 0x80

class CDaoFieldExchange {
public:
  enum DFX_Operation {
    AddToParameterList,  // the PARAMETERS clause
    AddToSelectList,     // the SELECT list
    BindField,           // the field's number and member
    BindParam,           // a parameter's value
    Fixup,               // the current record into the members
    AllocCache,
    StoreField,          // keeping the members, around AddNew and Edit
    LoadField,           // restoring them
    FreeCache,
    SetFieldNull,        // a member to its null value
    MarkForAddNew,       // the members' values before AddNew
    MarkForEdit,         // ... and Edit
    SetDirtyField,       // changed members into the record
    DefaultValue,
    DumpField,
    MaxDFXOperation
  };
  enum FieldType { none, param, outputColumn };

  UINT m_nOperation;
  CDaoRecordset* m_prs;

  void SetFieldType(UINT nFieldType) { m_nFieldType = nFieldType; }
  BOOL IsValidOperation();

  // ----- Implementation
  CDaoFieldExchange(UINT nOperation, CDaoRecordset* prs, void* pvField = NULL);

  UINT m_nFieldType;
  UINT m_nFieldFound;  // the field's number when looking for pvField
  void* m_pvField;     // the member looked for, or NULL
  int m_nField;        // the field being exchanged, from 1
  int m_nParam;        // the parameter being exchanged, from 1
  CString* m_pstrSQL;  // AddToSelectList and AddToParameterList build it here

  void AppendParamType(CString& strParam, short nType);
};

void AFXAPI DFX_Text(CDaoFieldExchange* pFX, LPCTSTR lpszName, CString& value,
                     int nPreAllocSize = AFX_DAO_TEXT_DEFAULT_SIZE,
                     DWORD dwBindOptions = AFX_DAO_ENABLE_FIELD_CACHE);
void AFXAPI DFX_Binary(CDaoFieldExchange* pFX, LPCTSTR lpszName, CByteArray& value,
                       int nPreAllocSize = AFX_DAO_BINARY_DEFAULT_SIZE, DWORD dwBindOptions = 0);
void AFXAPI DFX_Bool(CDaoFieldExchange* pFX, LPCTSTR lpszName, BOOL& value,
                     DWORD dwBindOptions = AFX_DAO_ENABLE_FIELD_CACHE);
void AFXAPI DFX_Byte(CDaoFieldExchange* pFX, LPCTSTR lpszName, BYTE& value,
                     DWORD dwBindOptions = AFX_DAO_ENABLE_FIELD_CACHE);
void AFXAPI DFX_Short(CDaoFieldExchange* pFX, LPCTSTR lpszName, short& value,
                      DWORD dwBindOptions = AFX_DAO_ENABLE_FIELD_CACHE);
void AFXAPI DFX_Long(CDaoFieldExchange* pFX, LPCTSTR lpszName, long& value,
                     DWORD dwBindOptions = AFX_DAO_ENABLE_FIELD_CACHE);
void AFXAPI DFX_Currency(CDaoFieldExchange* pFX, LPCTSTR lpszName, COleCurrency& value,
                         DWORD dwBindOptions = AFX_DAO_ENABLE_FIELD_CACHE);
void AFXAPI DFX_Single(CDaoFieldExchange* pFX, LPCTSTR lpszName, float& value,
                       DWORD dwBindOptions = AFX_DAO_ENABLE_FIELD_CACHE);
void AFXAPI DFX_Double(CDaoFieldExchange* pFX, LPCTSTR lpszName, double& value,
                       DWORD dwBindOptions = AFX_DAO_ENABLE_FIELD_CACHE);
void AFXAPI DFX_DateTime(CDaoFieldExchange* pFX, LPCTSTR lpszName, COleDateTime& value,
                         DWORD dwBindOptions = AFX_DAO_ENABLE_FIELD_CACHE);
void AFXAPI DFX_LongBinary(CDaoFieldExchange* pFX, LPCTSTR lpszName, CLongBinary& value,
                           DWORD dwPreAllocSize = AFX_DAO_LONGBINARY_DEFAULT_SIZE,
                           DWORD dwBindOptions = 0);

// ---------------------------------------------------------------------
// CDaoRecordset

class CDaoRecordset : public CObject {
  DECLARE_DYNAMIC(CDaoRecordset)
public:
  CDaoRecordset(CDaoDatabase* pDatabase = NULL);
  virtual ~CDaoRecordset();

  CDaoDatabase* m_pDatabase;
  CString m_strFilter;
  CString m_strSort;
  int m_nFields;  // the fields DoFieldExchange binds
  int m_nParams;  // the parameters it binds
  BOOL m_bCheckCacheForDirtyFields;

  DAORecordset* m_pDAORecordset;
  DAOFields* m_pDAOFields;
  DAOIndexes* m_pDAOIndexes;
  CDaoQueryDef* m_pQueryDef;
  CDaoTableDef* m_pTableDef;
  int m_nDefaultType;
  int m_nStatus;

  // lpszSQL: a table's or querydef's name, a SELECT statement, or NULL
  // for GetDefaultSQL.
  virtual void Open(int nOpenType = AFX_DAO_USE_DEFAULT_TYPE, LPCTSTR lpszSQL = NULL,
                    int nOptions = 0);
  virtual void Open(CDaoQueryDef* pQueryDef, int nOpenType = dbOpenDynaset, int nOptions = 0);
  virtual void Open(CDaoTableDef* pTableDef, int nOpenType = dbOpenTable, int nOptions = 0);
  virtual void Close();

  BOOL IsOpen() const;
  BOOL IsBOF() const;
  BOOL IsEOF() const;
  BOOL IsDeleted() const { return m_bDeleted; }
  BOOL CanAppend() const;
  BOOL CanBookmark();
  BOOL CanRestart();
  BOOL CanScroll() const { return m_bScrollable; }
  BOOL CanTransact();
  BOOL CanUpdate() const;
  short GetType();
  CString GetName();
  CString GetSQL() const { return m_strSQL; }
  short GetEditMode();
  long GetRecordCount();
  COleDateTime GetDateCreated();
  COleDateTime GetDateLastUpdated();
  CString GetValidationRule();
  CString GetValidationText();
  BOOL GetLockingMode();
  void SetLockingMode(BOOL bPessimistic);

  // Moving
  void MoveNext();
  void MovePrev();
  void MoveFirst();
  void MoveLast();
  virtual void Move(long lRows);
  long GetAbsolutePosition();
  void SetAbsolutePosition(long lPosition);
  float GetPercentPosition();
  void SetPercentPosition(float fPosition);
  COleVariant GetBookmark();
  void SetBookmark(COleVariant varBookmark);
  COleVariant GetLastModifiedBookmark();

  // Finding: nFindType is AFX_DAO_NEXT, AFX_DAO_PREV, AFX_DAO_FIRST or
  // AFX_DAO_LAST.
  virtual BOOL Find(long lFindType, LPCTSTR lpszFilter);
  BOOL FindFirst(LPCTSTR lpszFilter);
  BOOL FindLast(LPCTSTR lpszFilter);
  BOOL FindNext(LPCTSTR lpszFilter);
  BOOL FindPrev(LPCTSTR lpszFilter);
  CString GetCurrentIndex();
  void SetCurrentIndex(LPCTSTR lpszIndex);
  BOOL Seek(LPCTSTR lpszComparison, COleVariant* pKey1, COleVariant* pKey2 = NULL,
            COleVariant* pKey3 = NULL);
  BOOL Seek(LPCTSTR lpszComparison, COleVariant* pKeyArray, WORD nKeys);

  // Editing
  virtual void AddNew();
  virtual void Edit();
  virtual void Update();
  virtual void Delete();
  virtual void CancelUpdate();
  virtual void Requery();

  // Fields, by the bound members and by name
  void SetFieldDirty(void* pv, BOOL bDirty = TRUE);
  void SetFieldNull(void* pv, BOOL bNull = TRUE);
  BOOL IsFieldDirty(void* pv);
  BOOL IsFieldNull(void* pv);
  BOOL IsFieldNullable(void* pv);
  virtual void GetFieldValue(LPCTSTR lpszName, COleVariant& varValue);
  virtual void GetFieldValue(int nIndex, COleVariant& varValue);
  virtual COleVariant GetFieldValue(LPCTSTR lpszName);
  virtual COleVariant GetFieldValue(int nIndex);
  virtual void SetFieldValue(LPCTSTR lpszName, const COleVariant& varValue);
  virtual void SetFieldValue(int nIndex, const COleVariant& varValue);
  void SetFieldValue(LPCTSTR lpszName, LPCTSTR lpszValue);
  void SetFieldValue(int nIndex, LPCTSTR lpszValue);
  void SetFieldValueNull(LPCTSTR lpszName);
  void SetFieldValueNull(int nIndex);
  virtual COleVariant GetParamValue(int nIndex);
  virtual COleVariant GetParamValue(LPCTSTR lpszName);
  virtual void SetParamValue(int nIndex, const COleVariant& var);
  virtual void SetParamValue(LPCTSTR lpszName, const COleVariant& var);
  void SetParamValueNull(int nIndex);
  void SetParamValueNull(LPCTSTR lpszName);

  short GetFieldCount();
  void GetFieldInfo(int nIndex, CDaoFieldInfo& fieldinfo, DWORD dwInfoOptions = AFX_DAO_PRIMARY_INFO);
  void GetFieldInfo(LPCTSTR lpszName, CDaoFieldInfo& fieldinfo,
                    DWORD dwInfoOptions = AFX_DAO_PRIMARY_INFO);
  short GetIndexCount();
  void GetIndexInfo(int nIndex, CDaoIndexInfo& indexinfo, DWORD dwInfoOptions = AFX_DAO_PRIMARY_INFO);
  void GetIndexInfo(LPCTSTR lpszName, CDaoIndexInfo& indexinfo,
                    DWORD dwInfoOptions = AFX_DAO_PRIMARY_INFO);

  virtual CString GetDefaultDBName();
  virtual CString GetDefaultSQL();
  virtual void DoFieldExchange(CDaoFieldExchange* pFX);

  // ----- Implementation
  BOOL m_bOwnDatabase;  // the database opened for GetDefaultDBName
  BOOL m_bOpen;
  BOOL m_bDeleted;
  BOOL m_bScrollable;
  BOOL m_bAppendable;
  BOOL m_bUpdatable;
  short m_nOpenType;
  CString m_strSQL;
  CDaoQueryDef* m_pTempQueryDef;  // made for parameters
  // What Open was given, for a Requery that has to open again.
  CString m_strOpenSQL;
  BOOL m_bOpenSQL;
  int m_nOpenOptions;
  CString m_strOpenFilter;
  CString m_strOpenSort;
  BOOL m_bOwnQueryDef;
  UINT m_nEditMode;

  // The bound members: their addresses, flags and the values kept to see
  // whether they changed.
  CPtrArray m_arrFieldPtrs;
  CDWordArray m_arrFieldFlags;
  CArray<COleVariant, const COleVariant&> m_arrFieldCache;
  CArray<COleVariant, const COleVariant&> m_arrNullValues;  // the members' values when null
  CArray<COleVariant, const COleVariant&> m_arrParamValues;
  CStringArray m_arrParamNames;

  enum { fieldDirty = 0x1, fieldNull = 0x2, fieldCached = 0x4, fieldNoCache = 0x8 };

  void BuildSQL(LPCTSTR lpszSQL);
  void OpenFromSQL(int nOpenType, int nOptions);
  void BindFields();
  void Fixup();
  void SetCursorAttributes();
  int FieldIndex(void* pv);
  DAOField* GetDAOField(const VARIANT& var);
  void ThrowIfNotOpen() const;
  void ExchangeAll(UINT nOperation);
  void ClearFieldFlags(DWORD dwFlags);
};

#define AFX_DAO_NEXT (+1)
#define AFX_DAO_PREV (-1)
#define AFX_DAO_FIRST LONG_MIN
#define AFX_DAO_LAST LONG_MAX

// ---------------------------------------------------------------------
// A form view on a DAO recordset: the record commands move through it,
// and the form's values are written back before each move.

class CDaoRecordView : public CFormView {
  DECLARE_DYNAMIC(CDaoRecordView)
protected:
  CDaoRecordView(LPCTSTR lpszTemplateName);
  CDaoRecordView(UINT nIDTemplate);

public:
  virtual ~CDaoRecordView();
  virtual CDaoRecordset* OnGetRecordset() = 0;

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
  // The first and last records' bookmarks, found when the view opens.
  COleVariant m_varBookmarkCurrent;
  COleVariant m_varBookmarkFirst;
  COleVariant m_varBookmarkLast;
  UINT m_nStatus;
  void FindBounds();
  BOOL IsOnBookmark(const COleVariant& var);
  DECLARE_MESSAGE_MAP()
};

// ---------------------------------------------------------------------
// Dialog data exchange with DAO recordset fields: like DDX_Text and the
// others, but a null field shows as empty and an empty entry stores null.

void AFXAPI DDX_FieldText(CDataExchange* pDX, int nIDC, BYTE& value, CDaoRecordset* pRecordset);
void AFXAPI DDX_FieldText(CDataExchange* pDX, int nIDC, int& value, CDaoRecordset* pRecordset);
void AFXAPI DDX_FieldText(CDataExchange* pDX, int nIDC, UINT& value, CDaoRecordset* pRecordset);
void AFXAPI DDX_FieldText(CDataExchange* pDX, int nIDC, long& value, CDaoRecordset* pRecordset);
#ifndef _WINPORT
void AFXAPI DDX_FieldText(CDataExchange* pDX, int nIDC, DWORD& value, CDaoRecordset* pRecordset);
#else
// DWORD is UINT here, so the extra overload takes unsigned long.
void AFXAPI DDX_FieldText(CDataExchange* pDX, int nIDC, unsigned long& value, CDaoRecordset* pRecordset);
#endif
void AFXAPI DDX_FieldText(CDataExchange* pDX, int nIDC, CString& value,
                          CDaoRecordset* pRecordset);
void AFXAPI DDX_FieldText(CDataExchange* pDX, int nIDC, LPTSTR pstrValue, int nMaxLen,
                          CDaoRecordset* pRecordset);
void AFXAPI DDX_FieldText(CDataExchange* pDX, int nIDC, float& value, CDaoRecordset* pRecordset);
void AFXAPI DDX_FieldText(CDataExchange* pDX, int nIDC, double& value,
                          CDaoRecordset* pRecordset);
void AFXAPI DDX_FieldText(CDataExchange* pDX, int nIDC, short& value, CDaoRecordset* pRecordset);
void AFXAPI DDX_FieldText(CDataExchange* pDX, int nIDC, COleDateTime& value,
                          CDaoRecordset* pRecordset);
void AFXAPI DDX_FieldText(CDataExchange* pDX, int nIDC, COleCurrency& value,
                          CDaoRecordset* pRecordset);
void AFXAPI DDX_FieldCheck(CDataExchange* pDX, int nIDC, int& value, CDaoRecordset* pRecordset);
void AFXAPI DDX_FieldRadio(CDataExchange* pDX, int nIDC, int& value, CDaoRecordset* pRecordset);
void AFXAPI DDX_FieldLBString(CDataExchange* pDX, int nIDC, CString& value,
                              CDaoRecordset* pRecordset);
void AFXAPI DDX_FieldCBString(CDataExchange* pDX, int nIDC, CString& value,
                              CDaoRecordset* pRecordset);
void AFXAPI DDX_FieldLBStringExact(CDataExchange* pDX, int nIDC, CString& value,
                                   CDaoRecordset* pRecordset);
void AFXAPI DDX_FieldCBStringExact(CDataExchange* pDX, int nIDC, CString& value,
                                   CDaoRecordset* pRecordset);
void AFXAPI DDX_FieldLBIndex(CDataExchange* pDX, int nIDC, int& index, CDaoRecordset* pRecordset);
void AFXAPI DDX_FieldCBIndex(CDataExchange* pDX, int nIDC, int& index, CDaoRecordset* pRecordset);
void AFXAPI DDX_FieldScroll(CDataExchange* pDX, int nIDC, int& value, CDaoRecordset* pRecordset);
void AFXAPI DDX_FieldSlider(CDataExchange* pDX, int nIDC, int& value, CDaoRecordset* pRecordset);

#endif  // __AFXDAO_H__
