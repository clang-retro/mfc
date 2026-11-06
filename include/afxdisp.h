// afxdisp.h - OLE automation. Automation and ActiveX control containment
// are not part of this library; this header provides the start-up calls
// that application wizards generate, so such programs still build.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
#ifndef __AFXDISP_H__
#define __AFXDISP_H__

#include "afxwin.h"
#include <ole2.h>

BOOL AFXAPI AfxOleInit();
void AFXAPI AfxOleTerm(BOOL bJustRevoke = FALSE);
inline void AFXAPI AfxEnableControlContainer() {}
inline BOOL AFXAPI AfxOleGetUserCtrl() { return TRUE; }
inline void AFXAPI AfxOleSetUserCtrl(BOOL) {}

#endif  // __AFXDISP_H__
