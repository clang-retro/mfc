// sql.h - ODBC core: handles, types, return codes and the core API.
//
// SQLWCHAR is the system wchar_t so that it matches WCHAR and TCHAR. The
// wide entry points are provided by the portable layer, which converts to
// the driver manager's own wide type.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#ifndef _WINPORT_SQL_H
#define _WINPORT_SQL_H

#include <windows.h>

#ifndef SQL_API
#define SQL_API WINAPI
#endif

#ifndef ODBCVER
#define ODBCVER 0x0351
#endif

extern "C" {

typedef unsigned char SQLCHAR;
typedef signed char SQLSCHAR;
typedef wchar_t SQLWCHAR;
typedef unsigned char SQLDATE;
typedef unsigned char SQLDECIMAL;
typedef double SQLDOUBLE;
typedef double SQLFLOAT;
typedef int SQLINTEGER;
typedef unsigned int SQLUINTEGER;
typedef unsigned char SQLNUMERIC;
typedef void* SQLPOINTER;
typedef float SQLREAL;
typedef short SQLSMALLINT;
typedef unsigned short SQLUSMALLINT;
typedef unsigned char SQLTIME;
typedef unsigned char SQLTIMESTAMP;
typedef unsigned char SQLVARCHAR;
typedef SQLSMALLINT SQLRETURN;
typedef long long SQLBIGINT;
typedef unsigned long long SQLUBIGINT;
#if defined(__LP64__) || defined(_WIN64)
typedef long long SQLLEN;
typedef unsigned long long SQLULEN;
typedef unsigned long long SQLSETPOSIROW;
#else
typedef SQLINTEGER SQLLEN;
typedef SQLUINTEGER SQLULEN;
typedef SQLUSMALLINT SQLSETPOSIROW;
#endif
typedef SQLULEN SQLROWCOUNT;
typedef SQLULEN SQLROWSETSIZE;
typedef SQLULEN SQLTRANSID;
typedef SQLLEN SQLROWOFFSET;
typedef void* SQLHANDLE;
typedef SQLHANDLE SQLHENV;
typedef SQLHANDLE SQLHDBC;
typedef SQLHANDLE SQLHSTMT;
typedef SQLHANDLE SQLHDESC;
typedef void* SQLHWND;
typedef void* HENV;
typedef void* HDBC;
typedef void* HSTMT;
typedef short RETCODE;
typedef SQLSMALLINT SWORD;
typedef SQLUSMALLINT UWORD;
typedef SQLINTEGER SDWORD;
typedef SQLUINTEGER UDWORD;
typedef signed char SCHAR;
typedef double SDOUBLE;
typedef float SFLOAT;
typedef void* PTR;

#ifdef UNICODE
typedef SQLWCHAR SQLTCHAR;
#else
typedef SQLCHAR SQLTCHAR;
#endif

typedef struct tagDATE_STRUCT {
  SQLSMALLINT year;
  SQLUSMALLINT month;
  SQLUSMALLINT day;
} DATE_STRUCT, SQL_DATE_STRUCT;
typedef struct tagTIME_STRUCT {
  SQLUSMALLINT hour;
  SQLUSMALLINT minute;
  SQLUSMALLINT second;
} TIME_STRUCT, SQL_TIME_STRUCT;
typedef struct tagTIMESTAMP_STRUCT {
  SQLSMALLINT year;
  SQLUSMALLINT month;
  SQLUSMALLINT day;
  SQLUSMALLINT hour;
  SQLUSMALLINT minute;
  SQLUSMALLINT second;
  SQLUINTEGER fraction;
} TIMESTAMP_STRUCT, SQL_TIMESTAMP_STRUCT;

#define SQL_MAX_NUMERIC_LEN 16
typedef struct tagSQL_NUMERIC_STRUCT {
  SQLCHAR precision;
  SQLSCHAR scale;
  SQLCHAR sign;
  SQLCHAR val[SQL_MAX_NUMERIC_LEN];
} SQL_NUMERIC_STRUCT;

// Return codes
#define SQL_NULL_DATA (-1)
#define SQL_DATA_AT_EXEC (-2)
#define SQL_SUCCESS 0
#define SQL_SUCCESS_WITH_INFO 1
#define SQL_NO_DATA 100
#define SQL_ERROR (-1)
#define SQL_INVALID_HANDLE (-2)
#define SQL_STILL_EXECUTING 2
#define SQL_NEED_DATA 99
#define SQL_SUCCEEDED(rc) (((rc) & (~1)) == 0)

#define SQL_NTS (-3)
#define SQL_NTSL (-3L)
#define SQL_MAX_MESSAGE_LENGTH 512
#define SQL_DATE_LEN 10
#define SQL_TIME_LEN 8
#define SQL_TIMESTAMP_LEN 19

// Handle types
#define SQL_HANDLE_ENV 1
#define SQL_HANDLE_DBC 2
#define SQL_HANDLE_STMT 3
#define SQL_HANDLE_DESC 4
#define SQL_NULL_HANDLE 0L
#define SQL_NULL_HENV 0
#define SQL_NULL_HDBC 0
#define SQL_NULL_HSTMT 0
#define SQL_NULL_HDESC 0

// Attributes
#define SQL_ATTR_OUTPUT_NTS 10001
#define SQL_ATTR_AUTO_IPD 10001
#define SQL_ATTR_METADATA_ID 10014
#define SQL_ATTR_APP_ROW_DESC 10010
#define SQL_ATTR_APP_PARAM_DESC 10011
#define SQL_ATTR_IMP_ROW_DESC 10012
#define SQL_ATTR_IMP_PARAM_DESC 10013
#define SQL_ATTR_CURSOR_SCROLLABLE (-1)
#define SQL_ATTR_CURSOR_SENSITIVITY (-2)

// Data types
#define SQL_UNKNOWN_TYPE 0
#define SQL_CHAR 1
#define SQL_NUMERIC 2
#define SQL_DECIMAL 3
#define SQL_INTEGER 4
#define SQL_SMALLINT 5
#define SQL_FLOAT 6
#define SQL_REAL 7
#define SQL_DOUBLE 8
#define SQL_DATETIME 9
#define SQL_VARCHAR 12
#define SQL_TYPE_DATE 91
#define SQL_TYPE_TIME 92
#define SQL_TYPE_TIMESTAMP 93
#define SQL_DEFAULT 99
#define SQL_ARD_TYPE (-99)

#define SQL_NO_NULLS 0
#define SQL_NULLABLE 1
#define SQL_NULLABLE_UNKNOWN 2

// Fetch orientation
#define SQL_FETCH_NEXT 1
#define SQL_FETCH_FIRST 2
#define SQL_FETCH_LAST 3
#define SQL_FETCH_PRIOR 4
#define SQL_FETCH_ABSOLUTE 5
#define SQL_FETCH_RELATIVE 6

// Transactions and statement freeing
#define SQL_COMMIT 0
#define SQL_ROLLBACK 1
#define SQL_CLOSE 0
#define SQL_DROP 1
#define SQL_UNBIND 2
#define SQL_RESET_PARAMS 3

// SQLGetInfo
#define SQL_MAX_DRIVER_CONNECTIONS 0
#define SQL_MAX_CONCURRENT_ACTIVITIES 1
#define SQL_DATA_SOURCE_NAME 2
#define SQL_FETCH_DIRECTION 8
#define SQL_SERVER_NAME 13
#define SQL_SEARCH_PATTERN_ESCAPE 14
#define SQL_DBMS_NAME 17
#define SQL_DBMS_VER 18
#define SQL_ACCESSIBLE_TABLES 19
#define SQL_ACCESSIBLE_PROCEDURES 20
#define SQL_CURSOR_COMMIT_BEHAVIOR 23
#define SQL_DATA_SOURCE_READ_ONLY 25
#define SQL_DEFAULT_TXN_ISOLATION 26
#define SQL_IDENTIFIER_CASE 28
#define SQL_IDENTIFIER_QUOTE_CHAR 29
#define SQL_MAX_COLUMN_NAME_LEN 30
#define SQL_MAX_CURSOR_NAME_LEN 31
#define SQL_MAX_SCHEMA_NAME_LEN 32
#define SQL_MAX_CATALOG_NAME_LEN 34
#define SQL_MAX_TABLE_NAME_LEN 35
#define SQL_SCROLL_CONCURRENCY 43
#define SQL_TXN_CAPABLE 46
#define SQL_USER_NAME 47
#define SQL_TXN_ISOLATION_OPTION 72
#define SQL_INTEGRITY 73
#define SQL_GETDATA_EXTENSIONS 81
#define SQL_NULL_COLLATION 85
#define SQL_ALTER_TABLE 86
#define SQL_ORDER_BY_COLUMNS_IN_SELECT 90
#define SQL_SPECIAL_CHARACTERS 94
#define SQL_MAX_COLUMNS_IN_GROUP_BY 97
#define SQL_MAX_COLUMNS_IN_INDEX 98
#define SQL_MAX_COLUMNS_IN_ORDER_BY 99
#define SQL_MAX_COLUMNS_IN_SELECT 100
#define SQL_MAX_COLUMNS_IN_TABLE 101
#define SQL_MAX_INDEX_SIZE 102
#define SQL_MAX_ROW_SIZE 104
#define SQL_MAX_STATEMENT_LEN 105
#define SQL_MAX_TABLES_IN_SELECT 106
#define SQL_MAX_USER_NAME_LEN 107
#define SQL_OUTER_JOIN_CAPABILITIES 115
#define SQL_XOPEN_CLI_YEAR 10000
#define SQL_CURSOR_SENSITIVITY 10001
#define SQL_DESCRIBE_PARAMETER 10002
#define SQL_CATALOG_NAME 10003
#define SQL_COLLATION_SEQ 10004
#define SQL_MAX_IDENTIFIER_LEN 10005

// SQL_CURSOR_COMMIT_BEHAVIOR values
#define SQL_CB_DELETE 0
#define SQL_CB_CLOSE 1
#define SQL_CB_PRESERVE 2

// SQL_TXN_CAPABLE values
#define SQL_TC_NONE 0
#define SQL_TC_DML 1
#define SQL_TC_ALL 2
#define SQL_TC_DDL_COMMIT 3
#define SQL_TC_DDL_IGNORE 4

SQLRETURN SQL_API SQLAllocHandle(SQLSMALLINT HandleType, SQLHANDLE InputHandle, SQLHANDLE* OutputHandle);
SQLRETURN SQL_API SQLFreeHandle(SQLSMALLINT HandleType, SQLHANDLE Handle);
SQLRETURN SQL_API SQLFreeStmt(SQLHSTMT StatementHandle, SQLUSMALLINT Option);
SQLRETURN SQL_API SQLBindCol(SQLHSTMT StatementHandle, SQLUSMALLINT ColumnNumber, SQLSMALLINT TargetType,
                             SQLPOINTER TargetValue, SQLLEN BufferLength, SQLLEN* StrLen_or_Ind);
SQLRETURN SQL_API SQLCancel(SQLHSTMT StatementHandle);
SQLRETURN SQL_API SQLCloseCursor(SQLHSTMT StatementHandle);
SQLRETURN SQL_API SQLDisconnect(SQLHDBC ConnectionHandle);
SQLRETURN SQL_API SQLEndTran(SQLSMALLINT HandleType, SQLHANDLE Handle, SQLSMALLINT CompletionType);
SQLRETURN SQL_API SQLExecute(SQLHSTMT StatementHandle);
SQLRETURN SQL_API SQLFetch(SQLHSTMT StatementHandle);
SQLRETURN SQL_API SQLFetchScroll(SQLHSTMT StatementHandle, SQLSMALLINT FetchOrientation, SQLLEN FetchOffset);
SQLRETURN SQL_API SQLGetData(SQLHSTMT StatementHandle, SQLUSMALLINT ColumnNumber, SQLSMALLINT TargetType,
                             SQLPOINTER TargetValue, SQLLEN BufferLength, SQLLEN* StrLen_or_Ind);
SQLRETURN SQL_API SQLNumResultCols(SQLHSTMT StatementHandle, SQLSMALLINT* ColumnCount);
SQLRETURN SQL_API SQLRowCount(SQLHSTMT StatementHandle, SQLLEN* RowCount);
SQLRETURN SQL_API SQLParamData(SQLHSTMT StatementHandle, SQLPOINTER* Value);
SQLRETURN SQL_API SQLPutData(SQLHSTMT StatementHandle, SQLPOINTER Data, SQLLEN StrLen_or_Ind);
SQLRETURN SQL_API SQLSetEnvAttr(SQLHENV EnvironmentHandle, SQLINTEGER Attribute, SQLPOINTER Value,
                                SQLINTEGER StringLength);
SQLRETURN SQL_API SQLGetEnvAttr(SQLHENV EnvironmentHandle, SQLINTEGER Attribute, SQLPOINTER Value,
                                SQLINTEGER BufferLength, SQLINTEGER* StringLength);

// Functions with character arguments, in narrow and wide forms.
SQLRETURN SQL_API SQLConnectA(SQLHDBC hdbc, SQLCHAR* szDSN, SQLSMALLINT cbDSN, SQLCHAR* szUID, SQLSMALLINT cbUID,
                              SQLCHAR* szAuthStr, SQLSMALLINT cbAuthStr);
SQLRETURN SQL_API SQLConnectW(SQLHDBC hdbc, SQLWCHAR* szDSN, SQLSMALLINT cbDSN, SQLWCHAR* szUID, SQLSMALLINT cbUID,
                              SQLWCHAR* szAuthStr, SQLSMALLINT cbAuthStr);
SQLRETURN SQL_API SQLDescribeColA(SQLHSTMT hstmt, SQLUSMALLINT icol, SQLCHAR* szColName, SQLSMALLINT cbColNameMax,
                                  SQLSMALLINT* pcbColName, SQLSMALLINT* pfSqlType, SQLULEN* pcbColDef,
                                  SQLSMALLINT* pibScale, SQLSMALLINT* pfNullable);
SQLRETURN SQL_API SQLDescribeColW(SQLHSTMT hstmt, SQLUSMALLINT icol, SQLWCHAR* szColName, SQLSMALLINT cbColNameMax,
                                  SQLSMALLINT* pcbColName, SQLSMALLINT* pfSqlType, SQLULEN* pcbColDef,
                                  SQLSMALLINT* pibScale, SQLSMALLINT* pfNullable);
SQLRETURN SQL_API SQLExecDirectA(SQLHSTMT hstmt, SQLCHAR* szSqlStr, SQLINTEGER cbSqlStr);
SQLRETURN SQL_API SQLExecDirectW(SQLHSTMT hstmt, SQLWCHAR* szSqlStr, SQLINTEGER cbSqlStr);
SQLRETURN SQL_API SQLPrepareA(SQLHSTMT hstmt, SQLCHAR* szSqlStr, SQLINTEGER cbSqlStr);
SQLRETURN SQL_API SQLPrepareW(SQLHSTMT hstmt, SQLWCHAR* szSqlStr, SQLINTEGER cbSqlStr);
SQLRETURN SQL_API SQLGetDiagRecA(SQLSMALLINT fHandleType, SQLHANDLE handle, SQLSMALLINT iRecord, SQLCHAR* szSqlState,
                                 SQLINTEGER* pfNativeError, SQLCHAR* szErrorMsg, SQLSMALLINT cbErrorMsgMax,
                                 SQLSMALLINT* pcbErrorMsg);
SQLRETURN SQL_API SQLGetDiagRecW(SQLSMALLINT fHandleType, SQLHANDLE handle, SQLSMALLINT iRecord,
                                 SQLWCHAR* szSqlState, SQLINTEGER* pfNativeError, SQLWCHAR* szErrorMsg,
                                 SQLSMALLINT cbErrorMsgMax, SQLSMALLINT* pcbErrorMsg);
SQLRETURN SQL_API SQLGetInfoA(SQLHDBC hdbc, SQLUSMALLINT fInfoType, SQLPOINTER rgbInfoValue,
                              SQLSMALLINT cbInfoValueMax, SQLSMALLINT* pcbInfoValue);
SQLRETURN SQL_API SQLGetInfoW(SQLHDBC hdbc, SQLUSMALLINT fInfoType, SQLPOINTER rgbInfoValue,
                              SQLSMALLINT cbInfoValueMax, SQLSMALLINT* pcbInfoValue);
SQLRETURN SQL_API SQLSetConnectAttrA(SQLHDBC hdbc, SQLINTEGER fAttribute, SQLPOINTER rgbValue,
                                     SQLINTEGER cbValue);
SQLRETURN SQL_API SQLSetConnectAttrW(SQLHDBC hdbc, SQLINTEGER fAttribute, SQLPOINTER rgbValue,
                                     SQLINTEGER cbValue);
SQLRETURN SQL_API SQLGetConnectAttrA(SQLHDBC hdbc, SQLINTEGER fAttribute, SQLPOINTER rgbValue,
                                     SQLINTEGER cbValueMax, SQLINTEGER* pcbValue);
SQLRETURN SQL_API SQLGetConnectAttrW(SQLHDBC hdbc, SQLINTEGER fAttribute, SQLPOINTER rgbValue,
                                     SQLINTEGER cbValueMax, SQLINTEGER* pcbValue);
SQLRETURN SQL_API SQLSetStmtAttrA(SQLHSTMT hstmt, SQLINTEGER fAttribute, SQLPOINTER rgbValue, SQLINTEGER cbValue);
SQLRETURN SQL_API SQLSetStmtAttrW(SQLHSTMT hstmt, SQLINTEGER fAttribute, SQLPOINTER rgbValue, SQLINTEGER cbValue);
SQLRETURN SQL_API SQLGetStmtAttrA(SQLHSTMT hstmt, SQLINTEGER fAttribute, SQLPOINTER rgbValue,
                                  SQLINTEGER cbValueMax, SQLINTEGER* pcbValue);
SQLRETURN SQL_API SQLGetStmtAttrW(SQLHSTMT hstmt, SQLINTEGER fAttribute, SQLPOINTER rgbValue,
                                  SQLINTEGER cbValueMax, SQLINTEGER* pcbValue);
SQLRETURN SQL_API SQLColumnsA(SQLHSTMT hstmt, SQLCHAR* szCatalogName, SQLSMALLINT cbCatalogName,
                              SQLCHAR* szSchemaName, SQLSMALLINT cbSchemaName, SQLCHAR* szTableName,
                              SQLSMALLINT cbTableName, SQLCHAR* szColumnName, SQLSMALLINT cbColumnName);
SQLRETURN SQL_API SQLColumnsW(SQLHSTMT hstmt, SQLWCHAR* szCatalogName, SQLSMALLINT cbCatalogName,
                              SQLWCHAR* szSchemaName, SQLSMALLINT cbSchemaName, SQLWCHAR* szTableName,
                              SQLSMALLINT cbTableName, SQLWCHAR* szColumnName, SQLSMALLINT cbColumnName);
SQLRETURN SQL_API SQLTablesA(SQLHSTMT hstmt, SQLCHAR* szCatalogName, SQLSMALLINT cbCatalogName,
                             SQLCHAR* szSchemaName, SQLSMALLINT cbSchemaName, SQLCHAR* szTableName,
                             SQLSMALLINT cbTableName, SQLCHAR* szTableType, SQLSMALLINT cbTableType);
SQLRETURN SQL_API SQLTablesW(SQLHSTMT hstmt, SQLWCHAR* szCatalogName, SQLSMALLINT cbCatalogName,
                             SQLWCHAR* szSchemaName, SQLSMALLINT cbSchemaName, SQLWCHAR* szTableName,
                             SQLSMALLINT cbTableName, SQLWCHAR* szTableType, SQLSMALLINT cbTableType);

#ifdef UNICODE
#define SQLConnect SQLConnectW
#define SQLDescribeCol SQLDescribeColW
#define SQLExecDirect SQLExecDirectW
#define SQLPrepare SQLPrepareW
#define SQLGetDiagRec SQLGetDiagRecW
#define SQLGetInfo SQLGetInfoW
#define SQLSetConnectAttr SQLSetConnectAttrW
#define SQLGetConnectAttr SQLGetConnectAttrW
#define SQLSetStmtAttr SQLSetStmtAttrW
#define SQLGetStmtAttr SQLGetStmtAttrW
#define SQLColumns SQLColumnsW
#define SQLTables SQLTablesW
#else
#define SQLConnect SQLConnectA
#define SQLDescribeCol SQLDescribeColA
#define SQLExecDirect SQLExecDirectA
#define SQLPrepare SQLPrepareA
#define SQLGetDiagRec SQLGetDiagRecA
#define SQLGetInfo SQLGetInfoA
#define SQLSetConnectAttr SQLSetConnectAttrA
#define SQLGetConnectAttr SQLGetConnectAttrA
#define SQLSetStmtAttr SQLSetStmtAttrA
#define SQLGetStmtAttr SQLGetStmtAttrA
#define SQLColumns SQLColumnsA
#define SQLTables SQLTablesA
#endif

}  // extern "C"

#endif
