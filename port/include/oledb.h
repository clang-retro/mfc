// oledb.h - the part of OLE DB that data-bound controls use: rowsets,
// accessors, column information, row positions and their notifications.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#ifndef _WINPORT_OLEDB_H
#define _WINPORT_OLEDB_H

#include <windows.h>
#include <ole2.h>
#include <oaidl.h>

typedef ULONG_PTR DBCOUNTITEM;
typedef ULONG_PTR DBORDINAL;
typedef LONG_PTR DBROWCOUNT;
typedef LONG_PTR DBROWOFFSET;
typedef ULONG_PTR DBLENGTH;
typedef ULONG_PTR DBBYTEOFFSET;
typedef ULONG DBREFCOUNT;
typedef ULONG_PTR HROW;
typedef ULONG_PTR HACCESSOR;
typedef ULONG_PTR HCHAPTER;
typedef WORD DBTYPE;
typedef DWORD DBSTATUS;
typedef DWORD DBPART;
typedef DWORD DBMEMOWNER;
typedef DWORD DBPARAMIO;
typedef DWORD DBACCESSORFLAGS;
typedef DWORD DBBINDSTATUS;
typedef DWORD DBREASON;
typedef DWORD DBEVENTPHASE;
typedef DWORD DBPOSITIONFLAGS;
typedef DWORD DBROWSTATUS;
typedef DWORD DBROWOPTIONS;
typedef DWORD DBCOLUMNFLAGS;
typedef DWORD DBKIND;

#define DB_NULL_HACCESSOR 0
#define DB_NULL_HROW 0
#define DB_NULL_HCHAPTER 0

enum DBTYPEENUM {
  DBTYPE_EMPTY = 0,
  DBTYPE_NULL = 1,
  DBTYPE_I2 = 2,
  DBTYPE_I4 = 3,
  DBTYPE_R4 = 4,
  DBTYPE_R8 = 5,
  DBTYPE_CY = 6,
  DBTYPE_DATE = 7,
  DBTYPE_BSTR = 8,
  DBTYPE_IDISPATCH = 9,
  DBTYPE_ERROR = 10,
  DBTYPE_BOOL = 11,
  DBTYPE_VARIANT = 12,
  DBTYPE_IUNKNOWN = 13,
  DBTYPE_DECIMAL = 14,
  DBTYPE_UI1 = 17,
  DBTYPE_ARRAY = 0x2000,
  DBTYPE_BYREF = 0x4000,
  DBTYPE_I1 = 16,
  DBTYPE_UI2 = 18,
  DBTYPE_UI4 = 19,
  DBTYPE_I8 = 20,
  DBTYPE_UI8 = 21,
  DBTYPE_GUID = 72,
  DBTYPE_VECTOR = 0x1000,
  DBTYPE_RESERVED = 0x8000,
  DBTYPE_BYTES = 128,
  DBTYPE_STR = 129,
  DBTYPE_WSTR = 130,
  DBTYPE_NUMERIC = 131,
  DBTYPE_UDT = 132,
  DBTYPE_DBDATE = 133,
  DBTYPE_DBTIME = 134,
  DBTYPE_DBTIMESTAMP = 135
};

enum DBPARTENUM { DBPART_INVALID = 0, DBPART_VALUE = 0x1, DBPART_LENGTH = 0x2, DBPART_STATUS = 0x4 };
enum DBMEMOWNERENUM { DBMEMOWNER_CLIENTOWNED = 0, DBMEMOWNER_PROVIDEROWNED = 0x1 };
enum DBPARAMIOENUM { DBPARAMIO_NOTPARAM = 0, DBPARAMIO_INPUT = 0x1, DBPARAMIO_OUTPUT = 0x2 };
enum DBACCESSORFLAGSENUM {
  DBACCESSOR_INVALID = 0,
  DBACCESSOR_PASSBYREF = 0x1,
  DBACCESSOR_ROWDATA = 0x2,
  DBACCESSOR_PARAMETERDATA = 0x4,
  DBACCESSOR_OPTIMIZED = 0x8,
  DBACCESSOR_INHERITED = 0x10
};
enum DBBINDSTATUSENUM {
  DBBINDSTATUS_OK = 0,
  DBBINDSTATUS_BADORDINAL = 1,
  DBBINDSTATUS_UNSUPPORTEDCONVERSION = 2,
  DBBINDSTATUS_BADBINDINFO = 3,
  DBBINDSTATUS_BADSTORAGEFLAGS = 4,
  DBBINDSTATUS_NOINTERFACE = 5,
  DBBINDSTATUS_MULTIPLESTORAGE = 6
};
enum DBSTATUSENUM {
  DBSTATUS_S_OK = 0,
  DBSTATUS_E_BADACCESSOR = 1,
  DBSTATUS_E_CANTCONVERTVALUE = 2,
  DBSTATUS_S_ISNULL = 3,
  DBSTATUS_S_TRUNCATED = 4,
  DBSTATUS_E_SIGNMISMATCH = 5,
  DBSTATUS_E_DATAOVERFLOW = 6,
  DBSTATUS_E_CANTCREATE = 7,
  DBSTATUS_E_UNAVAILABLE = 8,
  DBSTATUS_E_PERMISSIONDENIED = 9,
  DBSTATUS_E_INTEGRITYVIOLATION = 10,
  DBSTATUS_E_SCHEMAVIOLATION = 11,
  DBSTATUS_E_BADSTATUS = 12,
  DBSTATUS_S_DEFAULT = 13
};
enum DBREASONENUM {
  DBREASON_ROWSET_FETCHPOSITIONCHANGE = 0,
  DBREASON_ROWSET_RELEASE,
  DBREASON_COLUMN_SET,
  DBREASON_COLUMN_RECALCULATED,
  DBREASON_ROW_ACTIVATE,
  DBREASON_ROW_RELEASE,
  DBREASON_ROW_DELETE,
  DBREASON_ROW_FIRSTCHANGE,
  DBREASON_ROW_INSERT,
  DBREASON_ROW_RESYNCH,
  DBREASON_ROW_UNDOCHANGE,
  DBREASON_ROW_UNDOINSERT,
  DBREASON_ROW_UNDODELETE,
  DBREASON_ROW_UPDATE,
  DBREASON_ROWSET_CHANGED
};
enum DBREASONENUM15 {
  DBREASON_ROWPOSITION_CHANGED = DBREASON_ROWSET_CHANGED + 1,
  DBREASON_ROWPOSITION_CHAPTERCHANGED,
  DBREASON_ROWPOSITION_CLEARED,
  DBREASON_ROW_ASYNCHINSERT
};
enum DBEVENTPHASEENUM {
  DBEVENTPHASE_OKTODO = 0,
  DBEVENTPHASE_ABOUTTODO,
  DBEVENTPHASE_SYNCHAFTER,
  DBEVENTPHASE_FAILEDTODO,
  DBEVENTPHASE_DIDEVENT
};
enum DBPOSITIONFLAGSENUM { DBPOSITION_OK = 0, DBPOSITION_NOROW, DBPOSITION_BOF, DBPOSITION_EOF };
enum DBKINDENUM {
  DBKIND_GUID_NAME = 0,
  DBKIND_GUID_PROPID,
  DBKIND_NAME,
  DBKIND_PGUID_NAME,
  DBKIND_PGUID_PROPID,
  DBKIND_PROPID,
  DBKIND_GUID
};

typedef struct tagDBID {
  union {
    GUID guid;
    GUID* pguid;
  } uGuid;
  DBKIND eKind;
  union {
    LPOLESTR pwszName;
    ULONG ulPropid;
  } uName;
} DBID;

typedef struct tagDBOBJECT {
  DWORD dwFlags;
  IID iid;
} DBOBJECT;

typedef struct tagDBBINDEXT {
  BYTE* pExtension;
  DBCOUNTITEM ulExtension;
} DBBINDEXT;

typedef struct tagDBBINDING {
  DBORDINAL iOrdinal;
  DBBYTEOFFSET obValue;
  DBBYTEOFFSET obLength;
  DBBYTEOFFSET obStatus;
  ITypeInfo* pTypeInfo;
  DBOBJECT* pObject;
  DBBINDEXT* pBindExt;
  DBPART dwPart;
  DBMEMOWNER dwMemOwner;
  DBPARAMIO eParamIO;
  DBLENGTH cbMaxLen;
  DWORD dwFlags;
  DBTYPE wType;
  BYTE bPrecision;
  BYTE bScale;
} DBBINDING;

typedef struct tagDBCOLUMNINFO {
  LPOLESTR pwszName;
  ITypeInfo* pTypeInfo;
  DBORDINAL iOrdinal;
  DBCOLUMNFLAGS dwFlags;
  DBLENGTH ulColumnSize;
  DBTYPE wType;
  BYTE bPrecision;
  BYTE bScale;
  DBID columnid;
} DBCOLUMNINFO;

extern "C" const IID IID_IAccessor, IID_IRowset, IID_IRowsetChange, IID_IColumnsInfo, IID_IRowsetNotify,
    IID_IRowPosition, IID_IRowPositionChange;

interface IAccessor : public IUnknown {
  STDMETHOD(AddRefAccessor)(HACCESSOR hAccessor, DBREFCOUNT* pcRefCount) = 0;
  STDMETHOD(CreateAccessor)(DBACCESSORFLAGS dwAccessorFlags, DBCOUNTITEM cBindings, const DBBINDING rgBindings[],
                            DBLENGTH cbRowSize, HACCESSOR* phAccessor, DBBINDSTATUS rgStatus[]) = 0;
  STDMETHOD(GetBindings)(HACCESSOR hAccessor, DBACCESSORFLAGS* pdwAccessorFlags, DBCOUNTITEM* pcBindings,
                         DBBINDING** prgBindings) = 0;
  STDMETHOD(ReleaseAccessor)(HACCESSOR hAccessor, DBREFCOUNT* pcRefCount) = 0;
};

interface IRowset : public IUnknown {
  STDMETHOD(AddRefRows)(DBCOUNTITEM cRows, const HROW rghRows[], DBREFCOUNT rgRefCounts[],
                        DBROWSTATUS rgRowStatus[]) = 0;
  STDMETHOD(GetData)(HROW hRow, HACCESSOR hAccessor, void* pData) = 0;
  STDMETHOD(GetNextRows)(HCHAPTER hReserved, DBROWOFFSET lRowsOffset, DBROWCOUNT cRows, DBCOUNTITEM* pcRowsObtained,
                         HROW** prghRows) = 0;
  STDMETHOD(ReleaseRows)(DBCOUNTITEM cRows, const HROW rghRows[], DBROWOPTIONS rgRowOptions[],
                         DBREFCOUNT rgRefCounts[], DBROWSTATUS rgRowStatus[]) = 0;
  STDMETHOD(RestartPosition)(HCHAPTER hReserved) = 0;
};

interface IRowsetChange : public IUnknown {
  STDMETHOD(DeleteRows)(HCHAPTER hReserved, DBCOUNTITEM cRows, const HROW rghRows[], DBROWSTATUS rgRowStatus[]) = 0;
  STDMETHOD(SetData)(HROW hRow, HACCESSOR hAccessor, void* pData) = 0;
  STDMETHOD(InsertRow)(HCHAPTER hReserved, HACCESSOR hAccessor, void* pData, HROW* phRow) = 0;
};

interface IColumnsInfo : public IUnknown {
  STDMETHOD(GetColumnInfo)(DBORDINAL* pcColumns, DBCOLUMNINFO** prgInfo, OLECHAR** ppStringsBuffer) = 0;
  STDMETHOD(MapColumnIDs)(DBORDINAL cColumnIDs, const DBID rgColumnIDs[], DBORDINAL rgColumns[]) = 0;
};

interface IRowsetNotify : public IUnknown {
  STDMETHOD(OnFieldChange)(IRowset* pRowset, HROW hRow, DBORDINAL cColumns, DBORDINAL rgColumns[], DBREASON eReason,
                           DBEVENTPHASE ePhase, BOOL fCantDeny) = 0;
  STDMETHOD(OnRowChange)(IRowset* pRowset, DBCOUNTITEM cRows, const HROW rghRows[], DBREASON eReason,
                         DBEVENTPHASE ePhase, BOOL fCantDeny) = 0;
  STDMETHOD(OnRowsetChange)(IRowset* pRowset, DBREASON eReason, DBEVENTPHASE ePhase, BOOL fCantDeny) = 0;
};

interface IRowPosition : public IUnknown {
  STDMETHOD(ClearRowPosition)() = 0;
  STDMETHOD(GetRowPosition)(HCHAPTER* phChapter, HROW* phRow, DBPOSITIONFLAGS* pdwPositionFlags) = 0;
  STDMETHOD(GetRowset)(REFIID riid, IUnknown** ppRowset) = 0;
  STDMETHOD(Initialize)(IUnknown* pRowset) = 0;
  STDMETHOD(SetRowPosition)(HCHAPTER hChapter, HROW hRow, DBPOSITIONFLAGS dwPositionFlags) = 0;
};

interface IRowPositionChange : public IUnknown {
  STDMETHOD(OnRowPositionChange)(DBREASON eReason, DBEVENTPHASE ePhase, BOOL fCantDeny) = 0;
};

WINPORT_UUIDOF(IAccessor)
WINPORT_UUIDOF(IRowset)
WINPORT_UUIDOF(IRowsetChange)
WINPORT_UUIDOF(IColumnsInfo)
WINPORT_UUIDOF(IRowsetNotify)
WINPORT_UUIDOF(IRowPosition)
WINPORT_UUIDOF(IRowPositionChange)

#endif
