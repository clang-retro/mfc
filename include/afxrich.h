// afxrich.h - CRichEditView, CRichEditDoc and CRichEditCntrItem: documents
// and views built on the rich edit control.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
#ifndef __AFXRICH_H__
#define __AFXRICH_H__

#include "afxwin.h"
#include "afxext.h"
#include "afxdlgs.h"
#include "afxole.h"
#include <richedit.h>
#include <richole.h>

class CRichEditView;
class CRichEditDoc;
class CRichEditCntrItem;

// ---------------------------------------------------------------------
// CRichEditView: a view that is a rich edit control.

class CRichEditView : public CCtrlView {
  DECLARE_DYNCREATE(CRichEditView)
public:
  CRichEditView();
  virtual ~CRichEditView();

  enum WordWrapType { WrapNone = 0, WrapToWindow = 1, WrapToTargetDevice = 2 };

  static const DWORD dwStyleDefault;

  int m_nWordWrap;
  int m_nBulletIndent;  // twips

  CRichEditCtrl& GetRichEditCtrl() const { return *(CRichEditCtrl*)this; }
  CRichEditDoc* GetDocument() const { return (CRichEditDoc*)m_pDocument; }
  long GetTextLength() const;
  long GetTextLengthEx(DWORD dwFlags, UINT uCodePage = (UINT)-1) const;

  // Formatting of the selection.
  CHARFORMAT2& GetCharFormatSelection();
  PARAFORMAT2& GetParaFormatSelection();
  void SetCharFormat(CHARFORMAT2 cf);
  BOOL SetParaFormat(PARAFORMAT2& pf);
  void OnCharEffect(DWORD dwMask, DWORD dwEffect);
  void OnParaAlign(WORD wAlign);
  void OnUpdateCharEffect(CCmdUI* pCmdUI, DWORD dwMask, DWORD dwEffect);
  void OnUpdateParaAlign(CCmdUI* pCmdUI, WORD wAlign);

  // The page, in twips.
  CSize GetPaperSize() const { return m_sizePaper; }
  void SetPaperSize(CSize sizePaper);
  CRect GetMargins() const { return m_rectMargin; }
  void SetMargins(const CRect& rectMargin) { m_rectMargin = rectMargin; }
  CRect GetPageRect() const { return CRect(CPoint(0, 0), m_sizePaper); }
  CRect GetPrintRect() const;
  int GetPrintWidth() const;

  virtual void WrapChanged();
  BOOL FindText(LPCTSTR lpszFind, BOOL bCase = TRUE, BOOL bWord = TRUE, BOOL bNext = TRUE);
  BOOL FindTextSimple(LPCTSTR lpszFind, BOOL bCase = TRUE, BOOL bWord = TRUE, BOOL bNext = TRUE);
  long PrintPage(CDC* pDC, long nIndexStart, long nIndexStop);
  long PrintInsideRect(CDC* pDC, RECT& rectLayout, long nIndexStart, long nIndexStop,
                       BOOL bOutput);
  void Stream(CArchive& ar, BOOL bSelection);
  static DWORD CALLBACK EditStreamCallBack(DWORD_PTR dwCookie, LPBYTE pbBuff, LONG cb, LONG* pcb);
  BOOL CanPaste() const;
  static BOOL AFX_CDECL IsRichEditFormat(CLIPFORMAT cf);

  // Objects in the text. The items are made when they are first asked
  // for, by the document's CreateClientItem.
  CRichEditCntrItem* GetSelectedItem() const;
  CRichEditCntrItem* GetInPlaceActiveItem() const;
  void InsertItem(CRichEditCntrItem* pItem);
  // Pastes the data as a new object; cf 0 embeds it, another format makes
  // a static picture from that format.
  void DoPaste(COleDataObject& dataobj, CLIPFORMAT cf, HMETAFILEPICT hMetaPict);
  virtual BOOL IsSelected(const CObject* pDocItem) const;
  virtual HRESULT QueryAcceptData(LPDATAOBJECT lpdataobj, CLIPFORMAT* lpcfFormat, DWORD dwReco,
                                  BOOL bReally, HGLOBAL hMetaFile);
  virtual HRESULT GetClipboardData(CHARRANGE* lpchrg, DWORD dwReco, LPDATAOBJECT lpRichDataObj,
                                   LPDATAOBJECT* lplpdataobj);
  virtual HMENU GetContextMenu(WORD seltype, LPOLEOBJECT lpoleobj, CHARRANGE* lpchrg);

  LPRICHEDITOLE m_lpRichEditOle;

  virtual void Serialize(CArchive& ar);
  virtual void DeleteContents();
  virtual void OnInitialUpdate();
  virtual BOOL PreCreateWindow(CREATESTRUCT& cs);
  virtual void OnPrepareDC(CDC* pDC, CPrintInfo* pInfo = NULL);
  virtual BOOL OnPreparePrinting(CPrintInfo* pInfo);
  virtual void OnBeginPrinting(CDC* pDC, CPrintInfo* pInfo);
  virtual void OnPrint(CDC* pDC, CPrintInfo* pInfo);
  virtual void OnEndPrinting(CDC* pDC, CPrintInfo* pInfo);
  virtual void OnFindNext(LPCTSTR lpszFind, BOOL bNext, BOOL bCase, BOOL bWord);
  virtual void OnReplaceSel(LPCTSTR lpszFind, BOOL bNext, BOOL bCase, BOOL bWord,
                            LPCTSTR lpszReplace);
  virtual void OnReplaceAll(LPCTSTR lpszFind, LPCTSTR lpszReplace, BOOL bCase, BOOL bWord);
  virtual void OnTextNotFound(LPCTSTR lpszFind);
  virtual void OnPrinterChanged(const CDC& dcPrinter);

  CRect m_rectMargin;
  CSize m_sizePaper;
  CDWordArray m_aPageStart;
  BOOL m_bSyncCharFormat;
  BOOL m_bSyncParaFormat;
  CHARFORMAT2 m_charformat;
  PARAFORMAT2 m_paraformat;

  BOOL SameAsSelected(LPCTSTR lpszCompare, BOOL bCase, BOOL bWord);
  void OnEditFindReplace(BOOL bFindOnly);

  afx_msg int OnCreate(LPCREATESTRUCT lpcs);
  afx_msg void OnDestroy();
  afx_msg void OnSize(UINT nType, int cx, int cy);
  afx_msg void OnUpdateNeedSel(CCmdUI* pCmdUI);
  afx_msg void OnUpdateNeedClip(CCmdUI* pCmdUI);
  afx_msg void OnUpdateNeedText(CCmdUI* pCmdUI);
  afx_msg void OnUpdateNeedFind(CCmdUI* pCmdUI);
  afx_msg void OnUpdateEditUndo(CCmdUI* pCmdUI);
  afx_msg void OnEditCut();
  afx_msg void OnEditCopy();
  afx_msg void OnEditPaste();
  afx_msg void OnEditClear();
  afx_msg void OnEditUndo();
  afx_msg void OnEditSelectAll();
  afx_msg void OnEditFind();
  afx_msg void OnEditReplace();
  afx_msg void OnEditRepeat();
  afx_msg void OnFormatFont();
  afx_msg void OnSelChange(NMHDR* pNMHDR, LRESULT* pResult);
  afx_msg BOOL OnEditChange();
  afx_msg LRESULT OnFindReplaceCmd(WPARAM wParam, LPARAM lParam);
  afx_msg void OnInsertObject();
  afx_msg void OnEditPasteSpecial();
  afx_msg void OnUpdateEditPasteSpecial(CCmdUI* pCmdUI);
  DECLARE_MESSAGE_MAP()

public:
  BEGIN_INTERFACE_PART(RichEditOleCallback, IRichEditOleCallback)
    STDMETHOD(GetNewStorage)(LPSTORAGE* lplpstg);
    STDMETHOD(GetInPlaceContext)(LPOLEINPLACEFRAME* lplpFrame, LPOLEINPLACEUIWINDOW* lplpDoc,
                                 LPOLEINPLACEFRAMEINFO lpFrameInfo);
    STDMETHOD(ShowContainerUI)(BOOL fShow);
    STDMETHOD(QueryInsertObject)(LPCLSID lpclsid, LPSTORAGE lpstg, LONG cp);
    STDMETHOD(DeleteObject)(LPOLEOBJECT lpoleobj);
    STDMETHOD(QueryAcceptData)(LPDATAOBJECT lpdataobj, CLIPFORMAT* lpcfFormat, DWORD reco,
                               BOOL fReally, HGLOBAL hMetaPict);
    STDMETHOD(ContextSensitiveHelp)(BOOL fEnterMode);
    STDMETHOD(GetClipboardData)(CHARRANGE* lpchrg, DWORD reco, LPDATAOBJECT* lplpdataobj);
    STDMETHOD(GetDragDropEffect)(BOOL fDrag, DWORD grfKeyState, LPDWORD pdwEffect);
    STDMETHOD(GetContextMenu)(WORD seltype, LPOLEOBJECT lpoleobj, CHARRANGE* lpchrg,
                              HMENU* lphmenu);
  END_INTERFACE_PART(RichEditOleCallback)

  DECLARE_INTERFACE_MAP()
};

// ---------------------------------------------------------------------
// CRichEditDoc: the document of a rich edit view; the text lives in the
// view's control, objects included. m_bRTF chooses rich text or plain
// text files. It is an OLE server document, which a program serves by
// overriding OnGetEmbeddedItem.

class CRichEditDoc : public COleServerDoc {
  DECLARE_DYNAMIC(CRichEditDoc)
protected:
  CRichEditDoc();

public:
  virtual ~CRichEditDoc();

  BOOL m_bRTF;

  virtual CRichEditView* GetView() const;
  // SF_RTF, or SF_TEXT for a plain text document.
  int GetStreamFormat() const;
  // NULL: override it to serve the document.
  virtual COleServerItem* OnGetEmbeddedItem();
  // Makes the item for an object in the text (preo) or for a new one
  // (NULL). Override it to make your own CRichEditCntrItem class.
  virtual CRichEditCntrItem* CreateClientItem(REOBJECT* preo = NULL) const;
  CRichEditCntrItem* LookupItem(LPOLEOBJECT lpobj) const;
  virtual void Serialize(CArchive& ar);
  virtual void DeleteContents();
  virtual BOOL OnNewDocument();
  virtual void UpdateModifiedFlag();
};

// ---------------------------------------------------------------------
// CRichEditCntrItem: an object in the text. The rich edit control owns
// the object; the item gives the document's OLE functions access to it.

class CRichEditCntrItem : public COleClientItem {
  DECLARE_SERIAL(CRichEditCntrItem)
public:
  CRichEditCntrItem(REOBJECT* preo = NULL, CRichEditDoc* pContainer = NULL);
  virtual ~CRichEditCntrItem();

  CRichEditDoc* GetDocument() const { return (CRichEditDoc*)m_pDocument; }
  // Takes the aspect and the object from the control's description.
  void SyncToRichEditObject(REOBJECT& reo);
  virtual void Release(OLECLOSE dwCloseOption = OLECLOSE_NOSAVE);
  virtual void OnChange(OLE_NOTIFICATION nCode, DWORD dwParam);
  virtual BOOL CanActivate();

  BOOL m_bLock;
};

#endif  // __AFXRICH_H__
