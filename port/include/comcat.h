// comcat.h - component categories: the categories a class implements or
// needs, as the registry keeps them.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#ifndef _WINPORT_COMCAT_H
#define _WINPORT_COMCAT_H

#include <objidl.h>

typedef GUID CATID;
typedef REFGUID REFCATID;

typedef struct tagCATEGORYINFO {
  CATID catid;
  LCID lcid;
  OLECHAR szDescription[128];
} CATEGORYINFO, *LPCATEGORYINFO;

#ifdef __cplusplus
interface IEnumGUID;
#else
typedef struct IEnumGUID IEnumGUID;
#endif
#ifdef __cplusplus
interface IEnumCATEGORYINFO;
#else
typedef struct IEnumCATEGORYINFO IEnumCATEGORYINFO;
#endif
#ifdef __cplusplus
interface ICatRegister;
#else
typedef struct ICatRegister ICatRegister;
#endif
#ifdef __cplusplus
interface ICatInformation;
#else
typedef struct ICatInformation ICatInformation;
#endif
typedef IEnumGUID IEnumCATID;
typedef IEnumGUID IEnumCLSID;

#ifdef __cplusplus
extern "C" {
#endif
extern const IID IID_IEnumGUID, IID_IEnumCATEGORYINFO, IID_ICatRegister, IID_ICatInformation;
extern const CLSID CLSID_StdComponentCategoriesMgr;
extern const CATID CATID_Insertable, CATID_Control, CATID_Programmable, CATID_IsShortcut,
    CATID_NeverShowExt, CATID_DocObject, CATID_Printable, CATID_RequiresDataPathHost,
    CATID_PersistsToMoniker, CATID_PersistsToStorage, CATID_PersistsToStreamInit,
    CATID_PersistsToStream, CATID_PersistsToMemory, CATID_PersistsToFile,
    CATID_PersistsToPropertyBag, CATID_InternetAware,
    CATID_SafeForScripting, CATID_SafeForInitializing;
#ifdef __cplusplus
}  // extern "C"
#endif
#define IID_IEnumCATID IID_IEnumGUID
#define IID_IEnumCLSID IID_IEnumGUID

#ifdef __cplusplus
interface IEnumGUID : public IUnknown {
  STDMETHOD(Next)(ULONG celt, GUID* rgelt, ULONG* pceltFetched) = 0;
  STDMETHOD(Skip)(ULONG celt) = 0;
  STDMETHOD(Reset)() = 0;
  STDMETHOD(Clone)(IEnumGUID** ppenum) = 0;
};
#else
typedef struct IEnumGUID IEnumGUID;
#endif
typedef IEnumGUID* LPENUMGUID;
typedef IEnumGUID* LPENUMCATID;
typedef IEnumGUID* LPENUMCLSID;

#ifdef __cplusplus
interface IEnumCATEGORYINFO : public IUnknown {
  STDMETHOD(Next)(ULONG celt, CATEGORYINFO* rgelt, ULONG* pceltFetched) = 0;
  STDMETHOD(Skip)(ULONG celt) = 0;
  STDMETHOD(Reset)() = 0;
  STDMETHOD(Clone)(IEnumCATEGORYINFO** ppenum) = 0;
};
#else
typedef struct IEnumCATEGORYINFO IEnumCATEGORYINFO;
#endif
typedef IEnumCATEGORYINFO* LPENUMCATEGORYINFO;

#ifdef __cplusplus
interface ICatRegister : public IUnknown {
  STDMETHOD(RegisterCategories)(ULONG cCategories, CATEGORYINFO rgCategoryInfo[]) = 0;
  STDMETHOD(UnRegisterCategories)(ULONG cCategories, CATID rgcatid[]) = 0;
  STDMETHOD(RegisterClassImplCategories)(REFCLSID rclsid, ULONG cCategories, CATID rgcatid[]) = 0;
  STDMETHOD(UnRegisterClassImplCategories)(REFCLSID rclsid, ULONG cCategories, CATID rgcatid[]) = 0;
  STDMETHOD(RegisterClassReqCategories)(REFCLSID rclsid, ULONG cCategories, CATID rgcatid[]) = 0;
  STDMETHOD(UnRegisterClassReqCategories)(REFCLSID rclsid, ULONG cCategories, CATID rgcatid[]) = 0;
};
#else
typedef struct ICatRegister ICatRegister;
#endif
typedef ICatRegister* LPCATREGISTER;

#ifdef __cplusplus
interface ICatInformation : public IUnknown {
  STDMETHOD(EnumCategories)(LCID lcid, IEnumCATEGORYINFO** ppenumCategoryInfo) = 0;
  STDMETHOD(GetCategoryDesc)(REFCATID rcatid, LCID lcid, LPWSTR* pszDesc) = 0;
  STDMETHOD(EnumClassesOfCategories)(ULONG cImplemented, const CATID rgcatidImpl[], ULONG cRequired,
                                     const CATID rgcatidReq[], IEnumGUID** ppenumClsid) = 0;
  STDMETHOD(IsClassOfCategories)(REFCLSID rclsid, ULONG cImplemented, const CATID rgcatidImpl[],
                                 ULONG cRequired, const CATID rgcatidReq[]) = 0;
  STDMETHOD(EnumImplCategoriesOfClass)(REFCLSID rclsid, IEnumGUID** ppenumCatid) = 0;
  STDMETHOD(EnumReqCategoriesOfClass)(REFCLSID rclsid, IEnumGUID** ppenumCatid) = 0;
};
#else
typedef struct ICatInformation ICatInformation;
#endif
typedef ICatInformation* LPCATINFORMATION;

WINPORT_UUIDOF(IEnumGUID)
WINPORT_UUIDOF(IEnumCATEGORYINFO)
WINPORT_UUIDOF(ICatRegister)
WINPORT_UUIDOF(ICatInformation)

#endif
