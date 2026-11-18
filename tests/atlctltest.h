// atlctltest.h - the controls of atlctltest, as MIDL would describe
// them, and the ids of its resources.
#pragma once

#define IDD_AXDLG 300
#define IDD_AXDLGEX 301
#define IDD_COMPOSITE 302
#define IDD_PAGE 303
#define IDC_CTL 1001
#define IDC_LIC 1002
#define IDC_LABEL 1003
#define IDC_INNER 1004
#define IDC_PAGEEDIT 1005
#define IDS_PAGETITLE 310

#ifndef RC_INVOKED
#include <windows.h>
#include <ole2.h>
#include <olectl.h>

// {6A1F0C2E-3B5D-4E7A-9C11-2F8D5E0B7A31}
static const GUID LIBID_AtlCtlLib = {
    0x6a1f0c2e, 0x3b5d, 0x4e7a, {0x9c, 0x11, 0x2f, 0x8d, 0x5e, 0x0b, 0x7a, 0x31}};
// The controls: windowed, windowless, licensed, composite; and a page.
static const CLSID CLSID_CtlTest = {
    0x6a1f0c2e, 0x3b5d, 0x4e7a, {0x9c, 0x11, 0x2f, 0x8d, 0x5e, 0x0b, 0x7a, 0x34}};
static const CLSID CLSID_CtlLess = {
    0x6a1f0c2e, 0x3b5d, 0x4e7a, {0x9c, 0x11, 0x2f, 0x8d, 0x5e, 0x0b, 0x7a, 0x35}};
static const CLSID CLSID_CtlLic = {
    0x6a1f0c2e, 0x3b5d, 0x4e7a, {0x9c, 0x11, 0x2f, 0x8d, 0x5e, 0x0b, 0x7a, 0x36}};
static const CLSID CLSID_CtlComp = {
    0x6a1f0c2e, 0x3b5d, 0x4e7a, {0x9c, 0x11, 0x2f, 0x8d, 0x5e, 0x0b, 0x7a, 0x37}};
static const CLSID CLSID_CtlPage = {
    0x6a1f0c2e, 0x3b5d, 0x4e7a, {0x9c, 0x11, 0x2f, 0x8d, 0x5e, 0x0b, 0x7a, 0x38}};

// interface ICtlTest : IDispatch (dual)
struct ICtlTest : public IDispatch {
  STDMETHOD(put_FillColor)(OLE_COLOR clr) = 0;  // [id(DISPID_FILLCOLOR), propput]
  STDMETHOD(get_FillColor)(OLE_COLOR* pclr) = 0;
  STDMETHOD(put_Caption)(BSTR bstr) = 0;        // [id(DISPID_CAPTION), propput]
  STDMETHOD(get_Caption)(BSTR* pbstr) = 0;
  STDMETHOD(put_Sides)(long n) = 0;             // [id(1), propput]
  STDMETHOD(get_Sides)(long* pn) = 0;
  STDMETHOD(Click)(long n) = 0;                 // [id(2)]: fires Clicked(n)
};
__CRT_UUID_DECL(ICtlTest, 0x6a1f0c2e, 0x3b5d, 0x4e7a, 0x9c, 0x11, 0x2f, 0x8d, 0x5e, 0x0b, 0x7a, 0x32)
#define IID_ICtlTest __uuidof(ICtlTest)

// dispinterface _ICtlTestEvents { [id(1)] void Clicked(long n); }
struct _ICtlTestEvents : public IDispatch {};
__CRT_UUID_DECL(_ICtlTestEvents, 0x6a1f0c2e, 0x3b5d, 0x4e7a, 0x9c, 0x11, 0x2f, 0x8d, 0x5e, 0x0b, 0x7a,
                0x33)
#define DIID__ICtlTestEvents __uuidof(_ICtlTestEvents)

#endif  // RC_INVOKED
