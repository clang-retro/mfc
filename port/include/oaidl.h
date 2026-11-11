// oaidl.h - automation: variants, safe arrays, IDispatch and type
// information.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#ifndef _WINPORT_OAIDL_H
#define _WINPORT_OAIDL_H

#include <objidl.h>

interface IDispatch;
interface ITypeInfo;
interface ITypeLib;
interface ITypeComp;
interface IRecordInfo;
interface IEnumVARIANT;

extern "C" {
extern const IID IID_IDispatch, IID_ITypeInfo, IID_ITypeInfo2, IID_ITypeLib, IID_ITypeLib2, IID_ITypeComp,
    IID_IEnumVARIANT, IID_IErrorInfo, IID_ICreateErrorInfo, IID_ISupportErrorInfo, IID_IRecordInfo,
    IID_ICreateTypeInfo, IID_ICreateTypeLib, IID_ICreateTypeLib2, IID_ITypeChangeEvents, IID_IErrorLog,
    IID_IPropertyBag;
}

typedef struct tagSAFEARRAYBOUND {
  ULONG cElements;
  LONG lLbound;
} SAFEARRAYBOUND, *LPSAFEARRAYBOUND;
typedef struct tagSAFEARRAY {
  USHORT cDims;
  USHORT fFeatures;
  ULONG cbElements;
  ULONG cLocks;
  PVOID pvData;
  SAFEARRAYBOUND rgsabound[1];
} SAFEARRAY, *LPSAFEARRAY;
#define FADF_AUTO 0x0001
#define FADF_STATIC 0x0002
#define FADF_EMBEDDED 0x0004
#define FADF_FIXEDSIZE 0x0010
#define FADF_RECORD 0x0020
#define FADF_HAVEIID 0x0040
#define FADF_HAVEVARTYPE 0x0080
#define FADF_BSTR 0x0100
#define FADF_UNKNOWN 0x0200
#define FADF_DISPATCH 0x0400
#define FADF_VARIANT 0x0800
#define FADF_RESERVED 0xF008

enum VARENUM {
  VT_EMPTY = 0,
  VT_NULL = 1,
  VT_I2 = 2,
  VT_I4 = 3,
  VT_R4 = 4,
  VT_R8 = 5,
  VT_CY = 6,
  VT_DATE = 7,
  VT_BSTR = 8,
  VT_DISPATCH = 9,
  VT_ERROR = 10,
  VT_BOOL = 11,
  VT_VARIANT = 12,
  VT_UNKNOWN = 13,
  VT_DECIMAL = 14,
  VT_I1 = 16,
  VT_UI1 = 17,
  VT_UI2 = 18,
  VT_UI4 = 19,
  VT_I8 = 20,
  VT_UI8 = 21,
  VT_INT = 22,
  VT_UINT = 23,
  VT_VOID = 24,
  VT_HRESULT = 25,
  VT_PTR = 26,
  VT_SAFEARRAY = 27,
  VT_CARRAY = 28,
  VT_USERDEFINED = 29,
  VT_LPSTR = 30,
  VT_LPWSTR = 31,
  VT_RECORD = 36,
  VT_INT_PTR = 37,
  VT_UINT_PTR = 38,
  VT_FILETIME = 64,
  VT_BLOB = 65,
  VT_STREAM = 66,
  VT_STORAGE = 67,
  VT_STREAMED_OBJECT = 68,
  VT_STORED_OBJECT = 69,
  VT_BLOB_OBJECT = 70,
  VT_CF = 71,
  VT_CLSID = 72,
  VT_VERSIONED_STREAM = 73,
  VT_BSTR_BLOB = 0xfff,
  VT_VECTOR = 0x1000,
  VT_ARRAY = 0x2000,
  VT_BYREF = 0x4000,
  VT_RESERVED = 0x8000,
  VT_ILLEGAL = 0xffff,
  VT_ILLEGALMASKED = 0xfff,
  VT_TYPEMASK = 0xfff
};

typedef struct tagVARIANT VARIANT, *LPVARIANT, VARIANTARG, *LPVARIANTARG;
struct tagVARIANT {
  union {
    struct {
      VARTYPE vt;
      WORD wReserved1;
      WORD wReserved2;
      WORD wReserved3;
      union {
        LONGLONG llVal;
        LONG lVal;
        BYTE bVal;
        SHORT iVal;
        FLOAT fltVal;
        DOUBLE dblVal;
        VARIANT_BOOL boolVal;
        SCODE scode;
        CY cyVal;
        DATE date;
        BSTR bstrVal;
        IUnknown* punkVal;
        IDispatch* pdispVal;
        SAFEARRAY* parray;
        BYTE* pbVal;
        SHORT* piVal;
        LONG* plVal;
        LONGLONG* pllVal;
        FLOAT* pfltVal;
        DOUBLE* pdblVal;
        VARIANT_BOOL* pboolVal;
        SCODE* pscode;
        CY* pcyVal;
        DATE* pdate;
        BSTR* pbstrVal;
        IUnknown** ppunkVal;
        IDispatch** ppdispVal;
        SAFEARRAY** pparray;
        VARIANT* pvarVal;
        PVOID byref;
        CHAR cVal;
        USHORT uiVal;
        ULONG ulVal;
        ULONGLONG ullVal;
        INT intVal;
        UINT uintVal;
        DECIMAL* pdecVal;
        CHAR* pcVal;
        USHORT* puiVal;
        ULONG* pulVal;
        ULONGLONG* pullVal;
        INT* pintVal;
        UINT* puintVal;
        struct {
          PVOID pvRecord;
          IRecordInfo* pRecInfo;
        };
      };
    };
    DECIMAL decVal;
  };
};
#define V_VT(X) ((X)->vt)
#define V_ISBYREF(X) (V_VT(X) & VT_BYREF)
#define V_ISARRAY(X) (V_VT(X) & VT_ARRAY)
#define V_ISVECTOR(X) (V_VT(X) & VT_VECTOR)
#define V_NONE(X) V_I2(X)
#define V_UI1(X) ((X)->bVal)
#define V_UI1REF(X) ((X)->pbVal)
#define V_I2(X) ((X)->iVal)
#define V_I2REF(X) ((X)->piVal)
#define V_I4(X) ((X)->lVal)
#define V_I4REF(X) ((X)->plVal)
#define V_I8(X) ((X)->llVal)
#define V_R4(X) ((X)->fltVal)
#define V_R4REF(X) ((X)->pfltVal)
#define V_R8(X) ((X)->dblVal)
#define V_R8REF(X) ((X)->pdblVal)
#define V_I1(X) ((X)->cVal)
#define V_UI2(X) ((X)->uiVal)
#define V_UI4(X) ((X)->ulVal)
#define V_UI8(X) ((X)->ullVal)
#define V_INT(X) ((X)->intVal)
#define V_UINT(X) ((X)->uintVal)
#define V_CY(X) ((X)->cyVal)
#define V_CYREF(X) ((X)->pcyVal)
#define V_DATE(X) ((X)->date)
#define V_DATEREF(X) ((X)->pdate)
#define V_BSTR(X) ((X)->bstrVal)
#define V_BSTRREF(X) ((X)->pbstrVal)
#define V_DISPATCH(X) ((X)->pdispVal)
#define V_DISPATCHREF(X) ((X)->ppdispVal)
#define V_ERROR(X) ((X)->scode)
#define V_ERRORREF(X) ((X)->pscode)
#define V_BOOL(X) ((X)->boolVal)
#define V_BOOLREF(X) ((X)->pboolVal)
#define V_UNKNOWN(X) ((X)->punkVal)
#define V_UNKNOWNREF(X) ((X)->ppunkVal)
#define V_VARIANTREF(X) ((X)->pvarVal)
#define V_ARRAY(X) ((X)->parray)
#define V_ARRAYREF(X) ((X)->pparray)
#define V_BYREF(X) ((X)->byref)
#define V_DECIMAL(X) ((X)->decVal)
#define V_DECIMALREF(X) ((X)->pdecVal)

typedef struct tagDISPPARAMS {
  VARIANTARG* rgvarg;
  DISPID* rgdispidNamedArgs;
  UINT cArgs;
  UINT cNamedArgs;
} DISPPARAMS;
typedef struct tagEXCEPINFO {
  WORD wCode;
  WORD wReserved;
  BSTR bstrSource;
  BSTR bstrDescription;
  BSTR bstrHelpFile;
  DWORD dwHelpContext;
  PVOID pvReserved;
  HRESULT(STDMETHODCALLTYPE* pfnDeferredFillIn)(struct tagEXCEPINFO*);
  SCODE scode;
} EXCEPINFO, *LPEXCEPINFO;

#define DISPID_UNKNOWN (-1)
#define DISPID_VALUE 0
#define DISPID_PROPERTYPUT (-3)
#define DISPID_NEWENUM (-4)
#define DISPID_EVALUATE (-5)
#define DISPID_CONSTRUCTOR (-6)
#define DISPID_DESTRUCTOR (-7)
#define DISPID_COLLECT (-8)
#define DISPATCH_METHOD 0x1
#define DISPATCH_PROPERTYGET 0x2
#define DISPATCH_PROPERTYPUT 0x4
#define DISPATCH_PROPERTYPUTREF 0x8

typedef enum tagSYSKIND { SYS_WIN16 = 0, SYS_WIN32, SYS_MAC, SYS_WIN64 } SYSKIND;
typedef enum tagLIBFLAGS { LIBFLAG_FRESTRICTED = 1, LIBFLAG_FCONTROL = 2, LIBFLAG_FHIDDEN = 4, LIBFLAG_FHASDISKIMAGE = 8 } LIBFLAGS;
typedef enum tagTYPEKIND {
  TKIND_ENUM = 0,
  TKIND_RECORD,
  TKIND_MODULE,
  TKIND_INTERFACE,
  TKIND_DISPATCH,
  TKIND_COCLASS,
  TKIND_ALIAS,
  TKIND_UNION,
  TKIND_MAX
} TYPEKIND;
typedef struct tagTYPEDESC {
  union {
    struct tagTYPEDESC* lptdesc;
    struct tagARRAYDESC* lpadesc;
    HREFTYPE hreftype;
  };
  VARTYPE vt;
} TYPEDESC;
typedef struct tagARRAYDESC {
  TYPEDESC tdescElem;
  USHORT cDims;
  SAFEARRAYBOUND rgbounds[1];
} ARRAYDESC;
typedef struct tagPARAMDESCEX {
  ULONG cBytes;
  VARIANTARG varDefaultValue;
} PARAMDESCEX, *LPPARAMDESCEX;
typedef struct tagPARAMDESC {
  LPPARAMDESCEX pparamdescex;
  USHORT wParamFlags;
} PARAMDESC, *LPPARAMDESC;
#define PARAMFLAG_NONE 0x00
#define PARAMFLAG_FIN 0x01
#define PARAMFLAG_FOUT 0x02
#define PARAMFLAG_FLCID 0x04
#define PARAMFLAG_FRETVAL 0x08
#define PARAMFLAG_FOPT 0x10
#define PARAMFLAG_FHASDEFAULT 0x20
#define PARAMFLAG_FHASCUSTDATA 0x40
typedef struct tagIDLDESC {
  ULONG_PTR dwReserved;
  USHORT wIDLFlags;
} IDLDESC, *LPIDLDESC;
#define IDLFLAG_NONE PARAMFLAG_NONE
#define IDLFLAG_FIN PARAMFLAG_FIN
#define IDLFLAG_FOUT PARAMFLAG_FOUT
#define IDLFLAG_FLCID PARAMFLAG_FLCID
#define IDLFLAG_FRETVAL PARAMFLAG_FRETVAL
typedef struct tagELEMDESC {
  TYPEDESC tdesc;
  union {
    IDLDESC idldesc;
    PARAMDESC paramdesc;
  };
} ELEMDESC, *LPELEMDESC;
typedef struct tagTYPEATTR {
  GUID guid;
  LCID lcid;
  DWORD dwReserved;
  MEMBERID memidConstructor;
  MEMBERID memidDestructor;
  LPOLESTR lpstrSchema;
  ULONG cbSizeInstance;
  TYPEKIND typekind;
  WORD cFuncs;
  WORD cVars;
  WORD cImplTypes;
  WORD cbSizeVft;
  WORD cbAlignment;
  WORD wTypeFlags;
  WORD wMajorVerNum;
  WORD wMinorVerNum;
  TYPEDESC tdescAlias;
  IDLDESC idldescType;
} TYPEATTR, *LPTYPEATTR;
typedef enum tagFUNCKIND { FUNC_VIRTUAL = 0, FUNC_PUREVIRTUAL, FUNC_NONVIRTUAL, FUNC_STATIC, FUNC_DISPATCH } FUNCKIND;
typedef enum tagINVOKEKIND {
  INVOKE_FUNC = 1,
  INVOKE_PROPERTYGET = 2,
  INVOKE_PROPERTYPUT = 4,
  INVOKE_PROPERTYPUTREF = 8
} INVOKEKIND;
typedef enum tagCALLCONV {
  CC_FASTCALL = 0,
  CC_CDECL = 1,
  CC_MSCPASCAL,
  CC_PASCAL = CC_MSCPASCAL,
  CC_MACPASCAL,
  CC_STDCALL,
  CC_FPFASTCALL,
  CC_SYSCALL,
  CC_MPWCDECL,
  CC_MPWPASCAL,
  CC_MAX
} CALLCONV;
typedef struct tagFUNCDESC {
  MEMBERID memid;
  SCODE* lprgscode;
  ELEMDESC* lprgelemdescParam;
  FUNCKIND funckind;
  INVOKEKIND invkind;
  CALLCONV callconv;
  SHORT cParams;
  SHORT cParamsOpt;
  SHORT oVft;
  SHORT cScodes;
  ELEMDESC elemdescFunc;
  WORD wFuncFlags;
} FUNCDESC, *LPFUNCDESC;
typedef enum tagVARKIND { VAR_PERINSTANCE = 0, VAR_STATIC, VAR_CONST, VAR_DISPATCH } VARKIND;
typedef struct tagVARDESC {
  MEMBERID memid;
  LPOLESTR lpstrSchema;
  union {
    ULONG oInst;
    VARIANT* lpvarValue;
  };
  ELEMDESC elemdescVar;
  WORD wVarFlags;
  VARKIND varkind;
} VARDESC, *LPVARDESC;
typedef enum tagTYPEFLAGS {
  TYPEFLAG_FAPPOBJECT = 0x1,
  TYPEFLAG_FCANCREATE = 0x2,
  TYPEFLAG_FLICENSED = 0x4,
  TYPEFLAG_FPREDECLID = 0x8,
  TYPEFLAG_FHIDDEN = 0x10,
  TYPEFLAG_FCONTROL = 0x20,
  TYPEFLAG_FDUAL = 0x40,
  TYPEFLAG_FNONEXTENSIBLE = 0x80,
  TYPEFLAG_FOLEAUTOMATION = 0x100,
  TYPEFLAG_FRESTRICTED = 0x200,
  TYPEFLAG_FAGGREGATABLE = 0x400,
  TYPEFLAG_FREPLACEABLE = 0x800,
  TYPEFLAG_FDISPATCHABLE = 0x1000,
  TYPEFLAG_FREVERSEBIND = 0x2000,
  TYPEFLAG_FPROXY = 0x4000
} TYPEFLAGS;
typedef enum tagFUNCFLAGS {
  FUNCFLAG_FRESTRICTED = 0x1,
  FUNCFLAG_FSOURCE = 0x2,
  FUNCFLAG_FBINDABLE = 0x4,
  FUNCFLAG_FREQUESTEDIT = 0x8,
  FUNCFLAG_FDISPLAYBIND = 0x10,
  FUNCFLAG_FDEFAULTBIND = 0x20,
  FUNCFLAG_FHIDDEN = 0x40,
  FUNCFLAG_FUSESGETLASTERROR = 0x80,
  FUNCFLAG_FDEFAULTCOLLELEM = 0x100,
  FUNCFLAG_FUIDEFAULT = 0x200,
  FUNCFLAG_FNONBROWSABLE = 0x400,
  FUNCFLAG_FREPLACEABLE = 0x800,
  FUNCFLAG_FIMMEDIATEBIND = 0x1000
} FUNCFLAGS;
typedef enum tagVARFLAGS {
  VARFLAG_FREADONLY = 0x1,
  VARFLAG_FSOURCE = 0x2,
  VARFLAG_FBINDABLE = 0x4,
  VARFLAG_FREQUESTEDIT = 0x8,
  VARFLAG_FDISPLAYBIND = 0x10,
  VARFLAG_FDEFAULTBIND = 0x20,
  VARFLAG_FHIDDEN = 0x40,
  VARFLAG_FRESTRICTED = 0x80,
  VARFLAG_FDEFAULTCOLLELEM = 0x100,
  VARFLAG_FUIDEFAULT = 0x200,
  VARFLAG_FNONBROWSABLE = 0x400,
  VARFLAG_FREPLACEABLE = 0x800,
  VARFLAG_FIMMEDIATEBIND = 0x1000
} VARFLAGS;
#define IMPLTYPEFLAG_FDEFAULT 0x1
#define IMPLTYPEFLAG_FSOURCE 0x2
#define IMPLTYPEFLAG_FRESTRICTED 0x4
#define IMPLTYPEFLAG_FDEFAULTVTABLE 0x8
typedef struct tagTLIBATTR {
  GUID guid;
  LCID lcid;
  SYSKIND syskind;
  WORD wMajorVerNum;
  WORD wMinorVerNum;
  WORD wLibFlags;
} TLIBATTR, *LPTLIBATTR;
typedef enum tagDESCKIND {
  DESCKIND_NONE = 0,
  DESCKIND_FUNCDESC,
  DESCKIND_VARDESC,
  DESCKIND_TYPECOMP,
  DESCKIND_IMPLICITAPPOBJ,
  DESCKIND_MAX
} DESCKIND;
typedef union tagBINDPTR {
  FUNCDESC* lpfuncdesc;
  VARDESC* lpvardesc;
  ITypeComp* lptcomp;
} BINDPTR, *LPBINDPTR;

interface IDispatch : public IUnknown {
  STDMETHOD(GetTypeInfoCount)(UINT* pctinfo) = 0;
  STDMETHOD(GetTypeInfo)(UINT iTInfo, LCID lcid, ITypeInfo** ppTInfo) = 0;
  STDMETHOD(GetIDsOfNames)(REFIID riid, LPOLESTR* rgszNames, UINT cNames, LCID lcid, DISPID* rgDispId) = 0;
  STDMETHOD(Invoke)(DISPID dispIdMember, REFIID riid, LCID lcid, WORD wFlags, DISPPARAMS* pDispParams,
                    VARIANT* pVarResult, EXCEPINFO* pExcepInfo, UINT* puArgErr) = 0;
};
typedef IDispatch* LPDISPATCH;

interface ITypeComp : public IUnknown {
  STDMETHOD(Bind)(LPOLESTR szName, ULONG lHashVal, WORD wFlags, ITypeInfo** ppTInfo, DESCKIND* pDescKind,
                  BINDPTR* pBindPtr) = 0;
  STDMETHOD(BindType)(LPOLESTR szName, ULONG lHashVal, ITypeInfo** ppTInfo, ITypeComp** ppTComp) = 0;
};
typedef ITypeComp* LPTYPECOMP;

interface ITypeInfo : public IUnknown {
  STDMETHOD(GetTypeAttr)(TYPEATTR** ppTypeAttr) = 0;
  STDMETHOD(GetTypeComp)(ITypeComp** ppTComp) = 0;
  STDMETHOD(GetFuncDesc)(UINT index, FUNCDESC** ppFuncDesc) = 0;
  STDMETHOD(GetVarDesc)(UINT index, VARDESC** ppVarDesc) = 0;
  STDMETHOD(GetNames)(MEMBERID memid, BSTR* rgBstrNames, UINT cMaxNames, UINT* pcNames) = 0;
  STDMETHOD(GetRefTypeOfImplType)(UINT index, HREFTYPE* pRefType) = 0;
  STDMETHOD(GetImplTypeFlags)(UINT index, INT* pImplTypeFlags) = 0;
  STDMETHOD(GetIDsOfNames)(LPOLESTR* rgszNames, UINT cNames, MEMBERID* pMemId) = 0;
  STDMETHOD(Invoke)(PVOID pvInstance, MEMBERID memid, WORD wFlags, DISPPARAMS* pDispParams, VARIANT* pVarResult,
                    EXCEPINFO* pExcepInfo, UINT* puArgErr) = 0;
  STDMETHOD(GetDocumentation)(MEMBERID memid, BSTR* pBstrName, BSTR* pBstrDocString, DWORD* pdwHelpContext,
                              BSTR* pBstrHelpFile) = 0;
  STDMETHOD(GetDllEntry)(MEMBERID memid, INVOKEKIND invKind, BSTR* pBstrDllName, BSTR* pBstrName,
                         WORD* pwOrdinal) = 0;
  STDMETHOD(GetRefTypeInfo)(HREFTYPE hRefType, ITypeInfo** ppTInfo) = 0;
  STDMETHOD(AddressOfMember)(MEMBERID memid, INVOKEKIND invKind, PVOID* ppv) = 0;
  STDMETHOD(CreateInstance)(IUnknown* pUnkOuter, REFIID riid, PVOID* ppvObj) = 0;
  STDMETHOD(GetMops)(MEMBERID memid, BSTR* pBstrMops) = 0;
  STDMETHOD(GetContainingTypeLib)(ITypeLib** ppTLib, UINT* pIndex) = 0;
  STDMETHOD_(void, ReleaseTypeAttr)(TYPEATTR* pTypeAttr) = 0;
  STDMETHOD_(void, ReleaseFuncDesc)(FUNCDESC* pFuncDesc) = 0;
  STDMETHOD_(void, ReleaseVarDesc)(VARDESC* pVarDesc) = 0;
};
typedef ITypeInfo* LPTYPEINFO;

interface ITypeLib : public IUnknown {
  STDMETHOD_(UINT, GetTypeInfoCount)() = 0;
  STDMETHOD(GetTypeInfo)(UINT index, ITypeInfo** ppTInfo) = 0;
  STDMETHOD(GetTypeInfoType)(UINT index, TYPEKIND* pTKind) = 0;
  STDMETHOD(GetTypeInfoOfGuid)(REFGUID guid, ITypeInfo** ppTinfo) = 0;
  STDMETHOD(GetLibAttr)(TLIBATTR** ppTLibAttr) = 0;
  STDMETHOD(GetTypeComp)(ITypeComp** ppTComp) = 0;
  STDMETHOD(GetDocumentation)(INT index, BSTR* pBstrName, BSTR* pBstrDocString, DWORD* pdwHelpContext,
                              BSTR* pBstrHelpFile) = 0;
  STDMETHOD(IsName)(LPOLESTR szNameBuf, ULONG lHashVal, BOOL* pfName) = 0;
  STDMETHOD(FindName)(LPOLESTR szNameBuf, ULONG lHashVal, ITypeInfo** ppTInfo, MEMBERID* rgMemId,
                      USHORT* pcFound) = 0;
  STDMETHOD_(void, ReleaseTLibAttr)(TLIBATTR* pTLibAttr) = 0;
};
typedef ITypeLib* LPTYPELIB;

interface IEnumVARIANT : public IUnknown {
  STDMETHOD(Next)(ULONG celt, VARIANT* rgVar, ULONG* pCeltFetched) = 0;
  STDMETHOD(Skip)(ULONG celt) = 0;
  STDMETHOD(Reset)() = 0;
  STDMETHOD(Clone)(IEnumVARIANT** ppEnum) = 0;
};
typedef IEnumVARIANT* LPENUMVARIANT;

interface IErrorInfo : public IUnknown {
  STDMETHOD(GetGUID)(GUID* pGUID) = 0;
  STDMETHOD(GetSource)(BSTR* pBstrSource) = 0;
  STDMETHOD(GetDescription)(BSTR* pBstrDescription) = 0;
  STDMETHOD(GetHelpFile)(BSTR* pBstrHelpFile) = 0;
  STDMETHOD(GetHelpContext)(DWORD* pdwHelpContext) = 0;
};
typedef IErrorInfo* LPERRORINFO;
interface ICreateErrorInfo : public IUnknown {
  STDMETHOD(SetGUID)(REFGUID rguid) = 0;
  STDMETHOD(SetSource)(LPOLESTR szSource) = 0;
  STDMETHOD(SetDescription)(LPOLESTR szDescription) = 0;
  STDMETHOD(SetHelpFile)(LPOLESTR szHelpFile) = 0;
  STDMETHOD(SetHelpContext)(DWORD dwHelpContext) = 0;
};
typedef ICreateErrorInfo* LPCREATEERRORINFO;
interface ISupportErrorInfo : public IUnknown {
  STDMETHOD(InterfaceSupportsErrorInfo)(REFIID riid) = 0;
};
typedef ISupportErrorInfo* LPSUPPORTERRORINFO;

interface IErrorLog : public IUnknown {
  STDMETHOD(AddError)(LPCOLESTR pszPropName, EXCEPINFO* pExcepInfo) = 0;
};
typedef IErrorLog* LPERRORLOG;
interface IPropertyBag : public IUnknown {
  STDMETHOD(Read)(LPCOLESTR pszPropName, VARIANT* pVar, IErrorLog* pErrorLog) = 0;
  STDMETHOD(Write)(LPCOLESTR pszPropName, VARIANT* pVar) = 0;
};
typedef IPropertyBag* LPPROPERTYBAG;

interface IRecordInfo : public IUnknown {
  STDMETHOD(RecordInit)(PVOID pvNew) = 0;
  STDMETHOD(RecordClear)(PVOID pvExisting) = 0;
  STDMETHOD(RecordCopy)(PVOID pvExisting, PVOID pvNew) = 0;
  STDMETHOD(GetGuid)(GUID* pguid) = 0;
  STDMETHOD(GetName)(BSTR* pbstrName) = 0;
  STDMETHOD(GetSize)(ULONG* pcbSize) = 0;
  STDMETHOD(GetTypeInfo)(ITypeInfo** ppTypeInfo) = 0;
  STDMETHOD(GetField)(PVOID pvData, LPCOLESTR szFieldName, VARIANT* pvarField) = 0;
  STDMETHOD(GetFieldNoCopy)(PVOID pvData, LPCOLESTR szFieldName, VARIANT* pvarField, PVOID* ppvDataCArray) = 0;
  STDMETHOD(PutField)(ULONG wFlags, PVOID pvData, LPCOLESTR szFieldName, VARIANT* pvarField) = 0;
  STDMETHOD(PutFieldNoCopy)(ULONG wFlags, PVOID pvData, LPCOLESTR szFieldName, VARIANT* pvarField) = 0;
  STDMETHOD(GetFieldNames)(ULONG* pcNames, BSTR* rgBstrNames) = 0;
  STDMETHOD_(BOOL, IsMatchingType)(IRecordInfo* pRecordInfo) = 0;
  STDMETHOD_(PVOID, RecordCreate)() = 0;
  STDMETHOD(RecordCreateCopy)(PVOID pvSource, PVOID* ppvDest) = 0;
  STDMETHOD(RecordDestroy)(PVOID pvRecord) = 0;
};

WINPORT_UUIDOF(IDispatch)
WINPORT_UUIDOF(ITypeComp)
WINPORT_UUIDOF(ITypeInfo)
WINPORT_UUIDOF(ITypeLib)
WINPORT_UUIDOF(IEnumVARIANT)
WINPORT_UUIDOF(IErrorInfo)
WINPORT_UUIDOF(ICreateErrorInfo)
WINPORT_UUIDOF(ISupportErrorInfo)
WINPORT_UUIDOF(IErrorLog)
WINPORT_UUIDOF(IPropertyBag)
WINPORT_UUIDOF(IRecordInfo)

#endif
