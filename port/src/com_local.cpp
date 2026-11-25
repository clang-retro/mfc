// com_local.cpp - COM between processes for a system where the layer has
// no other programs to talk to: the classic Mac OS. It takes the place of
// com_remote.cpp, com_proxy.cpp and com_ole.cpp there (the Makefile
// leaves those out). A program's class objects stay its own, no object
// is a proxy, no window belongs to another program, and the OLE
// clipboard holds only this program's data object.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#if defined(macintosh)
#include "com.h"

namespace winport {
namespace com {

// No other program looks for this program's class objects.
void PublishClass(REFCLSID) {}

// There is nothing published to take down.
void UnpublishClass(REFCLSID) {}

// No other program serves classes: the class is not registered.
HRESULT GetRemoteClassObject(REFCLSID, REFIID, void** out) {
  *out = NULL;
  return REGDB_E_CLASSNOTREG;
}

// No other program holds references to this program's objects.
void DisconnectObject(IUnknown*) {}

// No interface pointer is a proxy.
bool IsProxy(IUnknown*) { return false; }

// No window belongs to another program.
bool IsForeignWindow(HWND) { return false; }

// There are no other programs' top-level windows.
void ForeignTopLevels(std::vector<HWND>*) {}

// There is no other program's window to describe.
bool ForeignWindowInfo(HWND, WindowInfo*) { return false; }

// There is no other program's window to post to.
bool PostForeign(HWND, UINT, WPARAM, LPARAM) { return false; }

// No other program reads the OLE clipboard.
void OwnClipboard(bool) {}

// No other program puts a data object on the OLE clipboard.
HRESULT ForeignClipboard(IDataObject** out) {
  *out = NULL;
  return CLIPBRD_E_BAD_DATA;
}

// There is no other program's data object to let go.
void ClearForeignClipboard() {}

}  // namespace com
}  // namespace winport
#endif
