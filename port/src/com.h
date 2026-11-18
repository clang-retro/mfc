// com.h - what ole32.cpp and com_remote.cpp, COM between processes, share.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "winport.h"

#include <objbase.h>

#include <string>
#include <vector>

namespace winport {
namespace com {

// com_remote.cpp: a class object this program registered for other
// programs (CLSCTX_LOCAL_SERVER) is published where they find it, and
// taken down again.
void PublishClass(REFCLSID clsid);
void UnpublishClass(REFCLSID clsid);

// The class object of another program's server: the running one that
// published it, or one started from the class's LocalServer32 command.
HRESULT GetRemoteClassObject(REFCLSID clsid, REFIID riid, void** out);

// Other programs' references to an object of this one go.
void DisconnectObject(IUnknown* unk);

// Whether an interface pointer is a proxy for another program's object.
bool IsProxy(IUnknown* unk);

// The windows of the programs this one talks COM with, which the window
// functions see through stand-in handles: a program finds its server's
// frame (EnumWindows, GetWindowThreadProcessId), reads it and posts it
// messages.
struct WindowInfo {
  DWORD pid = 0, tid = 0;
  HWND owner = NULL;
  LONG style = 0, exStyle = 0;
  bool visible = false;
  RECT rect = {0, 0, 0, 0};
  std::wstring text;
};
bool IsForeignWindow(HWND h);  // a stand-in handle
void ForeignTopLevels(std::vector<HWND>* out);
bool ForeignWindowInfo(HWND h, WindowInfo* out);
bool PostForeign(HWND h, UINT msg, WPARAM wp, LPARAM lp);
// user_core.cpp: this program's own.
void LocalTopLevels(std::vector<HWND>* out);
bool LocalWindowInfo(HWND h, WindowInfo* out);

// The OLE clipboard between programs: this program's data object is on
// it, or no longer is; another program's, as a proxy; and making that
// program let its object go.
void OwnClipboard(bool own);
HRESULT ForeignClipboard(IDataObject** out);
void ClearForeignClipboard();
// ole_clipboard.cpp: the data object this program put there, AddRef'd
// (S_FALSE: none).
HRESULT LocalClipboardObject(IDataObject** out);

// ole32.cpp: a class object this program registered for other programs,
// AddRef'd. A single-use one is not given out twice; *last says whether
// no other one for the class is left to give.
HRESULT TakeClassObject(REFCLSID clsid, IUnknown** out, bool* last);

}  // namespace com
}  // namespace winport
