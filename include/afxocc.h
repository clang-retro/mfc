// afxocc.h - ActiveX control containment: the container of a window's
// controls, each control's site, and the manager that makes the controls
// of dialog templates.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
#ifndef __AFXOCC_H__
#define __AFXOCC_H__

#include "afxdisp.h"

class COleControlContainer;
class COleControlSite;

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
  virtual BOOL IsWindowlessControl() { return FALSE; }

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
  void* m_pWindowlessObject;
  int m_nFreezeEvents;

protected:
  HRESULT CreateOrLoad(REFCLSID clsid, CFile* pPersist, BOOL bStorage, BSTR bstrLicKey);
  void ConnectSink(REFIID iid, LPUNKNOWN pUnkSink, DWORD* pdwCookie);
  void DisconnectSink(REFIID iid, DWORD dwCookie);
  void AttachWindow();
  void DetachWindow();
  BOOL SetExtent();
  void FindEventsIID();
  HRESULT FireEvent(AFX_EVENT* pEvent);

  // IOleClientSite
  BEGIN_INTERFACE_PART(OleClientSite, IOleClientSite)
    STDMETHOD(SaveObject)();
    STDMETHOD(GetMoniker)(DWORD, DWORD, LPMONIKER*);
    STDMETHOD(GetContainer)(LPOLECONTAINER*);
    STDMETHOD(ShowObject)();
    STDMETHOD(OnShowWindow)(BOOL);
    STDMETHOD(RequestNewObjectLayout)();
  END_INTERFACE_PART(OleClientSite)

  BEGIN_INTERFACE_PART(OleIPSite, IOleInPlaceSite)
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

  // The control's events; its QueryInterface answers m_iidEvents.
  BEGIN_INTERFACE_PART(EventSink, IDispatch)
    STDMETHOD(GetTypeInfoCount)(UINT*);
    STDMETHOD(GetTypeInfo)(UINT, LCID, ITypeInfo**);
    STDMETHOD(GetIDsOfNames)(REFIID, LPOLESTR*, UINT, LCID, DISPID*);
    STDMETHOD(Invoke)(DISPID, REFIID, LCID, WORD, DISPPARAMS*, LPVARIANT, LPEXCEPINFO, UINT*);
  END_INTERFACE_PART(EventSink)

  DECLARE_INTERFACE_MAP()
  friend class COleControlContainer;
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
