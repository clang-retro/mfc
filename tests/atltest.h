// atltest.h - the interfaces of the ATL test server (atltest-dll) and
// its client (atltest), as MIDL would write them.
#pragma once

// The DLL's resources.
#define IDR_ATLTHING 101
#define IDS_FAIL 102
#define IDS_SINGLE 103

#ifndef RC_INVOKED
#include <windows.h>
#include <ole2.h>

// {B0378114-0AD3-4EBE-AC9F-E195A42D4E79}
static const GUID LIBID_AtlTestLib = {
    0xb0378114, 0x0ad3, 0x4ebe, {0xac, 0x9f, 0xe1, 0x95, 0xa4, 0x2d, 0x4e, 0x79}};
// {1DF77E1F-DA45-421A-BABF-24B927659DF9}
static const CLSID CLSID_AtlThing = {
    0x1df77e1f, 0xda45, 0x421a, {0xba, 0xbf, 0x24, 0xb9, 0x27, 0x65, 0x9d, 0xf9}};
// {857BF9B8-3ED8-4DEF-9290-1A92ACBCDD34}
static const CLSID CLSID_AtlSingle = {
    0x857bf9b8, 0x3ed8, 0x4def, {0x92, 0x90, 0x1a, 0x92, 0xac, 0xbc, 0xdd, 0x34}};

// The dual interface of AtlTest.Thing.
struct IAtlThing : public IDispatch {
  STDMETHOD(get_Value)(long* pVal) = 0;          // [id(1), propget]
  STDMETHOD(put_Value)(long newVal) = 0;         // [id(1), propput]
  STDMETHOD(Add)(long a, long* pResult) = 0;     // [id(2)], [out, retval]
  STDMETHOD(Fail)(long nKind) = 0;               // [id(3)]
  STDMETHOD(Fire)(long n) = 0;                   // [id(4)]
  STDMETHOD(get_Name)(BSTR* pVal) = 0;           // [id(5), propget]
};
__CRT_UUID_DECL(IAtlThing, 0x7621cf74, 0x79c1, 0x4b45, 0x8d, 0x08, 0xa0, 0xbc, 0x5d, 0xfc, 0xfc, 0x1e)
#define IID_IAtlThing __uuidof(IAtlThing)

// Its events: [id(1)] void Fired(long n).
struct _IAtlThingEvents : public IDispatch {};
__CRT_UUID_DECL(_IAtlThingEvents, 0xe62090eb, 0x4c73, 0x4ba2, 0x96, 0x88, 0xd9, 0x15, 0x73, 0x75, 0xbf, 0xc7)
#define DIID__IAtlThingEvents __uuidof(_IAtlThingEvents)

// A tear-off of AtlTest.Thing, and the interface of AtlTest.Single.
struct IAtlRare : public IUnknown {
  STDMETHOD(Magic)(long* p) = 0;
};
__CRT_UUID_DECL(IAtlRare, 0x422722b1, 0xc63c, 0x491a, 0xaf, 0xb4, 0x9c, 0xac, 0xe3, 0xce, 0xcf, 0xaa)
#define IID_IAtlRare __uuidof(IAtlRare)

// A cached tear-off of AtlTest.Thing.
struct IAtlCached : public IUnknown {
  STDMETHOD(Magic)(long* p) = 0;
};
__CRT_UUID_DECL(IAtlCached, 0x23c7d4df, 0xd0f6, 0x42a6, 0x8d, 0x4c, 0x55, 0x5f, 0x64, 0x77, 0xc9, 0xce)
#define IID_IAtlCached __uuidof(IAtlCached)

#endif  // RC_INVOKED
