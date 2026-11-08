// ctlpict.cpp - CPictureHolder, a control's picture property, and
// PX_Picture.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#include "afximpl.h"
#include <afxctl.h>

CPictureHolder::CPictureHolder() : m_pPict(NULL) {}

CPictureHolder::~CPictureHolder() {
  if (m_pPict != NULL) m_pPict->Release();
}

BOOL CPictureHolder::CreateFromDesc(PICTDESC* pDesc, BOOL bOwn) {
  LPPICTURE pPict = NULL;
  pDesc->cbSizeofstruct = sizeof(PICTDESC);
  if (FAILED(::OleCreatePictureIndirect(pDesc, IID_IPicture, bOwn, (void**)&pPict)) ||
      pPict == NULL)
    return FALSE;
  if (m_pPict != NULL) m_pPict->Release();
  m_pPict = pPict;
  return TRUE;
}

BOOL CPictureHolder::CreateEmpty() {
  PICTDESC desc;
  memset(&desc, 0, sizeof(desc));
  desc.picType = PICTYPE_NONE;
  return CreateFromDesc(&desc, FALSE);
}

BOOL CPictureHolder::CreateFromBitmap(UINT idResource) {
  HINSTANCE hInst = AfxFindResourceHandle(MAKEINTRESOURCE(idResource), RT_BITMAP);
  HBITMAP hbm = ::LoadBitmap(hInst, MAKEINTRESOURCE(idResource));
  if (hbm == NULL) return FALSE;
  if (CreateFromBitmap(hbm, NULL, TRUE)) return TRUE;
  ::DeleteObject(hbm);
  return FALSE;
}

BOOL CPictureHolder::CreateFromBitmap(CBitmap* pBitmap, CPalette* pPal, BOOL bTransferOwnership) {
  HBITMAP hbm = pBitmap != NULL ? (HBITMAP)pBitmap->GetSafeHandle() : NULL;
  HPALETTE hpal = pPal != NULL ? (HPALETTE)pPal->GetSafeHandle() : NULL;
  if (!CreateFromBitmap(hbm, hpal, bTransferOwnership)) return FALSE;
  if (bTransferOwnership) {
    if (pBitmap != NULL) pBitmap->Detach();
    if (pPal != NULL) pPal->Detach();
  }
  return TRUE;
}

BOOL CPictureHolder::CreateFromBitmap(HBITMAP hbm, HPALETTE hpal, BOOL bTransferOwnership) {
  PICTDESC desc;
  memset(&desc, 0, sizeof(desc));
  desc.picType = PICTYPE_BITMAP;
  desc.bmp.hbitmap = hbm;
  desc.bmp.hpal = hpal;
  return CreateFromDesc(&desc, bTransferOwnership);
}

BOOL CPictureHolder::CreateFromMetafile(HMETAFILE hmf, int xExt, int yExt,
                                        BOOL bTransferOwnership) {
  PICTDESC desc;
  memset(&desc, 0, sizeof(desc));
  desc.picType = PICTYPE_METAFILE;
  desc.wmf.hmeta = hmf;
  desc.wmf.xExt = xExt;
  desc.wmf.yExt = yExt;
  return CreateFromDesc(&desc, bTransferOwnership);
}

BOOL CPictureHolder::CreateFromIcon(UINT idResource) {
  HINSTANCE hInst = AfxFindResourceHandle(MAKEINTRESOURCE(idResource), RT_GROUP_ICON);
  HICON hIcon = ::LoadIcon(hInst, MAKEINTRESOURCE(idResource));
  if (hIcon == NULL) return FALSE;
  // A loaded icon is shared; the picture gets its own copy.
  HICON hCopy = ::CopyIcon(hIcon);
  if (hCopy == NULL) return FALSE;
  if (CreateFromIcon(hCopy, TRUE)) return TRUE;
  ::DestroyIcon(hCopy);
  return FALSE;
}

BOOL CPictureHolder::CreateFromIcon(HICON hIcon, BOOL bTransferOwnership) {
  PICTDESC desc;
  memset(&desc, 0, sizeof(desc));
  desc.picType = PICTYPE_ICON;
  desc.icon.hicon = hIcon;
  return CreateFromDesc(&desc, bTransferOwnership);
}

short CPictureHolder::GetType() {
  short nType = PICTYPE_UNINITIALIZED;
  if (m_pPict != NULL && FAILED(m_pPict->get_Type(&nType))) nType = PICTYPE_UNINITIALIZED;
  return nType;
}

BOOL CPictureHolder::GetDisplayString(CString& strValue) {
  switch (GetType()) {
    case PICTYPE_NONE:
    case PICTYPE_UNINITIALIZED: strValue = _T("(None)"); break;
    case PICTYPE_BITMAP: strValue = _T("(Bitmap)"); break;
    case PICTYPE_METAFILE: strValue = _T("(Metafile)"); break;
    case PICTYPE_ICON: strValue = _T("(Icon)"); break;
    case PICTYPE_ENHMETAFILE: strValue = _T("(Enhanced Metafile)"); break;
    default: strValue = _T("(Picture)"); break;
  }
  return TRUE;
}

LPPICTUREDISP CPictureHolder::GetPictureDispatch() {
  LPPICTUREDISP pDisp = NULL;
  if (m_pPict != NULL && FAILED(m_pPict->QueryInterface(IID_IPictureDisp, (void**)&pDisp)))
    pDisp = NULL;
  return pDisp;
}

void CPictureHolder::SetPictureDispatch(LPPICTUREDISP pDisp) {
  LPPICTURE pPict = NULL;
  if (pDisp != NULL && FAILED(pDisp->QueryInterface(IID_IPicture, (void**)&pPict))) pPict = NULL;
  if (m_pPict != NULL) m_pPict->Release();
  m_pPict = pPict;
}

void CPictureHolder::Render(CDC* pDC, const CRect& rcRender, const CRect& rcWBounds) {
  if (m_pPict == NULL || pDC == NULL) return;
  OLE_XSIZE_HIMETRIC cx = 0;
  OLE_YSIZE_HIMETRIC cy = 0;
  m_pPict->get_Width(&cx);
  m_pPict->get_Height(&cy);
  // HIMETRIC goes up; the source starts at the picture's top.
  RECT rcBounds = rcWBounds;
  m_pPict->Render(pDC->m_hDC, rcRender.left, rcRender.top, rcRender.Width(), rcRender.Height(), 0,
                  cy, cx, -cy, &rcBounds);
}

// ---------------------------------------------------------------------
// PX_Picture: the picture's own persistent data, through IPersistStream
// (or the property bag). A missing picture loads as the default, or as
// an empty one.

static BOOL ExchangePicture(CPropExchange* pPX, LPCTSTR pszPropName, CPictureHolder& pict,
                            LPPICTURE pDefault) {
  LPUNKNOWN pUnk = pict.m_pPict;
  if (pUnk != NULL) pUnk->AddRef();
  BOOL bOK = pPX->ExchangePersistentProp(pszPropName, &pUnk, IID_IPicture, pDefault);
  if (pPX->IsLoading()) {
    LPPICTURE pPict = NULL;
    if (pUnk != NULL && FAILED(pUnk->QueryInterface(IID_IPicture, (void**)&pPict))) pPict = NULL;
    if (pPict == NULL && pDefault != NULL) {
      pPict = pDefault;
      pPict->AddRef();
    }
    if (pPict != NULL) {
      if (pict.m_pPict != NULL) pict.m_pPict->Release();
      pict.m_pPict = pPict;
    } else {
      pict.CreateEmpty();
    }
  }
  if (pUnk != NULL) pUnk->Release();
  return bOK;
}

BOOL AFXAPI PX_Picture(CPropExchange* pPX, LPCTSTR pszPropName, CPictureHolder& pict) {
  return ExchangePicture(pPX, pszPropName, pict, NULL);
}

BOOL AFXAPI PX_Picture(CPropExchange* pPX, LPCTSTR pszPropName, CPictureHolder& pict,
                       CPictureHolder& pictDefault) {
  return ExchangePicture(pPX, pszPropName, pict, pictDefault.m_pPict);
}
