// afxisapi.h - Internet Server API: CHttpServer (a server extension with
// a parse map), CHttpFilter (a server filter), their contexts, and
// CHtmlStream.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#ifndef __AFXISAPI_H__
#define __AFXISAPI_H__

#include "afx.h"
#include <httpext.h>
#include <httpfilt.h>
#include <type_traits>
#include <utility>

#define ISAPIASSERT(expr) ASSERT(expr)
#define ISAPIVERIFY(expr) VERIFY(expr)
#define ISAPITRACE TRACE
#define ISAPITRACE0(sz) TRACE(_T("%s"), _T(sz))
#define ISAPITRACE1(sz, p1) TRACE(_T(sz), p1)
#define ISAPITRACE2(sz, p1, p2) TRACE(_T(sz), p1, p2)
#define ISAPITRACE3(sz, p1, p2, p3) TRACE(_T(sz), p1, p2, p3)

class CHttpServer;
class CHttpServerContext;
class CHttpFilter;
class CHttpFilterContext;

// ---------------------------------------------------------------------
// CHtmlStream: a growing memory buffer that a reply is written into.

class CHtmlStream {
public:
  CHtmlStream(UINT nGrowBytes = 4096);
  CHtmlStream(BYTE* lpBuffer, UINT nBufferSize, UINT nGrowBytes = 0);
  virtual ~CHtmlStream();

  void Attach(BYTE* lpBuffer, UINT nBufferSize, UINT nGrowBytes = 0);
  BYTE* Detach();
  DWORD GetStreamSize() const { return m_nStreamSize; }

  virtual void Abort();
  virtual void Close();
  virtual void InitStream();
  virtual void Reset();
  virtual void Write(const void* lpBuf, UINT nCount);

  CHtmlStream& operator<<(LPCSTR psz);
  CHtmlStream& operator<<(LPCWSTR psz);
  CHtmlStream& operator<<(const CString& str) { return *this << (LPCTSTR)str; }
  CHtmlStream& operator<<(short w);
  CHtmlStream& operator<<(int n);
  CHtmlStream& operator<<(long dw);
  CHtmlStream& operator<<(DWORD dw);
#ifndef _WINPORT  // where DWORD is UINT, the DWORD one takes it
  CHtmlStream& operator<<(UINT n) { return *this << (DWORD)n; }
#endif
  CHtmlStream& operator<<(float f);
  CHtmlStream& operator<<(double d);
  CHtmlStream& operator<<(const CHtmlStream& stream);
  CHtmlStream& operator<<(BYTE b);

  // ----- Implementation
  UINT m_nGrowBytes;
  DWORD m_nPosition;
  DWORD m_nBufferSize;
  DWORD m_nStreamSize;
  BYTE* m_lpBuffer;
  BOOL m_bAutoDelete;

protected:
  virtual BYTE* Alloc(DWORD nBytes);
  virtual BYTE* Realloc(BYTE* lpMem, DWORD nBytes);
  virtual BYTE* Memcpy(BYTE* lpMemTarget, const BYTE* lpMemSource, UINT nBytes);
  virtual void GrowStream(DWORD dwNewLen);
  virtual void Free(BYTE* lpMem);

private:
  CHtmlStream(const CHtmlStream&);
  void operator=(const CHtmlStream&);
};

// ---------------------------------------------------------------------
// CHttpServerContext: one request to an extension.

class CHttpServerContext {
public:
  CHttpServerContext(EXTENSION_CONTROL_BLOCK* pECB);
  virtual ~CHttpServerContext();

  // The server's variables (SERVER_NAME, QUERY_STRING and so on). The
  // size is in bytes.
  BOOL GetServerVariable(LPTSTR lpszVariableName, LPVOID lpvBuffer, LPDWORD lpdwSize);
  BOOL WriteClient(LPVOID lpvBuffer, LPDWORD lpdwBytes, DWORD dwReserved = 0);
  BOOL ReadClient(LPVOID lpvBuffer, LPDWORD lpdwSize);
  BOOL ServerSupportFunction(DWORD dwHSERRequest, LPVOID lpvBuffer, LPDWORD lpdwSize,
                             LPDWORD lpdwDataType);
  BOOL TransmitFile(HANDLE hFile, DWORD dwFlags = HSE_IO_DISCONNECT_AFTER_SEND,
                    LPVOID pstrHeader = NULL, DWORD dwHeaderLen = 0, LPVOID pstrTrailer = NULL,
                    DWORD dwTrailerLen = 0);
  HANDLE GetImpersonationToken() const;
  // The status line's code, sent with the reply (200 when not set).
  DWORD SetStatusCode(DWORD nStatusCode);

  CHttpServerContext& operator<<(LPCSTR psz);
  CHttpServerContext& operator<<(LPCWSTR psz);
  CHttpServerContext& operator<<(const CString& str) { return *this << (LPCTSTR)str; }
  CHttpServerContext& operator<<(short w);
  CHttpServerContext& operator<<(int n);
  CHttpServerContext& operator<<(long dw);
  CHttpServerContext& operator<<(DWORD dw);
#ifndef _WINPORT  // where DWORD is UINT, the DWORD one takes it
  CHttpServerContext& operator<<(UINT n) { return *this << (DWORD)n; }
#endif
  CHttpServerContext& operator<<(float f);
  CHttpServerContext& operator<<(double d);
  CHttpServerContext& operator<<(const CHtmlStream& stream);
  CHttpServerContext& operator<<(BYTE b);

  void Reset();

  EXTENSION_CONTROL_BLOCK* const m_pECB;
  CHtmlStream* m_pStream;
  DWORD m_dwStatusCode;
  // The reply's header lines, from AddHeader.
  CStringA m_strHeaders;
  // The request's data, read whole.
  LPBYTE m_pbRawData;
  DWORD m_dwRawSize;

private:
  CHttpServerContext(const CHttpServerContext&);
  void operator=(const CHttpServerContext&);
};

// ---------------------------------------------------------------------
// Parse maps: the commands an extension answers, each a member function
// that takes the context and then the request's arguments. The ITS_
// string says what the arguments are; the function's own parameter types
// say how each is read.

#define ITS_EMPTY "\x06"
#define ITS_I2 "\x01"
#define ITS_I4 "\x02"
#define ITS_R4 "\x03"
#define ITS_R8 "\x04"
#define ITS_PSTR "\x05"
#define ITS_RAW "\x07"
#define ITS_I8 "\x08"

// One argument of a command, as text. For ITS_RAW, pvRaw and cbRaw are the
// request's data and the next argument is its size.
struct AFX_PARSEARG {
  LPTSTR psz;
  LPVOID pvRaw;
};

typedef void (CHttpServer::*AFX_PISAPICMD)(CHttpServerContext*);
typedef int (*AFX_PARSEINVOKE)(CHttpServer* pThis, AFX_PISAPICMD pfn, CHttpServerContext* pCtxt,
                               AFX_PARSEARG* pArgs, UINT nArgs);

struct AFX_PARSEMAP_ENTRY {
  LPCTSTR pszFnName;      // NULL for a parameter list or the default command
  AFX_PISAPICMD pfn;
  AFX_PARSEINVOKE pfnInvoke;
  LPCSTR pszArgs;         // the ITS_ string, or for a parameter list the names
  LPCTSTR pszParamInfo;   // ON_PARSE_COMMAND_PARAMS: "name name2=default ..."
  int nKind;              // _AFX_PARSE_*
};

enum { _AFX_PARSE_END = 0, _AFX_PARSE_COMMAND, _AFX_PARSE_PARAMS, _AFX_PARSE_DEFAULT };

struct AFX_PARSEMAP {
  const AFX_PARSEMAP* (PASCAL* pfnGetBaseMap)();
  const AFX_PARSEMAP_ENTRY* lpEntries;
};

// Reading an argument's text as the parameter's type.
long AFXAPI _AfxParseLong(LPCTSTR psz);
LONGLONG AFXAPI _AfxParseLongLong(LPCTSTR psz);
double AFXAPI _AfxParseDouble(LPCTSTR psz);

template <class T>
inline typename std::enable_if<std::is_integral<T>::value && (sizeof(T) <= 4), T>::type
_AfxParseGet(AFX_PARSEARG& a, T*) {
  return (T)_AfxParseLong(a.psz);
}
template <class T>
inline typename std::enable_if<std::is_integral<T>::value && (sizeof(T) > 4), T>::type
_AfxParseGet(AFX_PARSEARG& a, T*) {
  return (T)_AfxParseLongLong(a.psz);
}
template <class T>
inline typename std::enable_if<std::is_floating_point<T>::value, T>::type _AfxParseGet(
    AFX_PARSEARG& a, T*) {
  return (T)_AfxParseDouble(a.psz);
}
inline LPTSTR _AfxParseGet(AFX_PARSEARG& a, TCHAR**) { return a.psz; }
inline LPCTSTR _AfxParseGet(AFX_PARSEARG& a, const TCHAR**) { return a.psz; }
inline LPVOID _AfxParseGet(AFX_PARSEARG& a, void**) { return a.pvRaw; }
inline LPCVOID _AfxParseGet(AFX_PARSEARG& a, const void**) { return a.pvRaw; }
inline LPBYTE _AfxParseGet(AFX_PARSEARG& a, BYTE**) { return (LPBYTE)a.pvRaw; }

template <class C, class M, class... A, size_t... I>
inline void _AfxParseCall(C* p, M pfn, CHttpServerContext* pCtxt, AFX_PARSEARG* args,
                          std::index_sequence<I...>) {
  (void)args;
  (p->*pfn)(pCtxt, _AfxParseGet(args[I], (typename std::decay<A>::type*)0)...);
}

template <class C, class R, class... A>
int _AfxParseInvoke(CHttpServer* pThis, AFX_PISAPICMD pfn, CHttpServerContext* pCtxt,
                    AFX_PARSEARG* args, UINT nArgs) {
  typedef R (C::*M)(CHttpServerContext*, A...);
  if (nArgs != sizeof...(A)) return -1;
  _AfxParseCall<C, M, A...>(static_cast<C*>(pThis), reinterpret_cast<M>(pfn), pCtxt, args,
                            std::index_sequence_for<A...>());
  return 0;
}
template <class C, class R, class... A>
inline AFX_PARSEINVOKE _AfxParseThunk(R (C::*)(CHttpServerContext*, A...)) {
  return &_AfxParseInvoke<C, R, A...>;
}

#define DECLARE_PARSE_MAP()                                       \
private:                                                          \
  static const AFX_PARSEMAP_ENTRY _parseEntries[];                \
                                                                  \
protected:                                                        \
  static const AFX_PARSEMAP parseMap;                             \
  static const AFX_PARSEMAP* PASCAL _GetBaseParseMap();           \
  virtual const AFX_PARSEMAP* GetParseMap() const;

#define BEGIN_PARSE_MAP(theClass, baseClass)                                              \
  const AFX_PARSEMAP* PASCAL theClass::_GetBaseParseMap() { return &baseClass::parseMap; } \
  const AFX_PARSEMAP* theClass::GetParseMap() const { return &theClass::parseMap; }         \
  const AFX_PARSEMAP theClass::parseMap = {&theClass::_GetBaseParseMap,                    \
                                           &theClass::_parseEntries[0]};                   \
  const AFX_PARSEMAP_ENTRY theClass::_parseEntries[] = {
#define ON_PARSE_COMMAND(FnName, mapClass, Args)                                        \
  {_T(#FnName), reinterpret_cast<AFX_PISAPICMD>(&mapClass::FnName),                     \
   _AfxParseThunk(&mapClass::FnName), Args, NULL, _AFX_PARSE_COMMAND},
#define ON_PARSE_COMMAND_PARAMS(Params) \
  {NULL, NULL, NULL, NULL, _T(Params), _AFX_PARSE_PARAMS},
#define DEFAULT_PARSE_COMMAND(FnName, mapClass) \
  {_T(#FnName), NULL, NULL, NULL, NULL, _AFX_PARSE_DEFAULT},
#define END_PARSE_MAP(theClass) \
  {NULL, NULL, NULL, NULL, NULL, _AFX_PARSE_END}};

// ---------------------------------------------------------------------
// CHttpServer: an Internet Server API extension. The DLL has one; the
// exported GetExtensionVersion, HttpExtensionProc and TerminateExtension
// call it.

class CHttpServer {
public:
  CHttpServer(TCHAR cDelimiter = '&');
  virtual ~CHttpServer();

  enum errors {
    callOK = 0,
    callParamRequired,
    callBadParamCount,
    callBadCommand,
    callNoStackSpace,
    callNoStream,
    callMissingQuote,
    callMissingParams,
    callBadParam
  };

  virtual BOOL GetExtensionVersion(HSE_VERSION_INFO* pVer);
  virtual DWORD HttpExtensionProc(EXTENSION_CONTROL_BLOCK* pECB);
  virtual BOOL TerminateExtension(DWORD dwFlags);

  virtual BOOL InitInstance(CHttpServerContext* pCtxt);
  virtual int CallFunction(CHttpServerContext* pCtxt, LPTSTR pszQuery, LPTSTR pszCommand);
  virtual BOOL OnParseError(CHttpServerContext* pCtxt, int nCause);
  virtual CHtmlStream* ConstructStream();
  virtual LPCTSTR GetTitle() const;
  virtual void OnWriteBody(CHttpServerContext* pCtxt, LPBYTE pbContent, DWORD dwSize);
  virtual void EndOfRequest(CHttpServerContext* pCtxt);

  // A header line ("Name: value\r\n") sent with the reply; call it
  // before StartContent.
  void AddHeader(CHttpServerContext* pCtxt, LPCTSTR pszString) const;
  void StartContent(CHttpServerContext* pCtxt) const;
  void EndContent(CHttpServerContext* pCtxt) const;
  void WriteTitle(CHttpServerContext* pCtxt) const;

  // ----- Implementation
  TCHAR m_cTokenDelimiter;
  CRITICAL_SECTION m_cs;

protected:
  static const AFX_PARSEMAP parseMap;
  static const AFX_PARSEMAP_ENTRY _parseEntries[];
  static const AFX_PARSEMAP* PASCAL _GetBaseParseMap() { return NULL; }
  virtual const AFX_PARSEMAP* GetParseMap() const;

  const AFX_PARSEMAP_ENTRY* LookUp(LPCTSTR szMethod, const AFX_PARSEMAP*& pMap,
                                   const AFX_PARSEMAP_ENTRY*& pParams,
                                   AFX_PISAPICMD pCmdDefault = NULL);
  int CallMemberFunc(CHttpServerContext* pCtxt, const AFX_PARSEMAP_ENTRY* pEntry,
                     const AFX_PARSEMAP_ENTRY* pParams, LPTSTR szParams);
  void SendReply(CHttpServerContext* pCtxt);
};

// ---------------------------------------------------------------------
// CHttpFilterContext and CHttpFilter: an Internet Server API filter,
// called for the notifications it asks for in GetFilterVersion.

class CHttpFilterContext {
public:
  CHttpFilterContext(PHTTP_FILTER_CONTEXT pfc);
  ~CHttpFilterContext() {}

  BOOL GetServerVariable(LPTSTR lpszVariableName, LPVOID lpvBuffer, LPDWORD lpdwSize);
  BOOL AddResponseHeaders(LPTSTR lpszHeaders, DWORD dwReserved = 0);
  BOOL WriteClient(LPVOID lpvBuffer, LPDWORD lpdwBytes, DWORD dwReserved = 0);
  LPVOID AllocMem(DWORD cbSize, DWORD dwReserved = 0);
  BOOL ServerSupportFunction(enum SF_REQ_TYPE sfReq, LPVOID lpvBuffer, LPDWORD lpdwSize,
                             LPDWORD lpdwDataType);

  PHTTP_FILTER_CONTEXT const m_pFC;

private:
  CHttpFilterContext(const CHttpFilterContext&);
  void operator=(const CHttpFilterContext&);
};

class CHttpFilter {
public:
  CHttpFilter();
  virtual ~CHttpFilter();

  virtual BOOL GetFilterVersion(PHTTP_FILTER_VERSION pVer);
  virtual DWORD HttpFilterProc(PHTTP_FILTER_CONTEXT pfc, DWORD dwNotificationType,
                               LPVOID pvNotification);
  virtual BOOL TerminateFilter(DWORD dwFlags);

  virtual DWORD OnReadRawData(CHttpFilterContext* pfc, PHTTP_FILTER_RAW_DATA pRawData);
  virtual DWORD OnPreprocHeaders(CHttpFilterContext* pfc, PHTTP_FILTER_PREPROC_HEADERS pHeaders);
  virtual DWORD OnAuthentication(CHttpFilterContext* pfc, PHTTP_FILTER_AUTHENT pAuthent);
  virtual DWORD OnUrlMap(CHttpFilterContext* pfc, PHTTP_FILTER_URL_MAP pUrlMap);
  virtual DWORD OnSendRawData(CHttpFilterContext* pfc, PHTTP_FILTER_RAW_DATA pRawData);
  virtual DWORD OnLog(CHttpFilterContext* pfc, PHTTP_FILTER_LOG pLog);
  virtual DWORD OnEndOfNetSession(CHttpFilterContext* pfc);
  virtual DWORD OnEndOfRequest(CHttpFilterContext* pfc);
};

#endif  // __AFXISAPI_H__
