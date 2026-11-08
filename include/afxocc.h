// afxocc.h - ActiveX control containment: the container of a window's
// controls, each control's site, and the manager that makes the controls
// of dialog templates.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#ifndef __AFXOCC_H__
#define __AFXOCC_H__

#include "afxdisp.h"
#include "ocdb.h"

class COleControlContainer;
class COleDropTarget;
class COleControlSite;
class CDataSourceControl;
struct CDataBoundProperty;

// The interface of the older data source controls (the Visual Basic
// ones), which make the cursor that bound controls use.
extern "C" const IID IID_IVBDSC;
struct IVBDSC : public IUnknown {
  virtual HRESULT STDMETHODCALLTYPE CancelUnload(BOOL* pfCancel) = 0;
  virtual HRESULT STDMETHODCALLTYPE Error(DWORD dwErr, BOOL* pfShowError) = 0;
  virtual HRESULT STDMETHODCALLTYPE CreateCursor(ICursor** ppCursor) = 0;
};

// The extender properties every container gives its controls.
#ifndef DISPID_NAME
#define DISPID_NAME (-800)
#endif
#ifndef DISPID_DELETE
#define DISPID_DELETE (-801)
#endif
#ifndef DISPID_OBJECT
#define DISPID_OBJECT (-802)
#endif
#ifndef DISPID_PARENT
#define DISPID_PARENT (-803)
#endif
// And the ones this library's sites give: where the control is, in the
// container's pixels, whether it shows, and its control ID.
#define AFX_DISPID_EXT_LEFT ((DISPID)0x80010100L)
#define AFX_DISPID_EXT_TOP ((DISPID)0x80010101L)
#define AFX_DISPID_EXT_WIDTH ((DISPID)0x80010102L)
#define AFX_DISPID_EXT_HEIGHT ((DISPID)0x80010103L)
#define AFX_DISPID_EXT_VISIBLE ((DISPID)0x80010104L)
#define AFX_DISPID_EXT_ID ((DISPID)0x80010105L)

// The OLE controls of a dialog template, kept between creating the
// dialog without them and making them on WM_INITDIALOG.
struct _AFX_OCC_DIALOG_INFO {
  struct ItemInfo {
    UINT nID;
    CLSID clsid;
    BOOL bAutoRadioButton;
    DWORD dwStyle;
    DWORD dwExStyle;
    CRect rect;  // in dialog units
    CString strText;
  };
  DLGTEMPLATE* m_pNewTemplate;  // the template without them
  ItemInfo* m_pItemInfo;
  int m_cItems;
  // The position of each OLE control among all the template's items,
  // for the Z order.
  int* m_pnIndex;
  int m_cAllItems;
  void* m_pDlgInit;  // the template's DLGINIT resource, with the controls' state
  BOOL m_bCreated;
};

// ---------------------------------------------------------------------
// COleControlSite: one hosted control.

class COleControlSite : public CCmdTarget {
public:
  explicit COleControlSite(COleControlContainer* pCtrlCont);
  ~COleControlSite();

  virtual HRESULT CreateControl(CWnd* pWndCtrl, REFCLSID clsid, LPCTSTR lpszWindowName,
                                DWORD dwStyle, const RECT& rect, UINT nID, CFile* pPersist = NULL,
                                BOOL bStorage = FALSE, BSTR bstrLicKey = NULL);
  virtual HRESULT CreateControl(CWnd* pWndCtrl, REFCLSID clsid, LPCTSTR lpszWindowName,
                                DWORD dwStyle, const POINT* ppt, const SIZE* psize, UINT nID,
                                CFile* pPersist = NULL, BOOL bStorage = FALSE,
                                BSTR bstrLicKey = NULL);
  virtual BOOL DestroyControl();

  // The control's properties and methods.
  void InvokeHelperV(DISPID dwDispID, WORD wFlags, VARTYPE vtRet, void* pvRet,
                     const BYTE* pbParamInfo, va_list argList);
  void AFX_CDECL InvokeHelper(DISPID dwDispID, WORD wFlags, VARTYPE vtRet, void* pvRet,
                              const BYTE* pbParamInfo, ...);
  void GetProperty(DISPID dwDispID, VARTYPE vtProp, void* pvProp) const;
  void SetPropertyV(DISPID dwDispID, VARTYPE vtProp, va_list argList);
  void AFX_CDECL SetProperty(DISPID dwDispID, VARTYPE vtProp, ...);
  BOOL AFX_CDECL SafeSetProperty(DISPID dwDispID, VARTYPE vtProp, ...);

  // The control's window.
  virtual DWORD GetStyle() const;
  virtual DWORD GetExStyle() const;
  virtual BOOL ModifyStyle(DWORD dwRemove, DWORD dwAdd, UINT nFlags);
  virtual BOOL ModifyStyleEx(DWORD dwRemove, DWORD dwAdd, UINT nFlags);
  virtual void SetWindowText(LPCTSTR lpszString);
  virtual void GetWindowText(CString& str) const;
  virtual int GetDlgCtrlID() const { return (int)m_nID; }
  virtual int SetDlgCtrlID(int nID);
  virtual void MoveWindow(int x, int y, int nWidth, int nHeight, BOOL bRepaint = TRUE);
  virtual BOOL SetWindowPos(const CWnd* pWndInsertAfter, int x, int y, int cx, int cy,
                            UINT nFlags);
  virtual BOOL ShowWindow(int nCmdShow);
  virtual BOOL IsWindowEnabled() const;
  virtual BOOL EnableWindow(BOOL bEnable);
  virtual CWnd* SetFocus();
  virtual CWnd* SetFocus(LPMSG lpmsg);
  virtual BOOL DoVerb(LONG nVerb, LPMSG lpMsg = NULL);
  virtual void FreezeEvents(BOOL bFreeze);
  virtual void GetControlInfo();
  virtual BOOL IsDefaultButton();
  virtual DWORD GetDefBtnCode();
  virtual void SetDefaultButton(BOOL bDefault);
  virtual BOOL GetEventIID(IID* piid);
  virtual BOOL IsWindowlessControl() { return m_bIsWindowless; }

  // Data binding to a data source control (an OLE DB DataSource, or an
  // older one that gives an ICursor): a property bound to a field of the
  // current row, or given the data source; and, for a data source
  // control's site, its rowset or cursor.
  virtual void BindDefaultProperty(DISPID dwDispID, VARTYPE vtProp, LPCTSTR szFieldName,
                                   CWnd* pDSCWnd);
  virtual void BindProperty(DISPID dwDispId, CWnd* pWndDSC);
  virtual IUnknown* GetDSCCursor();

  // The extended control (IOleControlSite::GetExtendedControl): an
  // IDispatch with the extender properties (Name, Object, Parent, Left,
  // Top, Width, Height, Visible, ID) whose other names and DISPIDs are
  // the control's own. The caller releases it.
  virtual LPDISPATCH GetExtendedControl();
  CString m_strName;  // the extender Name property

  COleControlContainer* m_pCtrlCont;
  HWND m_hWnd;
  CWnd* m_pWndCtrl;
  UINT m_nID;
  CRect m_rect;
  IID m_iidEvents;
  LPOLEOBJECT m_pObject;
  LPOLEINPLACEOBJECT m_pInPlaceObject;
  LPOLEINPLACEACTIVEOBJECT m_pActiveObject;
  COleDispatchDriver m_dispDriver;
  DWORD m_dwEventSink;
  DWORD m_dwPropNotifySink;
  DWORD m_dwNotifyDBEvents;
  DWORD m_dwMiscStatus;
  DWORD m_dwStyleMask;
  DWORD m_dwStyle;  // the styles the site keeps for the control
  CONTROLINFO m_ctlInfo;
  BOOL m_bIsWindowless;
  BOOL m_bInPlaceActive;
  BOOL m_bUIActive;
  BOOL m_bSubclassed;  // m_pWndCtrl subclassed the control's window
  CWnd* m_pWndOwned;   // the window object GetDlgItem made, which the site deletes
  CLSID m_clsid;
  void* m_pWindowlessObject;  // a windowless control's IOleInPlaceObjectWindowless
  int m_nFreezeEvents;
  CDataSourceControl* m_pDataSourceControl;  // when this control is a data source
  CDataBoundProperty* m_pBindings;           // this control's bound properties
  BOOL m_bIgnoreNotify;                      // setting a bound property from its field
  IDispatch* m_pExtendedControl;             // made on first asking, released with the site

protected:
  HRESULT CreateOrLoad(REFCLSID clsid, CFile* pPersist, BOOL bStorage, BSTR bstrLicKey);
  void ConnectSink(REFIID iid, LPUNKNOWN pUnkSink, DWORD* pdwCookie);
  void DisconnectSink(REFIID iid, DWORD dwCookie);
  void AttachWindow();
  void DetachWindow();
  BOOL SetExtent();
  void FindEventsIID();
  HRESULT FireEvent(AFX_EVENT* pEvent);
  // A windowless control: its part of the container window to repaint,
  // and a message for it.
  void InvalidateWindowless(LPCRECT lpRect, BOOL bErase);
  HRESULT SendWindowlessMessage(UINT msg, WPARAM wParam, LPARAM lParam, LRESULT* plResult);
  void OnActivated(DWORD dwFlags);
  void OnDeactivated();

  // IOleClientSite
  BEGIN_INTERFACE_PART(OleClientSite, IOleClientSite)
    STDMETHOD(SaveObject)();
    STDMETHOD(GetMoniker)(DWORD, DWORD, LPMONIKER*);
    STDMETHOD(GetContainer)(LPOLECONTAINER*);
    STDMETHOD(ShowObject)();
    STDMETHOD(OnShowWindow)(BOOL);
    STDMETHOD(RequestNewObjectLayout)();
  END_INTERFACE_PART(OleClientSite)

  // IOleInPlaceSite, and IOleInPlaceSiteEx and IOleInPlaceSiteWindowless
  // for windowless controls.
  BEGIN_INTERFACE_PART(OleIPSite, IOleInPlaceSiteWindowless)
    STDMETHOD(GetWindow)(HWND*);
    STDMETHOD(ContextSensitiveHelp)(BOOL);
    STDMETHOD(CanInPlaceActivate)();
    STDMETHOD(OnInPlaceActivate)();
    STDMETHOD(OnUIActivate)();
    STDMETHOD(GetWindowContext)(LPOLEINPLACEFRAME*, LPOLEINPLACEUIWINDOW*, LPRECT, LPRECT,
                                LPOLEINPLACEFRAMEINFO);
    STDMETHOD(Scroll)(SIZE);
    STDMETHOD(OnUIDeactivate)(BOOL);
    STDMETHOD(OnInPlaceDeactivate)();
    STDMETHOD(DiscardUndoState)();
    STDMETHOD(DeactivateAndUndo)();
    STDMETHOD(OnPosRectChange)(LPCRECT);
    STDMETHOD(OnInPlaceActivateEx)(BOOL*, DWORD);
    STDMETHOD(OnInPlaceDeactivateEx)(BOOL);
    STDMETHOD(RequestUIActivate)();
    STDMETHOD(CanWindowlessActivate)();
    STDMETHOD(GetCapture)();
    STDMETHOD(SetCapture)(BOOL);
    STDMETHOD(GetFocus)();
    STDMETHOD(SetFocus)(BOOL);
    STDMETHOD(GetDC)(LPCRECT, DWORD, HDC*);
    STDMETHOD(ReleaseDC)(HDC);
    STDMETHOD(InvalidateRect)(LPCRECT, BOOL);
    STDMETHOD(InvalidateRgn)(HRGN, BOOL);
    STDMETHOD(ScrollRect)(INT, INT, LPCRECT, LPCRECT);
    STDMETHOD(AdjustRect)(LPRECT);
    STDMETHOD(OnDefWindowMessage)(UINT, WPARAM, LPARAM, LRESULT*);
  END_INTERFACE_PART(OleIPSite)

  BEGIN_INTERFACE_PART(OleControlSite, IOleControlSite)
    STDMETHOD(OnControlInfoChanged)();
    STDMETHOD(LockInPlaceActive)(BOOL);
    STDMETHOD(GetExtendedControl)(LPDISPATCH*);
    STDMETHOD(TransformCoords)(POINTL*, POINTF*, DWORD);
    STDMETHOD(TranslateAccelerator)(LPMSG, DWORD);
    STDMETHOD(OnFocus)(BOOL);
    STDMETHOD(ShowPropertyFrame)();
  END_INTERFACE_PART(OleControlSite)

  // The ambient properties, the client site's IDispatch.
  BEGIN_INTERFACE_PART(AmbientProps, IDispatch)
    STDMETHOD(GetTypeInfoCount)(UINT*);
    STDMETHOD(GetTypeInfo)(UINT, LCID, ITypeInfo**);
    STDMETHOD(GetIDsOfNames)(REFIID, LPOLESTR*, UINT, LCID, DISPID*);
    STDMETHOD(Invoke)(DISPID, REFIID, LCID, WORD, DISPPARAMS*, LPVARIANT, LPEXCEPINFO, UINT*);
  END_INTERFACE_PART(AmbientProps)

  BEGIN_INTERFACE_PART(PropertyNotifySink, IPropertyNotifySink)
    STDMETHOD(OnChanged)(DISPID);
    STDMETHOD(OnRequestEdit)(DISPID);
  END_INTERFACE_PART(PropertyNotifySink)

  // A data source control's notifications (the older, ICursor ones).
  BEGIN_INTERFACE_PART(NotifyDBEvents, INotifyDBEvents)
    STDMETHOD(OKToDo)(DWORD, ULONG, DBNOTIFYREASON[]);
    STDMETHOD(Cancelled)(DWORD, ULONG, DBNOTIFYREASON[]);
    STDMETHOD(SyncBefore)(DWORD, ULONG, DBNOTIFYREASON[]);
    STDMETHOD(AboutToDo)(DWORD, ULONG, DBNOTIFYREASON[]);
    STDMETHOD(FailedToDo)(DWORD, ULONG, DBNOTIFYREASON[]);
    STDMETHOD(SyncAfter)(DWORD, ULONG, DBNOTIFYREASON[]);
    STDMETHOD(DidEvent)(DWORD, ULONG, DBNOTIFYREASON[]);
    HRESULT Notify(DSCSTATE nState, DWORD dwEventWhat, ULONG cReasons, DBNOTIFYREASON rgReasons[]);
  END_INTERFACE_PART(NotifyDBEvents)

  // The control's events; its QueryInterface answers m_iidEvents.
  BEGIN_INTERFACE_PART(EventSink, IDispatch)
    STDMETHOD(GetTypeInfoCount)(UINT*);
    STDMETHOD(GetTypeInfo)(UINT, LCID, ITypeInfo**);
    STDMETHOD(GetIDsOfNames)(REFIID, LPOLESTR*, UINT, LCID, DISPID*);
    STDMETHOD(Invoke)(DISPID, REFIID, LCID, WORD, DISPPARAMS*, LPVARIANT, LPEXCEPINFO, UINT*);
  END_INTERFACE_PART(EventSink)

  DECLARE_INTERFACE_MAP()
  friend class COleControlContainer;
  friend HRESULT AFXAPI _AfxFireDSCNotify(COleControlSite* pSite, DSCSTATE nState,
                                          DSCREASON nReason);
};

// ---------------------------------------------------------------------
// COleControlContainer: the controls of one window.

class COleControlContainer : public CCmdTarget {
public:
  explicit COleControlContainer(CWnd* pWnd);
  virtual ~COleControlContainer();

  virtual BOOL CreateControl(CWnd* pWndCtrl, REFCLSID clsid, LPCTSTR lpszWindowName,
                             DWORD dwStyle, const RECT& rect, UINT nID, CFile* pPersist = NULL,
                             BOOL bStorage = FALSE, BSTR bstrLicKey = NULL,
                             COleControlSite** ppNewSite = NULL);
  virtual BOOL CreateControl(CWnd* pWndCtrl, REFCLSID clsid, LPCTSTR lpszWindowName,
                             DWORD dwStyle, const POINT* ppt, const SIZE* psize, UINT nID,
                             CFile* pPersist = NULL, BOOL bStorage = FALSE,
                             BSTR bstrLicKey = NULL, COleControlSite** ppNewSite = NULL);
  virtual COleControlSite* FindItem(UINT nID) const;
  // Gives a CWnd the site of the control with the ID (DDX_Control).
  virtual void AttachControlSite(CWnd* pWnd, UINT nIDC = 0);
  virtual BOOL GetAmbientProp(COleControlSite* pSite, DISPID dispid, VARIANT* pvarResult);
  virtual void BroadcastAmbientPropertyChange(DISPID dispid);
  virtual void FreezeAllEvents(BOOL bFreeze);
  virtual void OnUIActivate(COleControlSite* pSite);
  virtual void OnUIDeactivate(COleControlSite* pSite);
  virtual void CreateOleFont(CFont* pFont);
  // Closes every control, before the window goes.
  virtual void DestroyAll();

  // Windowless controls: they draw on the window's DC when it paints,
  // and get its mouse messages over them and its keys when they have the
  // focus.
  virtual BOOL OnPaint(CDC* pDC);
  virtual BOOL HandleWindowlessMessage(UINT message, WPARAM wParam, LPARAM lParam,
                                       LRESULT* plResult);
  // The visible windowless control at a point of the window, the topmost.
  COleControlSite* FindWindowlessSite(CPoint point) const;
  // The drop target of the windowless control at a point (AddRef'd), or
  // NULL. When a windowless control that takes drops is activated and
  // the window is no drop target, the container makes it one
  // (m_pWindowlessDropTarget), so that drags reach the control.
  LPDROPTARGET GetWindowlessDropTarget(CPoint point) const;
  void RegisterWindowlessDropTarget(COleControlSite* pSite);

  // Dialog item functions that also reach controls.
  virtual CWnd* GetDlgItem(int nID) const;
  virtual void GetDlgItem(int nID, HWND* phWnd) const;
  virtual UINT GetDlgItemInt(int nID, BOOL* lpTrans, BOOL bSigned) const;
  virtual int GetDlgItemText(int nID, LPTSTR lpStr, int nMaxCount) const;
  virtual void SetDlgItemInt(int nID, UINT nValue, BOOL bSigned);
  virtual void SetDlgItemText(int nID, LPCTSTR lpszString);

  CWnd* m_pWnd;
  CPtrList m_listSitesOrWnds;  // the sites, in creation order
  CMapPtrToPtr m_siteMap;      // control window to site
  COleControlSite* m_pSiteUIActive;
  COleControlSite* m_pSiteFocus;
  COleControlSite* m_pSiteCapture;
  LPFONTDISP m_pOleFont;
  OLE_COLOR m_crBack;
  OLE_COLOR m_crFore;
  int m_nWindowlessControls;
  LPOLEINPLACEACTIVEOBJECT m_pActiveObject;
  COleDropTarget* m_pWindowlessDropTarget;

protected:
  // The frame and document window the controls see.
  BEGIN_INTERFACE_PART(OleIPFrame, IOleInPlaceFrame)
    STDMETHOD(GetWindow)(HWND*);
    STDMETHOD(ContextSensitiveHelp)(BOOL);
    STDMETHOD(GetBorder)(LPRECT);
    STDMETHOD(RequestBorderSpace)(LPCBORDERWIDTHS);
    STDMETHOD(SetBorderSpace)(LPCBORDERWIDTHS);
    STDMETHOD(SetActiveObject)(LPOLEINPLACEACTIVEOBJECT, LPCOLESTR);
    STDMETHOD(InsertMenus)(HMENU, LPOLEMENUGROUPWIDTHS);
    STDMETHOD(SetMenu)(HMENU, HOLEMENU, HWND);
    STDMETHOD(RemoveMenus)(HMENU);
    STDMETHOD(SetStatusText)(LPCOLESTR);
    STDMETHOD(EnableModeless)(BOOL);
    STDMETHOD(TranslateAccelerator)(LPMSG, WORD);
  END_INTERFACE_PART(OleIPFrame)

  BEGIN_INTERFACE_PART(OleContainer, IOleContainer)
    STDMETHOD(ParseDisplayName)(LPBINDCTX, LPOLESTR, ULONG*, LPMONIKER*);
    STDMETHOD(EnumObjects)(DWORD, LPENUMUNKNOWN*);
    STDMETHOD(LockContainer)(BOOL);
  END_INTERFACE_PART(OleContainer)

  DECLARE_INTERFACE_MAP()
  friend class COleControlSite;
};

// ---------------------------------------------------------------------
// COccManager: what AfxEnableControlContainer turns on.

class COccManager {
public:
  virtual ~COccManager() {}
  virtual BOOL OnEvent(CCmdTarget* pCmdTarget, UINT idCtrl, AFX_EVENT* pEvent,
                       AFX_CMDHANDLERINFO* pHandlerInfo);
  virtual COleControlContainer* CreateContainer(CWnd* pWnd);
  virtual COleControlSite* CreateSite(COleControlContainer* pCtrlCont);

  // A dialog template's OLE controls: PreCreateDialog takes them out of
  // the template, CreateDlgControls makes them, PostCreateDialog frees
  // what PreCreateDialog made.
  virtual const DLGTEMPLATE* PreCreateDialog(_AFX_OCC_DIALOG_INFO* pOccDialogInfo,
                                             const DLGTEMPLATE* pOrigTemplate);
  virtual void PostCreateDialog(_AFX_OCC_DIALOG_INFO* pOccDialogInfo);
  virtual BOOL CreateDlgControls(CWnd* pWndParent, LPCTSTR lpszResourceName,
                                 _AFX_OCC_DIALOG_INFO* pOccDialogInfo);
  virtual BOOL CreateDlgControls(CWnd* pWndParent, void* lpResource,
                                 _AFX_OCC_DIALOG_INFO* pOccDialogInfo);
  virtual BOOL IsDialogMessage(CWnd* pWndDlg, LPMSG lpMsg);

  static DLGTEMPLATE* PASCAL SplitDialogTemplate(const DLGTEMPLATE* pTemplate,
                                                 _AFX_OCC_DIALOG_INFO* pOccDialogInfo);
};

COccManager* AFXAPI _AfxGetOccManager();

#endif  // __AFXOCC_H__
