// stdafx.h - private precompiled-style header for the library sources.
#pragma once
#if defined(__WINDOWS__) && defined(__I86__) && !defined(_WIN32)
// 16-bit Windows builds the core classes only (afx.h and afxcoll.h).
#include <afx.h>
#include <afxres.h>
#include <afxcoll.h>
#include <atltypes.h>
// afxwin.h declares these on Win32; src/win16.cpp defines them.
int AFXAPI AfxMessageBox(LPCTSTR lpszText, UINT nType = MB_OK, UINT nIDHelp = 0);
void AFXAPI AfxWinTerm();
#else
#include <afxwin.h>
#endif
