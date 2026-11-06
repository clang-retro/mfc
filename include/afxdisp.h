// afxdisp.h - OLE automation: the variant, date, currency and safe array
// wrappers, COleDispatchDriver for calling automation objects, and the
// OLE exceptions. Exposing automation objects and containing ActiveX
// controls are not part of this library yet; the start-up calls that
// application wizards generate are here so such programs still build.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
#ifndef __AFXDISP_H__
#define __AFXDISP_H__

#include "afxwin.h"
#include <ole2.h>
#include <oleauto.h>

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
inline BOOL AFXAPI AfxOleGetUserCtrl() { return TRUE; }
inline void AFXAPI AfxOleSetUserCtrl(BOOL) {}

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
