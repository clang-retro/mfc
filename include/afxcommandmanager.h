// afxcommandmanager.h - CCommandManager (GetCmdMgr()), which knows each
// command's image in the toolbars' shared image strip, so that menus and
// buttons made later show the same picture.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#ifndef __AFXCOMMANDMANAGER_H__
#define __AFXCOMMANDMANAGER_H__

#ifndef __AFXWIN_H__
#include <afxwin.h>
#endif
#include <afxtempl.h>

class CCommandManager {
public:
  CCommandManager();
  virtual ~CCommandManager();

  void SetCmdImage(UINT uiCmd, int iImage, BOOL bUserImage);
  int GetCmdImage(UINT uiCmd, BOOL bUserImage = FALSE) const;
  void ClearCmdImage(UINT uiCmd);
  void ClearUserCmdImages();
  void ClearAllCmdImages();
  void CleanUp() { ClearAllCmdImages(); }
  void EnableMenuItemImage(UINT uiCmd, BOOL bEnable = TRUE, int nUserImage = -1);
  BOOL IsMenuItemWithoutImage(UINT uiCmd) const;
  BOOL LoadState(LPCTSTR = NULL) { return TRUE; }
  BOOL SaveState(LPCTSTR = NULL) { return TRUE; }

protected:
  CMap<UINT, UINT, int, int> m_CommandIndex, m_CommandIndexUser;
  CList<UINT, UINT> m_lstCommandsWithoutImages;
};

CCommandManager* AFXAPI GetCmdMgr();
#define afxCommandManager GetCmdMgr()

#endif  // __AFXCOMMANDMANAGER_H__
