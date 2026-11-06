// afxrich.h - CRichEditView, CRichEditDoc and CRichEditCntrItem: documents
// and views built on the rich edit control.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
#ifndef __AFXRICH_H__
#define __AFXRICH_H__

#include "afxwin.h"
#include "afxext.h"
#include "afxdlgs.h"
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
  DECLARE_MESSAGE_MAP()
};

// ---------------------------------------------------------------------
// CRichEditDoc: the document of a rich edit view; the text lives in the
// view's control. m_bRTF chooses rich text or plain text files.

class CRichEditDoc : public CDocument {
  DECLARE_DYNAMIC(CRichEditDoc)
protected:
  CRichEditDoc();

public:
  virtual ~CRichEditDoc();

  BOOL m_bRTF;

  virtual CRichEditView* GetView() const;
  virtual CRichEditCntrItem* CreateClientItem(REOBJECT* preo = NULL) const;
  virtual void Serialize(CArchive& ar);
  virtual void DeleteContents();
  virtual BOOL OnNewDocument();
};

// ---------------------------------------------------------------------
// CRichEditCntrItem: an object embedded in the text. Embedding needs the
// OLE container classes, which are not there yet; the class exists so
// that a document's CreateClientItem can be written.

class CRichEditCntrItem : public CCmdTarget {
  DECLARE_SERIAL(CRichEditCntrItem)
public:
  CRichEditCntrItem(REOBJECT* preo = NULL, CRichEditDoc* pContainer = NULL);
  virtual ~CRichEditCntrItem();

  CRichEditDoc* GetDocument() const { return m_pDocument; }
  void SyncToRichEditObject(REOBJECT& reo);

  CRichEditDoc* m_pDocument;
  BOOL m_bLock;
};

#endif  // __AFXRICH_H__
