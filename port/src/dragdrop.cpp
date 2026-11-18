// dragdrop.cpp - OLE drag and drop within the program: the drop targets
// windows register (RegisterDragDrop, RevokeDragDrop), and the drag loop
// (DoDragDrop) that follows the cursor from target to target.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#include "winport.h"

#include <ole2.h>

namespace {

// The window property that holds a window's drop target.
const wchar_t kTargetProp[] = L"OleDropTargetInterface";

IDropTarget* TargetOf(HWND h) { return h ? (IDropTarget*)GetPropW(h, kTargetProp) : NULL; }

// The target under a point: the window there, or the nearest of its
// parents that registered one.
IDropTarget* TargetAt(POINT pt, HWND* where) {
  for (HWND h = WindowFromPoint(pt); h != NULL; h = GetParent(h)) {
    if (!IsWindowEnabled(h)) break;
    if (IDropTarget* t = TargetOf(h)) {
      *where = h;
      return t;
    }
  }
  *where = NULL;
  return NULL;
}

DWORD KeyState() {
  DWORD keys = 0;
  if (GetAsyncKeyState(VK_LBUTTON) & 0x8000) keys |= MK_LBUTTON;
  if (GetAsyncKeyState(VK_RBUTTON) & 0x8000) keys |= MK_RBUTTON;
  if (GetAsyncKeyState(VK_MBUTTON) & 0x8000) keys |= MK_MBUTTON;
  if (GetAsyncKeyState(VK_SHIFT) & 0x8000) keys |= MK_SHIFT;
  if (GetAsyncKeyState(VK_CONTROL) & 0x8000) keys |= MK_CONTROL;
  if (GetAsyncKeyState(VK_MENU) & 0x8000) keys |= MK_ALT;
  return keys;
}

}  // namespace

extern "C" {

HRESULT WINAPI RegisterDragDrop(HWND hwnd, LPDROPTARGET target) {
  if (target == NULL) return E_INVALIDARG;
  if (hwnd == NULL || !IsWindow(hwnd)) return DRAGDROP_E_INVALIDHWND;
  if (TargetOf(hwnd) != NULL) return DRAGDROP_E_ALREADYREGISTERED;
  if (!SetPropW(hwnd, kTargetProp, (HANDLE)target)) return E_OUTOFMEMORY;
  target->AddRef();
  return S_OK;
}

HRESULT WINAPI RevokeDragDrop(HWND hwnd) {
  if (hwnd == NULL || !IsWindow(hwnd)) return DRAGDROP_E_INVALIDHWND;
  IDropTarget* t = (IDropTarget*)RemovePropW(hwnd, kTargetProp);
  if (t == NULL) return DRAGDROP_E_NOTREGISTERED;
  t->Release();
  return S_OK;
}

HRESULT WINAPI DoDragDrop(LPDATAOBJECT data, LPDROPSOURCE source, DWORD ok, LPDWORD effect) {
  if (data == NULL || source == NULL || effect == NULL) return E_INVALIDARG;
  *effect = DROPEFFECT_NONE;
  IDropTarget* cur = NULL;
  HWND curWnd = NULL;
  HRESULT hr;
  for (;;) {
    // The program goes on running while the drag does.
    MSG msg;
    BOOL escape = FALSE;
    while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE)) {
      if (msg.message == WM_QUIT) {
        PostQuitMessage((int)msg.wParam);
        escape = TRUE;
        break;
      }
      if (msg.message == WM_KEYDOWN && msg.wParam == VK_ESCAPE) {
        escape = TRUE;
        continue;
      }
      TranslateMessage(&msg);
      DispatchMessageW(&msg);
    }
    DWORD keys = KeyState();
    hr = source->QueryContinueDrag(escape, keys);
    POINT pt;
    GetCursorPos(&pt);
    POINTL ptl = {pt.x, pt.y};
    if (hr != S_OK) {
      if (hr == DRAGDROP_S_DROP && cur != NULL) {
        DWORD e = ok;
        if (FAILED(cur->Drop(data, keys, ptl, &e))) e = DROPEFFECT_NONE;
        *effect = e & ok;
        cur->Release();
        cur = NULL;
        return *effect != DROPEFFECT_NONE ? DRAGDROP_S_DROP : DRAGDROP_S_CANCEL;
      }
      if (cur != NULL) {
        cur->DragLeave();
        cur->Release();
      }
      return hr == DRAGDROP_S_DROP ? DRAGDROP_S_CANCEL : hr;
    }

    // Follow the cursor.
    HWND where;
    IDropTarget* now = TargetAt(pt, &where);
    DWORD e = ok;
    if (where != curWnd || now != cur) {
      if (cur != NULL) {
        cur->DragLeave();
        cur->Release();
      }
      cur = now;
      curWnd = where;
      if (cur != NULL) {
        cur->AddRef();
        if (FAILED(cur->DragEnter(data, keys, ptl, &e))) e = DROPEFFECT_NONE;
      } else {
        e = DROPEFFECT_NONE;
      }
    } else if (cur != NULL) {
      if (FAILED(cur->DragOver(keys, ptl, &e))) e = DROPEFFECT_NONE;
    } else {
      e = DROPEFFECT_NONE;
    }
    e &= ok | DROPEFFECT_SCROLL;
    if (source->GiveFeedback(e) == DRAGDROP_S_USEDEFAULTCURSORS)
      SetCursor(LoadCursor(NULL, (e & ~DROPEFFECT_SCROLL) != DROPEFFECT_NONE ? IDC_ARROW : IDC_NO));
    MsgWaitForMultipleObjects(0, NULL, FALSE, 30, QS_ALLINPUT);
  }
}

}  // extern "C"
