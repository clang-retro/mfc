// mshtml.h - the HTML document object model, as far as the HTML views and
// dialogs use it: documents, elements and their collections, windows,
// events and selections. There is no MSHTML outside Windows yet, so no
// object gives these interfaces there.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#ifndef _WINPORT_MSHTML_H
#define _WINPORT_MSHTML_H

#include <windows.h>
#include <ole2.h>
#include <oleauto.h>
#include <ocidl.h>
#include <docobj.h>

extern "C" const IID IID_IHTMLDocument, IID_IHTMLDocument2, IID_IHTMLElement, IID_IHTMLElementCollection,
    IID_IHTMLEventObj, IID_IHTMLFramesCollection2, IID_IHTMLWindow2, IID_IHTMLSelectionObject,
    DIID_HTMLElementEvents2, DIID_HTMLDocumentEvents2;

interface IHTMLElement;
interface IHTMLElementCollection;
interface IHTMLWindow2;
interface IHTMLSelectionObject;

interface IHTMLElementCollection : public IDispatch {
  STDMETHOD(toString)(BSTR* String) = 0;
  STDMETHOD(put_length)(LONG v) = 0;
  STDMETHOD(get_length)(LONG* p) = 0;
  STDMETHOD(get__newEnum)(IUnknown** p) = 0;
  STDMETHOD(item)(VARIANT name, VARIANT index, IDispatch** pdisp) = 0;
  STDMETHOD(tags)(VARIANT tagName, IDispatch** pdisp) = 0;
};

interface IHTMLElement : public IDispatch {
  STDMETHOD(setAttribute)(BSTR strAttributeName, VARIANT AttributeValue, LONG lFlags) = 0;
  STDMETHOD(getAttribute)(BSTR strAttributeName, LONG lFlags, VARIANT* AttributeValue) = 0;
  STDMETHOD(removeAttribute)(BSTR strAttributeName, LONG lFlags, VARIANT_BOOL* pfSuccess) = 0;
  STDMETHOD(put_className)(BSTR v) = 0;
  STDMETHOD(get_className)(BSTR* p) = 0;
  STDMETHOD(put_id)(BSTR v) = 0;
  STDMETHOD(get_id)(BSTR* p) = 0;
  STDMETHOD(get_tagName)(BSTR* p) = 0;
  STDMETHOD(get_parentElement)(IHTMLElement** p) = 0;
  STDMETHOD(put_title)(BSTR v) = 0;
  STDMETHOD(get_title)(BSTR* p) = 0;
  STDMETHOD(put_innerHTML)(BSTR v) = 0;
  STDMETHOD(get_innerHTML)(BSTR* p) = 0;
  STDMETHOD(put_innerText)(BSTR v) = 0;
  STDMETHOD(get_innerText)(BSTR* p) = 0;
  STDMETHOD(put_outerHTML)(BSTR v) = 0;
  STDMETHOD(get_outerHTML)(BSTR* p) = 0;
  STDMETHOD(put_outerText)(BSTR v) = 0;
  STDMETHOD(get_outerText)(BSTR* p) = 0;
  STDMETHOD(insertAdjacentHTML)(BSTR where, BSTR html) = 0;
  STDMETHOD(insertAdjacentText)(BSTR where, BSTR text) = 0;
  STDMETHOD(scrollIntoView)(VARIANT varargStart) = 0;
  STDMETHOD(contains)(IHTMLElement* pChild, VARIANT_BOOL* pfResult) = 0;
  STDMETHOD(click)() = 0;
  STDMETHOD(get_children)(IDispatch** p) = 0;
  STDMETHOD(get_all)(IDispatch** p) = 0;
};

interface IHTMLEventObj : public IDispatch {
  STDMETHOD(get_srcElement)(IHTMLElement** p) = 0;
  STDMETHOD(get_altKey)(VARIANT_BOOL* p) = 0;
  STDMETHOD(get_ctrlKey)(VARIANT_BOOL* p) = 0;
  STDMETHOD(get_shiftKey)(VARIANT_BOOL* p) = 0;
  STDMETHOD(put_returnValue)(VARIANT v) = 0;
  STDMETHOD(get_returnValue)(VARIANT* p) = 0;
  STDMETHOD(put_cancelBubble)(VARIANT_BOOL v) = 0;
  STDMETHOD(get_cancelBubble)(VARIANT_BOOL* p) = 0;
  STDMETHOD(get_fromElement)(IHTMLElement** p) = 0;
  STDMETHOD(get_toElement)(IHTMLElement** p) = 0;
  STDMETHOD(put_keyCode)(LONG v) = 0;
  STDMETHOD(get_keyCode)(LONG* p) = 0;
  STDMETHOD(get_button)(LONG* p) = 0;
  STDMETHOD(get_type)(BSTR* p) = 0;
  STDMETHOD(get_qualifier)(BSTR* p) = 0;
  STDMETHOD(get_reason)(LONG* p) = 0;
  STDMETHOD(get_x)(LONG* p) = 0;
  STDMETHOD(get_y)(LONG* p) = 0;
  STDMETHOD(get_clientX)(LONG* p) = 0;
  STDMETHOD(get_clientY)(LONG* p) = 0;
  STDMETHOD(get_offsetX)(LONG* p) = 0;
  STDMETHOD(get_offsetY)(LONG* p) = 0;
  STDMETHOD(get_screenX)(LONG* p) = 0;
  STDMETHOD(get_screenY)(LONG* p) = 0;
  STDMETHOD(get_srcFilter)(IDispatch** p) = 0;
};

interface IHTMLSelectionObject : public IDispatch {
  STDMETHOD(createRange)(IDispatch** range) = 0;
  STDMETHOD(empty)() = 0;
  STDMETHOD(clear)() = 0;
  STDMETHOD(get_type)(BSTR* p) = 0;
};

interface IHTMLDocument : public IDispatch {
  STDMETHOD(get_Script)(IDispatch** p) = 0;
};

interface IHTMLDocument2 : public IHTMLDocument {
  STDMETHOD(get_all)(IHTMLElementCollection** p) = 0;
  STDMETHOD(get_body)(IHTMLElement** p) = 0;
  STDMETHOD(get_activeElement)(IHTMLElement** p) = 0;
  STDMETHOD(get_images)(IHTMLElementCollection** p) = 0;
  STDMETHOD(get_applets)(IHTMLElementCollection** p) = 0;
  STDMETHOD(get_links)(IHTMLElementCollection** p) = 0;
  STDMETHOD(get_forms)(IHTMLElementCollection** p) = 0;
  STDMETHOD(get_anchors)(IHTMLElementCollection** p) = 0;
  STDMETHOD(put_title)(BSTR v) = 0;
  STDMETHOD(get_title)(BSTR* p) = 0;
  STDMETHOD(get_scripts)(IHTMLElementCollection** p) = 0;
  STDMETHOD(put_designMode)(BSTR v) = 0;
  STDMETHOD(get_designMode)(BSTR* p) = 0;
  STDMETHOD(get_selection)(IHTMLSelectionObject** p) = 0;
  STDMETHOD(get_readyState)(BSTR* p) = 0;
  STDMETHOD(get_frames)(IDispatch** p) = 0;
  STDMETHOD(get_URL)(BSTR* p) = 0;
  STDMETHOD(put_URL)(BSTR v) = 0;
  STDMETHOD(get_parentWindow)(IHTMLWindow2** p) = 0;
  STDMETHOD(write)(SAFEARRAY* psarray) = 0;
  STDMETHOD(writeln)(SAFEARRAY* psarray) = 0;
  STDMETHOD(open)(BSTR url, VARIANT name, VARIANT features, VARIANT replace, IDispatch** pomWindowResult) = 0;
  STDMETHOD(close)() = 0;
  STDMETHOD(clear)() = 0;
  STDMETHOD(queryCommandSupported)(BSTR cmdID, VARIANT_BOOL* pfRet) = 0;
  STDMETHOD(queryCommandEnabled)(BSTR cmdID, VARIANT_BOOL* pfRet) = 0;
  STDMETHOD(queryCommandState)(BSTR cmdID, VARIANT_BOOL* pfRet) = 0;
  STDMETHOD(queryCommandIndeterm)(BSTR cmdID, VARIANT_BOOL* pfRet) = 0;
  STDMETHOD(queryCommandText)(BSTR cmdID, BSTR* pcmdText) = 0;
  STDMETHOD(queryCommandValue)(BSTR cmdID, VARIANT* pcmdValue) = 0;
  STDMETHOD(execCommand)(BSTR cmdID, VARIANT_BOOL showUI, VARIANT value, VARIANT_BOOL* pfRet) = 0;
  STDMETHOD(execCommandShowHelp)(BSTR cmdID, VARIANT_BOOL* pfRet) = 0;
  STDMETHOD(createElement)(BSTR eTag, IHTMLElement** newElem) = 0;
  STDMETHOD(elementFromPoint)(LONG x, LONG y, IHTMLElement** elementHit) = 0;
};

interface IHTMLFramesCollection2 : public IDispatch {
  STDMETHOD(item)(VARIANT* pvarIndex, VARIANT* pvarResult) = 0;
  STDMETHOD(get_length)(LONG* p) = 0;
};

interface IHTMLWindow2 : public IHTMLFramesCollection2 {
  STDMETHOD(get_frames)(IHTMLFramesCollection2** p) = 0;
  STDMETHOD(put_defaultStatus)(BSTR v) = 0;
  STDMETHOD(get_defaultStatus)(BSTR* p) = 0;
  STDMETHOD(put_status)(BSTR v) = 0;
  STDMETHOD(get_status)(BSTR* p) = 0;
  STDMETHOD(alert)(BSTR message) = 0;
  STDMETHOD(close)() = 0;
  STDMETHOD(get_document)(IHTMLDocument2** p) = 0;
  STDMETHOD(get_event)(IHTMLEventObj** p) = 0;
  STDMETHOD(execScript)(BSTR code, BSTR language, VARIANT* pvarRet) = 0;
};

WINPORT_UUIDOF(IHTMLDocument)
WINPORT_UUIDOF(IHTMLDocument2)
WINPORT_UUIDOF(IHTMLElement)
WINPORT_UUIDOF(IHTMLElementCollection)
WINPORT_UUIDOF(IHTMLEventObj)
WINPORT_UUIDOF(IHTMLWindow2)
WINPORT_UUIDOF(IHTMLSelectionObject)

#endif
