// richtest.cpp - a rich edit document and view: character and paragraph
// formatting of the selection, finding and replacing, saving and loading
// rich text and plain text, laying out printed pages, word wrap, objects
// embedded in the text, and the OLE dialogs.
#include <afxwin.h>
#include <afxext.h>
#include <afxrich.h>
#include <afxodlgs.h>
#include <richole.h>
#include "richtest.h"
#include "testutil.h"
#include "testobj.h"

class CRichDoc : public CRichEditDoc {
  DECLARE_DYNCREATE(CRichDoc)
};
IMPLEMENT_DYNCREATE(CRichDoc, CRichEditDoc)

// Records what an update handler did.
class CTestCmdUI : public CCmdUI {
public:
  int m_nCheck;
  BOOL m_bEnabled;
  CTestCmdUI() : m_nCheck(-1), m_bEnabled(-1) {}
  virtual void Enable(BOOL bOn) { m_bEnabled = bOn; }
  virtual void SetCheck(int nCheck) { m_nCheck = nCheck; }
};

// A device context that says it is printing.
class CPrintingDC : public CDC {
public:
  void SetPrinting() { m_bPrinting = TRUE; }
};

static CString ReadFileText(LPCTSTR lpszPath) {
  CString str;
  CFile file;
  if (!file.Open(lpszPath, CFile::modeRead)) return str;
  UINT n = (UINT)file.GetLength();
  char* p = new char[n + 1];
  n = file.Read(p, n);
  p[n] = 0;
  str = CString(p);
  delete[] p;
  return str;
}

// Ends the next dialog of this thread with nCloseID, from a thread timer
// that runs inside the dialog's own message loop.
static UINT_PTR s_nCloseTimer = 0;
static int s_nCloseID = IDCANCEL;

static BOOL CALLBACK FindDialog(HWND hWnd, LPARAM lParam) {
  TCHAR szClass[16];
  ::GetClassName(hWnd, szClass, 16);
  if (lstrcmp(szClass, _T("#32770")) == 0 && ::IsWindowVisible(hWnd) && ::IsWindowEnabled(hWnd)) {
    *(HWND*)lParam = hWnd;
    return FALSE;
  }
  return TRUE;
}

static void CALLBACK CloseDialogTimer(HWND, UINT, UINT_PTR, DWORD) {
  HWND hDlg = NULL;
  ::EnumThreadWindows(::GetCurrentThreadId(), FindDialog, (LPARAM)&hDlg);
  if (hDlg == NULL) return;
  ::KillTimer(NULL, s_nCloseTimer);
  s_nCloseTimer = 0;
  ::PostMessage(hDlg, WM_COMMAND, MAKEWPARAM(s_nCloseID, BN_CLICKED),
                (LPARAM)::GetDlgItem(hDlg, s_nCloseID));
}

static void AutoCloseDialog(int nID) {
  if (s_nCloseTimer != 0) ::KillTimer(NULL, s_nCloseTimer);
  s_nCloseID = nID;
  s_nCloseTimer = ::SetTimer(NULL, 0, 300, CloseDialogTimer);
}

class CMainFrame : public CFrameWnd {
  DECLARE_DYNCREATE(CMainFrame)
public:
  int m_nStep;
  CMainFrame() : m_nStep(0) {}

  afx_msg int OnCreate(LPCREATESTRUCT lpcs) {
    if (CFrameWnd::OnCreate(lpcs) == -1) return -1;
    SetTimer(1, 250, NULL);
    return 0;
  }

  CRichEditView* View() { return (CRichEditView*)GetActiveView(); }
  CRichDoc* Doc() { return (CRichDoc*)GetActiveDocument(); }

  CString Text() {
    CString str;
    View()->GetWindowText(str);
    return str;
  }

  CString Sel() { return View()->GetRichEditCtrl().GetSelText(); }

  afx_msg void OnTimer(UINT_PTR nIDEvent) {
    if (nIDEvent != 1) {
      CFrameWnd::OnTimer(nIDEvent);
      return;
    }
    CRichEditView* pView = View();
    CRichEditCtrl& ctrl = pView->GetRichEditCtrl();
    switch (m_nStep++) {
      case 0: {
        // The view is a rich edit control.
        TCHAR szClass[64];
        ::GetClassName(pView->m_hWnd, szClass, 64);
        TestLog("class %s", (const char*)CStringA(szClass));
        CHECK(lstrcmpi(szClass, RICHEDIT_CLASS) == 0);
        CHECK(Doc() != NULL && Doc()->GetView() == pView && Doc()->m_bRTF);

        // A known starting format (the control's own default depends on
        // the system font).
        CHARFORMAT2 cfDefault;
        memset(&cfDefault, 0, sizeof(cfDefault));
        cfDefault.dwMask = CFM_BOLD | CFM_ITALIC | CFM_FACE | CFM_SIZE;
        cfDefault.yHeight = 200;
        lstrcpy(cfDefault.szFaceName, _T("Arial"));
        CHECK(ctrl.SetDefaultCharFormat(cfDefault));
        ctrl.SetWindowText(_T("Hello world. Hello again.\r\nSecond line"));
        CHECK(pView->GetTextLength() > 0);
        // Bold on the first word.
        ctrl.SetSel(0, 5);
        pView->OnCharEffect(CFM_BOLD, CFE_BOLD);
        CHARFORMAT2& cf = pView->GetCharFormatSelection();
        CHECK((cf.dwMask & CFM_BOLD) && (cf.dwEffects & CFE_BOLD));
        CTestCmdUI ui;
        pView->OnUpdateCharEffect(&ui, CFM_BOLD, CFE_BOLD);
        CHECK(ui.m_nCheck == 1);
        // A selection that is partly bold is neither.
        ctrl.SetSel(0, 10);
        pView->m_bSyncCharFormat = TRUE;  // as EN_SELCHANGE would
        pView->OnUpdateCharEffect(&ui, CFM_BOLD, CFE_BOLD);
        CHECK(ui.m_nCheck == 2);
        // ...and turning it on makes all of it bold.
        pView->OnCharEffect(CFM_BOLD, CFE_BOLD);
        pView->OnUpdateCharEffect(&ui, CFM_BOLD, CFE_BOLD);
        CHECK(ui.m_nCheck == 1);
        // Italic, then off again.
        pView->OnCharEffect(CFM_ITALIC, CFE_ITALIC);
        pView->OnCharEffect(CFM_ITALIC, CFE_ITALIC);
        pView->OnUpdateCharEffect(&ui, CFM_ITALIC, CFE_ITALIC);
        CHECK(ui.m_nCheck == 0);

        // Centre the second paragraph.
        int nLine2 = ctrl.LineIndex(1);
        ctrl.SetSel(nLine2, nLine2);
        pView->OnParaAlign(PFA_CENTER);
        pView->m_bSyncParaFormat = TRUE;
        CHECK(pView->GetParaFormatSelection().wAlignment == PFA_CENTER);
        pView->OnUpdateParaAlign(&ui, PFA_CENTER);
        CHECK(ui.m_nCheck == 1);
        ctrl.SetSel(0, 0);
        pView->m_bSyncParaFormat = TRUE;
        CHECK(pView->GetParaFormatSelection().wAlignment != PFA_CENTER);

        // The edit commands' update handlers.
        CTestCmdUI uiCut;
        uiCut.m_nID = ID_EDIT_CUT;
        uiCut.DoUpdate(pView, FALSE);
        CHECK(uiCut.m_bEnabled == FALSE);
        CHECK(Doc()->IsModified());
        TestSaveWindowBitmap(m_hWnd, _T("richtest.bmp"));
        break;
      }
      case 1: {
        // Finding, case-insensitively, forward and wrapping.
        ctrl.SetSel(0, 0);
        CHECK(pView->FindText(_T("hello"), FALSE, FALSE, TRUE));
        CHECK(Sel() == _T("Hello"));
        CHARRANGE cr;
        ctrl.GetSel(cr);
        CHECK(cr.cpMin == 0 && cr.cpMax == 5);
        CHECK(pView->FindText(_T("hello"), FALSE, FALSE, TRUE));
        ctrl.GetSel(cr);
        CHECK(cr.cpMin == 13);
        CHECK(pView->FindText(_T("hello"), FALSE, FALSE, TRUE));
        ctrl.GetSel(cr);
        CHECK(cr.cpMin == 0);
        // Backwards from the start wraps to the last one.
        ctrl.SetSel(0, 0);
        CHECK(pView->FindText(_T("hello"), FALSE, FALSE, FALSE));
        ctrl.GetSel(cr);
        CHECK(cr.cpMin == 13);
        // Case and whole words.
        CHECK(!pView->FindText(_T("hello"), TRUE, FALSE, TRUE));
        CHECK(!pView->FindText(_T("Hell"), FALSE, TRUE, TRUE));
        CHECK(pView->FindText(_T("Hell"), FALSE, FALSE, TRUE));
        // Replace all.
        pView->OnReplaceAll(_T("hello"), _T("Bye"), FALSE, TRUE);
        TestLog("after replace: %s", (const char*)CStringA(Text()));
        CHECK(Text().Find(_T("Bye world. Bye again.")) == 0);
        CHECK(Text().Find(_T("Hello")) < 0);
        break;
      }
      case 2: {
        // Rich text out and back in, formatting and all.
        CString strText = Text();
        TCHAR szTemp[MAX_PATH];
        ::GetTempPath(MAX_PATH, szTemp);
        CString strRtf = CString(szTemp) + _T("richtest.rtf");
        CString strTxt = CString(szTemp) + _T("richtest.txt");
        CHECK(Doc()->OnSaveDocument(strRtf));
        CString strFile = ReadFileText(strRtf);
        CHECK(strFile.Left(5) == _T("{\\rtf"));
        Doc()->OnNewDocument();
        CHECK(pView->GetTextLength() == 0);
        CHECK(!Doc()->IsModified());
        CHECK(Doc()->OnOpenDocument(strRtf));
        CString strLoaded = Text();
        strLoaded.Replace(_T("\r\n"), _T("\r"));
        CString strWant = strText;
        strWant.Replace(_T("\r\n"), _T("\r"));
        TestLog("loaded: %s", (const char*)CStringA(strLoaded));
        CHECK(strLoaded == strWant);
        CHECK(!Doc()->IsModified());
        ctrl.SetSel(0, 3);
        pView->m_bSyncCharFormat = TRUE;
        CHECK(pView->GetCharFormatSelection().dwEffects & CFE_BOLD);
        // Plain text: the words alone.
        Doc()->m_bRTF = FALSE;
        CHECK(Doc()->OnSaveDocument(strTxt));
        strFile = ReadFileText(strTxt);
        TestLog("text file: %s", (const char*)CStringA(strFile));
        CHECK(strFile.Left(21) == _T("Bye world. Bye again."));
        CHECK(strFile.Find(_T("{")) < 0);
        Doc()->m_bRTF = TRUE;
        ::DeleteFile(strRtf);
        ::DeleteFile(strTxt);
        break;
      }
      case 3: {
        // Pages: a long document takes more than one.
        CString str;
        for (int i = 0; i < 200; i++) {
          CString strLine;
          strLine.Format(_T("Line %d of the document, long enough to see.\r\n"), i);
          str += strLine;
        }
        ctrl.SetWindowText(str);
        CPrintingDC dc;
        if (!AfxGetApp()->CreatePrinterDC(dc)) dc.CreateDC(_T("DISPLAY"), NULL, NULL, NULL);
        pView->OnPrinterChanged(dc);
        CSize sizePaper = pView->GetPaperSize();
        TestLog("paper %d x %d twips", sizePaper.cx, sizePaper.cy);
        CHECK(sizePaper.cx > 0 && sizePaper.cy > 0);
        CRect rectPrint = pView->GetPrintRect();
        CHECK(rectPrint.Width() == sizePaper.cx - 3600);
        CRect rect = rectPrint;
        long nNext = pView->PrintInsideRect(&dc, rect, 0, -1, FALSE);
        long nLen = pView->GetTextLength();
        TestLog("first page ends at %ld of %ld", nNext, nLen);
        // Wine's rich edit has no EM_FORMATRANGE yet (it answers 0), so
        // there the pages can only be checked to stop.
        BOOL bFormats = nNext != 0;
        if (bFormats) {
          CHECK(nNext > 0 && nNext < nLen);
          long nNext2 = pView->PrintInsideRect(&dc, rect = rectPrint, nNext, -1, FALSE);
          CHECK(nNext2 > nNext);
        } else {
          TestLog("EM_FORMATRANGE does nothing here; checking pagination stops");
        }
        ctrl.FormatRange(NULL, FALSE);
        CPrintInfo info;
        info.m_bPreview = TRUE;  // so no printing dialog is involved
        dc.SetPrinting();  // as during a print job
        pView->OnBeginPrinting(&dc, &info);
        info.m_nCurPage = 1;
        pView->OnPrepareDC(&dc, &info);
        CHECK(info.m_bContinuePrinting);
        info.m_nCurPage = 2;
        pView->OnPrepareDC(&dc, &info);
        CHECK(info.m_bContinuePrinting == bFormats);
        pView->OnEndPrinting(&dc, &info);
        break;
      }
      case 4: {
        // Word wrap: a long line is one line without it.
        CString str;
        for (int i = 0; i < 40; i++) str += _T("wrap ");
        ctrl.SetWindowText(str);
        pView->m_nWordWrap = CRichEditView::WrapToWindow;
        pView->WrapChanged();
        int nWrapped = ctrl.GetLineCount();
        pView->m_nWordWrap = CRichEditView::WrapNone;
        pView->WrapChanged();
        int nUnwrapped = ctrl.GetLineCount();
        TestLog("lines %d wrapped, %d not", nWrapped, nUnwrapped);
        CHECK(nWrapped > 1 && nUnwrapped == 1);
        pView->m_nWordWrap = CRichEditView::WrapToWindow;
        pView->WrapChanged();
        Doc()->SetModifiedFlag(FALSE);
        break;
      }
      case 5: {
        // An object embedded in the text, through the view.
        ctrl.SetWindowText(_T("ab"));
        ctrl.SetSel(1, 1);
        CRichEditCntrItem* pItem = Doc()->CreateClientItem(NULL);
        BOOL bCreated = pItem->CreateNewItem(CLSID_TestObject);
        CHECK(bCreated);
        if (!bCreated) {
          pItem->Delete();
          break;
        }
        pView->InsertItem(pItem);
        IRichEditOle* pReOle = ctrl.GetIRichEditOle();
        CHECK(pReOle != NULL);
        if (pReOle != NULL) {
          TestLog("objects %d", pReOle->GetObjectCount());
          CHECK(pReOle->GetObjectCount() == 1);
          pReOle->Release();
        }
        CHECK(pView->GetTextLength() == 3);
        CHECK(Doc()->IsModified());
        // Selecting the object's character selects the item.
        ctrl.SetSel(1, 2);
        CHECK(pView->GetSelectedItem() == pItem);
        CHECK(pView->IsSelected(pItem));
        ctrl.SetSel(0, 1);
        CHECK(pView->GetSelectedItem() == NULL);
        CHECK(!pView->IsSelected(pItem));
        pView->UpdateWindow();
        TestSaveWindowBitmap(m_hWnd, _T("richobj.bmp"));

        // Saved as rich text the object goes with it, and comes back.
        TCHAR szTemp[MAX_PATH];
        ::GetTempPath(MAX_PATH, szTemp);
        CString strRtf = CString(szTemp) + _T("richobj.rtf");
        CHECK(Doc()->OnSaveDocument(strRtf));
        CString strFile = ReadFileText(strRtf);
        TestLog("rtf %d chars, object at %d", strFile.GetLength(), strFile.Find(_T("\\object")));
        // Wine's rich edit writes only the object's picture.
        CHECK(strFile.Find(_T("\\object")) >= 0 || strFile.Find(_T("\\pict")) >= 0);
        CHECK(Doc()->OnOpenDocument(strRtf));
        pReOle = ctrl.GetIRichEditOle();
        if (pReOle != NULL) {
          TestLog("objects after loading %d", pReOle->GetObjectCount());
          CHECK(pReOle->GetObjectCount() == 1);
          pReOle->Release();
        }
        ctrl.SetSel(1, 2);
        CRichEditCntrItem* pLoaded = pView->GetSelectedItem();
        CHECK(pLoaded != NULL);
        if (pLoaded != NULL) CHECK(pLoaded->GetDocument() == Doc());
        ::DeleteFile(strRtf);
        Doc()->SetModifiedFlag(FALSE);
        break;
      }
      case 6: {
        // The OLE dialogs: Insert Object, cancelled.
        KillTimer(1);
        {
          COleInsertDialog dlg;
          AutoCloseDialog(IDCANCEL);
          INT_PTR nResult = dlg.DoModal();
          TestLog("insert object: %d (%u)", (int)nResult, dlg.GetLastError());
          // Wine's oledlg ends a cancelled Insert Object with OLEUI_FALSE.
          CHECK(nResult == IDCANCEL || (nResult == IDABORT && dlg.GetLastError() == OLEUI_FALSE));
        }
        // Paste Special over text on the clipboard: OK takes the text.
        COleDataSource* pSource = new COleDataSource;
        HGLOBAL hText = ::GlobalAlloc(GMEM_MOVEABLE, 6);
        memcpy(::GlobalLock(hText), "paste", 6);
        ::GlobalUnlock(hText);
        pSource->CacheGlobalData(CF_TEXT, hText);
        pSource->SetClipboard();
        {
          COlePasteSpecialDialog dlg;
          dlg.AddFormat(CF_TEXT, TYMED_HGLOBAL, AFX_IDS_RTF_FORMAT_TEXT, FALSE, FALSE);
          dlg.AddStandardFormats();
          AutoCloseDialog(IDOK);
          INT_PTR nResult = dlg.DoModal();
          TestLog("paste special: %d (%u), index %d, type %u", (int)nResult, dlg.GetLastError(),
                  dlg.GetPasteIndex(), dlg.GetSelectionType());
          CHECK(nResult == IDOK);
          CHECK(dlg.GetPasteIndex() == 0);
          CHECK(dlg.GetSelectionType() == COlePasteSpecialDialog::pasteOther);
        }
        // ...and the view's command pastes it into the text.
        ctrl.SetWindowText(_T(""));
        AutoCloseDialog(IDOK);
        pView->SendMessage(WM_COMMAND, ID_EDIT_PASTE_SPECIAL);
        TestLog("after paste special: %s", (const char*)CStringA(Text()));
        CHECK(Text() == _T("paste"));
        ::OleSetClipboard(NULL);
        // Links: no links, so whatever oledlg does with that, it ends.
        {
          COleLinksDialog dlg(Doc(), pView);
          AutoCloseDialog(IDCANCEL);
          INT_PTR nResult = dlg.DoModal();
          TestLog("links: %d (%u)", (int)nResult, dlg.GetLastError());
          CHECK(nResult == IDOK || nResult == IDCANCEL || nResult == IDABORT);
        }
        ::KillTimer(NULL, s_nCloseTimer);
        s_nCloseTimer = 0;
        SetTimer(1, 250, NULL);
        break;
      }
      default:
        Doc()->SetModifiedFlag(FALSE);
        KillTimer(1);
        PostMessage(WM_CLOSE);
        break;
    }
  }
  DECLARE_MESSAGE_MAP()
};
IMPLEMENT_DYNCREATE(CMainFrame, CFrameWnd)
BEGIN_MESSAGE_MAP(CMainFrame, CFrameWnd)
  ON_WM_CREATE()
  ON_WM_TIMER()
END_MESSAGE_MAP()

class CRichApp : public CWinApp {
public:
  virtual BOOL InitInstance() {
    TestOpenLog(_T("richtest.log"));
    if (!AfxOleInit()) {
      TestLog("AfxOleInit failed");
      g_nFailures++;
      return FALSE;
    }
    RegisterTestObject();
    CSingleDocTemplate* pTemplate =
        new CSingleDocTemplate(IDR_MAINFRAME, RUNTIME_CLASS(CRichDoc), RUNTIME_CLASS(CMainFrame),
                               RUNTIME_CLASS(CRichEditView));
    AddDocTemplate(pTemplate);
    CCommandLineInfo cmdInfo;
    ParseCommandLine(cmdInfo);
    if (!ProcessShellCommand(cmdInfo)) {
      TestLog("ProcessShellCommand failed");
      g_nFailures++;
      return FALSE;
    }
    m_pMainWnd->SetWindowPos(NULL, 0, 0, 480, 320, SWP_NOMOVE | SWP_NOZORDER);
    m_pMainWnd->ShowWindow(SW_SHOW);
    m_pMainWnd->UpdateWindow();
    return TRUE;
  }
  virtual int ExitInstance() {
    UnregisterTestObject();
    CWinApp::ExitInstance();
    return TestFinish();
  }
};

CRichApp theApp;
