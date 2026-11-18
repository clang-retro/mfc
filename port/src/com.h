// com.h - what ole32.cpp and com_remote.cpp, COM between processes, share.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "winport.h"

#include <objbase.h>

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

// ole32.cpp: a class object this program registered for other programs,
// AddRef'd. A single-use one is not given out twice; *last says whether
// no other one for the class is left to give.
HRESULT TakeClassObject(REFCLSID clsid, IUnknown** out, bool* last);

}  // namespace com
}  // namespace winport
