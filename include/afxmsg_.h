// afxmsg_.h - message maps.
//
// Each entry carries a small function (a captureless lambda) that unpacks
// the message parameters and calls the handler with the types MFC
// documents, so no table of handler signatures is needed.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#ifndef __AFXMSG__H__
#define __AFXMSG__H__

class CCmdTarget;
class CCmdUI;

typedef void (CCmdTarget::*AFX_PMSG)(void);
typedef LRESULT (*AFX_THUNK)(CCmdTarget* pTarget, UINT nMsg, WPARAM wParam, LPARAM lParam);

enum AfxSig {
  AfxSig_end = 0,
  AfxSig_thunk = 1,      // pThunk does the call
  AfxSig_registered = 2  // nSig is the address of a registered message number
};

struct AFX_MSGMAP_ENTRY {
  UINT nMessage;   // window message, WM_COMMAND, WM_NOTIFY or reflected
  UINT nCode;      // control or notification code
  UINT nID;        // control or command ID (0 for window messages)
  UINT nLastID;    // last ID of a range
  UINT_PTR nSig;   // AfxSig, or for registered messages a UINT*
  AFX_PMSG pfn;    // not used by these entries
  AFX_THUNK pThunk;
};

struct AFX_MSGMAP {
  const AFX_MSGMAP*(PASCAL* pfnGetBaseMap)();
  const AFX_MSGMAP_ENTRY* lpEntries;
};

// What OnCmdMsg passes with WM_NOTIFY commands.
struct AFX_NOTIFY {
  LRESULT* pResult;
  NMHDR* pNMHDR;
};

// Filled in by OnCmdMsg when asked only whether a handler exists.
struct AFX_CMDHANDLERINFO {
  CCmdTarget* pTarget;
  AFX_PMSG pmf;
};

// Command codes
#define CN_COMMAND 0
#define CN_UPDATE_COMMAND_UI ((UINT)(-1))
#define CN_EVENT ((UINT)(-100))
#define CN_OLECOMMAND ((UINT)(-101))
#define CN_OLE_UNREGISTER ((UINT)(-102))

#define WM_REFLECT_BASE 0xBC00
#ifndef WM_CTLCOLOR
#define WM_CTLCOLOR 0x0019
#endif

#define DECLARE_MESSAGE_MAP() \
protected: \
  static const AFX_MSGMAP* PASCAL GetThisMessageMap(); \
  virtual const AFX_MSGMAP* GetMessageMap() const;

#define BEGIN_TEMPLATE_MESSAGE_MAP(theClass, type_name, baseClass) \
  template <typename type_name> \
  const AFX_MSGMAP* theClass<type_name>::GetMessageMap() const { return GetThisMessageMap(); } \
  template <typename type_name> \
  const AFX_MSGMAP* PASCAL theClass<type_name>::GetThisMessageMap() { \
    typedef theClass<type_name> ThisClass; \
    typedef baseClass TheBaseClass; \
    (void)sizeof(ThisClass*); \
    static const AFX_MSGMAP_ENTRY _messageEntries[] = {

#define BEGIN_MESSAGE_MAP(theClass, baseClass) \
  const AFX_MSGMAP* theClass::GetMessageMap() const { return GetThisMessageMap(); } \
  const AFX_MSGMAP* PASCAL theClass::GetThisMessageMap() { \
    typedef theClass ThisClass; \
    typedef baseClass TheBaseClass; \
    (void)sizeof(ThisClass*); \
    static const AFX_MSGMAP_ENTRY _messageEntries[] = {

#define END_MESSAGE_MAP() \
      {0, 0, 0, 0, AfxSig_end, (AFX_PMSG)0, (AFX_THUNK)0}}; \
    static const AFX_MSGMAP messageMap = {&TheBaseClass::GetThisMessageMap, &_messageEntries[0]}; \
    return &messageMap; \
  }

#define _AFX_THUNK(body) \
  [](CCmdTarget* _t, UINT _m, WPARAM _w, LPARAM _l) -> LRESULT { \
    (void)_t; (void)_m; (void)_w; (void)_l; body }
#define _AFX_THIS static_cast<ThisClass*>(_t)

// Handler names. Visual C++ 6 code writes them as plain names
// (OnAppAbout, CWinApp::OnFileNew); Visual C++ 2005 and later code writes
// member pointers (&CMyApp::OnAppAbout). Standard C++ has no single
// expression that accepts both, so the second form needs
// _AFX_MSGMAP_MEMBER_POINTERS defined before afxwin.h is included.
#ifdef _AFX_MSGMAP_MEMBER_POINTERS
#define _AFX_CALL(memberFxn) (_AFX_THIS->*memberFxn)
#else
#define _AFX_CALL(memberFxn) _AFX_THIS->memberFxn
#endif

// ---------------------------------------------------------------------
// Commands. For these, the thunk gets the command ID in wParam and the
// CCmdUI* or AFX_NOTIFY* in lParam.

#define ON_COMMAND(id, memberFxn) \
  {WM_COMMAND, CN_COMMAND, (WORD)(id), (WORD)(id), AfxSig_thunk, (AFX_PMSG)0, \
   _AFX_THUNK(_AFX_CALL(memberFxn)(); return TRUE;)},
#define ON_COMMAND_EX(id, memberFxn) \
  {WM_COMMAND, CN_COMMAND, (WORD)(id), (WORD)(id), AfxSig_thunk, (AFX_PMSG)0, \
   _AFX_THUNK(return (LRESULT)(_AFX_CALL(memberFxn)((UINT)_w) != 0);)},
#define ON_COMMAND_RANGE(id, idLast, memberFxn) \
  {WM_COMMAND, CN_COMMAND, (WORD)(id), (WORD)(idLast), AfxSig_thunk, (AFX_PMSG)0, \
   _AFX_THUNK(_AFX_CALL(memberFxn)((UINT)_w); return TRUE;)},
#define ON_COMMAND_EX_RANGE(id, idLast, memberFxn) \
  {WM_COMMAND, CN_COMMAND, (WORD)(id), (WORD)(idLast), AfxSig_thunk, (AFX_PMSG)0, \
   _AFX_THUNK(return (LRESULT)(_AFX_CALL(memberFxn)((UINT)_w) != 0);)},
#define ON_UPDATE_COMMAND_UI(id, memberFxn) \
  {WM_COMMAND, CN_UPDATE_COMMAND_UI, (WORD)(id), (WORD)(id), AfxSig_thunk, (AFX_PMSG)0, \
   _AFX_THUNK(_AFX_CALL(memberFxn)((CCmdUI*)_l); return TRUE;)},
#define ON_UPDATE_COMMAND_UI_RANGE(id, idLast, memberFxn) \
  {WM_COMMAND, CN_UPDATE_COMMAND_UI, (WORD)(id), (WORD)(idLast), AfxSig_thunk, (AFX_PMSG)0, \
   _AFX_THUNK(_AFX_CALL(memberFxn)((CCmdUI*)_l); return TRUE;)},
#define ON_UPDATE_COMMAND_UI_REFLECT(memberFxn) \
  {WM_COMMAND + WM_REFLECT_BASE, CN_UPDATE_COMMAND_UI, 0, 0, AfxSig_thunk, (AFX_PMSG)0, \
   _AFX_THUNK(_AFX_CALL(memberFxn)((CCmdUI*)_l); return TRUE;)},

#define ON_CONTROL(wNotifyCode, id, memberFxn) \
  {WM_COMMAND, (WORD)(wNotifyCode), (WORD)(id), (WORD)(id), AfxSig_thunk, (AFX_PMSG)0, \
   _AFX_THUNK(_AFX_CALL(memberFxn)(); return TRUE;)},
#define ON_CONTROL_RANGE(wNotifyCode, id, idLast, memberFxn) \
  {WM_COMMAND, (WORD)(wNotifyCode), (WORD)(id), (WORD)(idLast), AfxSig_thunk, (AFX_PMSG)0, \
   _AFX_THUNK(_AFX_CALL(memberFxn)((UINT)_w); return TRUE;)},
#define ON_CONTROL_REFLECT(wNotifyCode, memberFxn) \
  {WM_COMMAND + WM_REFLECT_BASE, (WORD)(wNotifyCode), 0, 0, AfxSig_thunk, (AFX_PMSG)0, \
   _AFX_THUNK(_AFX_CALL(memberFxn)(); return TRUE;)},
#define ON_CONTROL_REFLECT_EX(wNotifyCode, memberFxn) \
  {WM_COMMAND + WM_REFLECT_BASE, (WORD)(wNotifyCode), 0, 0, AfxSig_thunk, (AFX_PMSG)0, \
   _AFX_THUNK(return (LRESULT)(_AFX_CALL(memberFxn)() != 0);)},

#define ON_NOTIFY(wNotifyCode, id, memberFxn) \
  {WM_NOTIFY, (WORD)(int)(wNotifyCode), (WORD)(id), (WORD)(id), AfxSig_thunk, (AFX_PMSG)0, \
   _AFX_THUNK(_AFX_CALL(memberFxn)(((AFX_NOTIFY*)_l)->pNMHDR, ((AFX_NOTIFY*)_l)->pResult); \
              return TRUE;)},
#define ON_NOTIFY_RANGE(wNotifyCode, id, idLast, memberFxn) \
  {WM_NOTIFY, (WORD)(int)(wNotifyCode), (WORD)(id), (WORD)(idLast), AfxSig_thunk, (AFX_PMSG)0, \
   _AFX_THUNK(_AFX_CALL(memberFxn)((UINT)_w, ((AFX_NOTIFY*)_l)->pNMHDR, \
                                   ((AFX_NOTIFY*)_l)->pResult); return TRUE;)},
#define ON_NOTIFY_EX(wNotifyCode, id, memberFxn) \
  {WM_NOTIFY, (WORD)(int)(wNotifyCode), (WORD)(id), (WORD)(id), AfxSig_thunk, (AFX_PMSG)0, \
   _AFX_THUNK(return (LRESULT)(_AFX_CALL(memberFxn)((UINT)_w, ((AFX_NOTIFY*)_l)->pNMHDR, \
                                                    ((AFX_NOTIFY*)_l)->pResult) != 0);)},
#define ON_NOTIFY_EX_RANGE(wNotifyCode, id, idLast, memberFxn) \
  {WM_NOTIFY, (WORD)(int)(wNotifyCode), (WORD)(id), (WORD)(idLast), AfxSig_thunk, (AFX_PMSG)0, \
   _AFX_THUNK(return (LRESULT)(_AFX_CALL(memberFxn)((UINT)_w, ((AFX_NOTIFY*)_l)->pNMHDR, \
                                                    ((AFX_NOTIFY*)_l)->pResult) != 0);)},
#define ON_NOTIFY_REFLECT(wNotifyCode, memberFxn) \
  {WM_NOTIFY + WM_REFLECT_BASE, (WORD)(int)(wNotifyCode), 0, 0, AfxSig_thunk, (AFX_PMSG)0, \
   _AFX_THUNK(_AFX_CALL(memberFxn)(((AFX_NOTIFY*)_l)->pNMHDR, ((AFX_NOTIFY*)_l)->pResult); \
              return TRUE;)},
#define ON_NOTIFY_REFLECT_EX(wNotifyCode, memberFxn) \
  {WM_NOTIFY + WM_REFLECT_BASE, (WORD)(int)(wNotifyCode), 0, 0, AfxSig_thunk, (AFX_PMSG)0, \
   _AFX_THUNK(return (LRESULT)(_AFX_CALL(memberFxn)(((AFX_NOTIFY*)_l)->pNMHDR, \
                                                    ((AFX_NOTIFY*)_l)->pResult) != 0);)},

// ---------------------------------------------------------------------
// User-defined messages

#define ON_MESSAGE(message, memberFxn) \
  {(UINT)(message), 0, 0, 0, AfxSig_thunk, (AFX_PMSG)0, \
   _AFX_THUNK(return (LRESULT)_AFX_CALL(memberFxn)(_w, _l);)},
#define ON_MESSAGE_VOID(message, memberFxn) \
  {(UINT)(message), 0, 0, 0, AfxSig_thunk, (AFX_PMSG)0, \
   _AFX_THUNK(_AFX_CALL(memberFxn)(); return 0;)},
#define ON_REGISTERED_MESSAGE(nMessageVariable, memberFxn) \
  {0xC000, 0, 0, 0, (UINT_PTR)(UINT*)(&nMessageVariable), (AFX_PMSG)0, \
   _AFX_THUNK(return (LRESULT)_AFX_CALL(memberFxn)(_w, _l);)},
#define ON_THREAD_MESSAGE(message, memberFxn) \
  {(UINT)(message), 0, 0, 0, AfxSig_thunk, (AFX_PMSG)0, \
   _AFX_THUNK(_AFX_CALL(memberFxn)(_w, _l); return TRUE;)},
#define ON_REGISTERED_THREAD_MESSAGE(nMessageVariable, memberFxn) \
  {0xC000, 0, 0, 0, (UINT_PTR)(UINT*)(&nMessageVariable), (AFX_PMSG)0, \
   _AFX_THUNK(_AFX_CALL(memberFxn)(_w, _l); return TRUE;)},

// ---------------------------------------------------------------------
// Control notifications

#define ON_BN_CLICKED(id, memberFxn) ON_CONTROL(BN_CLICKED, id, memberFxn)
#define ON_BN_DOUBLECLICKED(id, memberFxn) ON_CONTROL(BN_DOUBLECLICKED, id, memberFxn)
#define ON_BN_SETFOCUS(id, memberFxn) ON_CONTROL(BN_SETFOCUS, id, memberFxn)
#define ON_BN_KILLFOCUS(id, memberFxn) ON_CONTROL(BN_KILLFOCUS, id, memberFxn)
#define ON_BN_PAINT(id, memberFxn) ON_CONTROL(BN_PAINT, id, memberFxn)
#define ON_BN_HILITE(id, memberFxn) ON_CONTROL(BN_HILITE, id, memberFxn)
#define ON_BN_UNHILITE(id, memberFxn) ON_CONTROL(BN_UNHILITE, id, memberFxn)
#define ON_BN_DISABLE(id, memberFxn) ON_CONTROL(BN_DISABLE, id, memberFxn)
#define ON_BN_PUSHED ON_BN_HILITE
#define ON_BN_UNPUSHED ON_BN_UNHILITE
#define ON_BN_DBLCLK ON_BN_DOUBLECLICKED

#define ON_EN_SETFOCUS(id, memberFxn) ON_CONTROL(EN_SETFOCUS, id, memberFxn)
#define ON_EN_KILLFOCUS(id, memberFxn) ON_CONTROL(EN_KILLFOCUS, id, memberFxn)
#define ON_EN_CHANGE(id, memberFxn) ON_CONTROL(EN_CHANGE, id, memberFxn)
#define ON_EN_UPDATE(id, memberFxn) ON_CONTROL(EN_UPDATE, id, memberFxn)
#define ON_EN_ERRSPACE(id, memberFxn) ON_CONTROL(EN_ERRSPACE, id, memberFxn)
#define ON_EN_MAXTEXT(id, memberFxn) ON_CONTROL(EN_MAXTEXT, id, memberFxn)
#define ON_EN_HSCROLL(id, memberFxn) ON_CONTROL(EN_HSCROLL, id, memberFxn)
#define ON_EN_VSCROLL(id, memberFxn) ON_CONTROL(EN_VSCROLL, id, memberFxn)

#define ON_LBN_ERRSPACE(id, memberFxn) ON_CONTROL(LBN_ERRSPACE, id, memberFxn)
#define ON_LBN_SELCHANGE(id, memberFxn) ON_CONTROL(LBN_SELCHANGE, id, memberFxn)
#define ON_LBN_DBLCLK(id, memberFxn) ON_CONTROL(LBN_DBLCLK, id, memberFxn)
#define ON_LBN_SELCANCEL(id, memberFxn) ON_CONTROL(LBN_SELCANCEL, id, memberFxn)
#define ON_LBN_SETFOCUS(id, memberFxn) ON_CONTROL(LBN_SETFOCUS, id, memberFxn)
#define ON_LBN_KILLFOCUS(id, memberFxn) ON_CONTROL(LBN_KILLFOCUS, id, memberFxn)

#define ON_CBN_ERRSPACE(id, memberFxn) ON_CONTROL(CBN_ERRSPACE, id, memberFxn)
#define ON_CBN_SELCHANGE(id, memberFxn) ON_CONTROL(CBN_SELCHANGE, id, memberFxn)
#define ON_CBN_DBLCLK(id, memberFxn) ON_CONTROL(CBN_DBLCLK, id, memberFxn)
#define ON_CBN_SETFOCUS(id, memberFxn) ON_CONTROL(CBN_SETFOCUS, id, memberFxn)
#define ON_CBN_KILLFOCUS(id, memberFxn) ON_CONTROL(CBN_KILLFOCUS, id, memberFxn)
#define ON_CBN_EDITCHANGE(id, memberFxn) ON_CONTROL(CBN_EDITCHANGE, id, memberFxn)
#define ON_CBN_EDITUPDATE(id, memberFxn) ON_CONTROL(CBN_EDITUPDATE, id, memberFxn)
#define ON_CBN_DROPDOWN(id, memberFxn) ON_CONTROL(CBN_DROPDOWN, id, memberFxn)
#define ON_CBN_CLOSEUP(id, memberFxn) ON_CONTROL(CBN_CLOSEUP, id, memberFxn)
#define ON_CBN_SELENDOK(id, memberFxn) ON_CONTROL(CBN_SELENDOK, id, memberFxn)
#define ON_CBN_SELENDCANCEL(id, memberFxn) ON_CONTROL(CBN_SELENDCANCEL, id, memberFxn)

#define ON_STN_CLICKED(id, memberFxn) ON_CONTROL(STN_CLICKED, id, memberFxn)
#define ON_STN_DBLCLK(id, memberFxn) ON_CONTROL(STN_DBLCLK, id, memberFxn)
#define ON_STN_ENABLE(id, memberFxn) ON_CONTROL(STN_ENABLE, id, memberFxn)
#define ON_STN_DISABLE(id, memberFxn) ON_CONTROL(STN_DISABLE, id, memberFxn)

// ---------------------------------------------------------------------
// Window messages (ON_WM_PAINT and the rest)

#include <afxmsg_wm_.h>

#endif // __AFXMSG__H__
