// objidl.h - the COM object interfaces: memory, streams and storages,
// monikers and bind contexts, the running object table, data objects
// and advise sinks, persistence and enumerators.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#ifndef _WINPORT_OBJIDL_H
#define _WINPORT_OBJIDL_H

#include <unknwn.h>

interface IStream;
interface IStorage;
interface IMoniker;
interface IBindCtx;
interface IRunningObjectTable;
interface IEnumMoniker;
interface IEnumString;
interface IEnumUnknown;
interface IEnumSTATSTG;
interface IEnumFORMATETC;
interface IEnumSTATDATA;
interface IAdviseSink;
interface IDataObject;
interface ILockBytes;

extern "C" {
extern const IID IID_IMalloc, IID_ISequentialStream, IID_IStream, IID_IStorage, IID_IEnumSTATSTG,
    IID_ILockBytes, IID_IRootStorage, IID_IPersist, IID_IPersistStream, IID_IPersistStorage,
    IID_IPersistFile, IID_IMoniker, IID_IBindCtx, IID_IRunningObjectTable, IID_IEnumMoniker,
    IID_IEnumString, IID_IEnumUnknown, IID_IDataObject, IID_IEnumFORMATETC, IID_IEnumSTATDATA,
    IID_IAdviseSink, IID_IAdviseSink2, IID_IDataAdviseHolder, IID_IMessageFilter,
    IID_IExternalConnection, IID_IMarshal, IID_IStdMarshalInfo, IID_IROTData, IID_IClassActivator;
}

interface IMalloc : public IUnknown {
  STDMETHOD_(void*, Alloc)(SIZE_T cb) = 0;
  STDMETHOD_(void*, Realloc)(void* pv, SIZE_T cb) = 0;
  STDMETHOD_(void, Free)(void* pv) = 0;
  STDMETHOD_(SIZE_T, GetSize)(void* pv) = 0;
  STDMETHOD_(int, DidAlloc)(void* pv) = 0;
  STDMETHOD_(void, HeapMinimize)() = 0;
};
typedef IMalloc* LPMALLOC;

typedef struct tagSTATSTG {
  LPOLESTR pwcsName;
  DWORD type;
  ULARGE_INTEGER cbSize;
  FILETIME mtime;
  FILETIME ctime;
  FILETIME atime;
  DWORD grfMode;
  DWORD grfLocksSupported;
  CLSID clsid;
  DWORD grfStateBits;
  DWORD reserved;
} STATSTG;
typedef enum tagSTGTY { STGTY_STORAGE = 1, STGTY_STREAM = 2, STGTY_LOCKBYTES = 3, STGTY_PROPERTY = 4 } STGTY;
typedef enum tagSTREAM_SEEK { STREAM_SEEK_SET = 0, STREAM_SEEK_CUR = 1, STREAM_SEEK_END = 2 } STREAM_SEEK;
typedef enum tagLOCKTYPE { LOCK_WRITE = 1, LOCK_EXCLUSIVE = 2, LOCK_ONLYONCE = 4 } LOCKTYPE;

interface ISequentialStream : public IUnknown {
  STDMETHOD(Read)(void* pv, ULONG cb, ULONG* pcbRead) = 0;
  STDMETHOD(Write)(const void* pv, ULONG cb, ULONG* pcbWritten) = 0;
};
interface IStream : public ISequentialStream {
  STDMETHOD(Seek)(LARGE_INTEGER dlibMove, DWORD dwOrigin, ULARGE_INTEGER* plibNewPosition) = 0;
  STDMETHOD(SetSize)(ULARGE_INTEGER libNewSize) = 0;
  STDMETHOD(CopyTo)(IStream* pstm, ULARGE_INTEGER cb, ULARGE_INTEGER* pcbRead, ULARGE_INTEGER* pcbWritten) = 0;
  STDMETHOD(Commit)(DWORD grfCommitFlags) = 0;
  STDMETHOD(Revert)() = 0;
  STDMETHOD(LockRegion)(ULARGE_INTEGER libOffset, ULARGE_INTEGER cb, DWORD dwLockType) = 0;
  STDMETHOD(UnlockRegion)(ULARGE_INTEGER libOffset, ULARGE_INTEGER cb, DWORD dwLockType) = 0;
  STDMETHOD(Stat)(STATSTG* pstatstg, DWORD grfStatFlag) = 0;
  STDMETHOD(Clone)(IStream** ppstm) = 0;
};
typedef IStream* LPSTREAM;

interface IEnumSTATSTG : public IUnknown {
  STDMETHOD(Next)(ULONG celt, STATSTG* rgelt, ULONG* pceltFetched) = 0;
  STDMETHOD(Skip)(ULONG celt) = 0;
  STDMETHOD(Reset)() = 0;
  STDMETHOD(Clone)(IEnumSTATSTG** ppenum) = 0;
};
typedef IEnumSTATSTG* LPENUMSTATSTG;

interface IStorage : public IUnknown {
  STDMETHOD(CreateStream)(const OLECHAR* pwcsName, DWORD grfMode, DWORD reserved1, DWORD reserved2,
                          IStream** ppstm) = 0;
  STDMETHOD(OpenStream)(const OLECHAR* pwcsName, void* reserved1, DWORD grfMode, DWORD reserved2,
                        IStream** ppstm) = 0;
  STDMETHOD(CreateStorage)(const OLECHAR* pwcsName, DWORD grfMode, DWORD reserved1, DWORD reserved2,
                           IStorage** ppstg) = 0;
  STDMETHOD(OpenStorage)(const OLECHAR* pwcsName, IStorage* pstgPriority, DWORD grfMode, SNB snbExclude,
                         DWORD reserved, IStorage** ppstg) = 0;
  STDMETHOD(CopyTo)(DWORD ciidExclude, const IID* rgiidExclude, SNB snbExclude, IStorage* pstgDest) = 0;
  STDMETHOD(MoveElementTo)(const OLECHAR* pwcsName, IStorage* pstgDest, const OLECHAR* pwcsNewName,
                           DWORD grfFlags) = 0;
  STDMETHOD(Commit)(DWORD grfCommitFlags) = 0;
  STDMETHOD(Revert)() = 0;
  STDMETHOD(EnumElements)(DWORD reserved1, void* reserved2, DWORD reserved3, IEnumSTATSTG** ppenum) = 0;
  STDMETHOD(DestroyElement)(const OLECHAR* pwcsName) = 0;
  STDMETHOD(RenameElement)(const OLECHAR* pwcsOldName, const OLECHAR* pwcsNewName) = 0;
  STDMETHOD(SetElementTimes)(const OLECHAR* pwcsName, const FILETIME* pctime, const FILETIME* patime,
                             const FILETIME* pmtime) = 0;
  STDMETHOD(SetClass)(REFCLSID clsid) = 0;
  STDMETHOD(SetStateBits)(DWORD grfStateBits, DWORD grfMask) = 0;
  STDMETHOD(Stat)(STATSTG* pstatstg, DWORD grfStatFlag) = 0;
};
typedef IStorage* LPSTORAGE;

interface ILockBytes : public IUnknown {
  STDMETHOD(ReadAt)(ULARGE_INTEGER ulOffset, void* pv, ULONG cb, ULONG* pcbRead) = 0;
  STDMETHOD(WriteAt)(ULARGE_INTEGER ulOffset, const void* pv, ULONG cb, ULONG* pcbWritten) = 0;
  STDMETHOD(Flush)() = 0;
  STDMETHOD(SetSize)(ULARGE_INTEGER cb) = 0;
  STDMETHOD(LockRegion)(ULARGE_INTEGER libOffset, ULARGE_INTEGER cb, DWORD dwLockType) = 0;
  STDMETHOD(UnlockRegion)(ULARGE_INTEGER libOffset, ULARGE_INTEGER cb, DWORD dwLockType) = 0;
  STDMETHOD(Stat)(STATSTG* pstatstg, DWORD grfStatFlag) = 0;
};
typedef ILockBytes* LPLOCKBYTES;

interface IRootStorage : public IUnknown {
  STDMETHOD(SwitchToFile)(LPOLESTR pszFile) = 0;
};
typedef IRootStorage* LPROOTSTORAGE;

interface IPersist : public IUnknown {
  STDMETHOD(GetClassID)(CLSID* pClassID) = 0;
};
typedef IPersist* LPPERSIST;
interface IPersistStream : public IPersist {
  STDMETHOD(IsDirty)() = 0;
  STDMETHOD(Load)(IStream* pStm) = 0;
  STDMETHOD(Save)(IStream* pStm, BOOL fClearDirty) = 0;
  STDMETHOD(GetSizeMax)(ULARGE_INTEGER* pcbSize) = 0;
};
typedef IPersistStream* LPPERSISTSTREAM;
interface IPersistStorage : public IPersist {
  STDMETHOD(IsDirty)() = 0;
  STDMETHOD(InitNew)(IStorage* pStg) = 0;
  STDMETHOD(Load)(IStorage* pStg) = 0;
  STDMETHOD(Save)(IStorage* pStgSave, BOOL fSameAsLoad) = 0;
  STDMETHOD(SaveCompleted)(IStorage* pStgNew) = 0;
  STDMETHOD(HandsOffStorage)() = 0;
};
typedef IPersistStorage* LPPERSISTSTORAGE;
interface IPersistFile : public IPersist {
  STDMETHOD(IsDirty)() = 0;
  STDMETHOD(Load)(LPCOLESTR pszFileName, DWORD dwMode) = 0;
  STDMETHOD(Save)(LPCOLESTR pszFileName, BOOL fRemember) = 0;
  STDMETHOD(SaveCompleted)(LPCOLESTR pszFileName) = 0;
  STDMETHOD(GetCurFile)(LPOLESTR* ppszFileName) = 0;
};
typedef IPersistFile* LPPERSISTFILE;

interface IEnumUnknown : public IUnknown {
  STDMETHOD(Next)(ULONG celt, IUnknown** rgelt, ULONG* pceltFetched) = 0;
  STDMETHOD(Skip)(ULONG celt) = 0;
  STDMETHOD(Reset)() = 0;
  STDMETHOD(Clone)(IEnumUnknown** ppenum) = 0;
};
typedef IEnumUnknown* LPENUMUNKNOWN;
interface IEnumString : public IUnknown {
  STDMETHOD(Next)(ULONG celt, LPOLESTR* rgelt, ULONG* pceltFetched) = 0;
  STDMETHOD(Skip)(ULONG celt) = 0;
  STDMETHOD(Reset)() = 0;
  STDMETHOD(Clone)(IEnumString** ppenum) = 0;
};
typedef IEnumString* LPENUMSTRING;
interface IEnumMoniker : public IUnknown {
  STDMETHOD(Next)(ULONG celt, IMoniker** rgelt, ULONG* pceltFetched) = 0;
  STDMETHOD(Skip)(ULONG celt) = 0;
  STDMETHOD(Reset)() = 0;
  STDMETHOD(Clone)(IEnumMoniker** ppenum) = 0;
};
typedef IEnumMoniker* LPENUMMONIKER;

typedef struct tagBIND_OPTS {
  DWORD cbStruct;
  DWORD grfFlags;
  DWORD grfMode;
  DWORD dwTickCountDeadline;
} BIND_OPTS, *LPBIND_OPTS;
typedef struct tagBIND_OPTS2 {
  DWORD cbStruct;
  DWORD grfFlags;
  DWORD grfMode;
  DWORD dwTickCountDeadline;
  DWORD dwTrackFlags;
  DWORD dwClassContext;
  LCID locale;
  COSERVERINFO* pServerInfo;
} BIND_OPTS2, *LPBIND_OPTS2;
typedef enum tagBIND_FLAGS { BIND_MAYBOTHERUSER = 1, BIND_JUSTTESTEXISTENCE = 2 } BIND_FLAGS;

interface IRunningObjectTable : public IUnknown {
  STDMETHOD(Register)(DWORD grfFlags, IUnknown* punkObject, IMoniker* pmkObjectName, DWORD* pdwRegister) = 0;
  STDMETHOD(Revoke)(DWORD dwRegister) = 0;
  STDMETHOD(IsRunning)(IMoniker* pmkObjectName) = 0;
  STDMETHOD(GetObject)(IMoniker* pmkObjectName, IUnknown** ppunkObject) = 0;
  STDMETHOD(NoteChangeTime)(DWORD dwRegister, FILETIME* pfiletime) = 0;
  STDMETHOD(GetTimeOfLastChange)(IMoniker* pmkObjectName, FILETIME* pfiletime) = 0;
  STDMETHOD(EnumRunning)(IEnumMoniker** ppenumMoniker) = 0;
};
typedef IRunningObjectTable* LPRUNNINGOBJECTTABLE;
#define ROTFLAGS_REGISTRATIONKEEPSALIVE 0x1
#define ROTFLAGS_ALLOWANYCLIENT 0x2

interface IBindCtx : public IUnknown {
  STDMETHOD(RegisterObjectBound)(IUnknown* punk) = 0;
  STDMETHOD(RevokeObjectBound)(IUnknown* punk) = 0;
  STDMETHOD(ReleaseBoundObjects)() = 0;
  STDMETHOD(SetBindOptions)(BIND_OPTS* pbindopts) = 0;
  STDMETHOD(GetBindOptions)(BIND_OPTS* pbindopts) = 0;
  STDMETHOD(GetRunningObjectTable)(IRunningObjectTable** pprot) = 0;
  STDMETHOD(RegisterObjectParam)(LPOLESTR pszKey, IUnknown* punk) = 0;
  STDMETHOD(GetObjectParam)(LPOLESTR pszKey, IUnknown** ppunk) = 0;
  STDMETHOD(EnumObjectParam)(IEnumString** ppenum) = 0;
  STDMETHOD(RevokeObjectParam)(LPOLESTR pszKey) = 0;
};
typedef IBindCtx* LPBC;
typedef IBindCtx* LPBINDCTX;

typedef enum tagMKSYS {
  MKSYS_NONE = 0,
  MKSYS_GENERICCOMPOSITE = 1,
  MKSYS_FILEMONIKER = 2,
  MKSYS_ANTIMONIKER = 3,
  MKSYS_ITEMMONIKER = 4,
  MKSYS_POINTERMONIKER = 5,
  MKSYS_CLASSMONIKER = 7,
  MKSYS_OBJREFMONIKER = 8,
  MKSYS_SESSIONMONIKER = 9,
  MKSYS_LUAMONIKER = 10
} MKSYS;
typedef enum tagMKREDUCE {
  MKRREDUCE_ONE = 3 << 16,
  MKRREDUCE_TOUSER = 2 << 16,
  MKRREDUCE_THROUGHUSER = 1 << 16,
  MKRREDUCE_ALL = 0
} MKRREDUCE;

interface IMoniker : public IPersistStream {
  STDMETHOD(BindToObject)(IBindCtx* pbc, IMoniker* pmkToLeft, REFIID riidResult, void** ppvResult) = 0;
  STDMETHOD(BindToStorage)(IBindCtx* pbc, IMoniker* pmkToLeft, REFIID riid, void** ppvObj) = 0;
  STDMETHOD(Reduce)(IBindCtx* pbc, DWORD dwReduceHowFar, IMoniker** ppmkToLeft, IMoniker** ppmkReduced) = 0;
  STDMETHOD(ComposeWith)(IMoniker* pmkRight, BOOL fOnlyIfNotGeneric, IMoniker** ppmkComposite) = 0;
  STDMETHOD(Enum)(BOOL fForward, IEnumMoniker** ppenumMoniker) = 0;
  STDMETHOD(IsEqual)(IMoniker* pmkOtherMoniker) = 0;
  STDMETHOD(Hash)(DWORD* pdwHash) = 0;
  STDMETHOD(IsRunning)(IBindCtx* pbc, IMoniker* pmkToLeft, IMoniker* pmkNewlyRunning) = 0;
  STDMETHOD(GetTimeOfLastChange)(IBindCtx* pbc, IMoniker* pmkToLeft, FILETIME* pFileTime) = 0;
  STDMETHOD(Inverse)(IMoniker** ppmk) = 0;
  STDMETHOD(CommonPrefixWith)(IMoniker* pmkOther, IMoniker** ppmkPrefix) = 0;
  STDMETHOD(RelativePathTo)(IMoniker* pmkOther, IMoniker** ppmkRelPath) = 0;
  STDMETHOD(GetDisplayName)(IBindCtx* pbc, IMoniker* pmkToLeft, LPOLESTR* ppszDisplayName) = 0;
  STDMETHOD(ParseDisplayName)(IBindCtx* pbc, IMoniker* pmkToLeft, LPOLESTR pszDisplayName, ULONG* pchEaten,
                              IMoniker** ppmkOut) = 0;
  STDMETHOD(IsSystemMoniker)(DWORD* pdwMksys) = 0;
};
typedef IMoniker* LPMONIKER;

interface IROTData : public IUnknown {
  STDMETHOD(GetComparisonData)(BYTE* pbData, ULONG cbMax, ULONG* pcbData) = 0;
};

// Data transfer
typedef struct tagDVTARGETDEVICE {
  DWORD tdSize;
  WORD tdDriverNameOffset;
  WORD tdDeviceNameOffset;
  WORD tdPortNameOffset;
  WORD tdExtDevmodeOffset;
  BYTE tdData[1];
} DVTARGETDEVICE;
typedef struct tagFORMATETC {
  CLIPFORMAT cfFormat;
  DVTARGETDEVICE* ptd;
  DWORD dwAspect;
  LONG lindex;
  DWORD tymed;
} FORMATETC, *LPFORMATETC;
typedef enum tagTYMED {
  TYMED_HGLOBAL = 1,
  TYMED_FILE = 2,
  TYMED_ISTREAM = 4,
  TYMED_ISTORAGE = 8,
  TYMED_GDI = 16,
  TYMED_MFPICT = 32,
  TYMED_ENHMF = 64,
  TYMED_NULL = 0
} TYMED;
typedef struct tagSTGMEDIUM {
  DWORD tymed;
  union {
    HBITMAP hBitmap;
    HMETAFILEPICT hMetaFilePict;
    HENHMETAFILE hEnhMetaFile;
    HGLOBAL hGlobal;
    LPOLESTR lpszFileName;
    IStream* pstm;
    IStorage* pstg;
  };
  IUnknown* pUnkForRelease;
} STGMEDIUM, uSTGMEDIUM, *LPSTGMEDIUM;
typedef enum tagADVF {
  ADVF_NODATA = 1,
  ADVF_PRIMEFIRST = 2,
  ADVF_ONLYONCE = 4,
  ADVF_DATAONSTOP = 64,
  ADVFCACHE_NOHANDLER = 8,
  ADVFCACHE_FORCEBUILTIN = 16,
  ADVFCACHE_ONSAVE = 32
} ADVF;
typedef struct tagSTATDATA {
  FORMATETC formatetc;
  DWORD advf;
  IAdviseSink* pAdvSink;
  DWORD dwConnection;
} STATDATA, *LPSTATDATA;
typedef enum tagDATADIR { DATADIR_GET = 1, DATADIR_SET = 2 } DATADIR;

interface IEnumFORMATETC : public IUnknown {
  STDMETHOD(Next)(ULONG celt, FORMATETC* rgelt, ULONG* pceltFetched) = 0;
  STDMETHOD(Skip)(ULONG celt) = 0;
  STDMETHOD(Reset)() = 0;
  STDMETHOD(Clone)(IEnumFORMATETC** ppenum) = 0;
};
typedef IEnumFORMATETC* LPENUMFORMATETC;
interface IEnumSTATDATA : public IUnknown {
  STDMETHOD(Next)(ULONG celt, STATDATA* rgelt, ULONG* pceltFetched) = 0;
  STDMETHOD(Skip)(ULONG celt) = 0;
  STDMETHOD(Reset)() = 0;
  STDMETHOD(Clone)(IEnumSTATDATA** ppenum) = 0;
};
typedef IEnumSTATDATA* LPENUMSTATDATA;
interface IAdviseSink : public IUnknown {
  STDMETHOD_(void, OnDataChange)(FORMATETC* pFormatetc, STGMEDIUM* pStgmed) = 0;
  STDMETHOD_(void, OnViewChange)(DWORD dwAspect, LONG lindex) = 0;
  STDMETHOD_(void, OnRename)(IMoniker* pmk) = 0;
  STDMETHOD_(void, OnSave)() = 0;
  STDMETHOD_(void, OnClose)() = 0;
};
typedef IAdviseSink* LPADVISESINK;
interface IAdviseSink2 : public IAdviseSink {
  STDMETHOD_(void, OnLinkSrcChange)(IMoniker* pmk) = 0;
};
typedef IAdviseSink2* LPADVISESINK2;
interface IDataObject : public IUnknown {
  STDMETHOD(GetData)(FORMATETC* pformatetcIn, STGMEDIUM* pmedium) = 0;
  STDMETHOD(GetDataHere)(FORMATETC* pformatetc, STGMEDIUM* pmedium) = 0;
  STDMETHOD(QueryGetData)(FORMATETC* pformatetc) = 0;
  STDMETHOD(GetCanonicalFormatEtc)(FORMATETC* pformatectIn, FORMATETC* pformatetcOut) = 0;
  STDMETHOD(SetData)(FORMATETC* pformatetc, STGMEDIUM* pmedium, BOOL fRelease) = 0;
  STDMETHOD(EnumFormatEtc)(DWORD dwDirection, IEnumFORMATETC** ppenumFormatEtc) = 0;
  STDMETHOD(DAdvise)(FORMATETC* pformatetc, DWORD advf, IAdviseSink* pAdvSink, DWORD* pdwConnection) = 0;
  STDMETHOD(DUnadvise)(DWORD dwConnection) = 0;
  STDMETHOD(EnumDAdvise)(IEnumSTATDATA** ppenumAdvise) = 0;
};
typedef IDataObject* LPDATAOBJECT;
interface IDataAdviseHolder : public IUnknown {
  STDMETHOD(Advise)(IDataObject* pDataObject, FORMATETC* pFetc, DWORD advf, IAdviseSink* pAdvise,
                    DWORD* pdwConnection) = 0;
  STDMETHOD(Unadvise)(DWORD dwConnection) = 0;
  STDMETHOD(EnumAdvise)(IEnumSTATDATA** ppenumAdvise) = 0;
  STDMETHOD(SendOnDataChange)(IDataObject* pDataObject, DWORD dwReserved, DWORD advf) = 0;
};
typedef IDataAdviseHolder* LPDATAADVISEHOLDER;

// Calls
typedef struct tagINTERFACEINFO {
  IUnknown* pUnk;
  IID iid;
  WORD wMethod;
} INTERFACEINFO, *LPINTERFACEINFO;
typedef enum tagCALLTYPE {
  CALLTYPE_TOPLEVEL = 1,
  CALLTYPE_NESTED = 2,
  CALLTYPE_ASYNC = 3,
  CALLTYPE_TOPLEVEL_CALLPENDING = 4,
  CALLTYPE_ASYNC_CALLPENDING = 5
} CALLTYPE;
typedef enum tagSERVERCALL { SERVERCALL_ISHANDLED = 0, SERVERCALL_REJECTED = 1, SERVERCALL_RETRYLATER = 2 } SERVERCALL;
typedef enum tagPENDINGTYPE { PENDINGTYPE_TOPLEVEL = 1, PENDINGTYPE_NESTED = 2 } PENDINGTYPE;
typedef enum tagPENDINGMSG { PENDINGMSG_CANCELCALL = 0, PENDINGMSG_WAITNOPROCESS = 1, PENDINGMSG_WAITDEFPROCESS = 2 } PENDINGMSG;

interface IMessageFilter : public IUnknown {
  STDMETHOD_(DWORD, HandleInComingCall)(DWORD dwCallType, HTASK htaskCaller, DWORD dwTickCount,
                                        LPINTERFACEINFO lpInterfaceInfo) = 0;
  STDMETHOD_(DWORD, RetryRejectedCall)(HTASK htaskCallee, DWORD dwTickCount, DWORD dwRejectType) = 0;
  STDMETHOD_(DWORD, MessagePending)(HTASK htaskCallee, DWORD dwTickCount, DWORD dwPendingType) = 0;
};
typedef IMessageFilter* LPMESSAGEFILTER;
typedef enum tagEXTCONN { EXTCONN_STRONG = 1, EXTCONN_WEAK = 2, EXTCONN_CALLABLE = 4 } EXTCONN;
interface IExternalConnection : public IUnknown {
  STDMETHOD_(DWORD, AddConnection)(DWORD extconn, DWORD reserved) = 0;
  STDMETHOD_(DWORD, ReleaseConnection)(DWORD extconn, DWORD reserved, BOOL fLastReleaseCloses) = 0;
};
typedef IExternalConnection* LPEXTERNALCONNECTION;
interface IMarshal : public IUnknown {
  STDMETHOD(GetUnmarshalClass)(REFIID riid, void* pv, DWORD dwDestContext, void* pvDestContext,
                               DWORD mshlflags, CLSID* pCid) = 0;
  STDMETHOD(GetMarshalSizeMax)(REFIID riid, void* pv, DWORD dwDestContext, void* pvDestContext,
                               DWORD mshlflags, DWORD* pSize) = 0;
  STDMETHOD(MarshalInterface)(IStream* pStm, REFIID riid, void* pv, DWORD dwDestContext, void* pvDestContext,
                              DWORD mshlflags) = 0;
  STDMETHOD(UnmarshalInterface)(IStream* pStm, REFIID riid, void** ppv) = 0;
  STDMETHOD(ReleaseMarshalData)(IStream* pStm) = 0;
  STDMETHOD(DisconnectObject)(DWORD dwReserved) = 0;
};
typedef IMarshal* LPMARSHAL;

WINPORT_UUIDOF(IMalloc)
WINPORT_UUIDOF(ISequentialStream)
WINPORT_UUIDOF(IStream)
WINPORT_UUIDOF(IEnumSTATSTG)
WINPORT_UUIDOF(IStorage)
WINPORT_UUIDOF(ILockBytes)
WINPORT_UUIDOF(IRootStorage)
WINPORT_UUIDOF(IPersist)
WINPORT_UUIDOF(IPersistStream)
WINPORT_UUIDOF(IPersistStorage)
WINPORT_UUIDOF(IPersistFile)
WINPORT_UUIDOF(IEnumUnknown)
WINPORT_UUIDOF(IEnumString)
WINPORT_UUIDOF(IEnumMoniker)
WINPORT_UUIDOF(IRunningObjectTable)
WINPORT_UUIDOF(IBindCtx)
WINPORT_UUIDOF(IMoniker)
WINPORT_UUIDOF(IROTData)
WINPORT_UUIDOF(IEnumFORMATETC)
WINPORT_UUIDOF(IEnumSTATDATA)
WINPORT_UUIDOF(IAdviseSink)
WINPORT_UUIDOF(IAdviseSink2)
WINPORT_UUIDOF(IDataObject)
WINPORT_UUIDOF(IDataAdviseHolder)
WINPORT_UUIDOF(IMessageFilter)
WINPORT_UUIDOF(IExternalConnection)
WINPORT_UUIDOF(IMarshal)

#endif
