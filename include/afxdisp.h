// afxdisp.h - OLE automation: the variant, date, currency and safe array
// wrappers, COleDispatchDriver for calling automation objects, dispatch
// maps and class factories for exposing them, and the OLE exceptions.
// Containing ActiveX controls is not part of this library yet; the
// start-up call that application wizards generate is here so such
// programs still build.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
#ifndef __AFXDISP_H__
#define __AFXDISP_H__

#include "afxwin.h"
#include <ole2.h>
#include <oleauto.h>
#include <olectl.h>
#include <type_traits>
#include <utility>

typedef const VARIANT* LPCVARIANT;
typedef const SAFEARRAY* LPCSAFEARRAY;

class COleException;
class COleDispatchException;
class COleDateTime;
class COleDateTimeSpan;
class COleCurrency;
class COleVariant;
class COleSafeArray;
class COleDispatchDriver;

BOOL AFXAPI AfxOleInit();
void AFXAPI AfxOleTerm(BOOL bJustRevoke = FALSE);
// Control containment is not implemented; this lets programs start up.
class COccManager;
void AFXAPI AfxEnableControlContainer(COccManager* pOccManager = NULL);

// ---------------------------------------------------------------------
// Parameter types for COleDispatchDriver::InvokeHelper: one byte per
// parameter, the VARTYPE, with VT_MFCBYREF for a pointer.

#define VT_MFCBYREF 0x40
#define VT_MFCMARKER 0xFF

#define VTS_I2 "\x02"
#define VTS_I4 "\x03"
#define VTS_R4 "\x04"
#define VTS_R8 "\x05"
#define VTS_CY "\x06"
#define VTS_DATE "\x07"
#define VTS_WBSTR "\x08"
#define VTS_DISPATCH "\x09"
#define VTS_SCODE "\x0A"
#define VTS_BOOL "\x0B"
#define VTS_VARIANT "\x0C"
#define VTS_UNKNOWN "\x0D"
#if defined(_UNICODE) || defined(OLE2ANSI)
#define VTS_BSTR VTS_WBSTR  // an LPCOLESTR
#define VT_BSTRT VT_BSTR
#else
#define VTS_BSTR "\x0E"  // an LPCSTR
#define VT_BSTRA 14
#define VT_BSTRT VT_BSTRA
#endif
#define VTS_I1 "\x10"
#define VTS_UI1 "\x11"
#define VTS_UI2 "\x12"
#define VTS_UI4 "\x13"
#define VTS_I8 "\x14"
#define VTS_UI8 "\x15"

#define VTS_PI2 "\x42"
#define VTS_PI4 "\x43"
#define VTS_PR4 "\x44"
#define VTS_PR8 "\x45"
#define VTS_PCY "\x46"
#define VTS_PDATE "\x47"
#define VTS_PBSTR "\x48"
#define VTS_PDISPATCH "\x49"
#define VTS_PSCODE "\x4A"
#define VTS_PBOOL "\x4B"
#define VTS_PVARIANT "\x4C"
#define VTS_PUNKNOWN "\x4D"
#define VTS_PI1 "\x50"
#define VTS_PUI1 "\x51"
#define VTS_PUI2 "\x52"
#define VTS_PUI4 "\x53"
#define VTS_PI8 "\x54"
#define VTS_PUI8 "\x55"
#define VTS_NONE NULL

// The control property types, which travel as the basic ones.
#define VTS_COLOR VTS_I4
#define VTS_XPOS_PIXELS VTS_I4
#define VTS_YPOS_PIXELS VTS_I4
#define VTS_XSIZE_PIXELS VTS_I4
#define VTS_YSIZE_PIXELS VTS_I4
#define VTS_XPOS_HIMETRIC VTS_I4
#define VTS_YPOS_HIMETRIC VTS_I4
#define VTS_XSIZE_HIMETRIC VTS_I4
#define VTS_YSIZE_HIMETRIC VTS_I4
#define VTS_TRISTATE VTS_I2
#define VTS_OPTEXCLUSIVE VTS_BOOL
#define VTS_HANDLE VTS_I4
#define VTS_FONT VTS_DISPATCH
#define VTS_PICTURE VTS_UNKNOWN

// ---------------------------------------------------------------------
// Exceptions

class COleException : public CException {
  DECLARE_DYNAMIC(COleException)
public:
  COleException();
  SCODE m_sc;
  static SCODE PASCAL Process(const CException* pAnyException);
  virtual BOOL GetErrorMessage(LPTSTR lpszError, UINT nMaxError,
                               PUINT pnHelpContext = NULL) const;
  virtual BOOL GetErrorMessage(LPTSTR lpszError, UINT nMaxError, PUINT pnHelpContext = NULL);
};

// An exception an automation method raised (DISP_E_EXCEPTION).
class COleDispatchException : public CException {
  DECLARE_DYNAMIC(COleDispatchException)
public:
  COleDispatchException(LPCTSTR lpszDescription, UINT nHelpID, WORD wCode);
  WORD m_wCode;
  SCODE m_scError;
  CString m_strSource;
  CString m_strDescription;
  CString m_strHelpFile;
  DWORD m_dwHelpContext;
  static void PASCAL Process(EXCEPINFO* pInfo, const CException* pAnyException);
  virtual BOOL GetErrorMessage(LPTSTR lpszError, UINT nMaxError,
                               PUINT pnHelpContext = NULL) const;
  virtual BOOL GetErrorMessage(LPTSTR lpszError, UINT nMaxError, PUINT pnHelpContext = NULL);
};

void AFXAPI AfxThrowOleException(SCODE sc);
void AFXAPI AfxCheckError(SCODE sc);
void AFXAPI AfxThrowOleDispatchException(WORD wCode, LPCTSTR lpszDescription, UINT nHelpID = 0);
void AFXAPI AfxThrowOleDispatchException(WORD wCode, UINT nDescriptionID, UINT nHelpID = (UINT)-1);

// ---------------------------------------------------------------------
// COleDateTimeSpan: a length of time in days.

class COleDateTimeSpan {
public:
  enum DateTimeSpanStatus { valid = 0, invalid = 1, null = 2 };

  COleDateTimeSpan() : m_span(0), m_status(valid) {}
  COleDateTimeSpan(double dblSpanSrc) : m_span(dblSpanSrc), m_status(valid) {}
  COleDateTimeSpan(long lDays, int nHours, int nMins, int nSecs) {
    SetDateTimeSpan(lDays, nHours, nMins, nSecs);
  }

  double m_span;
  DateTimeSpanStatus m_status;

  void SetStatus(DateTimeSpanStatus status) { m_status = status; }
  DateTimeSpanStatus GetStatus() const { return m_status; }

  double GetTotalDays() const { return m_span; }
  double GetTotalHours() const { return m_span * 24; }
  double GetTotalMinutes() const { return m_span * 24 * 60; }
  double GetTotalSeconds() const { return m_span * 24 * 60 * 60; }
  long GetDays() const { return (long)m_span; }
  long GetHours() const;
  long GetMinutes() const;
  long GetSeconds() const;

  const COleDateTimeSpan& operator=(double dblSpanSrc) {
    m_span = dblSpanSrc;
    m_status = valid;
    return *this;
  }
  BOOL operator==(const COleDateTimeSpan& s) const {
    return m_status == s.m_status && m_span == s.m_span;
  }
  BOOL operator!=(const COleDateTimeSpan& s) const { return !(*this == s); }
  BOOL operator<(const COleDateTimeSpan& s) const { return m_span < s.m_span; }
  BOOL operator>(const COleDateTimeSpan& s) const { return m_span > s.m_span; }
  BOOL operator<=(const COleDateTimeSpan& s) const { return m_span <= s.m_span; }
  BOOL operator>=(const COleDateTimeSpan& s) const { return m_span >= s.m_span; }

  COleDateTimeSpan operator+(const COleDateTimeSpan& s) const;
  COleDateTimeSpan operator-(const COleDateTimeSpan& s) const;
  const COleDateTimeSpan& operator+=(const COleDateTimeSpan& s) { return *this = *this + s; }
  const COleDateTimeSpan& operator-=(const COleDateTimeSpan& s) { return *this = *this - s; }
  COleDateTimeSpan operator-() const { return COleDateTimeSpan(-m_span); }
  operator double() const { return m_span; }

  void SetDateTimeSpan(long lDays, int nHours, int nMins, int nSecs);

  // %D days, %H hours, %M minutes, %S seconds, %% a percent sign.
  CString Format(LPCTSTR lpszFormat) const;
  CString Format(UINT nID) const;
};

// ---------------------------------------------------------------------
// COleDateTime: a DATE, days since 30 December 1899 with the time as the
// fraction, valid from the year 100 to 9999.

class COleDateTime {
public:
  enum DateTimeStatus { valid = 0, invalid = 1, null = 2 };

  static COleDateTime PASCAL GetCurrentTime();

  COleDateTime() : m_dt(0), m_status(valid) {}
  COleDateTime(const VARIANT& varSrc);
  COleDateTime(DATE dtSrc) : m_dt(dtSrc), m_status(valid) {}
  COleDateTime(time_t timeSrc);
  COleDateTime(const SYSTEMTIME& systimeSrc);
  COleDateTime(const FILETIME& filetimeSrc);
  COleDateTime(int nYear, int nMonth, int nDay, int nHour, int nMin, int nSec) {
    SetDateTime(nYear, nMonth, nDay, nHour, nMin, nSec);
  }
  COleDateTime(WORD wDosDate, WORD wDosTime);

  DATE m_dt;
  DateTimeStatus m_status;

  void SetStatus(DateTimeStatus status) { m_status = status; }
  DateTimeStatus GetStatus() const { return m_status; }

  BOOL GetAsSystemTime(SYSTEMTIME& st) const;
  int GetYear() const;
  int GetMonth() const;  // 1 = January
  int GetDay() const;
  int GetHour() const;
  int GetMinute() const;
  int GetSecond() const;
  int GetDayOfWeek() const;  // 1 = Sunday
  int GetDayOfYear() const;  // 1 = 1 January

  const COleDateTime& operator=(const VARIANT& varSrc);
  const COleDateTime& operator=(DATE dtSrc);
  const COleDateTime& operator=(const time_t& timeSrc);
  const COleDateTime& operator=(const SYSTEMTIME& systimeSrc);
  const COleDateTime& operator=(const FILETIME& filetimeSrc);

  BOOL operator==(const COleDateTime& date) const {
    return m_status == date.m_status && m_dt == date.m_dt;
  }
  BOOL operator!=(const COleDateTime& date) const { return !(*this == date); }
  // Negative dates count their time forwards, so compare as real time.
  BOOL operator<(const COleDateTime& date) const;
  BOOL operator>(const COleDateTime& date) const;
  BOOL operator<=(const COleDateTime& date) const;
  BOOL operator>=(const COleDateTime& date) const;

  COleDateTime operator+(const COleDateTimeSpan& dateSpan) const;
  COleDateTime operator-(const COleDateTimeSpan& dateSpan) const;
  const COleDateTime& operator+=(const COleDateTimeSpan& s) { return *this = *this + s; }
  const COleDateTime& operator-=(const COleDateTimeSpan& s) { return *this = *this - s; }
  COleDateTimeSpan operator-(const COleDateTime& date) const;
  operator DATE() const { return m_dt; }

  int SetDateTime(int nYear, int nMonth, int nDay, int nHour, int nMin, int nSec);
  int SetDate(int nYear, int nMonth, int nDay) { return SetDateTime(nYear, nMonth, nDay, 0, 0, 0); }
  int SetTime(int nHour, int nMin, int nSec) { return SetDateTime(1899, 12, 30, nHour, nMin, nSec); }

  // dwFlags: LOCALE_NOUSEROVERRIDE, VAR_TIMEVALUEONLY, VAR_DATEVALUEONLY.
  BOOL ParseDateTime(LPCTSTR lpszDate, DWORD dwFlags = 0, LCID lcid = LANG_USER_DEFAULT);
  CString Format(DWORD dwFlags = 0, LCID lcid = LANG_USER_DEFAULT) const;
  CString Format(LPCTSTR lpszFormat) const;  // strftime codes
  CString Format(UINT nFormatID) const;
};

// ---------------------------------------------------------------------
// COleCurrency: a CURRENCY, an integer count of 1/10000 units.

class COleCurrency {
public:
  enum CurrencyStatus { valid = 0, invalid = 1, null = 2 };

  COleCurrency() : m_status(valid) { m_cur.int64 = 0; }
  COleCurrency(CURRENCY cySrc) : m_cur(cySrc), m_status(valid) {}
  COleCurrency(const VARIANT& varSrc);
  COleCurrency(long nUnits, long nFractionalUnits) { SetCurrency(nUnits, nFractionalUnits); }

  CURRENCY m_cur;
  CurrencyStatus m_status;

  void SetStatus(CurrencyStatus status) { m_status = status; }
  CurrencyStatus GetStatus() const { return m_status; }

  const COleCurrency& operator=(CURRENCY cySrc) {
    m_cur = cySrc;
    m_status = valid;
    return *this;
  }
  const COleCurrency& operator=(const VARIANT& varSrc);

  BOOL operator==(const COleCurrency& cur) const {
    return m_status == cur.m_status && m_cur.int64 == cur.m_cur.int64;
  }
  BOOL operator!=(const COleCurrency& cur) const { return !(*this == cur); }
  BOOL operator<(const COleCurrency& cur) const { return m_cur.int64 < cur.m_cur.int64; }
  BOOL operator>(const COleCurrency& cur) const { return m_cur.int64 > cur.m_cur.int64; }
  BOOL operator<=(const COleCurrency& cur) const { return m_cur.int64 <= cur.m_cur.int64; }
  BOOL operator>=(const COleCurrency& cur) const { return m_cur.int64 >= cur.m_cur.int64; }

  COleCurrency operator+(const COleCurrency& cur) const;
  COleCurrency operator-(const COleCurrency& cur) const;
  const COleCurrency& operator+=(const COleCurrency& cur) { return *this = *this + cur; }
  const COleCurrency& operator-=(const COleCurrency& cur) { return *this = *this - cur; }
  COleCurrency operator-() const;
  COleCurrency operator*(long nOperand) const;
  COleCurrency operator/(long nOperand) const;
  const COleCurrency& operator*=(long nOperand) { return *this = *this * nOperand; }
  const COleCurrency& operator/=(long nOperand) { return *this = *this / nOperand; }
  operator CURRENCY() const { return m_cur; }

  void SetCurrency(long nUnits, long nFractionalUnits);
  BOOL ParseCurrency(LPCTSTR lpszCurrency, DWORD dwFlags = 0, LCID lcid = LANG_USER_DEFAULT);
  CString Format(DWORD dwFlags = 0, LCID lcid = LANG_USER_DEFAULT) const;
};

// ---------------------------------------------------------------------
// COleVariant: a VARIANT that cleans up after itself.

class COleVariant : public tagVARIANT {
public:
  COleVariant() { ::VariantInit(this); }
  COleVariant(const VARIANT& varSrc);
  COleVariant(LPCVARIANT pSrc);
  COleVariant(const COleVariant& varSrc);
  COleVariant(LPCTSTR lpszSrc);
  COleVariant(LPCTSTR lpszSrc, VARTYPE vtSrc);  // VT_BSTR or VT_BSTRT
  COleVariant(CString& strSrc);
  COleVariant(BYTE nSrc);
  COleVariant(short nSrc, VARTYPE vtSrc = VT_I2);  // VT_I2 or VT_BOOL
  COleVariant(long lSrc, VARTYPE vtSrc = VT_I4);   // VT_I4, VT_ERROR or VT_BOOL
  COleVariant(const COleCurrency& curSrc);
  COleVariant(float fltSrc);
  COleVariant(double dblSrc);
  COleVariant(const COleDateTime& timeSrc);
  COleVariant(const CByteArray& arrSrc);
  ~COleVariant() { ::VariantClear(this); }

  void Clear() { ::VariantClear(this); }
  void ChangeType(VARTYPE vartype, LPVARIANT pSrc = NULL);
  void Attach(VARIANT& varSrc);
  VARIANT Detach();
  void SetString(LPCTSTR lpszSrc, VARTYPE vtSrc);

  BOOL operator==(const VARIANT& varSrc) const;
  BOOL operator==(LPCVARIANT pSrc) const { return *this == *pSrc; }
  BOOL operator!=(const VARIANT& varSrc) const { return !(*this == varSrc); }

  const COleVariant& operator=(const VARIANT& varSrc);
  const COleVariant& operator=(LPCVARIANT pSrc) { return *this = *pSrc; }
  const COleVariant& operator=(const COleVariant& varSrc) { return *this = (const VARIANT&)varSrc; }
  const COleVariant& operator=(LPCTSTR lpszSrc);
  const COleVariant& operator=(const CString& strSrc) { return *this = (LPCTSTR)strSrc; }
  const COleVariant& operator=(BYTE nSrc);
  const COleVariant& operator=(short nSrc);
  const COleVariant& operator=(long lSrc);
  const COleVariant& operator=(const COleCurrency& curSrc);
  const COleVariant& operator=(float fltSrc);
  const COleVariant& operator=(double dblSrc);
  const COleVariant& operator=(const COleDateTime& dateSrc);
  const COleVariant& operator=(const CByteArray& arrSrc);

  operator LPVARIANT() { return this; }
  operator LPCVARIANT() const { return this; }
};

CArchive& AFXAPI operator<<(CArchive& ar, COleVariant varSrc);
CArchive& AFXAPI operator>>(CArchive& ar, COleVariant& varSrc);
CArchive& AFXAPI operator<<(CArchive& ar, COleCurrency curSrc);
CArchive& AFXAPI operator>>(CArchive& ar, COleCurrency& curSrc);
CArchive& AFXAPI operator<<(CArchive& ar, COleDateTime dateSrc);
CArchive& AFXAPI operator>>(CArchive& ar, COleDateTime& dateSrc);
CArchive& AFXAPI operator<<(CArchive& ar, COleDateTimeSpan dateSpanSrc);
CArchive& AFXAPI operator>>(CArchive& ar, COleDateTimeSpan& dateSpanSrc);

// ---------------------------------------------------------------------
// COleSafeArray: a VARIANT that holds a SAFEARRAY.

class COleSafeArray : public tagVARIANT {
public:
  COleSafeArray() {
    ::VariantInit(this);
    vt = VT_EMPTY;
    m_dwElementSize = 0;
    m_dwDims = 0;
  }
  COleSafeArray(const SAFEARRAY& saSrc, VARTYPE vtSrc);
  COleSafeArray(LPCSAFEARRAY pSrc, VARTYPE vtSrc);
  COleSafeArray(const COleSafeArray& saSrc);
  COleSafeArray(const VARIANT& varSrc);
  COleSafeArray(LPCVARIANT pSrc);
  COleSafeArray(const COleVariant& varSrc);
  ~COleSafeArray() { Destroy(); }

  void Clear() { Destroy(); }
  void Attach(VARIANT& varSrc);
  VARIANT Detach();

  const COleSafeArray& operator=(const COleSafeArray& saSrc);
  const COleSafeArray& operator=(const VARIANT& varSrc);
  const COleSafeArray& operator=(LPCVARIANT pSrc) { return *this = *pSrc; }
  const COleSafeArray& operator=(const COleVariant& varSrc) { return *this = (const VARIANT&)varSrc; }
  operator LPVARIANT() { return this; }
  operator LPCVARIANT() const { return this; }

  void Create(VARTYPE vtSrc, DWORD dwDims, DWORD* rgElements);
  void Create(VARTYPE vtSrc, DWORD dwDims, SAFEARRAYBOUND* rgsabounds);
  void CreateOneDim(VARTYPE vtSrc, DWORD dwElements, const void* pvSrcData = NULL,
                    long nLBound = 0);
  void Destroy();
  void DestroyData();
  void DestroyDescriptor();
  DWORD GetOneDimSize();
  void ResizeOneDim(DWORD dwElements);
  void Redim(SAFEARRAYBOUND* psaboundNew);
  void AccessData(void** ppvData);
  void UnaccessData();
  void AllocData();
  void AllocDescriptor(DWORD dwDims);
  void Copy(LPSAFEARRAY* ppsa);
  void GetLBound(DWORD dwDim, long* pLBound);
  void GetUBound(DWORD dwDim, long* pUBound);
  void GetElement(long* rgIndices, void* pvData);
  void PtrOfIndex(long* rgIndices, void** ppvData);
  void PutElement(long* rgIndices, void* pvData);
  void Lock();
  void Unlock();
  DWORD GetDim();
  DWORD GetElemSize();

  DWORD m_dwElementSize;
  DWORD m_dwDims;
};

// ---------------------------------------------------------------------
// COleDispatchDriver: calls an automation object through IDispatch.

class COleDispatchDriver {
public:
  COleDispatchDriver();
  COleDispatchDriver(LPDISPATCH lpDispatch, BOOL bAutoRelease = TRUE);
  COleDispatchDriver(const COleDispatchDriver& dispatchSrc);
  ~COleDispatchDriver() { ReleaseDispatch(); }

  LPDISPATCH m_lpDispatch;
  BOOL m_bAutoRelease;

  BOOL CreateDispatch(REFCLSID clsid, COleException* pError = NULL);
  BOOL CreateDispatch(LPCTSTR lpszProgID, COleException* pError = NULL);
  void AttachDispatch(LPDISPATCH lpDispatch, BOOL bAutoRelease = TRUE);
  LPDISPATCH DetachDispatch();
  void ReleaseDispatch();

  void AFX_CDECL InvokeHelper(DISPID dwDispID, WORD wFlags, VARTYPE vtRet, void* pvRet,
                              const BYTE* pbParamInfo, ...);
  void AFX_CDECL SetProperty(DISPID dwDispID, VARTYPE vtProp, ...);
  void GetProperty(DISPID dwDispID, VARTYPE vtProp, void* pvProp) const;
  void InvokeHelperV(DISPID dwDispID, WORD wFlags, VARTYPE vtRet, void* pvRet,
                     const BYTE* pbParamInfo, va_list argList);

  const COleDispatchDriver& operator=(const COleDispatchDriver& dispatchSrc);
  operator LPDISPATCH() { return m_lpDispatch; }
};

// ---------------------------------------------------------------------
// Dispatch maps: the methods and properties a CCmdTarget exposes through
// IDispatch after EnableAutomation.
//
// Each entry holds a thunk made from the member function's own type, so
// the arguments reach the function as the C++ types it declares, without
// knowledge of the processor's calling convention. The VTS_ string still
// says what each argument is converted to first, as in MFC.

enum {
  _AFX_DISP_END = 0,
  _AFX_DISP_METHOD,
  _AFX_DISP_PROPERTY,     // a member variable
  _AFX_DISP_PROPERTY_EX,  // get and set functions
  _AFX_DISP_DEFVALUE      // DISPID_VALUE names another entry
};

// One argument of a call, converted to its parameter's type.
struct AFX_DISPARG {
  VARIANT v;       // the value; the function gets a pointer into it for by-reference types
  VARIANT* pVar;   // the caller's VARIANT, for VTS_VARIANT and VTS_PVARIANT
  VARIANT* pRef;   // the caller's by-reference argument, written back after the call
  BOOL bVal;       // a BOOL for VTS_PBOOL
  CStringA strA;   // the text of a VTS_BSTR argument in an ANSI program
  AFX_DISPARG() : pVar(NULL), pRef(NULL), bVal(FALSE) { ::VariantInit(&v); }
  ~AFX_DISPARG() { ::VariantClear(&v); }
};

typedef HRESULT (*AFX_DISPINVOKE)(CCmdTarget* pThis, AFX_PMSG pfn, AFX_DISPARG* pArgs, UINT nArgs,
                                  VARIANT* pResult);

struct AFX_DISPMAP_ENTRY {
  LPCTSTR lpszName;
  long lDispID;          // DISPID_UNKNOWN: numbered by position
  LPCSTR lpszParams;     // VTS_ string
  WORD vt;               // return or property type
  WORD nKind;
  AFX_PMSG pfn;          // method or get function
  AFX_DISPINVOKE pfnInvoke;
  AFX_PMSG pfnSet;       // set function
  AFX_DISPINVOKE pfnSetInvoke;
  void* (*pfnMember)(CCmdTarget*);  // address of the property's member variable
  void (*pfnNotify)(CCmdTarget*);   // called after the property is set
};

LONGLONG AFXAPI _AfxDispGetInt(const VARIANT& v);
double AFXAPI _AfxDispGetReal(const VARIANT& v);

// Reading an argument as the parameter's type. The tag pointer picks the
// overload: the parameter's type with references and const removed.
template <class T>
inline typename std::enable_if<std::is_integral<T>::value || std::is_enum<T>::value, T>::type
_AfxDispGet(AFX_DISPARG& a, T*) {
  return (T)_AfxDispGetInt(a.v);
}
template <class T>
inline typename std::enable_if<std::is_floating_point<T>::value, T>::type _AfxDispGet(AFX_DISPARG& a,
                                                                                    T*) {
  return (T)_AfxDispGetReal(a.v);
}
inline CY _AfxDispGet(AFX_DISPARG& a, CY*) { return a.v.cyVal; }
inline VARIANT& _AfxDispGet(AFX_DISPARG& a, VARIANT*) { return *a.pVar; }
inline VARIANT* _AfxDispGet(AFX_DISPARG& a, VARIANT**) { return a.pVar; }
inline const char* _AfxDispGet(AFX_DISPARG& a, const char**) {
  if (a.v.vt == VT_BSTR) a.strA = a.v.bstrVal;
  return a.strA;
}
inline const wchar_t* _AfxDispGet(AFX_DISPARG& a, const wchar_t**) { return a.v.bstrVal; }
inline wchar_t* _AfxDispGet(AFX_DISPARG& a, wchar_t**) { return a.v.bstrVal; }
// An interface pointer, or a pointer to a by-reference value.
template <class T>
inline T* _AfxDispPtr(AFX_DISPARG& a, std::true_type) {
  return static_cast<T*>(a.v.punkVal);
}
template <class T>
inline T* _AfxDispPtr(AFX_DISPARG& a, std::false_type) {
  if (a.v.vt == VT_BOOL && sizeof(T) == sizeof(BOOL)) return (T*)&a.bVal;
  return (T*)&a.v.llVal;  // the union's members all start here
}
template <class T>
inline T* _AfxDispGet(AFX_DISPARG& a, T**) {
  return _AfxDispPtr<T>(a, std::integral_constant<bool, std::is_base_of<IUnknown, T>::value>());
}

// Storing a return value; the caller converts it to the entry's type.
template <class T>
inline typename std::enable_if<std::is_integral<T>::value || std::is_enum<T>::value>::type
_AfxDispSet(VARIANT& r, T value) {
  if (std::is_same<T, bool>::value) {
    r.vt = VT_BOOL;
    r.boolVal = value ? VARIANT_TRUE : VARIANT_FALSE;
  } else if (sizeof(T) > 4) {
    r.vt = std::is_unsigned<T>::value ? VT_UI8 : VT_I8;
    r.llVal = (LONGLONG)value;
  } else if (std::is_unsigned<T>::value && sizeof(T) == 4) {
    r.vt = VT_UI4;
    r.ulVal = (ULONG)value;
  } else {
    r.vt = VT_I4;
    r.lVal = (LONG)value;
  }
}
inline void _AfxDispSet(VARIANT& r, float value) {
  r.vt = VT_R4;
  r.fltVal = value;
}
inline void _AfxDispSet(VARIANT& r, double value) {
  r.vt = VT_R8;
  r.dblVal = value;
}
inline void _AfxDispSet(VARIANT& r, CY value) {
  r.vt = VT_CY;
  r.cyVal = value;
}
inline void _AfxDispSet(VARIANT& r, BSTR value) {  // the caller owns it
  r.vt = VT_BSTR;
  r.bstrVal = value;
}
inline void _AfxDispSet(VARIANT& r, VARIANT value) { r = value; }  // the caller owns it
inline void _AfxDispSet(VARIANT& r, const COleVariant& value) {  // a copy
  ::VariantCopy(&r, const_cast<COleVariant*>(&value));
}
inline void _AfxDispSet(VARIANT& r, IDispatch* value) {  // the caller owns the reference
  r.vt = VT_DISPATCH;
  r.pdispVal = value;
}
inline void _AfxDispSet(VARIANT& r, IUnknown* value) {
  r.vt = VT_UNKNOWN;
  r.punkVal = value;
}

template <class R>
struct _AfxDispRet {
  template <class F>
  static void Call(VARIANT* r, F f) {
    _AfxDispSet(*r, f());
  }
};
template <>
struct _AfxDispRet<void> {
  template <class F>
  static void Call(VARIANT*, F f) {
    f();
  }
};

template <class C, class M, class R, class... A, size_t... I>
inline void _AfxDispCall(C* p, M pfn, AFX_DISPARG* args, VARIANT* r, std::index_sequence<I...>) {
  (void)args;
  _AfxDispRet<R>::Call(
      r, [&]() -> R { return (p->*pfn)(_AfxDispGet(args[I], (typename std::decay<A>::type*)0)...); });
}

template <class C, class R, class... A>
HRESULT _AfxDispInvoke(CCmdTarget* pThis, AFX_PMSG pfn, AFX_DISPARG* args, UINT nArgs,
                       VARIANT* r) {
  typedef R (C::*M)(A...);
  if (nArgs < sizeof...(A)) return DISP_E_BADPARAMCOUNT;
  _AfxDispCall<C, M, R, A...>(static_cast<C*>(pThis), reinterpret_cast<M>(pfn), args, r,
                              std::index_sequence_for<A...>());
  return S_OK;
}
template <class C, class R, class... A>
HRESULT _AfxDispInvokeConst(CCmdTarget* pThis, AFX_PMSG pfn, AFX_DISPARG* args, UINT nArgs,
                            VARIANT* r) {
  typedef R (C::*M)(A...) const;
  if (nArgs < sizeof...(A)) return DISP_E_BADPARAMCOUNT;
  _AfxDispCall<C, M, R, A...>(static_cast<C*>(pThis), reinterpret_cast<M>(pfn), args, r,
                              std::index_sequence_for<A...>());
  return S_OK;
}

template <class C, class R, class... A>
inline AFX_DISPINVOKE _AfxDispThunk(R (C::*)(A...)) {
  return &_AfxDispInvoke<C, R, A...>;
}
template <class C, class R, class... A>
inline AFX_DISPINVOKE _AfxDispThunk(R (C::*)(A...) const) {
  return &_AfxDispInvokeConst<C, R, A...>;
}

#define _AFX_DISP_FN(theClass, pfn) \
  reinterpret_cast<AFX_PMSG>(&theClass::pfn), _AfxDispThunk(&theClass::pfn)
#define _AFX_DISP_NOFN (AFX_PMSG)0, (AFX_DISPINVOKE)0
#define _AFX_DISP_MEMBER(theClass, memberName) \
  [](CCmdTarget* p) -> void* { return (void*)&static_cast<theClass*>(p)->memberName; }
#define _AFX_DISP_NOTIFY(theClass, pfnAfterSet) \
  [](CCmdTarget* p) { static_cast<theClass*>(p)->pfnAfterSet(); }

#define BEGIN_DISPATCH_MAP(theClass, baseClass) \
  const AFX_DISPMAP* theClass::GetDispatchMap() const { return GetThisDispatchMap(); } \
  const AFX_DISPMAP* PASCAL theClass::GetThisDispatchMap() { \
    typedef baseClass TheBaseClass; \
    static const AFX_DISPMAP_ENTRY _dispatchEntries[] = {

#define END_DISPATCH_MAP() \
      {NULL, DISPID_UNKNOWN, NULL, VT_EMPTY, _AFX_DISP_END, _AFX_DISP_NOFN, _AFX_DISP_NOFN, NULL, \
       NULL}}; \
    static const AFX_DISPMAP dispatchMap = {&TheBaseClass::GetThisDispatchMap, \
                                            &_dispatchEntries[0]}; \
    return &dispatchMap; \
  }

#define DISP_FUNCTION_ID(theClass, szExternalName, dispid, pfnMember, vtRetVal, vtsParams) \
  {_T(szExternalName), (long)(dispid), vtsParams, (WORD)(vtRetVal), _AFX_DISP_METHOD, \
   _AFX_DISP_FN(theClass, pfnMember), _AFX_DISP_NOFN, NULL, NULL},
#define DISP_FUNCTION(theClass, szExternalName, pfnMember, vtRetVal, vtsParams) \
  DISP_FUNCTION_ID(theClass, szExternalName, DISPID_UNKNOWN, pfnMember, vtRetVal, vtsParams)

#define DISP_PROPERTY_ID(theClass, szExternalName, dispid, memberName, vtPropType) \
  {_T(szExternalName), (long)(dispid), NULL, (WORD)(vtPropType), _AFX_DISP_PROPERTY, \
   _AFX_DISP_NOFN, _AFX_DISP_NOFN, _AFX_DISP_MEMBER(theClass, memberName), NULL},
#define DISP_PROPERTY(theClass, szExternalName, memberName, vtPropType) \
  DISP_PROPERTY_ID(theClass, szExternalName, DISPID_UNKNOWN, memberName, vtPropType)

#define DISP_PROPERTY_NOTIFY_ID(theClass, szExternalName, dispid, memberName, pfnAfterSet, \
                                vtPropType) \
  {_T(szExternalName), (long)(dispid), NULL, (WORD)(vtPropType), _AFX_DISP_PROPERTY, \
   _AFX_DISP_NOFN, _AFX_DISP_NOFN, _AFX_DISP_MEMBER(theClass, memberName), \
   _AFX_DISP_NOTIFY(theClass, pfnAfterSet)},
#define DISP_PROPERTY_NOTIFY(theClass, szExternalName, memberName, pfnAfterSet, vtPropType) \
  DISP_PROPERTY_NOTIFY_ID(theClass, szExternalName, DISPID_UNKNOWN, memberName, pfnAfterSet, \
                          vtPropType)

#define DISP_PROPERTY_PARAM_ID(theClass, szExternalName, dispid, pfnGet, pfnSet, vtPropType, \
                               vtsParams) \
  {_T(szExternalName), (long)(dispid), vtsParams, (WORD)(vtPropType), _AFX_DISP_PROPERTY_EX, \
   _AFX_DISP_FN(theClass, pfnGet), _AFX_DISP_FN(theClass, pfnSet), NULL, NULL},
#define DISP_PROPERTY_PARAM(theClass, szExternalName, pfnGet, pfnSet, vtPropType, vtsParams) \
  DISP_PROPERTY_PARAM_ID(theClass, szExternalName, DISPID_UNKNOWN, pfnGet, pfnSet, vtPropType, \
                         vtsParams)
#define DISP_PROPERTY_EX_ID(theClass, szExternalName, dispid, pfnGet, pfnSet, vtPropType) \
  DISP_PROPERTY_PARAM_ID(theClass, szExternalName, dispid, pfnGet, pfnSet, vtPropType, NULL)
#define DISP_PROPERTY_EX(theClass, szExternalName, pfnGet, pfnSet, vtPropType) \
  DISP_PROPERTY_PARAM_ID(theClass, szExternalName, DISPID_UNKNOWN, pfnGet, pfnSet, vtPropType, \
                         NULL)

// DISPID_VALUE, the default member, is another name for an entry.
#define DISP_DEFVALUE(theClass, szExternalName) \
  {_T(szExternalName), DISPID_UNKNOWN, NULL, VT_EMPTY, _AFX_DISP_DEFVALUE, _AFX_DISP_NOFN, \
   _AFX_DISP_NOFN, NULL, NULL},
#define DISP_DEFVALUE_ID(theClass, dispid) \
  {NULL, (long)(dispid), NULL, VT_EMPTY, _AFX_DISP_DEFVALUE, _AFX_DISP_NOFN, _AFX_DISP_NOFN, \
   NULL, NULL},

// ---------------------------------------------------------------------
// Class factories. DECLARE_OLECREATE gives a class a COleObjectFactory
// that creates it for COM clients; IMPLEMENT_OLECREATE names its CLSID
// and ProgID.

enum AFX_REG_FLAGS {
  afxRegDefault = 0x0000,
  afxRegInsertable = 0x0001,
  afxRegApartmentThreading = 0x0002,
  afxRegFreeThreading = 0x0004
};

class COleObjectFactory : public CCmdTarget {
  DECLARE_DYNAMIC(COleObjectFactory)
public:
  COleObjectFactory(REFCLSID clsid, CRuntimeClass* pRuntimeClass, BOOL bMultiInstance,
                    LPCTSTR lpszProgID);
  COleObjectFactory(REFCLSID clsid, CRuntimeClass* pRuntimeClass, BOOL bMultiInstance, int nFlags,
                    LPCTSTR lpszProgID);
  virtual ~COleObjectFactory();

  BOOL IsRegistered() const { return m_dwRegister != 0; }
  REFCLSID GetClassID() const { return m_clsid; }
  LPCTSTR GetProgID() const { return m_lpszProgID; }

  virtual BOOL Register();
  BOOL Unregister();
  void Revoke();
  void UpdateRegistry(LPCTSTR lpszProgID = NULL);
  virtual BOOL UpdateRegistry(BOOL bRegister);
  BOOL IsLicenseValid();

  static BOOL PASCAL RegisterAll();
  static void PASCAL RevokeAll();
  static BOOL PASCAL UpdateRegistryAll(BOOL bRegister = TRUE);

  // The factories in the program, in the order they were constructed.
  COleObjectFactory* m_pNextFactory;
  static COleObjectFactory* PASCAL GetFirstFactory();

protected:
  virtual CCmdTarget* OnCreateObject();
  virtual BOOL VerifyUserLicense() { return TRUE; }
  virtual BOOL GetLicenseKey(DWORD dwReserved, BSTR* pbstrKey);
  virtual BOOL VerifyLicenseKey(BSTR bstrKey);

public:
  DWORD m_dwRegister;
  CLSID m_clsid;
  CRuntimeClass* m_pRuntimeClass;
  BOOL m_bMultiInstance;
  LPCTSTR m_lpszProgID;
  int m_nFlags;
  BOOL m_bLicenseChecked;
  BOOL m_bLicenseValid;

  BEGIN_INTERFACE_PART(ClassFactory, IClassFactory2)
    STDMETHOD(CreateInstance)(LPUNKNOWN pUnkOuter, REFIID riid, LPVOID* ppvObject);
    STDMETHOD(LockServer)(BOOL fLock);
    STDMETHOD(GetLicInfo)(LPLICINFO pLicInfo);
    STDMETHOD(RequestLicKey)(DWORD dwReserved, BSTR* pbstrKey);
    STDMETHOD(CreateInstanceLic)(LPUNKNOWN pUnkOuter, LPUNKNOWN pUnkReserved, REFIID riid,
                                 BSTR bstrKey, LPVOID* ppvObject);
  END_INTERFACE_PART(ClassFactory)

  // Creates an object for a client; with a license key when bstrKey is not NULL.
  HRESULT CreateObjectFor(LPUNKNOWN pUnkOuter, REFIID riid, LPVOID* ppvObject, BSTR bstrKey);

  virtual void OnFinalRelease() {}  // factories are static objects

  DECLARE_INTERFACE_MAP()
};
typedef COleObjectFactory COleObjectFactoryEx;

#define DECLARE_OLECREATE(class_name) \
public: \
  static COleObjectFactory factory; \
  static const GUID guid;

#define IMPLEMENT_OLECREATE(class_name, external_name, l, w1, w2, b1, b2, b3, b4, b5, b6, b7, b8) \
  const GUID class_name::guid = {l, w1, w2, {b1, b2, b3, b4, b5, b6, b7, b8}}; \
  COleObjectFactory class_name::factory(class_name::guid, RUNTIME_CLASS(class_name), FALSE, \
                                        _T(external_name));

#define IMPLEMENT_OLECREATE_FLAGS(class_name, external_name, nFlags, l, w1, w2, b1, b2, b3, b4, \
                                  b5, b6, b7, b8) \
  const GUID class_name::guid = {l, w1, w2, {b1, b2, b3, b4, b5, b6, b7, b8}}; \
  COleObjectFactory class_name::factory(class_name::guid, RUNTIME_CLASS(class_name), FALSE, \
                                        nFlags, _T(external_name));

// For a DLL server's DllGetClassObject and DllCanUnloadNow.
HRESULT AFXAPI AfxDllGetClassObject(REFCLSID rclsid, REFIID riid, LPVOID* ppv);
HRESULT AFXAPI AfxDllCanUnloadNow();

// ---------------------------------------------------------------------
// Connection points: an object's outgoing interfaces. A class with
// connections calls EnableConnections in its constructor and lists its
// points in a connection map; each point is a member made with
// BEGIN_CONNECTION_PART.

class CConnectionPoint : public CCmdTarget {
  DECLARE_DYNAMIC(CConnectionPoint)
public:
  CConnectionPoint();
  virtual ~CConnectionPoint();

  POSITION GetStartPosition() const;
  LPUNKNOWN GetNextConnection(POSITION& pos) const;
  const CPtrArray* GetConnections();

  virtual LPCONNECTIONPOINTCONTAINER GetContainer();
  virtual REFIID GetIID() = 0;
  virtual void OnAdvise(BOOL bAdvise);
  virtual int GetMaxConnections();  // -1 for any number
  virtual HRESULT QuerySinkInterface(LPUNKNOWN pUnkSink, void** ppInterface);

  CCmdTarget* m_pContainer;  // set by EnableConnections
  CPtrArray m_connections;   // the sinks, each AddRef'd

  BEGIN_INTERFACE_PART(ConnPt, IConnectionPoint)
    STDMETHOD(GetConnectionInterface)(IID* pIID);
    STDMETHOD(GetConnectionPointContainer)(IConnectionPointContainer** ppCPC);
    STDMETHOD(Advise)(LPUNKNOWN pUnkSink, DWORD* pdwCookie);
    STDMETHOD(Unadvise)(DWORD dwCookie);
    STDMETHOD(EnumConnections)(LPENUMCONNECTIONS* ppEnum);
  END_INTERFACE_PART(ConnPt)

  DECLARE_INTERFACE_MAP()
};

#define BEGIN_CONNECTION_PART(theClass, localClass) \
  class X##localClass : public CConnectionPoint { \
  public: \
    X##localClass() {}
#define CONNECTION_IID(iid) \
  REFIID GetIID() { return iid; }
#define END_CONNECTION_PART(localClass) \
  } m_x##localClass; \
  friend class X##localClass;

// Connects a sink to a source's connection point for iid. With bRefCount
// FALSE the connection does not keep the sink alive (the sink is usually
// the caller itself).
BOOL AFXAPI AfxConnectionAdvise(LPUNKNOWN pUnkSrc, REFIID iid, LPUNKNOWN pUnkSink, BOOL bRefCount,
                                DWORD* pdwCookie);
BOOL AFXAPI AfxConnectionUnadvise(LPUNKNOWN pUnkSrc, REFIID iid, LPUNKNOWN pUnkSink,
                                  BOOL bRefCount, DWORD dwCookie);

// ---------------------------------------------------------------------
// Type libraries. DECLARE_OLETYPELIB in the class and IMPLEMENT_OLETYPELIB
// in its source give its IDispatch type information from a registered
// type library.

class CTypeLibCache {
public:
  CTypeLibCache() : m_pTypeLibID(NULL), m_lcid((LCID)-1), m_ptlib(NULL), m_ptinfo(NULL) {}
  ~CTypeLibCache();
  void Lock() {}
  void Unlock() {}
  BOOL Lookup(LCID lcid, LPTYPELIB* pptlib);
  void Cache(LCID lcid, LPTYPELIB ptlib);
  BOOL LookupTypeInfo(LCID lcid, REFGUID guid, LPTYPEINFO* pptinfo);
  void CacheTypeInfo(LCID lcid, REFGUID guid, LPTYPEINFO ptinfo);

  const GUID* m_pTypeLibID;
  LCID m_lcid;
  LPTYPELIB m_ptlib;
  GUID m_guidInfo;
  LPTYPEINFO m_ptinfo;
};

CTypeLibCache* AFXAPI AfxGetTypeLibCache(const GUID* pTypeLibID);

#define DECLARE_OLETYPELIB(class_name) \
protected: \
  virtual UINT GetTypeInfoCount(); \
  virtual HRESULT GetTypeLib(LCID lcid, LPTYPELIB* ppTypeLib); \
  virtual CTypeLibCache* GetTypeLibCache();

#define IMPLEMENT_OLETYPELIB(class_name, tlid, wVerMajor, wVerMinor) \
  UINT class_name::GetTypeInfoCount() { return 1; } \
  HRESULT class_name::GetTypeLib(LCID lcid, LPTYPELIB* ppTypeLib) { \
    return ::LoadRegTypeLib(tlid, wVerMajor, wVerMinor, lcid, ppTypeLib); \
  } \
  CTypeLibCache* class_name::GetTypeLibCache() { return AfxGetTypeLibCache(&tlid); }

// Registers the type library in pszFileName (by default the module's own
// file) and checks that it is tlid.
BOOL AFXAPI AfxOleRegisterTypeLib(HINSTANCE hInstance, REFGUID tlid, LPCTSTR pszFileName = NULL,
                                  LPCTSTR pszHelpDir = NULL);
// Unregisters a type library: one version, or with 0 for wVerMajor all
// of them.
BOOL AFXAPI AfxOleUnregisterTypeLib(REFGUID tlid, WORD wVerMajor = 0, WORD wVerMinor = 0,
                                    LCID lcid = 0);

// ---------------------------------------------------------------------
// Dialog data exchange with dates and currency.

void AFXAPI DDX_Text(CDataExchange* pDX, int nIDC, COleDateTime& value);
void AFXAPI DDX_Text(CDataExchange* pDX, int nIDC, COleCurrency& value);
void AFXAPI DDX_DateTimeCtrl(CDataExchange* pDX, int nIDC, COleDateTime& value);
void AFXAPI DDX_MonthCalCtrl(CDataExchange* pDX, int nIDC, COleDateTime& value);
void AFXAPI DDV_MinMaxDateTime(CDataExchange* pDX, COleDateTime& refValue,
                               const COleDateTime& refMinRange, const COleDateTime& refMaxRange);
void AFXAPI DDV_MinMaxMonth(CDataExchange* pDX, COleDateTime& refValue,
                            const COleDateTime& refMinRange, const COleDateTime& refMaxRange);

#endif  // __AFXDISP_H__
