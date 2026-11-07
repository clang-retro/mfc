// afxctl.h - writing ActiveX controls: COleControl, its event maps, stock
// properties, methods and events, property persistence (CPropExchange
// and the PX_ functions), CFontHolder, COleControlModule, and the
// factory and registration helpers.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
#ifndef __AFXCTL_H__
#define __AFXCTL_H__

#include "afxwin.h"
#include "afxext.h"
#include "afxdisp.h"
#include <olectl.h>
#include <ocidl.h>

class COleControl;
class CPropExchange;
class CFontHolder;

#ifndef DISPID_READYSTATE
#define DISPID_READYSTATE (-525)
#endif
#ifndef DISPID_READYSTATECHANGE
#define DISPID_READYSTATECHANGE (-609)
#endif

// The parameters of an event, for FireEvent: a VTS_ string.
#define EVENT_PARAM(vtsParams) (const BYTE*)(vtsParams)

// Stock event arguments.
#define LEFT_BUTTON 0x01
#define RIGHT_BUTTON 0x02
#define MIDDLE_BUTTON 0x04
#define SHIFT_MASK 0x01
#define CTRL_MASK 0x02
#define ALT_MASK 0x04

// ---------------------------------------------------------------------
// COleControlModule: the application object of a control's DLL.

class COleControlModule : public CWinApp {
  DECLARE_DYNAMIC(COleControlModule)
public:
  COleControlModule(LPCTSTR lpszAppName = NULL) : CWinApp(lpszAppName) {}
  virtual BOOL InitInstance();
  virtual int ExitInstance();
};

// ---------------------------------------------------------------------
// Class factories of controls. DECLARE_OLECREATE_EX gives the class a
// factory whose UpdateRegistry the program writes (usually with
// AfxOleRegisterControlClass), and GetClassID.

#define BEGIN_OLEFACTORY(class_name)                                                    \
protected:                                                                              \
  class class_name##Factory : public COleObjectFactoryEx {                              \
  public:                                                                               \
    class_name##Factory(REFCLSID clsid, CRuntimeClass* pRuntimeClass, BOOL bMultiInstance, \
                        LPCTSTR lpszProgID)                                             \
        : COleObjectFactoryEx(clsid, pRuntimeClass, bMultiInstance, lpszProgID) {}      \
    virtual BOOL UpdateRegistry(BOOL);

#define END_OLEFACTORY(class_name)      \
  };                                    \
  friend class class_name##Factory;     \
  static class_name##Factory factory;   \
                                        \
public:                                 \
  static const GUID guid;               \
  virtual HRESULT GetClassID(LPCLSID pclsid);

#define DECLARE_OLECREATE_EX(class_name) \
  BEGIN_OLEFACTORY(class_name)           \
  END_OLEFACTORY(class_name)

#define IMPLEMENT_OLECREATE_EX(class_name, external_name, l, w1, w2, b1, b2, b3, b4, b5, b6, \
                               b7, b8)                                                       \
  const GUID class_name::guid = {l, w1, w2, {b1, b2, b3, b4, b5, b6, b7, b8}};               \
  class_name::class_name##Factory class_name::factory(class_name::guid,                      \
                                                      RUNTIME_CLASS(class_name), FALSE,      \
                                                      _T(external_name));                    \
  HRESULT class_name::GetClassID(LPCLSID pclsid) {                                           \
    *pclsid = guid;                                                                          \
    return S_OK;                                                                             \
  }

// The user type name (a string resource) and OLEMISC_ flags of a control.
#define DECLARE_OLECTLTYPE(class_name)     \
  virtual UINT GetUserTypeNameID();        \
  virtual DWORD GetMiscStatus();

#define IMPLEMENT_OLECTLTYPE(class_name, idsUserTypeName, dwOleMisc) \
  UINT class_name::GetUserTypeNameID() { return idsUserTypeName; }   \
  DWORD class_name::GetMiscStatus() { return dwOleMisc; }

// The CLSIDs of a control's property pages.
#define DECLARE_PROPPAGEIDS(class_name) \
protected:                              \
  virtual LPCLSID GetPropPageIDs(ULONG& cPropPages);

#define BEGIN_PROPPAGEIDS(class_name, count)              \
  static CLSID _afxPropPageIDs_##class_name[(count) + 1]; \
  static int _afxPropPageCount_##class_name = -1;         \
  LPCLSID class_name::GetPropPageIDs(ULONG& cPropPages) { \
    int& _nPages = _afxPropPageCount_##class_name;        \
    LPCLSID _pIDs = _afxPropPageIDs_##class_name;         \
    const int _nMax = (count);                            \
    if (_nPages == -1) {                                  \
      _nPages = 0;
#define PROPPAGEID(clsid) \
  if (_nPages < _nMax) _pIDs[_nPages++] = clsid;
#define END_PROPPAGEIDS(class_name) \
    }                               \
    cPropPages = (ULONG)_nPages;    \
    return _pIDs;                   \
  }

// Registration (ctlreg.cpp).
BOOL AFXAPI AfxOleRegisterControlClass(HINSTANCE hInstance, REFCLSID clsid, LPCTSTR pszProgID,
                                       UINT idTypeName, UINT idBitmap, int nRegFlags,
                                       DWORD dwMiscStatus, REFGUID tlid, WORD wVerMajor,
                                       WORD wVerMinor);
BOOL AFXAPI AfxOleUnregisterClass(REFCLSID clsid, LPCTSTR pszProgID);
BOOL AFXAPI AfxOleRegisterPropertyPageClass(HINSTANCE hInstance, REFCLSID clsid, UINT idTypeName,
                                            int nRegFlags = afxRegApartmentThreading);

// ---------------------------------------------------------------------
// Event maps: the events a control fires, by name and dispatch ID. The
// stock events listed here are the ones the control fires by itself.

struct AFX_EVENTMAP_ENTRY {
  LPCTSTR lpszName;
  DISPID dispid;
  LPCSTR lpszParams;  // VTS_ string
};

struct AFX_EVENTMAP {
  const AFX_EVENTMAP* (PASCAL* pfnGetBaseMap)();
  const AFX_EVENTMAP_ENTRY* lpEntries;
};

#define DECLARE_EVENT_MAP()                                \
protected:                                                 \
  static const AFX_EVENTMAP* PASCAL GetThisEventMap();     \
  virtual const AFX_EVENTMAP* GetEventMap() const;

#define BEGIN_EVENT_MAP(theClass, baseClass)                                       \
  const AFX_EVENTMAP* theClass::GetEventMap() const { return GetThisEventMap(); }  \
  const AFX_EVENTMAP* PASCAL theClass::GetThisEventMap() {                         \
    typedef baseClass TheBaseClass;                                                \
    static const AFX_EVENTMAP_ENTRY _eventEntries[] = {

#define END_EVENT_MAP()                                                        \
      {NULL, DISPID_UNKNOWN, NULL}};                                           \
    static const AFX_EVENTMAP eventMap = {&TheBaseClass::GetThisEventMap,      \
                                          &_eventEntries[0]};                  \
    return &eventMap;                                                          \
  }

#define EVENT_CUSTOM(pszName, pfnFire, vtsParams) {_T(pszName), DISPID_UNKNOWN, vtsParams},
#define EVENT_CUSTOM_ID(pszName, dispid, pfnFire, vtsParams) {_T(pszName), (DISPID)(dispid), vtsParams},

#define EVENT_STOCK_CLICK() {_T("Click"), DISPID_CLICK, VTS_NONE},
#define EVENT_STOCK_DBLCLICK() {_T("DblClick"), DISPID_DBLCLICK, VTS_NONE},
#define EVENT_STOCK_KEYDOWN() {_T("KeyDown"), DISPID_KEYDOWN, VTS_PI2 VTS_I2},
#define EVENT_STOCK_KEYPRESS() {_T("KeyPress"), DISPID_KEYPRESS, VTS_PI2},
#define EVENT_STOCK_KEYUP() {_T("KeyUp"), DISPID_KEYUP, VTS_PI2 VTS_I2},
#define EVENT_STOCK_MOUSEDOWN() \
  {_T("MouseDown"), DISPID_MOUSEDOWN, VTS_I2 VTS_I2 VTS_XPOS_PIXELS VTS_YPOS_PIXELS},
#define EVENT_STOCK_MOUSEMOVE() \
  {_T("MouseMove"), DISPID_MOUSEMOVE, VTS_I2 VTS_I2 VTS_XPOS_PIXELS VTS_YPOS_PIXELS},
#define EVENT_STOCK_MOUSEUP() \
  {_T("MouseUp"), DISPID_MOUSEUP, VTS_I2 VTS_I2 VTS_XPOS_PIXELS VTS_YPOS_PIXELS},
#define EVENT_STOCK_ERROREVENT()                                                         \
  {_T("Error"), DISPID_ERROREVENT,                                                       \
   VTS_I2 VTS_PBSTR VTS_SCODE VTS_BSTR VTS_BSTR VTS_I4 VTS_PBOOL},
#define EVENT_STOCK_READYSTATECHANGE() {_T("ReadyStateChange"), DISPID_READYSTATECHANGE, VTS_I4},

// ---------------------------------------------------------------------
// Stock properties and methods, for a control's dispatch map.

#define DISP_STOCKPROP_APPEARANCE() \
  DISP_PROPERTY_EX_ID(COleControl, "Appearance", DISPID_APPEARANCE, GetAppearance, SetAppearance, VT_I2)
#define DISP_STOCKPROP_BACKCOLOR() \
  DISP_PROPERTY_EX_ID(COleControl, "BackColor", DISPID_BACKCOLOR, GetBackColor, SetBackColor, VT_COLOR)
#define DISP_STOCKPROP_BORDERSTYLE() \
  DISP_PROPERTY_EX_ID(COleControl, "BorderStyle", DISPID_BORDERSTYLE, GetBorderStyle, SetBorderStyle, VT_I2)
#define DISP_STOCKPROP_CAPTION() \
  DISP_PROPERTY_EX_ID(COleControl, "Caption", DISPID_CAPTION, GetText, SetText, VT_BSTR)
#define DISP_STOCKPROP_ENABLED() \
  DISP_PROPERTY_EX_ID(COleControl, "Enabled", DISPID_ENABLED, GetEnabled, SetEnabled, VT_BOOL)
#define DISP_STOCKPROP_FONT() \
  DISP_PROPERTY_EX_ID(COleControl, "Font", DISPID_FONT, GetFont, SetFont, VT_FONT)
#define DISP_STOCKPROP_FORECOLOR() \
  DISP_PROPERTY_EX_ID(COleControl, "ForeColor", DISPID_FORECOLOR, GetForeColor, SetForeColor, VT_COLOR)
#define DISP_STOCKPROP_HWND() \
  DISP_PROPERTY_EX_ID(COleControl, "hWnd", DISPID_HWND, GetHwnd, SetNotSupported, VT_HANDLE)
#define DISP_STOCKPROP_TEXT() \
  DISP_PROPERTY_EX_ID(COleControl, "Text", DISPID_TEXT, GetText, SetText, VT_BSTR)
#define DISP_STOCKPROP_READYSTATE() \
  DISP_PROPERTY_EX_ID(COleControl, "ReadyState", DISPID_READYSTATE, GetReadyState, SetNotSupported, VT_I4)

#define DISP_STOCKFUNC_REFRESH() \
  DISP_FUNCTION_ID(COleControl, "Refresh", DISPID_REFRESH, Refresh, VT_EMPTY, VTS_NONE)
#define DISP_STOCKFUNC_DOCLICK() \
  DISP_FUNCTION_ID(COleControl, "DoClick", DISPID_DOCLICK, DoClick, VT_EMPTY, VTS_NONE)

// ---------------------------------------------------------------------
// CFontHolder: a font object (IFont) for a control's font property.

class CFontHolder {
public:
  explicit CFontHolder(LPPROPERTYNOTIFYSINK pNotify);
  ~CFontHolder();

  // A font like the ambient font (pFontDispAmbient), or from the
  // description, or the system's default.
  void InitializeFont(const FONTDESC* pFontDesc = NULL, LPDISPATCH pFontDispAmbient = NULL);
  void SetFont(LPFONT pNewFont);  // takes the reference
  void ReleaseFont();
  HFONT GetFontHandle();
  HFONT GetFontHandle(long cyLogical, long cyHimetric);
  CFont* Select(CDC* pDC, long cyLogical, long cyHimetric);
  BOOL GetDisplayString(CString& strValue);
  LPFONTDISP GetFontDispatch();  // AddRef'd
  void QueryTextMetrics(LPTEXTMETRIC lptm);

  LPFONT m_pFont;

protected:
  DWORD m_dwConnectCookie;
  LPPROPERTYNOTIFYSINK m_pNotify;
};

// ---------------------------------------------------------------------
// CPropExchange: saving, loading and resetting a control's properties.
// DoPropExchange calls the PX_ functions, which go through it.

class CPropExchange {
public:
  virtual ~CPropExchange() {}
  BOOL IsLoading() const { return m_bLoading; }
  DWORD GetVersion() const { return m_dwVersion; }
  BOOL IsAsynchronous() const { return m_bAsync; }
  BOOL ExchangeVersion(DWORD& dwVersionLoaded, DWORD dwVersionDefault, BOOL bConvert);

  virtual BOOL ExchangeProp(LPCTSTR pszPropName, VARTYPE vtProp, void* pvProp,
                            const void* pvDefault = NULL) = 0;
  virtual BOOL ExchangeBlobProp(LPCTSTR pszPropName, HGLOBAL* phBlob,
                                HGLOBAL hBlobDefault = NULL) = 0;
  virtual BOOL ExchangeFontProp(LPCTSTR pszPropName, CFontHolder& font,
                                const FONTDESC* pFontDesc, LPFONTDISP pFontDispAmbient) = 0;
  virtual BOOL ExchangePersistentProp(LPCTSTR pszPropName, LPUNKNOWN* ppUnk, REFIID iid,
                                      LPUNKNOWN pUnkDefault) = 0;

protected:
  CPropExchange() : m_bLoading(FALSE), m_bAsync(FALSE), m_dwVersion(0) {}
  BOOL m_bLoading;
  BOOL m_bAsync;
  DWORD m_dwVersion;
};

BOOL AFXAPI PX_Short(CPropExchange* pPX, LPCTSTR pszPropName, short& sValue);
BOOL AFXAPI PX_Short(CPropExchange* pPX, LPCTSTR pszPropName, short& sValue, short sDefault);
BOOL AFXAPI PX_UShort(CPropExchange* pPX, LPCTSTR pszPropName, USHORT& usValue);
BOOL AFXAPI PX_UShort(CPropExchange* pPX, LPCTSTR pszPropName, USHORT& usValue,
                      USHORT usDefault);
BOOL AFXAPI PX_Long(CPropExchange* pPX, LPCTSTR pszPropName, long& lValue);
BOOL AFXAPI PX_Long(CPropExchange* pPX, LPCTSTR pszPropName, long& lValue, long lDefault);
BOOL AFXAPI PX_ULong(CPropExchange* pPX, LPCTSTR pszPropName, ULONG& ulValue);
BOOL AFXAPI PX_ULong(CPropExchange* pPX, LPCTSTR pszPropName, ULONG& ulValue, ULONG ulDefault);
BOOL AFXAPI PX_Color(CPropExchange* pPX, LPCTSTR pszPropName, OLE_COLOR& clrValue);
BOOL AFXAPI PX_Color(CPropExchange* pPX, LPCTSTR pszPropName, OLE_COLOR& clrValue,
                     OLE_COLOR clrDefault);
BOOL AFXAPI PX_Bool(CPropExchange* pPX, LPCTSTR pszPropName, BOOL& bValue);
BOOL AFXAPI PX_Bool(CPropExchange* pPX, LPCTSTR pszPropName, BOOL& bValue, BOOL bDefault);
BOOL AFXAPI PX_String(CPropExchange* pPX, LPCTSTR pszPropName, CString& strValue);
BOOL AFXAPI PX_String(CPropExchange* pPX, LPCTSTR pszPropName, CString& strValue,
                      const CString& strDefault);
BOOL AFXAPI PX_String(CPropExchange* pPX, LPCTSTR pszPropName, CString& strValue,
                      LPCTSTR lpszDefault);
BOOL AFXAPI PX_Currency(CPropExchange* pPX, LPCTSTR pszPropName, CY& cyValue);
BOOL AFXAPI PX_Currency(CPropExchange* pPX, LPCTSTR pszPropName, CY& cyValue, CY cyDefault);
BOOL AFXAPI PX_Float(CPropExchange* pPX, LPCTSTR pszPropName, float& floatValue);
BOOL AFXAPI PX_Float(CPropExchange* pPX, LPCTSTR pszPropName, float& floatValue,
                     float floatDefault);
BOOL AFXAPI PX_Double(CPropExchange* pPX, LPCTSTR pszPropName, double& doubleValue);
BOOL AFXAPI PX_Double(CPropExchange* pPX, LPCTSTR pszPropName, double& doubleValue,
                      double doubleDefault);
BOOL AFXAPI PX_Blob(CPropExchange* pPX, LPCTSTR pszPropName, HGLOBAL& hBlob,
                    HGLOBAL hBlobDefault = NULL);
BOOL AFXAPI PX_Font(CPropExchange* pPX, LPCTSTR pszPropName, CFontHolder& font,
                    const FONTDESC* pFontDesc = NULL, LPFONTDISP pFontDispAmbient = NULL);
BOOL AFXAPI PX_IUnknown(CPropExchange* pPX, LPCTSTR pszPropName, LPUNKNOWN& pUnk, REFIID iid,
                        LPUNKNOWN pUnkDefault = NULL);

// ---------------------------------------------------------------------
// COleControl

class COleControl : public CWnd {
  DECLARE_DYNAMIC(COleControl)
public:
  COleControl();
  virtual ~COleControl();

  // The class's identity, from DECLARE_OLECREATE_EX and IMPLEMENT_OLECTLTYPE.
  virtual HRESULT GetClassID(LPCLSID pclsid) = 0;
  virtual UINT GetUserTypeNameID() = 0;
  virtual DWORD GetMiscStatus() = 0;

  // The primary dispatch interface and the events interface, named in
  // the control's constructor.
  void InitializeIIDs(const IID* piidPrimary, const IID* piidEvents);

  // ----- Drawing
  virtual void OnDraw(CDC* pDC, const CRect& rcBounds, const CRect& rcInvalid);
  virtual void OnDrawMetafile(CDC* pDC, const CRect& rcBounds);
  void DoSuperclassPaint(CDC* pDC, const CRect& rcBounds);
  virtual BOOL IsSubclassedControl();
  void InvalidateControl(LPCRECT lpRect = NULL, BOOL bErase = TRUE);
  void Refresh();
  void DoClick();
  virtual void OnClick(USHORT iButton);

  // ----- Size, in pixels; the extent is in HIMETRIC
  void SetInitialSize(int cx, int cy);
  void GetControlSize(int* pcx, int* pcy);
  BOOL SetControlSize(int cx, int cy);
  BOOL GetRectInContainer(LPRECT lpRect);
  BOOL SetRectInContainer(LPCRECT lpRect);
  virtual BOOL OnSetExtent(LPSIZEL lpSizeL);
  virtual BOOL OnSetObjectRects(LPCRECT lprcPosRect, LPCRECT lprcClipRect);
  CWnd* GetOuterWindow() const;

  // ----- Stock properties
  OLE_COLOR GetBackColor();
  void SetBackColor(OLE_COLOR clrBack);
  OLE_COLOR GetForeColor();
  void SetForeColor(OLE_COLOR clrFore);
  short GetAppearance();
  void SetAppearance(short sAppearance);
  short GetBorderStyle();
  void SetBorderStyle(short sBorderStyle);
  BOOL GetEnabled();
  void SetEnabled(BOOL bEnabled);
  LPFONTDISP GetFont();
  void SetFont(LPFONTDISP pFontDisp);
  CFontHolder& InternalGetFont();
  CFont* SelectStockFont(CDC* pDC);
  CFont* SelectFontObject(CDC* pDC, CFontHolder& fontHolder);
  OLE_HANDLE GetHwnd();
  BSTR GetText();
  void SetText(LPCTSTR pszText);
  const CString& InternalGetText();
  long GetReadyState();
  void InternalSetReadyState(long lNewState);
  void GetStockTextMetrics(LPTEXTMETRIC lptm);
  void GetFontTextMetrics(LPTEXTMETRIC lptm, CFontHolder& fontHolder);

  virtual void OnBackColorChanged();
  virtual void OnForeColorChanged();
  virtual void OnAppearanceChanged();
  virtual void OnBorderStyleChanged();
  virtual void OnEnabledChanged();
  virtual void OnFontChanged();
  virtual void OnTextChanged();

  // ----- Properties and their notifications
  void SetModifiedFlag(BOOL bModified = TRUE);
  BOOL IsModified();
  void BoundPropertyChanged(DISPID dispid);
  BOOL BoundPropertyRequestEdit(DISPID dispid);
  void SetNotPermitted();
  void SetNotSupported();
  void GetNotSupported();
  void ThrowError(SCODE sc, UINT nDescriptionID, UINT nHelpID = (UINT)-1);
  void ThrowError(SCODE sc, LPCTSTR pszDescription = NULL, UINT nHelpID = 0);
  static OLE_COLOR PASCAL TranslateColor(OLE_COLOR clrColor, HPALETTE hpal = NULL);
  COLORREF TranslateColorRef(OLE_COLOR clrColor) { return TranslateColor(clrColor); }

  // ----- Events
  void AFX_CDECL FireEvent(DISPID dispid, const BYTE* pbParams, ...);
  void FireEventV(DISPID dispid, const BYTE* pbParams, va_list argList);
  void FireClick();
  void FireDblClick();
  void FireKeyDown(USHORT* pnChar, short nShiftState);
  void FireKeyPress(USHORT* pnChar);
  void FireKeyUp(USHORT* pnChar, short nShiftState);
  void FireMouseDown(short nButton, short nShiftState, OLE_XPOS_PIXELS x, OLE_YPOS_PIXELS y);
  void FireMouseMove(short nButton, short nShiftState, OLE_XPOS_PIXELS x, OLE_YPOS_PIXELS y);
  void FireMouseUp(short nButton, short nShiftState, OLE_XPOS_PIXELS x, OLE_YPOS_PIXELS y);
  void FireError(SCODE scode, LPCTSTR lpszDescription, UINT nHelpID = 0);
  void FireReadyStateChange();
  BOOL AreEventsFrozen() { return m_nFreezeEvents > 0; }
  virtual void OnFreezeEvents(BOOL bFreeze);
  // The stock events in the event map, which the control fires by itself.
  DWORD GetStockEventMask() const;

  // ----- The container
  LPOLECLIENTSITE GetClientSite() { return m_pClientSite; }
  virtual void OnSetClientSite();
  BOOL GetAmbientProperty(DISPID dispid, VARTYPE vtProp, void* pvProp);
  OLE_COLOR AmbientBackColor();
  OLE_COLOR AmbientForeColor();
  CString AmbientDisplayName();
  LPFONTDISP AmbientFont();
  LCID AmbientLocaleID();
  short AmbientAppearance();
  short AmbientTextAlign();
  BOOL AmbientUserMode();
  BOOL AmbientUIDead();
  BOOL AmbientShowGrabHandles();
  BOOL AmbientShowHatching();
  CString AmbientScaleUnits();
  virtual void OnAmbientPropertyChange(DISPID dispid);
  BOOL LockInPlaceActive(BOOL bLock);
  BOOL TransformCoords(POINTL* lpptlHimetric, POINTF* lpptfContainer, DWORD flags);
  BOOL ClipCaretRect(LPRECT lpRect) { return lpRect != NULL; }
  void PreModalDialog(HWND hWndParent = NULL);
  void PostModalDialog(HWND hWndParent = NULL);

  // ----- Activation
  virtual HRESULT OnActivateInPlace(BOOL bUIActivate, LPMSG pMsg);
  virtual BOOL OnEdit(LPMSG lpMsg, HWND hWndParent, LPCRECT lpRect);
  virtual BOOL OnProperties(LPMSG lpMsg, HWND hWndParent, LPCRECT lpRect);
  virtual BOOL OnDoVerb(LONG iVerb, LPMSG lpMsg, HWND hWndParent, LPCRECT lpRect);
  virtual void OnClose(DWORD dwSaveOption);
  virtual void OnGetControlInfo(LPCONTROLINFO pControlInfo);
  virtual void OnMnemonic(LPMSG pMsg);
  BOOL IsInPlaceActive() const { return m_bInPlaceActive; }
  BOOL IsUIActive() const { return m_bUIActive; }
  BOOL IsConvertingVBX() { return FALSE; }
  virtual BOOL PreTranslateMessage(MSG* pMsg);
  virtual BOOL PreCreateWindow(CREATESTRUCT& cs);
  virtual void OnFinalRelease();
  virtual BOOL OnSetData(LPFORMATETC, LPSTGMEDIUM, BOOL) { return FALSE; }

  // ----- Persistence
  virtual void DoPropExchange(CPropExchange* pPX);
  BOOL ExchangeVersion(CPropExchange* pPX, DWORD dwVersionDefault, BOOL bConvert = TRUE);
  BOOL ExchangeExtent(CPropExchange* pPX);
  void ExchangeStockProps(CPropExchange* pPX);
  virtual void OnResetState();
  // The stock properties in the dispatch map, which ExchangeStockProps saves.
  DWORD GetStockPropMask() const;
  BOOL IsInitialized() const { return m_bInitialized; }

  // ----- State
  CString m_strText;
  OLE_COLOR m_clrBackColor;
  OLE_COLOR m_clrForeColor;
  short m_sAppearance;
  short m_sBorderStyle;
  BOOL m_bEnabled;
  long m_lReadyState;
  CFontHolder m_font;
  long m_cxExtent;  // HIMETRIC
  long m_cyExtent;
  BOOL m_bModified;
  BOOL m_bInitialized;
  BOOL m_bInPlaceActive;
  BOOL m_bUIActive;
  BOOL m_bInPlaceSiteEx;
  short m_nFreezeEvents;
  BOOL m_bPendingUIActivation;
  CRect m_rcPos;
  CRect m_rcClip;
  const IID* m_piidPrimary;
  const IID* m_piidEvents;
  LPOLECLIENTSITE m_pClientSite;
  LPOLECONTROLSITE m_pControlSite;
  LPOLEINPLACESITE m_pInPlaceSite;
  LPOLEINPLACEFRAME m_pInPlaceFrame;
  LPOLEINPLACEUIWINDOW m_pInPlaceDoc;
  OLEINPLACEFRAMEINFO m_frameInfo;
  LPOLEADVISEHOLDER m_pOleAdviseHolder;
  LPADVISESINK m_pAdviseSink;  // IViewObject's
  DWORD m_dwAdviseFlags;
  CWnd* m_pReflect;  // between the container and a subclassed control
  COleDispatchDriver m_ambientDispDriver;

  // The window is made in place in the container.
  virtual BOOL CreateControlWindow(HWND hWndParent, const CRect& rcPos, LPCRECT prcClipped = NULL);
  void DestroyControlWindow();
  void UIDeactivate();
  void InPlaceDeactivate();
  void SendAdvise(UINT uCode);
  HRESULT SaveToArchive(CArchive& ar);
  HRESULT LoadFromArchive(CArchive& ar);
  void ButtonDown(USHORT iButton, UINT nFlags, CPoint point);
  void ButtonUp(USHORT iButton, UINT nFlags, CPoint point);
  void ButtonDblClk(USHORT iButton, UINT nFlags, CPoint point);
  void KeyDown(USHORT nChar);
  void KeyUp(USHORT nChar);

  afx_msg void OnPaint();
  afx_msg BOOL OnEraseBkgnd(CDC* pDC);
  afx_msg void OnLButtonDown(UINT nFlags, CPoint point);
  afx_msg void OnLButtonUp(UINT nFlags, CPoint point);
  afx_msg void OnLButtonDblClk(UINT nFlags, CPoint point);
  afx_msg void OnRButtonDown(UINT nFlags, CPoint point);
  afx_msg void OnRButtonUp(UINT nFlags, CPoint point);
  afx_msg void OnRButtonDblClk(UINT nFlags, CPoint point);
  afx_msg void OnMButtonDown(UINT nFlags, CPoint point);
  afx_msg void OnMButtonUp(UINT nFlags, CPoint point);
  afx_msg void OnMButtonDblClk(UINT nFlags, CPoint point);
  afx_msg void OnMouseMove(UINT nFlags, CPoint point);
  afx_msg void OnKeyDown(UINT nChar, UINT nRepCnt, UINT nFlags);
  afx_msg void OnKeyUp(UINT nChar, UINT nRepCnt, UINT nFlags);
  afx_msg void OnChar(UINT nChar, UINT nRepCnt, UINT nFlags);
  afx_msg void OnSysKeyDown(UINT nChar, UINT nRepCnt, UINT nFlags);
  afx_msg void OnSysKeyUp(UINT nChar, UINT nRepCnt, UINT nFlags);
  afx_msg void OnSetFocus(CWnd* pOldWnd);
  afx_msg void OnKillFocus(CWnd* pNewWnd);
  afx_msg UINT OnGetDlgCode();
  afx_msg int OnMouseActivate(CWnd* pDesktopWnd, UINT nHitTest, UINT message);
  afx_msg LRESULT OnSetText(WPARAM wParam, LPARAM lParam);
  DECLARE_MESSAGE_MAP()

  DECLARE_EVENT_MAP()
  virtual LPCLSID GetPropPageIDs(ULONG& cPropPages);

public:
  // The connection points: events and property notifications.
  class XEventConnPt : public CConnectionPoint {
  public:
    REFIID GetIID();
    virtual void OnAdvise(BOOL bAdvise);
  } m_xEventConnPt;
  friend class XEventConnPt;
  BEGIN_CONNECTION_PART(COleControl, PropConnPt)
    CONNECTION_IID(IID_IPropertyNotifySink)
  END_CONNECTION_PART(PropConnPt)
  virtual BOOL GetExtraConnectionPoints(CPtrArray* pConnPoints);
  virtual BOOL GetDispatchIID(IID* pIID);
  virtual LPUNKNOWN GetInterfaceHook(const void* piid);

  BEGIN_INTERFACE_PART(OleObject, IOleObject)
    STDMETHOD(SetClientSite)(LPOLECLIENTSITE pClientSite);
    STDMETHOD(GetClientSite)(LPOLECLIENTSITE* ppClientSite);
    STDMETHOD(SetHostNames)(LPCOLESTR szContainerApp, LPCOLESTR szContainerObj);
    STDMETHOD(Close)(DWORD dwSaveOption);
    STDMETHOD(SetMoniker)(DWORD dwWhichMoniker, LPMONIKER pmk);
    STDMETHOD(GetMoniker)(DWORD dwAssign, DWORD dwWhichMoniker, LPMONIKER* ppmk);
    STDMETHOD(InitFromData)(LPDATAOBJECT pDataObject, BOOL fCreation, DWORD dwReserved);
    STDMETHOD(GetClipboardData)(DWORD dwReserved, LPDATAOBJECT* ppDataObject);
    STDMETHOD(DoVerb)(LONG iVerb, LPMSG lpmsg, LPOLECLIENTSITE pActiveSite, LONG lindex,
                      HWND hwndParent, LPCRECT lprcPosRect);
    STDMETHOD(EnumVerbs)(LPENUMOLEVERB* ppEnumOleVerb);
    STDMETHOD(Update)();
    STDMETHOD(IsUpToDate)();
    STDMETHOD(GetUserClassID)(CLSID* pClsid);
    STDMETHOD(GetUserType)(DWORD dwFormOfType, LPOLESTR* pszUserType);
    STDMETHOD(SetExtent)(DWORD dwDrawAspect, LPSIZEL psizel);
    STDMETHOD(GetExtent)(DWORD dwDrawAspect, LPSIZEL psizel);
    STDMETHOD(Advise)(LPADVISESINK pAdvSink, DWORD* pdwConnection);
    STDMETHOD(Unadvise)(DWORD dwConnection);
    STDMETHOD(EnumAdvise)(LPENUMSTATDATA* ppenumAdvise);
    STDMETHOD(GetMiscStatus)(DWORD dwAspect, DWORD* pdwStatus);
    STDMETHOD(SetColorScheme)(LPLOGPALETTE pLogpal);
  END_INTERFACE_PART(OleObject)

  BEGIN_INTERFACE_PART(OleInPlaceObject, IOleInPlaceObject)
    STDMETHOD(GetWindow)(HWND* phwnd);
    STDMETHOD(ContextSensitiveHelp)(BOOL fEnterMode);
    STDMETHOD(InPlaceDeactivate)();
    STDMETHOD(UIDeactivate)();
    STDMETHOD(SetObjectRects)(LPCRECT lprcPosRect, LPCRECT lprcClipRect);
    STDMETHOD(ReactivateAndUndo)();
  END_INTERFACE_PART(OleInPlaceObject)

  BEGIN_INTERFACE_PART(OleInPlaceActiveObject, IOleInPlaceActiveObject)
    STDMETHOD(GetWindow)(HWND* phwnd);
    STDMETHOD(ContextSensitiveHelp)(BOOL fEnterMode);
    STDMETHOD(TranslateAccelerator)(LPMSG lpmsg);
    STDMETHOD(OnFrameWindowActivate)(BOOL fActivate);
    STDMETHOD(OnDocWindowActivate)(BOOL fActivate);
    STDMETHOD(ResizeBorder)(LPCRECT prcBorder, LPOLEINPLACEUIWINDOW pUIWindow, BOOL fFrameWindow);
    STDMETHOD(EnableModeless)(BOOL fEnable);
  END_INTERFACE_PART(OleInPlaceActiveObject)

  BEGIN_INTERFACE_PART(OleControl, IOleControl)
    STDMETHOD(GetControlInfo)(LPCONTROLINFO pCI);
    STDMETHOD(OnMnemonic)(LPMSG pMsg);
    STDMETHOD(OnAmbientPropertyChange)(DISPID dispid);
    STDMETHOD(FreezeEvents)(BOOL bFreeze);
  END_INTERFACE_PART(OleControl)

  BEGIN_INTERFACE_PART(ViewObject, IViewObject2)
    STDMETHOD(Draw)(DWORD dwDrawAspect, LONG lindex, void* pvAspect, DVTARGETDEVICE* ptd,
                    HDC hdcTargetDev, HDC hdcDraw, LPCRECTL lprcBounds, LPCRECTL lprcWBounds,
                    BOOL(STDMETHODCALLTYPE* pfnContinue)(ULONG_PTR dwContinue),
                    ULONG_PTR dwContinue);
    STDMETHOD(GetColorSet)(DWORD dwDrawAspect, LONG lindex, void* pvAspect, DVTARGETDEVICE* ptd,
                           HDC hicTargetDev, LOGPALETTE** ppColorSet);
    STDMETHOD(Freeze)(DWORD dwDrawAspect, LONG lindex, void* pvAspect, DWORD* pdwFreeze);
    STDMETHOD(Unfreeze)(DWORD dwFreeze);
    STDMETHOD(SetAdvise)(DWORD aspects, DWORD advf, LPADVISESINK pAdvSink);
    STDMETHOD(GetAdvise)(DWORD* pAspects, DWORD* pAdvf, LPADVISESINK* ppAdvSink);
    STDMETHOD(GetExtent)(DWORD dwDrawAspect, LONG lindex, DVTARGETDEVICE* ptd, LPSIZEL lpsizel);
  END_INTERFACE_PART(ViewObject)

  BEGIN_INTERFACE_PART(PersistStreamInit, IPersistStreamInit)
    STDMETHOD(GetClassID)(LPCLSID pClassID);
    STDMETHOD(IsDirty)();
    STDMETHOD(Load)(LPSTREAM pStm);
    STDMETHOD(Save)(LPSTREAM pStm, BOOL fClearDirty);
    STDMETHOD(GetSizeMax)(ULARGE_INTEGER* pcbSize);
    STDMETHOD(InitNew)();
  END_INTERFACE_PART(PersistStreamInit)

  BEGIN_INTERFACE_PART(PersistStorage, IPersistStorage)
    STDMETHOD(GetClassID)(LPCLSID pClassID);
    STDMETHOD(IsDirty)();
    STDMETHOD(InitNew)(LPSTORAGE pStg);
    STDMETHOD(Load)(LPSTORAGE pStg);
    STDMETHOD(Save)(LPSTORAGE pStgSave, BOOL fSameAsLoad);
    STDMETHOD(SaveCompleted)(LPSTORAGE pStgNew);
    STDMETHOD(HandsOffStorage)();
  END_INTERFACE_PART(PersistStorage)

  BEGIN_INTERFACE_PART(PersistPropertyBag, IPersistPropertyBag)
    STDMETHOD(GetClassID)(LPCLSID pClassID);
    STDMETHOD(InitNew)();
    STDMETHOD(Load)(LPPROPERTYBAG pPropBag, LPERRORLOG pErrorLog);
    STDMETHOD(Save)(LPPROPERTYBAG pPropBag, BOOL fClearDirty, BOOL fSaveAllProperties);
  END_INTERFACE_PART(PersistPropertyBag)

  BEGIN_INTERFACE_PART(ProvideClassInfo, IProvideClassInfo2)
    STDMETHOD(GetClassInfo)(LPTYPEINFO* ppTypeInfo);
    STDMETHOD(GetGUID)(DWORD dwGuidKind, GUID* pGUID);
  END_INTERFACE_PART(ProvideClassInfo)

  BEGIN_INTERFACE_PART(SpecifyPropertyPages, ISpecifyPropertyPages)
    STDMETHOD(GetPages)(CAUUID* pPages);
  END_INTERFACE_PART(SpecifyPropertyPages)

  // The font's changes.
  BEGIN_INTERFACE_PART(FontNotification, IPropertyNotifySink)
    STDMETHOD(OnChanged)(DISPID dispid);
    STDMETHOD(OnRequestEdit)(DISPID dispid);
  END_INTERFACE_PART(FontNotification)

  DECLARE_INTERFACE_MAP()
  DECLARE_CONNECTION_MAP()
};

// The stock property and event masks.
enum {
  STOCKPROP_BACKCOLOR = 0x0001,
  STOCKPROP_CAPTION = 0x0002,
  STOCKPROP_FONT = 0x0004,
  STOCKPROP_FORECOLOR = 0x0008,
  STOCKPROP_TEXT = 0x0010,
  STOCKPROP_BORDERSTYLE = 0x0020,
  STOCKPROP_ENABLED = 0x0040,
  STOCKPROP_APPEARANCE = 0x0080,
  STOCKPROP_READYSTATE = 0x0100,
};
enum {
  STOCKEVENT_CLICK = 0x0001,
  STOCKEVENT_DBLCLICK = 0x0002,
  STOCKEVENT_KEYDOWN = 0x0004,
  STOCKEVENT_KEYPRESS = 0x0008,
  STOCKEVENT_KEYUP = 0x0010,
  STOCKEVENT_MOUSEDOWN = 0x0020,
  STOCKEVENT_MOUSEMOVE = 0x0040,
  STOCKEVENT_MOUSEUP = 0x0080,
  STOCKEVENT_ERROR = 0x0100,
  STOCKEVENT_READYSTATECHANGE = 0x0200,
};

#endif  // __AFXCTL_H__
