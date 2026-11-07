// ocdb.h - the OLE controls data binding interfaces: the cursors
// (ICursor and the interfaces it leads to) that the older data source
// controls give, and the notifications (INotifyDBEvents) they send.
//
// Only what MFC's data binding uses, for toolchains whose SDK headers
// leave this header out.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#ifndef __OCDB_H_
#define __OCDB_H_

#include <ole2.h>

#ifdef __cplusplus
extern "C" {
#endif

// Names: Unicode, unless OLE2ANSI.
#ifdef OLE2ANSI
#define LPDBSTR LPSTR
#define DBCHAR char
#define DBTEXT(quote) quote
#else
#define LPDBSTR LPWSTR
#define DBCHAR WCHAR
#define DBTEXT(quote) L##quote
#endif
typedef LPDBSTR* LPLPDBSTR;

// Column IDs
typedef enum tagDBCOLKIND {
  DBCOLKIND_GUID_NAME = 0,
  DBCOLKIND_GUID_NUMBER = 1,
  DBCOLKIND_NAME = 2
} DBCOLKIND;

#define GUID_NAMEONLY {0x88c8d398, 0x863c, 0x101b, {0xac, 0x3b, 0x00, 0xaa, 0x00, 0x44, 0x77, 0x3d}}
#define GUID_NUMBERONLY {0x88c8d399, 0x863c, 0x101b, {0xac, 0x3b, 0x00, 0xaa, 0x00, 0x44, 0x77, 0x3d}}

typedef struct tagDBCOLUMNID {
  GUID guid;
  DBCOLKIND dwKind;
  union {
    LONG lNumber;
    LPDBSTR lpdbsz;
  };
} DBCOLUMNID;

#define DBTYPE_COLUMNID 507
#define DBTYPE_EXT 0x100

// A VARIANT with the cursor types besides.
typedef struct tagDBVARIANT DBVARIANT;
struct tagDBVARIANT {
  VARTYPE vt;
  unsigned short wReserved1;
  unsigned short wReserved2;
  unsigned short wReserved3;
  union {
    unsigned char bVal;
    short iVal;
    long lVal;
    float fltVal;
    double dblVal;
    VARIANT_BOOL xbool;
    SCODE scode;
    CY cyVal;
    DATE date;
    BSTR bstrVal;
    IUnknown* punkVal;
    IDispatch* pdispVal;
    SAFEARRAY* parray;
    void* byref;
    BLOB blob;
    DBCOLUMNID* pColumnid;
    LPSTR pszVal;
    LPWSTR pwszVal;
    VARIANT* pvarVal;
    DBVARIANT* pdbvarVal;
  };
};

// Fetching rows
typedef enum tagDBROWFETCH {
  DBROWFETCH_DEFAULT = 0,
  DBROWFETCH_CALLEEALLOCATES = 1,
  DBROWFETCH_FORCEREFRESH = 2
} DBROWFETCH;

typedef struct tagDBFETCHROWS {
  ULONG_PTR cRowsRequested;
  DWORD dwFlags;
  void* pData;
  void* pVarData;
  ULONG_PTR cbVarData;
  ULONG_PTR cRowsReturned;
} DBFETCHROWS;

#define DB_NOMAXLENGTH (DWORD)0
#define DB_NOVALUE (DWORD)0xFFFFFFFF
#define DB_NULL (DWORD)0xFFFFFFFF
#define DB_EMPTY (DWORD)0xFFFFFFFE
#define DB_USEENTRYID (DWORD)0xFFFFFFFD
#define DB_CANTCOERCE (DWORD)0xFFFFFFFC
#define DB_TRUNCATED (DWORD)0xFFFFFFFB
#define DB_UNKNOWN (DWORD)0xFFFFFFFA
#define DB_NOINFO (DWORD)0xFFFFFFF9

// Column bindings
typedef enum _DBBINDING {
  DBBINDING_DEFAULT = 0,
  DBBINDING_VARIANT = 1,
  DBBINDING_ENTRYID = 2
} _DBBINDING;

typedef enum tagDBBINDTYPE {
  DBBINDTYPE_DATA = 0,
  DBBINDTYPE_ENTRYID = 1,
  DBBDINTYPE_EITHER = 2,
  DBBINDTYPE_BOTH = 3
} DBBINDTYPE;

typedef struct tagDBCOLUMNBINDING {
  DBCOLUMNID columnID;
  ULONG obData;
  ULONG cbMaxLen;
  ULONG obVarDataLen;
  ULONG obInfo;
  DWORD dwBinding;
  DWORD dwDataType;
} DBCOLUMNBINDING;

typedef struct tagDBBINDPARAMS {
  ULONG cbMaxLen;
  DWORD dwBinding;
  DWORD dwDataType;
  ULONG cbVarDataLen;
  DWORD dwInfo;
  void* pData;
} DBBINDPARAMS;

typedef enum tagDBCOLUMNBINDOPTS {
  DBCOLUMNBINDOPTS_REPLACE = 0,
  DBCOLUMNBINDOPTS_ADD = 1
} DBCOLUMNBINDOPTS;

// The columns of a columns cursor: its metadata, one row a column.
#define CID_NUMBER_INVALID -1
#define CID_NUMBER_AUTOINCREMENT 0
#define CID_NUMBER_BASECOLUMNNAME 1
#define CID_NUMBER_BASENAME 2
#define CID_NUMBER_BINARYCOMPARABLE 3
#define CID_NUMBER_BINDTYPE 4
#define CID_NUMBER_CASESENSITIVE 5
#define CID_NUMBER_COLLATINGORDER 6
#define CID_NUMBER_COLUMNID 7
#define CID_NUMBER_CURSORCOLUMN 8
#define CID_NUMBER_DATACOLUMN 9
#define CID_NUMBER_DEFAULTVALUE 10
#define CID_NUMBER_ENTRYIDMAXLENGTH 11
#define CID_NUMBER_FIXED 12
#define CID_NUMBER_HASDEFAULT 13
#define CID_NUMBER_MAXLENGTH 14
#define CID_NUMBER_MULTIVALUED 15
#define CID_NUMBER_NAME 16
#define CID_NUMBER_NULLABLE 17
#define CID_NUMBER_PHYSICALSORT 18
#define CID_NUMBER_NUMBER 19
#define CID_NUMBER_ROWENTRYID 20
#define CID_NUMBER_SCALE 21
#define CID_NUMBER_SEARCHABLE 22
#define CID_NUMBER_TYPE 23
#define CID_NUMBER_UNIQUE 24
#define CID_NUMBER_UPDATABLE 25
#define CID_NUMBER_VERSION 26
#define CID_NUMBER_STATUS 27

#define _DBCIDGUID {0xfe284700L, 0xd188, 0x11cd, {0xad, 0x48, 0x00, 0xaa, 0x00, 0x3c, 0x9c, 0xb6}}
#define _AFX_DBCOLUMN(n) {_DBCIDGUID, DBCOLKIND_GUID_NUMBER, {n}}
static const DBCOLUMNID COLUMNID_INVALID = _AFX_DBCOLUMN(CID_NUMBER_INVALID);
static const DBCOLUMNID COLUMN_COLUMNID = _AFX_DBCOLUMN(CID_NUMBER_COLUMNID);
static const DBCOLUMNID COLUMN_DATACOLUMN = _AFX_DBCOLUMN(CID_NUMBER_DATACOLUMN);
static const DBCOLUMNID COLUMN_MAXLENGTH = _AFX_DBCOLUMN(CID_NUMBER_MAXLENGTH);
static const DBCOLUMNID COLUMN_NAME = _AFX_DBCOLUMN(CID_NUMBER_NAME);
static const DBCOLUMNID COLUMN_NUMBER = _AFX_DBCOLUMN(CID_NUMBER_NUMBER);
static const DBCOLUMNID COLUMN_TYPE = _AFX_DBCOLUMN(CID_NUMBER_TYPE);
static const DBCOLUMNID COLUMN_UPDATABLE = _AFX_DBCOLUMN(CID_NUMBER_UPDATABLE);
#undef _AFX_DBCOLUMN

// Bookmarks: one byte each. (oledb.h has its own DBBMK_INVALID.)
static const BYTE DBBMK_CURRENT = 0x1;
static const BYTE DBBMK_BEGINNING = 0x2;
static const BYTE DBBMK_END = 0x3;

typedef enum tagDBUPDATELOCK {
  DBUPDATELOCK_PESSIMISTIC = 0,
  DBUPDATELOCK_OPTIMISTIC = 1
} DBUPDATELOCK;

typedef enum tagDBCOLUMNDATA {
  DBCOLUMNDATA_UNCHANGED = 0,
  DBCOLUMNDATA_CHANGED = 1,
  DBCOLUMNDATA_UNKNOWN = 2
} DBCOLUMNDATA;

typedef enum tagDBROWACTION {
  DBROWACTION_IGNORE = 0,
  DBROWACTION_UPDATE = 1,
  DBROWACTION_DELETE = 2,
  DBROWACTION_ADD = 3,
  DBROWACTION_LOCK = 4,
  DBROWACTION_UNLOCK = 5
} DBROWACTION;

typedef enum tagDBUPDATEABLE {
  DBUPDATEABLE_UPDATEABLE = 0,
  DBUPDATEABLE_NOTUPDATEABLE = 1,
  DBUPDATEABLE_UNKNOWN = 2
} DBUPDATEABLE;

typedef enum tagDBCLONEOPTS {
  DBCLONEOPTS_DEFAULT = 0,
  DBCLONEOPTS_SAMEROW = 1
} DBCLONEOPTS;

typedef enum tagDBEDITMODE {
  DBEDITMODE_NONE = 1,
  DBEDITMODE_UPDATE = 2,
  DBEDITMODE_ADD = 3
} DBEDITMODE;

// Notifications: what changed, and why.
typedef enum tagDBEVENTWHATS {
  DBEVENT_CURRENT_ROW_CHANGED = 1,
  DBEVENT_CURRENT_ROW_DATA_CHANGED = 2,
  DBEVENT_NONCURRENT_ROW_DATA_CHANGED = 4,
  DBEVENT_SET_OF_COLUMNS_CHANGED = 8,
  DBEVENT_ORDER_OF_COLUMNS_CHANGED = 16,
  DBEVENT_SET_OF_ROWS_CHANGED = 32,
  DBEVENT_ORDER_OF_ROWS_CHANGED = 64,
  DBEVENT_METADATA_CHANGED = 128,
  DBEVENT_ASYNCH_OP_FINISHED = 256,
  DBEVENT_FIND_CRITERIA_CHANGED = 512
} DBEVENTWHATS;

// (oledb.h has a DBREASON type of its own.)
typedef enum tagDBREASON {
  DBREASON_DELETED = 1,
  DBREASON_INSERTED = 2,
  DBREASON_MODIFIED = 3,
  DBREASON_REMOVEDFROMCURSOR = 4,
  DBREASON_MOVEDINCURSOR = 5,
  DBREASON_MOVE = 6,
  DBREASON_FIND = 7,
  DBREASON_NEWINDEX = 8,
  DBREASON_ROWFIXUP = 9,
  DBREASON_RECALC = 10,
  DBREASON_REFRESH = 11,
  DBREASON_NEWPARAMETERS = 12,
  DBREASON_SORTCHANGED = 13,
  DBREASON_FILTERCHANGED = 14,
  DBREASON_QUERYSPECCHANGED = 15,
  DBREASON_SEEK = 16,
  DBREASON_PERCENT = 17,
  DBREASON_FINDCRITERIACHANGED = 18,
  DBREASON_SETRANGECHANGED = 19,
  DBREASON_ADDNEW = 20,
  DBREASON_MOVEPERCENT = 21,
  DBREASON_BEGINTRANSACT = 22,
  DBREASON_ROLLBACK = 23,
  DBREASON_COMMIT = 24,
  DBREASON_CLOSE = 25,
  DBREASON_BULK_ERROR = 26,
  DBREASON_BULK_NOTTRANSACTABLE = 27,
  DBREASON_BULK_ABOUTTOEXECUTE = 28,
  DBREASON_CANCELUPDATE = 29,
  DBREASON_SETCOLUMN = 30,
  DBREASON_EDIT = 31,
  DBREASON_UNLOAD = 32
} _DBREASON;

typedef struct tagDBNOTIFYREASON {
  DWORD dwReason;
  DBVARIANT arg1;
  DBVARIANT arg2;
} DBNOTIFYREASON;

// Results
#define OCDB_E_BADENTRYID MAKE_SCODE(SEVERITY_ERROR, FACILITY_ITF, 0x0e04)
#define OCDB_E_CANTCOERCE MAKE_SCODE(SEVERITY_ERROR, FACILITY_ITF, 0x0e0a)
#define OCDB_E_COLUMNUNAVAILABLE MAKE_SCODE(SEVERITY_ERROR, FACILITY_ITF, 0x0e0c)
#define OCDB_E_ROWDELETED MAKE_SCODE(SEVERITY_ERROR, FACILITY_ITF, 0x0e11)
#define OCDB_E_UPDATEINPROGRESS MAKE_SCODE(SEVERITY_ERROR, FACILITY_ITF, 0x0e15)
#define OCDB_E_STATEERROR MAKE_SCODE(SEVERITY_ERROR, FACILITY_ITF, 0x0e17)
#define OCDB_E_BADFETCHINFO MAKE_SCODE(SEVERITY_ERROR, FACILITY_ITF, 0x0e18)
#define OCDB_S_CANCEL MAKE_SCODE(SEVERITY_SUCCESS, FACILITY_ITF, 0x0ec1)
#define OCDB_S_ENDOFCURSOR MAKE_SCODE(SEVERITY_SUCCESS, FACILITY_ITF, 0x0ec3)
#define OCDB_S_OPERATIONCANCELLED MAKE_SCODE(SEVERITY_SUCCESS, FACILITY_ITF, 0x0ec5)
#define OCDB_S_CURRENTROWUNCHANGED MAKE_SCODE(SEVERITY_SUCCESS, FACILITY_ITF, 0x0eca)
#define OCDB_S_ROWUPDATED MAKE_SCODE(SEVERITY_SUCCESS, FACILITY_ITF, 0x0ecc)

// The interfaces
extern const IID IID_ICursor;
extern const IID IID_ICursorMove;
extern const IID IID_ICursorUpdateARow;
extern const IID IID_INotifyDBEvents;

#ifdef __cplusplus
}  // extern "C"

#undef Delete

struct ICursor : public IUnknown {
  virtual HRESULT STDMETHODCALLTYPE GetColumnsCursor(REFIID riid, IUnknown** ppvColumnsCursor,
                                                     ULONG_PTR* pcRows) = 0;
  virtual HRESULT STDMETHODCALLTYPE SetBindings(ULONG_PTR cCol, DBCOLUMNBINDING rgBoundColumns[],
                                                ULONG_PTR cbRowLength, DWORD dwFlags) = 0;
  virtual HRESULT STDMETHODCALLTYPE GetBindings(ULONG_PTR* pcCol,
                                                DBCOLUMNBINDING* prgBoundColumns[],
                                                ULONG_PTR* pcbRowLength) = 0;
  virtual HRESULT STDMETHODCALLTYPE GetNextRows(LARGE_INTEGER udlRowsToSkip,
                                                DBFETCHROWS* pFetchParams) = 0;
  virtual HRESULT STDMETHODCALLTYPE Requery() = 0;
};

struct ICursorMove : public ICursor {
  virtual HRESULT STDMETHODCALLTYPE Move(ULONG cbBookmark, void* pBookmark,
                                         LARGE_INTEGER dlOffset, DBFETCHROWS* pFetchParams) = 0;
  virtual HRESULT STDMETHODCALLTYPE GetBookmark(DBCOLUMNID* pBookmarkType, ULONG cbMaxSize,
                                                ULONG* pcbBookmark, void* pBookmark) = 0;
  virtual HRESULT STDMETHODCALLTYPE Clone(DWORD dwFlags, REFIID riid,
                                          IUnknown** ppvClonedCursor) = 0;
};

struct ICursorUpdateARow : public IUnknown {
  virtual HRESULT STDMETHODCALLTYPE BeginUpdate(DWORD dwFlags) = 0;
  virtual HRESULT STDMETHODCALLTYPE SetColumn(DBCOLUMNID* pcid, DBBINDPARAMS* pBindParams) = 0;
  virtual HRESULT STDMETHODCALLTYPE GetColumn(DBCOLUMNID* pcid, DBBINDPARAMS* pBindParams,
                                              DWORD* pdwFlags) = 0;
  virtual HRESULT STDMETHODCALLTYPE GetEditMode(DWORD* pdwState) = 0;
  virtual HRESULT STDMETHODCALLTYPE Update(DBCOLUMNID* pBookmarkType, ULONG* pcbBookmark,
                                           void** ppBookmark) = 0;
  virtual HRESULT STDMETHODCALLTYPE Cancel() = 0;
  virtual HRESULT STDMETHODCALLTYPE Delete() = 0;
};

// What a cursor's user is told, through the connection point of the
// data source control. A return of S_FALSE (or an error) from OKToDo
// refuses the change.
struct INotifyDBEvents : public IUnknown {
  virtual HRESULT STDMETHODCALLTYPE OKToDo(DWORD dwEventWhat, ULONG cReasons,
                                           DBNOTIFYREASON rgReasons[]) = 0;
  virtual HRESULT STDMETHODCALLTYPE Cancelled(DWORD dwEventWhat, ULONG cReasons,
                                              DBNOTIFYREASON rgReasons[]) = 0;
  virtual HRESULT STDMETHODCALLTYPE SyncBefore(DWORD dwEventWhat, ULONG cReasons,
                                               DBNOTIFYREASON rgReasons[]) = 0;
  virtual HRESULT STDMETHODCALLTYPE AboutToDo(DWORD dwEventWhat, ULONG cReasons,
                                              DBNOTIFYREASON rgReasons[]) = 0;
  virtual HRESULT STDMETHODCALLTYPE FailedToDo(DWORD dwEventWhat, ULONG cReasons,
                                               DBNOTIFYREASON rgReasons[]) = 0;
  virtual HRESULT STDMETHODCALLTYPE SyncAfter(DWORD dwEventWhat, ULONG cReasons,
                                              DBNOTIFYREASON rgReasons[]) = 0;
  virtual HRESULT STDMETHODCALLTYPE DidEvent(DWORD dwEventWhat, ULONG cReasons,
                                             DBNOTIFYREASON rgReasons[]) = 0;
};

#endif  // __cplusplus

#endif  // __OCDB_H_
