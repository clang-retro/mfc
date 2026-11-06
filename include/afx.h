// afx.h - core classes: CObject, CRuntimeClass, exceptions, CString,
// CFile, CArchive, CTime.
//
// Part of clang-retro MFC, an independent implementation of the MFC API
// written from its public documentation.
#ifndef __AFX_H__
#define __AFX_H__

#ifndef __cplusplus
#error MFC requires C++ compilation
#endif

#if defined(_UNICODE) && !defined(UNICODE)
#define UNICODE
#endif
#if defined(UNICODE) && !defined(_UNICODE)
#define _UNICODE
#endif

#ifndef WINVER
#define WINVER 0x0400
#endif

#include <windows.h>
#if !defined(__has_include) || __has_include(<tchar.h>)
#include <tchar.h>
#else
#include "afxtchar.h"  // C libraries without one, such as picolibc
#endif
#include <stddef.h>
#include <limits.h>
#include <errno.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <stdio.h>
#include <time.h>
#include <new>

// The Windows SDK's min and max macros are C-only in MinGW's headers.
// Programs written for MFC use them freely, so provide function
// templates that also accept mixed argument types. (std::min and
// std::max still win overload resolution for same-type arguments.)
#if !defined(NOMINMAX) && !defined(min) && !defined(max)
#include <type_traits>
template <class _AfxT1, class _AfxT2>
inline typename std::common_type<_AfxT1, _AfxT2>::type min(_AfxT1 a, _AfxT2 b) { return b < a ? b : a; }
template <class _AfxT1, class _AfxT2>
inline typename std::common_type<_AfxT1, _AfxT2>::type max(_AfxT1 a, _AfxT2 b) { return a < b ? b : a; }
#endif

// Link the MFC library and the system libraries it uses, as Visual C++
// does, unless _AFX_NOFORCE_LIBS is defined. (clang ignores
// #pragma comment(lib) for MinGW targets, so the linker directives are
// written directly.)
#ifndef _AFX_NOFORCE_LIBS
#ifdef _UNICODE
#define _AFX_LIB_NAME "mfcu"
#else
#define _AFX_LIB_NAME "mfc"
#endif
__asm__(".section .drectve,\"yn\"\n"
        ".ascii \" -defaultlib:" _AFX_LIB_NAME " -defaultlib:user32 -defaultlib:gdi32"
        " -defaultlib:comdlg32 -defaultlib:comctl32 -defaultlib:shell32 -defaultlib:advapi32"
        " -defaultlib:winspool -defaultlib:ole32\"\n"
        ".text");
#endif

#define _MFC_VER 0x0600
#define _CLANG_RETRO_MFC 1

// ---------------------------------------------------------------------
// Calling conventions and decorations

#ifndef AFXAPI
#define AFXAPI __stdcall

// MFC lets code call GetSafeHwnd() and friends through a NULL pointer.
// Modern compilers assume 'this' is never NULL and fold such checks away,
// so the pointer is passed through an empty asm statement first.
inline bool _AfxIsNullPtr(const void* p) {
#if defined(__GNUC__) || defined(__clang__)
  __asm__("" : "+r"(p));
#endif
  return p == 0;
}
#define AFX_IS_NULL_THIS() _AfxIsNullPtr(this)
#endif
#ifndef AFX_CDECL
#define AFX_CDECL __cdecl
#endif
#define AFX_STATIC static
#define AFX_STATIC_DATA static
#define AFX_INLINE inline
#define AFX_API
#define AFX_EXT_CLASS
#define AFX_EXT_API
#define AFX_EXT_DATA
#define AFX_CORE_DATA
#define AFX_DATA
#define AFX_DATADEF
#define AFX_NOVTABLE
#define AFX_IMPORT_DATA
#define AFX_CLASS_EXPORT
#define AFX_CLASS_IMPORT
#define AFX_COMDAT
#define AFX_MSG_CALL
#ifndef afx_msg
#define afx_msg
#endif
#ifndef PASCAL
#define PASCAL __stdcall
#endif
#ifndef AFX_DEPRECATED
#define AFX_DEPRECATED(x)
#endif
#define AFX_MANAGE_STATE(p)
struct AFX_MODULE_STATE;
AFX_MODULE_STATE* __stdcall AfxGetModuleState();
#define AfxGetStaticModuleState() AfxGetModuleState()
#define METHOD_PROLOGUE(theClass, localClass) \
  theClass* pThis = ((theClass*)((BYTE*)this - offsetof(theClass, m_x##localClass)));
#define METHOD_PROLOGUE_EX(theClass, localClass) METHOD_PROLOGUE(theClass, localClass)
#define METHOD_PROLOGUE_EX_(theClass, localClass) METHOD_PROLOGUE(theClass, localClass)

#ifndef _countof
#define _countof(a) (sizeof(a) / sizeof((a)[0]))
#endif
#ifndef _AFX_FUNCNAME
#define _AFX_FUNCNAME(x) x
#endif
#ifndef UNUSED
#ifdef _DEBUG
#define UNUSED(x)
#else
#define UNUSED(x) x
#endif
#endif
#define UNUSED_ALWAYS(x) (void)(x)

// Types that MFC adds on top of windows.h
struct __POSITION {};
typedef __POSITION* POSITION;
#define BEFORE_START_POSITION ((POSITION)-1L)

#ifndef _AFX_NO_DEFAULT_FLOAT
#endif

// ---------------------------------------------------------------------
// Diagnostics

void AFX_CDECL AfxTrace(LPCTSTR lpszFormat, ...);
BOOL AFXAPI AfxAssertFailedLine(LPCSTR lpszFileName, int nLine);
void AFXAPI AfxAssertValidObject(const class CObject* pOb, LPCSTR lpszFileName,
                                 int nLine);
BOOL AFXAPI AfxIsValidAddress(const void* lp, UINT_PTR nBytes,
                              BOOL bReadWrite = TRUE);
BOOL AFXAPI AfxIsValidString(LPCWSTR lpsz, int nLength = -1);
BOOL AFXAPI AfxIsValidString(LPCSTR lpsz, int nLength = -1);
void AFXAPI AfxDebugBreak();

#define THIS_FILE __FILE__
#ifdef _DEBUG
#define ASSERT(f) \
  do { if (!(f) && AfxAssertFailedLine(THIS_FILE, __LINE__)) AfxDebugBreak(); } while (0)
#define VERIFY(f) ASSERT(f)
#define ASSERT_VALID(pOb) (::AfxAssertValidObject(pOb, THIS_FILE, __LINE__))
#define DEBUG_ONLY(f) (f)
#define TRACE ::AfxTrace
#define TRACE0(sz) ::AfxTrace(_T("%s"), _T(sz))
#define TRACE1(sz, p1) ::AfxTrace(_T(sz), p1)
#define TRACE2(sz, p1, p2) ::AfxTrace(_T(sz), p1, p2)
#define TRACE3(sz, p1, p2, p3) ::AfxTrace(_T(sz), p1, p2, p3)
#else
#define ASSERT(f) ((void)0)
#define VERIFY(f) ((void)(f))
#define ASSERT_VALID(pOb) ((void)0)
#define DEBUG_ONLY(f) ((void)0)
inline void AFX_CDECL AfxTraceNull(...) {}
#define TRACE 1 ? (void)0 : ::AfxTraceNull
#define TRACE0(sz)
#define TRACE1(sz, p1)
#define TRACE2(sz, p1, p2)
#define TRACE3(sz, p1, p2, p3)
#endif
#define ATLASSERT ASSERT
#define ATLTRACE TRACE
#define ENSURE(cond) do { if (!(cond)) AfxThrowInvalidArgException(); } while (0)
#define ENSURE_VALID(p) ENSURE((p) != NULL)
#define ENSURE_ARG(cond) ENSURE(cond)
#define ASSERT_POINTER(p, type) ASSERT(((p) != NULL) && AfxIsValidAddress((p), sizeof(type), FALSE))
#define ASSERT_NULL_OR_POINTER(p, type) ASSERT(((p) == NULL) || AfxIsValidAddress((p), sizeof(type), FALSE))

// DEBUG_NEW records the file and line of an allocation in MFC's debug
// builds; here it is plain new with the placement form MFC declares.
void* AFX_CDECL operator new(size_t nSize, LPCSTR lpszFileName, int nLine);
void* AFX_CDECL operator new[](size_t nSize, LPCSTR lpszFileName, int nLine);
void AFX_CDECL operator delete(void* p, LPCSTR lpszFileName, int nLine);
void AFX_CDECL operator delete[](void* p, LPCSTR lpszFileName, int nLine);
#ifdef _DEBUG
#define DEBUG_NEW new (THIS_FILE, __LINE__)
#else
#define DEBUG_NEW new
#endif

// ---------------------------------------------------------------------
// Forward declarations

class CObject;
class CRuntimeClass;
class CException;
class CArchive;
class CDumpContext;
class CFile;
class CStdioFile;
class CMemFile;
class CFileException;
class CArchiveException;
class CMemoryException;
class CNotSupportedException;
class CInvalidArgException;
class CSimpleException;
class CUserException;
class CResourceException;
class CTime;
class CTimeSpan;
struct CFileStatus;
class CDocument;

// ---------------------------------------------------------------------
// CRuntimeClass

struct AFX_CLASSINIT {
  AFX_CLASSINIT(CRuntimeClass* pNewClass);
};

class CRuntimeClass {
public:
  LPCSTR m_lpszClassName;
  int m_nObjectSize;
  UINT m_wSchema;
  CObject*(PASCAL* m_pfnCreateObject)();
  CRuntimeClass*(PASCAL* m_pfnGetBaseClass)();
  CRuntimeClass* m_pBaseClass;
  CRuntimeClass* m_pNextClass;
  const AFX_CLASSINIT* m_pClassInit;

  CObject* CreateObject();
  BOOL IsDerivedFrom(const CRuntimeClass* pBaseClass) const;
  static CRuntimeClass* PASCAL FromName(LPCSTR lpszClassName);
  static CRuntimeClass* PASCAL FromName(LPCWSTR lpszClassName);
  static CObject* PASCAL CreateObject(LPCSTR lpszClassName);
  static CObject* PASCAL CreateObject(LPCWSTR lpszClassName);
  void Store(CArchive& ar) const;
  static CRuntimeClass* PASCAL Load(CArchive& ar, UINT* pwSchemaNum);
  BOOL IsSerializable() const { return (m_wSchema & 0xffff) != 0xffff; }
};

#define VERSIONABLE_SCHEMA (0x80000000)

#define _RUNTIME_CLASS(class_name) \
  ((CRuntimeClass*)(&class_name::class##class_name))
#define RUNTIME_CLASS(class_name) _RUNTIME_CLASS(class_name)
#define ASSERT_KINDOF(class_name, object) \
  ASSERT((object)->IsKindOf(RUNTIME_CLASS(class_name)))
#define DYNAMIC_DOWNCAST(class_name, object) \
  ((class_name*)AfxDynamicDownCast(RUNTIME_CLASS(class_name), (CObject*)(object)))
#define STATIC_DOWNCAST(class_name, object) \
  (static_cast<class_name*>(AfxStaticDownCast(RUNTIME_CLASS(class_name), (CObject*)(object))))
#define STATIC_DOWNCAST_T(class_name, T1, object) \
  (static_cast<class_name<T1>*>(object))

#define DECLARE_DYNAMIC(class_name) \
public: \
  static CRuntimeClass class##class_name; \
  static CRuntimeClass* PASCAL GetThisClass(); \
  virtual CRuntimeClass* GetRuntimeClass() const;

#define _DECLARE_DYNAMIC(class_name) DECLARE_DYNAMIC(class_name)

#define DECLARE_DYNCREATE(class_name) \
  DECLARE_DYNAMIC(class_name) \
  static CObject* PASCAL CreateObject();

#define _DECLARE_DYNCREATE(class_name) DECLARE_DYNCREATE(class_name)

#define DECLARE_SERIAL(class_name) \
  _DECLARE_DYNCREATE(class_name) \
  friend CArchive& AFXAPI operator>>(CArchive& ar, class_name*& pOb);

#define IMPLEMENT_RUNTIMECLASS(class_name, base_class_name, wSchema, pfnNew, class_init) \
  CRuntimeClass* PASCAL class_name::GetThisClass() { return _RUNTIME_CLASS(class_name); } \
  static CRuntimeClass* PASCAL _AfxBaseOf##class_name() { return RUNTIME_CLASS(base_class_name); } \
  CRuntimeClass class_name::class##class_name = { \
      #class_name, sizeof(class class_name), wSchema, pfnNew, \
      &_AfxBaseOf##class_name, RUNTIME_CLASS(base_class_name), NULL, class_init}; \
  CRuntimeClass* class_name::GetRuntimeClass() const { return _RUNTIME_CLASS(class_name); }

#define IMPLEMENT_DYNAMIC(class_name, base_class_name) \
  IMPLEMENT_RUNTIMECLASS(class_name, base_class_name, 0xFFFF, NULL, NULL)

#define IMPLEMENT_DYNCREATE(class_name, base_class_name) \
  CObject* PASCAL class_name::CreateObject() { return new class_name; } \
  IMPLEMENT_RUNTIMECLASS(class_name, base_class_name, 0xFFFF, class_name::CreateObject, NULL)

#define IMPLEMENT_SERIAL(class_name, base_class_name, wSchema) \
  CObject* PASCAL class_name::CreateObject() { return new class_name; } \
  extern AFX_CLASSINIT _init_##class_name; \
  IMPLEMENT_RUNTIMECLASS(class_name, base_class_name, wSchema, class_name::CreateObject, \
                         &_init_##class_name) \
  AFX_CLASSINIT _init_##class_name(RUNTIME_CLASS(class_name)); \
  CArchive& AFXAPI operator>>(CArchive& ar, class_name*& pOb) { \
    pOb = (class_name*)ar.ReadObject(RUNTIME_CLASS(class_name)); \
    return ar; \
  }

// The template forms (MFC 7).
#define IMPLEMENT_RUNTIMECLASS_T(class_name, T1, base_class_name, wSchema, pfnNew, class_init) \
  template <typename T1> CRuntimeClass* PASCAL class_name<T1>::GetThisClass() { \
    return _RUNTIME_CLASS(class_name); } \
  template <typename T1> CRuntimeClass class_name<T1>::class##class_name = { \
      #class_name, sizeof(class class_name<T1>), wSchema, pfnNew, \
      &base_class_name::GetThisClass, RUNTIME_CLASS(base_class_name), NULL, class_init}; \
  template <typename T1> CRuntimeClass* class_name<T1>::GetRuntimeClass() const { \
    return _RUNTIME_CLASS(class_name); }
#define IMPLEMENT_DYNAMIC_T(class_name, T1, base_class_name) \
  IMPLEMENT_RUNTIMECLASS_T(class_name, T1, base_class_name, 0xFFFF, NULL, NULL)

// ---------------------------------------------------------------------
// CObject

class CObject {
public:
  virtual CRuntimeClass* GetRuntimeClass() const;
  virtual ~CObject() {}

  void* PASCAL operator new(size_t nSize) { return ::operator new(nSize); }
  void* PASCAL operator new(size_t, void* p) { return p; }
  void PASCAL operator delete(void* p) { ::operator delete(p); }
  void PASCAL operator delete(void*, void*) {}
  void* PASCAL operator new(size_t nSize, LPCSTR, int) { return ::operator new(nSize); }
  void PASCAL operator delete(void* p, LPCSTR, int) { ::operator delete(p); }

protected:
  CObject() {}

private:
  CObject(const CObject&);
  void operator=(const CObject&);

public:
  BOOL IsSerializable() const;
  BOOL IsKindOf(const CRuntimeClass* pClass) const;
  virtual void Serialize(CArchive& ar);
  virtual void AssertValid() const;
  virtual void Dump(CDumpContext& dc) const;

  static CRuntimeClass classCObject;
  static CRuntimeClass* PASCAL GetThisClass();
};

CObject* AFXAPI AfxDynamicDownCast(CRuntimeClass* pClass, CObject* pObject);
CObject* AFXAPI AfxStaticDownCast(CRuntimeClass* pClass, CObject* pObject);
void AFXAPI AfxDoForAllClasses(void(AFX_CDECL* pfn)(const CRuntimeClass* pClass,
                                                    void* pContext),
                               void* pContext);

// ---------------------------------------------------------------------
// Exceptions
//
// MFC exceptions are heap objects thrown by pointer; the handler deletes
// them with Delete(). TRY/CATCH are C++ try/catch.

class CException : public CObject {
  DECLARE_DYNAMIC(CException)
public:
  CException();
  explicit CException(BOOL bAutoDelete);
  virtual ~CException() {}
  void Delete();
  virtual BOOL GetErrorMessage(LPTSTR lpszError, UINT nMaxError,
                               PUINT pnHelpContext = NULL) const;
  virtual BOOL GetErrorMessage(LPTSTR lpszError, UINT nMaxError,
                               PUINT pnHelpContext = NULL);
  virtual int ReportError(UINT nType = MB_OK, UINT nMessageID = 0);

  BOOL m_bAutoDelete;
  BOOL m_bReadyForDelete;
};

class CSimpleException : public CException {
  DECLARE_DYNAMIC(CSimpleException)
public:
  CSimpleException() : m_nResourceID(0) {}
  explicit CSimpleException(BOOL bAutoDelete)
      : CException(bAutoDelete), m_nResourceID(0) {}
  virtual BOOL GetErrorMessage(LPTSTR lpszError, UINT nMaxError,
                               PUINT pnHelpContext = NULL) const;
  virtual BOOL GetErrorMessage(LPTSTR lpszError, UINT nMaxError,
                               PUINT pnHelpContext = NULL);
  UINT m_nResourceID;
};

class CMemoryException : public CSimpleException {
  DECLARE_DYNAMIC(CMemoryException)
public:
  CMemoryException() {}
  explicit CMemoryException(BOOL b, UINT = 0) : CSimpleException(b) {}
};

class CNotSupportedException : public CSimpleException {
  DECLARE_DYNAMIC(CNotSupportedException)
public:
  CNotSupportedException() {}
  explicit CNotSupportedException(BOOL b, UINT = 0) : CSimpleException(b) {}
};

class CInvalidArgException : public CSimpleException {
  DECLARE_DYNAMIC(CInvalidArgException)
public:
  CInvalidArgException() {}
  explicit CInvalidArgException(BOOL b, UINT = 0) : CSimpleException(b) {}
};

class CUserException : public CSimpleException {
  DECLARE_DYNAMIC(CUserException)
public:
  CUserException() {}
  explicit CUserException(BOOL b, UINT = 0) : CSimpleException(b) {}
};

class CResourceException : public CSimpleException {
  DECLARE_DYNAMIC(CResourceException)
public:
  CResourceException() {}
  explicit CResourceException(BOOL b, UINT = 0) : CSimpleException(b) {}
};


void AFXAPI AfxThrowMemoryException();
void AFXAPI AfxThrowNotSupportedException();
void AFXAPI AfxThrowInvalidArgException();
void AFXAPI AfxThrowUserException();
void AFXAPI AfxThrowResourceException();
void AFXAPI AfxThrowArchiveException(int cause, LPCTSTR lpszArchiveName = NULL);
void AFXAPI AfxThrowFileException(int cause, LONG lOsError = -1,
                                  LPCTSTR lpszFileName = NULL);
void AFXAPI AfxAbort();

#define TRY { try {
#define CATCH(class, e) } catch (class * e) { \
  ASSERT(e->IsKindOf(RUNTIME_CLASS(class))); (void)e;
#define AND_CATCH(class, e) } catch (class * e) { \
  ASSERT(e->IsKindOf(RUNTIME_CLASS(class))); (void)e;
#define END_CATCH } }
#define CATCH_ALL(e) } catch (CException * e) { (void)e;
#define AND_CATCH_ALL(e) } catch (CException * e) { (void)e;
#define END_CATCH_ALL } }
#define END_TRY } catch (CException * e) { e->Delete(); } }
#define THROW(e) throw e
#define THROW_LAST() throw
#define DELETE_EXCEPTION(e) do { if (e) { e->Delete(); } } while (0)
#define NO_CPP_EXCEPTION(expr)

// ---------------------------------------------------------------------
// Strings

#include "afxstr.h"

class CArchiveException : public CException {
  DECLARE_DYNAMIC(CArchiveException)
public:
  enum {
    none, genericException, readOnly, endOfFile, writeOnly, badIndex,
    badClass, badSchema, bufferFull
  };
  CArchiveException(int cause = none, LPCTSTR lpszArchiveName = NULL);
  virtual BOOL GetErrorMessage(LPTSTR lpszError, UINT nMaxError,
                               PUINT pnHelpContext = NULL) const;
  virtual BOOL GetErrorMessage(LPTSTR lpszError, UINT nMaxError,
                               PUINT pnHelpContext = NULL);
  int m_cause;
  CString m_strFileName;
};

class CFileException : public CException {
  DECLARE_DYNAMIC(CFileException)
public:
  enum {
    none, genericException, fileNotFound, badPath, tooManyOpenFiles,
    accessDenied, invalidFile, removeCurrentDir, directoryFull, badSeek,
    hardIO, sharingViolation, lockViolation, diskFull, endOfFile
  };
  CFileException(int cause = CFileException::none, LONG lOsError = -1,
                 LPCTSTR lpszArchiveName = NULL);
  void Clear() { m_cause = none; m_lOsError = 0; m_strFileName.Empty(); }
  virtual BOOL GetErrorMessage(LPTSTR lpszError, UINT nMaxError,
                               PUINT pnHelpContext = NULL) const;
  virtual BOOL GetErrorMessage(LPTSTR lpszError, UINT nMaxError,
                               PUINT pnHelpContext = NULL);
  static int PASCAL OsErrorToException(LONG lOsError);
  static int PASCAL ErrnoToException(int nErrno);
  static void PASCAL ThrowOsError(LONG lOsError, LPCTSTR lpszFileName = NULL);
  static void PASCAL ThrowErrno(int nErrno, LPCTSTR lpszFileName = NULL);

  int m_cause;
  LONG m_lOsError;
  CString m_strFileName;
};

// ---------------------------------------------------------------------
// CTimeSpan and CTime

class CTimeSpan {
public:
  CTimeSpan() : m_timeSpan(0) {}
  CTimeSpan(__time64_t time) : m_timeSpan(time) {}
  CTimeSpan(LONG lDays, int nHours, int nMins, int nSecs)
      : m_timeSpan(nSecs + 60 * (nMins + 60 * (nHours + (__time64_t)24 * lDays))) {}
  LONGLONG GetDays() const { return m_timeSpan / (24 * 3600); }
  LONGLONG GetTotalHours() const { return m_timeSpan / 3600; }
  LONG GetHours() const { return (LONG)(GetTotalHours() - GetDays() * 24); }
  LONGLONG GetTotalMinutes() const { return m_timeSpan / 60; }
  LONG GetMinutes() const { return (LONG)(GetTotalMinutes() - GetTotalHours() * 60); }
  LONGLONG GetTotalSeconds() const { return m_timeSpan; }
  LONG GetSeconds() const { return (LONG)(GetTotalSeconds() - GetTotalMinutes() * 60); }
  __time64_t GetTimeSpan() const { return m_timeSpan; }
  CTimeSpan operator+(CTimeSpan s) const { return CTimeSpan(m_timeSpan + s.m_timeSpan); }
  CTimeSpan operator-(CTimeSpan s) const { return CTimeSpan(m_timeSpan - s.m_timeSpan); }
  CTimeSpan& operator+=(CTimeSpan s) { m_timeSpan += s.m_timeSpan; return *this; }
  CTimeSpan& operator-=(CTimeSpan s) { m_timeSpan -= s.m_timeSpan; return *this; }
  bool operator==(CTimeSpan s) const { return m_timeSpan == s.m_timeSpan; }
  bool operator!=(CTimeSpan s) const { return m_timeSpan != s.m_timeSpan; }
  bool operator<(CTimeSpan s) const { return m_timeSpan < s.m_timeSpan; }
  bool operator>(CTimeSpan s) const { return m_timeSpan > s.m_timeSpan; }
  bool operator<=(CTimeSpan s) const { return m_timeSpan <= s.m_timeSpan; }
  bool operator>=(CTimeSpan s) const { return m_timeSpan >= s.m_timeSpan; }
  CString Format(LPCTSTR pFormat) const;
  CString Format(UINT nID) const;

private:
  __time64_t m_timeSpan;
};

class CTime {
public:
  static CTime PASCAL GetCurrentTime();
  static BOOL PASCAL IsValidFILETIME(const FILETIME& ft);
  CTime() : m_time(0) {}
  CTime(__time64_t time) : m_time(time) {}
  CTime(int nYear, int nMonth, int nDay, int nHour, int nMin, int nSec,
        int nDST = -1);
  CTime(WORD wDosDate, WORD wDosTime, int nDST = -1);
  CTime(const SYSTEMTIME& st, int nDST = -1);
  CTime(const FILETIME& ft, int nDST = -1);

  CTime& operator=(__time64_t time) { m_time = time; return *this; }
  CTime& operator+=(CTimeSpan s) { m_time += s.GetTimeSpan(); return *this; }
  CTime& operator-=(CTimeSpan s) { m_time -= s.GetTimeSpan(); return *this; }
  CTimeSpan operator-(CTime t) const { return CTimeSpan(m_time - t.m_time); }
  CTime operator-(CTimeSpan s) const { return CTime(m_time - s.GetTimeSpan()); }
  CTime operator+(CTimeSpan s) const { return CTime(m_time + s.GetTimeSpan()); }
  bool operator==(CTime t) const { return m_time == t.m_time; }
  bool operator!=(CTime t) const { return m_time != t.m_time; }
  bool operator<(CTime t) const { return m_time < t.m_time; }
  bool operator>(CTime t) const { return m_time > t.m_time; }
  bool operator<=(CTime t) const { return m_time <= t.m_time; }
  bool operator>=(CTime t) const { return m_time >= t.m_time; }

  struct tm* GetGmtTm(struct tm* ptm) const;
  struct tm* GetLocalTm(struct tm* ptm) const;
  bool GetAsSystemTime(SYSTEMTIME& st) const;
  __time64_t GetTime() const { return m_time; }
  int GetYear() const;
  int GetMonth() const;
  int GetDay() const;
  int GetHour() const;
  int GetMinute() const;
  int GetSecond() const;
  int GetDayOfWeek() const;
  CString Format(LPCTSTR pszFormat) const;
  CString FormatGmt(LPCTSTR pszFormat) const;
  CString Format(UINT nFormatID) const;
  CString FormatGmt(UINT nFormatID) const;

private:
  __time64_t m_time;
};

// ---------------------------------------------------------------------
// Files

struct CFileStatus {
  CTime m_ctime;
  CTime m_mtime;
  CTime m_atime;
  ULONGLONG m_size;
  BYTE m_attribute;
  BYTE m_padding;
  TCHAR m_szFullName[MAX_PATH];
};

class CFile : public CObject {
  DECLARE_DYNAMIC(CFile)
public:
  enum OpenFlags {
    modeRead = 0x0000, modeWrite = 0x0001, modeReadWrite = 0x0002,
    shareCompat = 0x0000, shareExclusive = 0x0010, shareDenyWrite = 0x0020,
    shareDenyRead = 0x0030, shareDenyNone = 0x0040, modeNoInherit = 0x0080,
    modeCreate = 0x1000, modeNoTruncate = 0x2000, typeText = 0x4000,
    typeBinary = (int)0x8000, osNoBuffer = 0x10000, osWriteThrough = 0x20000,
    osRandomAccess = 0x40000, osSequentialScan = 0x80000
  };
  enum Attribute {
    normal = 0x00, readOnly = 0x01, hidden = 0x02, system = 0x04,
    volume = 0x08, directory = 0x10, archive = 0x20
  };
  enum SeekPosition { begin = 0x0, current = 0x1, end = 0x2 };

  static const HANDLE hFileNull;

  CFile();
  CFile(HANDLE hFile);
  CFile(LPCTSTR lpszFileName, UINT nOpenFlags);
  virtual ~CFile();

  HANDLE m_hFile;
  operator HANDLE() const { return m_hFile; }

  virtual ULONGLONG GetPosition() const;
  virtual BOOL GetStatus(CFileStatus& rStatus) const;
  virtual CString GetFileName() const;
  virtual CString GetFileTitle() const;
  virtual CString GetFilePath() const;
  virtual void SetFilePath(LPCTSTR lpszNewName);

  virtual BOOL Open(LPCTSTR lpszFileName, UINT nOpenFlags,
                    CFileException* pError = NULL);
  static void PASCAL Rename(LPCTSTR lpszOldName, LPCTSTR lpszNewName);
  static void PASCAL Remove(LPCTSTR lpszFileName);
  static BOOL PASCAL GetStatus(LPCTSTR lpszFileName, CFileStatus& rStatus);
  static void PASCAL SetStatus(LPCTSTR lpszFileName, const CFileStatus& status);

  ULONGLONG SeekToEnd();
  void SeekToBegin();

  virtual CFile* Duplicate() const;
  virtual ULONGLONG Seek(LONGLONG lOff, UINT nFrom);
  virtual void SetLength(ULONGLONG dwNewLen);
  virtual ULONGLONG GetLength() const;
  virtual UINT Read(void* lpBuf, UINT nCount);
  virtual void Write(const void* lpBuf, UINT nCount);
  virtual void LockRange(ULONGLONG dwPos, ULONGLONG dwCount);
  virtual void UnlockRange(ULONGLONG dwPos, ULONGLONG dwCount);
  virtual void Abort();
  virtual void Flush();
  virtual void Close();

  enum BufferCommand { bufferRead, bufferWrite, bufferCommit, bufferCheck };
  enum BufferFlags { bufferDirect = 0x01, bufferBlocking = 0x02 };
  virtual UINT GetBufferPtr(UINT nCommand, UINT nCount = 0,
                            void** ppBufStart = NULL, void** ppBufMax = NULL);

  virtual void AssertValid() const;

protected:
  BOOL m_bCloseOnDelete;
  CString m_strFileName;
};

class CStdioFile : public CFile {
  DECLARE_DYNAMIC(CStdioFile)
public:
  CStdioFile();
  CStdioFile(FILE* pOpenStream);
  CStdioFile(LPCTSTR lpszFileName, UINT nOpenFlags);
  virtual ~CStdioFile();

  FILE* m_pStream;

  virtual void WriteString(LPCTSTR lpsz);
  virtual LPTSTR ReadString(LPTSTR lpsz, UINT nMax);
  virtual BOOL ReadString(CString& rString);

  virtual ULONGLONG GetPosition() const;
  virtual BOOL Open(LPCTSTR lpszFileName, UINT nOpenFlags,
                    CFileException* pError = NULL);
  virtual UINT Read(void* lpBuf, UINT nCount);
  virtual void Write(const void* lpBuf, UINT nCount);
  virtual ULONGLONG Seek(LONGLONG lOff, UINT nFrom);
  virtual void Abort();
  virtual void Flush();
  virtual void Close();
  virtual ULONGLONG GetLength() const;
  virtual CFile* Duplicate() const;
  virtual void LockRange(ULONGLONG dwPos, ULONGLONG dwCount);
  virtual void UnlockRange(ULONGLONG dwPos, ULONGLONG dwCount);

protected:
  BOOL m_bTextMode;
};

class CMemFile : public CFile {
  DECLARE_DYNAMIC(CMemFile)
public:
  CMemFile(UINT nGrowBytes = 1024);
  CMemFile(BYTE* lpBuffer, UINT nBufferSize, UINT nGrowBytes = 0);
  virtual ~CMemFile();

  void Attach(BYTE* lpBuffer, UINT nBufferSize, UINT nGrowBytes = 0);
  BYTE* Detach();

  virtual ULONGLONG GetPosition() const;
  virtual BOOL GetStatus(CFileStatus& rStatus) const;
  virtual ULONGLONG Seek(LONGLONG lOff, UINT nFrom);
  virtual void SetLength(ULONGLONG dwNewLen);
  virtual UINT Read(void* lpBuf, UINT nCount);
  virtual void Write(const void* lpBuf, UINT nCount);
  virtual void Abort();
  virtual void Flush();
  virtual void Close();
  virtual ULONGLONG GetLength() const;
  virtual UINT GetBufferPtr(UINT nCommand, UINT nCount = 0,
                            void** ppBufStart = NULL, void** ppBufMax = NULL);
  virtual CFile* Duplicate() const;
  virtual void LockRange(ULONGLONG dwPos, ULONGLONG dwCount);
  virtual void UnlockRange(ULONGLONG dwPos, ULONGLONG dwCount);

protected:
  virtual BYTE* Alloc(SIZE_T nBytes);
  virtual BYTE* Realloc(BYTE* lpMem, SIZE_T nBytes);
  virtual BYTE* Memcpy(BYTE* lpMemTarget, const BYTE* lpMemSource, SIZE_T nBytes);
  virtual void Free(BYTE* lpMem);
  virtual void GrowFile(SIZE_T dwNewLen);

  SIZE_T m_nGrowBytes;
  SIZE_T m_nPosition;
  SIZE_T m_nBufferSize;
  SIZE_T m_nFileSize;
  BYTE* m_lpBuffer;
  BOOL m_bAutoDelete;
};

class CFileFind : public CObject {
  DECLARE_DYNAMIC(CFileFind)
public:
  CFileFind();
  virtual ~CFileFind();

  ULONGLONG GetLength() const;
  virtual CString GetFileName() const;
  virtual CString GetFilePath() const;
  virtual CString GetFileTitle() const;
  virtual CString GetFileURL() const;
  virtual CString GetRoot() const;
  virtual BOOL GetLastWriteTime(FILETIME* pTimeStamp) const;
  virtual BOOL GetLastAccessTime(FILETIME* pTimeStamp) const;
  virtual BOOL GetCreationTime(FILETIME* pTimeStamp) const;
  virtual BOOL GetLastWriteTime(CTime& refTime) const;
  virtual BOOL GetLastAccessTime(CTime& refTime) const;
  virtual BOOL GetCreationTime(CTime& refTime) const;
  virtual BOOL MatchesMask(DWORD dwMask) const;
  virtual BOOL IsDots() const;
  BOOL IsReadOnly() const { return MatchesMask(FILE_ATTRIBUTE_READONLY); }
  BOOL IsDirectory() const { return MatchesMask(FILE_ATTRIBUTE_DIRECTORY); }
  BOOL IsCompressed() const { return MatchesMask(FILE_ATTRIBUTE_COMPRESSED); }
  BOOL IsSystem() const { return MatchesMask(FILE_ATTRIBUTE_SYSTEM); }
  BOOL IsHidden() const { return MatchesMask(FILE_ATTRIBUTE_HIDDEN); }
  BOOL IsTemporary() const { return MatchesMask(FILE_ATTRIBUTE_TEMPORARY); }
  BOOL IsNormal() const { return MatchesMask(FILE_ATTRIBUTE_NORMAL); }
  BOOL IsArchived() const { return MatchesMask(FILE_ATTRIBUTE_ARCHIVE); }

  void Close();
  virtual BOOL FindFile(LPCTSTR pstrName = NULL, DWORD dwUnused = 0);
  virtual BOOL FindNextFile();

protected:
  WIN32_FIND_DATA* m_pFoundInfo;
  WIN32_FIND_DATA* m_pNextInfo;
  HANDLE m_hContext;
  CString m_strRoot;
  TCHAR m_chDirSeparator;
};

// ---------------------------------------------------------------------
// CArchive

class CArchive {
public:
  enum Mode { store = 0, load = 1, bNoFlushOnDelete = 2, bNoByteSwap = 4 };

  CArchive(CFile* pFile, UINT nMode, int nBufSize = 4096, void* lpBuf = NULL);
  ~CArchive();

  BOOL IsLoading() const { return (m_nMode & load) != 0; }
  BOOL IsStoring() const { return (m_nMode & load) == 0; }
  BOOL IsByteSwapping() const { return FALSE; }
  BOOL IsBufferEmpty() const { return m_lpBufCur == m_lpBufMax; }
  CFile* GetFile() const { return m_pFile; }
  UINT GetObjectSchema();
  void SetObjectSchema(UINT nSchema) { m_nObjectSchema = nSchema; }

  CDocument* m_pDocument;

  UINT Read(void* lpBuf, UINT nMax);
  void EnsureRead(void* lpBuf, UINT nCount);
  void Write(const void* lpBuf, UINT nMax);
  void Flush();
  void Close();
  void Abort();

  void WriteString(LPCTSTR lpsz);
  LPTSTR ReadString(LPTSTR lpsz, UINT nMax);
  BOOL ReadString(CString& rString);

  // Objects
  void WriteObject(const CObject* pOb);
  CObject* ReadObject(const CRuntimeClass* pClass);
  void WriteClass(const CRuntimeClass* pClassRef);
  CRuntimeClass* ReadClass(const CRuntimeClass* pClassRefRequested = NULL,
                           UINT* pSchema = NULL, DWORD* pObTag = NULL);
  void SerializeClass(const CRuntimeClass* pClassRef);
  void MapObject(const CObject* pOb);
  void SetStoreParams(UINT nHashSize = 2053, UINT nBlockSize = 128);
  void SetLoadParams(UINT nGrowBy = 1024);

  CArchive& operator<<(BYTE by) { return WriteT(by); }
  CArchive& operator<<(WORD w) { return WriteT(w); }
  CArchive& operator<<(LONG l) { return WriteT(l); }
  CArchive& operator<<(DWORD dw) { return WriteT(dw); }
  CArchive& operator<<(float f) { return WriteT(f); }
  CArchive& operator<<(double d) { return WriteT(d); }
  CArchive& operator<<(LONGLONG dwdw) { return WriteT(dwdw); }
  CArchive& operator<<(ULONGLONG dwdw) { return WriteT(dwdw); }
  CArchive& operator<<(int i) { return WriteT((LONG)i); }
  CArchive& operator<<(short w) { return WriteT((WORD)w); }
  CArchive& operator<<(char ch) { return WriteT((BYTE)ch); }
  CArchive& operator<<(signed char ch) { return WriteT((BYTE)ch); }
  CArchive& operator<<(unsigned u) { return WriteT((LONG)u); }
  CArchive& operator<<(bool b) { return WriteT((BYTE)(b ? 1 : 0)); }
#if !defined(_NATIVE_WCHAR_T_DEFINED) || 1
  CArchive& operator<<(wchar_t ch) { return WriteT((WORD)ch); }
#endif
  CArchive& operator<<(const CObject* pOb) { WriteObject(pOb); return *this; }
  CArchive& operator<<(const RECT& r) { Write(&r, sizeof r); return *this; }
  CArchive& operator<<(POINT p) { Write(&p, sizeof p); return *this; }
  CArchive& operator<<(SIZE s) { Write(&s, sizeof s); return *this; }

  CArchive& operator>>(BYTE& by) { return ReadT(by); }
  CArchive& operator>>(WORD& w) { return ReadT(w); }
  CArchive& operator>>(DWORD& dw) { return ReadT(dw); }
  CArchive& operator>>(LONG& l) { return ReadT(l); }
  CArchive& operator>>(float& f) { return ReadT(f); }
  CArchive& operator>>(double& d) { return ReadT(d); }
  CArchive& operator>>(LONGLONG& dwdw) { return ReadT(dwdw); }
  CArchive& operator>>(ULONGLONG& dwdw) { return ReadT(dwdw); }
  CArchive& operator>>(int& i) { LONG l; ReadT(l); i = (int)l; return *this; }
  CArchive& operator>>(short& w) { WORD x; ReadT(x); w = (short)x; return *this; }
  CArchive& operator>>(char& ch) { BYTE x; ReadT(x); ch = (char)x; return *this; }
  CArchive& operator>>(signed char& ch) { BYTE x; ReadT(x); ch = (signed char)x; return *this; }
  CArchive& operator>>(unsigned& u) { LONG l; ReadT(l); u = (unsigned)l; return *this; }
  CArchive& operator>>(bool& b) { BYTE x; ReadT(x); b = x != 0; return *this; }
  CArchive& operator>>(wchar_t& ch) { WORD x; ReadT(x); ch = (wchar_t)x; return *this; }
  CArchive& operator>>(CObject*& pOb) { pOb = ReadObject(NULL); return *this; }
  CArchive& operator>>(const CObject*& pOb) { pOb = ReadObject(NULL); return *this; }
  CArchive& operator>>(RECT& r) { EnsureRead(&r, sizeof r); return *this; }
  CArchive& operator>>(POINT& p) { EnsureRead(&p, sizeof p); return *this; }
  CArchive& operator>>(SIZE& s) { EnsureRead(&s, sizeof s); return *this; }

  // Counts written as a WORD, or 0xFFFF and a DWORD.
  DWORD_PTR ReadCount();
  void WriteCount(DWORD_PTR dwCount);

  CString m_strFileName;
  BOOL m_bForceFlat;
  BOOL m_bDirectBuffer;
  BOOL m_bBlocking;
  UINT m_nObjectSchema;

protected:
  template <class T> CArchive& WriteT(const T& v) { Write(&v, sizeof v); return *this; }
  template <class T> CArchive& ReadT(T& v) { EnsureRead(&v, sizeof v); return *this; }
  void FillBuffer(UINT nBytesNeeded);

  UINT m_nMode;
  BOOL m_bUserBuf;
  int m_nBufSize;
  CFile* m_pFile;
  BYTE* m_lpBufCur;
  BYTE* m_lpBufMax;
  BYTE* m_lpBufStart;

  // Object maps: on store, object/class pointer -> index; on load,
  // index -> pointer. Both are kept in simple growable arrays here.
  UINT m_nMapCount;
  void** m_pLoadArray;
  UINT m_nLoadArraySize;
  struct StoreEntry { const void* key; DWORD value; };
  StoreEntry* m_pStoreMap;
  UINT m_nStoreMapSize;
  UINT m_nStoreMapCount;
  UINT m_nGrowSize;
  UINT m_nHashSize;

  DWORD LookupStore(const void* key) const;
  void AddStore(const void* key, DWORD value);
  void AddLoad(void* p);

  friend class CRuntimeClass;

private:
  CArchive(const CArchive&);
  void operator=(const CArchive&);
};

CArchive& AFXAPI operator<<(CArchive& ar, const CStringA& str);
CArchive& AFXAPI operator<<(CArchive& ar, const CStringW& str);
CArchive& AFXAPI operator>>(CArchive& ar, CStringA& str);
CArchive& AFXAPI operator>>(CArchive& ar, CStringW& str);
CArchive& AFXAPI operator<<(CArchive& ar, CTime t);
CArchive& AFXAPI operator>>(CArchive& ar, CTime& t);
CArchive& AFXAPI operator<<(CArchive& ar, CTimeSpan t);
CArchive& AFXAPI operator>>(CArchive& ar, CTimeSpan& t);

// ---------------------------------------------------------------------
// CDumpContext (output goes to OutputDebugString)

class CDumpContext {
public:
  CDumpContext(CFile* pFile = NULL);
  int GetDepth() const { return m_nDepth; }
  void SetDepth(int nNewDepth) { m_nDepth = nNewDepth; }
  CDumpContext& operator<<(LPCSTR lpsz);
  CDumpContext& operator<<(LPCWSTR lpsz);
  CDumpContext& operator<<(const void* lp);
  CDumpContext& operator<<(const CObject* pOb);
  CDumpContext& operator<<(const CObject& ob);
  CDumpContext& operator<<(BYTE by);
  CDumpContext& operator<<(WORD w);
  CDumpContext& operator<<(UINT u);
  CDumpContext& operator<<(LONG l);
  CDumpContext& operator<<(DWORD dw);
  CDumpContext& operator<<(float f);
  CDumpContext& operator<<(double d);
  CDumpContext& operator<<(int n);
  CDumpContext& operator<<(LONGLONG n);
  CDumpContext& operator<<(ULONGLONG n);
  CDumpContext& operator<<(HWND h) { return *this << (const void*)h; }
  CDumpContext& operator<<(HDC h) { return *this << (const void*)h; }
  CDumpContext& operator<<(HMENU h) { return *this << (const void*)h; }
  CDumpContext& operator<<(HACCEL h) { return *this << (const void*)h; }
  CDumpContext& operator<<(HFONT h) { return *this << (const void*)h; }
  CDumpContext& operator<<(const CStringA& s) { return *this << (LPCSTR)s; }
  CDumpContext& operator<<(const CStringW& s) { return *this << (LPCWSTR)s; }
  void HexDump(LPCTSTR lpszLine, BYTE* pby, int nBytes, int nWidth);
  void Flush();

protected:
  int m_nDepth;
  CFile* m_pFile;
  void OutputString(LPCTSTR lpsz);
};

extern CDumpContext afxDump;
extern BOOL afxTraceEnabled;

// ---------------------------------------------------------------------
// Global helpers

BOOL AFXAPI AfxExtractSubString(CString& rString, LPCTSTR lpszFullString,
                                int iSubString, TCHAR chSep = _T('\n'));
UINT AFXAPI AfxGetFileTitle(LPCTSTR lpszPathName, LPTSTR lpszTitle, UINT nMax);
UINT AFXAPI AfxGetFileName(LPCTSTR lpszPathName, LPTSTR lpszTitle, UINT nMax);
BOOL AFXAPI AfxFullPath(LPTSTR lpszPathOut, LPCTSTR lpszFileIn);
BOOL AFXAPI AfxComparePath(LPCTSTR lpszPath1, LPCTSTR lpszPath2);
HINSTANCE AFXAPI AfxGetResourceHandle();
void AFXAPI AfxSetResourceHandle(HINSTANCE hInstResource);
HINSTANCE AFXAPI AfxGetInstanceHandle();
HINSTANCE AFXAPI AfxFindResourceHandle(LPCTSTR lpszName, LPCTSTR lpszType);
LPCTSTR AFXAPI AfxGetAppName();

#endif // __AFX_H__
