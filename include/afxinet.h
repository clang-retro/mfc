// afxinet.h - the internet classes over WinINet: sessions, HTTP, FTP and
// gopher connections, files read and written through them, FTP and
// gopher directory listings, and URL parsing.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#ifndef __AFXINET_H__
#define __AFXINET_H__

#include "afx.h"
#include <wininet.h>

#if !defined(_AFX_NOFORCE_LIBS) && defined(_WIN32)
__asm__(".section .drectve,\"yn\"\n"
        ".ascii \" -defaultlib:wininet\"\n"
        ".text");
#endif

class CInternetSession;
class CInternetConnection;
class CFtpConnection;
class CHttpConnection;
class CGopherConnection;
class CInternetFile;
class CHttpFile;
class CGopherFile;
class CGopherLocator;
class CFtpFileFind;
class CGopherFileFind;
class CWnd;

// ---------------------------------------------------------------------
// URL parsing. The service types are WinINet's, and these for the
// schemes it has no service for.

#define AFX_INET_SERVICE_FTP INTERNET_SERVICE_FTP
#define AFX_INET_SERVICE_HTTP INTERNET_SERVICE_HTTP
#define AFX_INET_SERVICE_GOPHER INTERNET_SERVICE_GOPHER
#define AFX_INET_SERVICE_UNK 0x1000
#define AFX_INET_SERVICE_FILE (AFX_INET_SERVICE_UNK + 1)
#define AFX_INET_SERVICE_MAILTO (AFX_INET_SERVICE_UNK + 2)
#define AFX_INET_SERVICE_MID (AFX_INET_SERVICE_UNK + 3)
#define AFX_INET_SERVICE_CID (AFX_INET_SERVICE_UNK + 4)
#define AFX_INET_SERVICE_NEWS (AFX_INET_SERVICE_UNK + 5)
#define AFX_INET_SERVICE_NNTP (AFX_INET_SERVICE_UNK + 6)
#define AFX_INET_SERVICE_PROSPERO (AFX_INET_SERVICE_UNK + 7)
#define AFX_INET_SERVICE_TELNET (AFX_INET_SERVICE_UNK + 8)
#define AFX_INET_SERVICE_WAIS (AFX_INET_SERVICE_UNK + 9)
#define AFX_INET_SERVICE_AFS (AFX_INET_SERVICE_UNK + 10)
#define AFX_INET_SERVICE_HTTPS (AFX_INET_SERVICE_UNK + 11)

// Splits a URL into its service, server, object (path and query) and
// port. FALSE when it is not a URL.
BOOL AFXAPI AfxParseURL(LPCTSTR pstrURL, DWORD& dwServiceType, CString& strServer,
                        CString& strObject, INTERNET_PORT& nPort);
// The same, with the user name and password, and InternetCrackUrl's ICU_
// flags.
BOOL AFXAPI AfxParseURLEx(LPCTSTR pstrURL, DWORD& dwServiceType, CString& strServer,
                          CString& strObject, INTERNET_PORT& nPort, CString& strUsername,
                          CString& strPassword, DWORD dwFlags = 0);

// ---------------------------------------------------------------------
// CInternetException: a WinINet error.

class CInternetException : public CException {
  DECLARE_DYNAMIC(CInternetException)
public:
  explicit CInternetException(DWORD dwError);
  virtual ~CInternetException();

  // The text of the error, with the server's own words for an FTP or
  // gopher error (InternetGetLastResponseInfo).
  virtual BOOL GetErrorMessage(LPTSTR lpstrError, UINT nMaxError,
                               PUINT pnHelpContext = NULL) const;
  virtual BOOL GetErrorMessage(LPTSTR lpstrError, UINT nMaxError, PUINT pnHelpContext = NULL);

  DWORD_PTR m_dwContext;
  DWORD m_dwError;
  CString m_strResponse;  // the server's response, for ERROR_INTERNET_EXTENDED_ERROR
};

// Throws the error dwError, or GetLastError's when it is 0.
void AFXAPI AfxThrowInternetException(DWORD_PTR dwContext, DWORD dwError = 0);

// ---------------------------------------------------------------------
// CInternetSession: a WinINet session (InternetOpen), from which the
// connections and URLs are opened.

class CInternetSession : public CObject {
  DECLARE_DYNAMIC(CInternetSession)
public:
  CInternetSession(LPCTSTR pstrAgent = NULL, DWORD_PTR dwContext = 1,
                   DWORD dwAccessType = PRE_CONFIG_INTERNET_ACCESS,
                   LPCTSTR pstrProxyName = NULL, LPCTSTR pstrProxyBypass = NULL,
                   DWORD dwFlags = 0);
  virtual ~CInternetSession();

  BOOL QueryOption(DWORD dwOption, LPVOID lpBuffer, LPDWORD lpdwBufLen) const;
  BOOL QueryOption(DWORD dwOption, DWORD& dwValue) const;
  BOOL QueryOption(DWORD dwOption, CString& refString) const;
  BOOL SetOption(DWORD dwOption, LPVOID lpBuffer, DWORD dwBufferLength, DWORD dwFlags = 0);
  BOOL SetOption(DWORD dwOption, DWORD dwValue, DWORD dwFlags = 0);

  // A file for an http, https, ftp, gopher or file URL: a CHttpFile,
  // CInternetFile, CGopherFile or CStdioFile, which the caller deletes.
  CStdioFile* OpenURL(LPCTSTR pstrURL, DWORD_PTR dwContext = 1,
                      DWORD dwFlags = INTERNET_FLAG_TRANSFER_ASCII, LPCTSTR pstrHeaders = NULL,
                      DWORD dwHeadersLength = 0);
  CFtpConnection* GetFtpConnection(LPCTSTR pstrServer, LPCTSTR pstrUserName = NULL,
                                   LPCTSTR pstrPassword = NULL,
                                   INTERNET_PORT nPort = INTERNET_INVALID_PORT_NUMBER,
                                   BOOL bPassive = FALSE);
  CHttpConnection* GetHttpConnection(LPCTSTR pstrServer,
                                     INTERNET_PORT nPort = INTERNET_INVALID_PORT_NUMBER,
                                     LPCTSTR pstrUserName = NULL, LPCTSTR pstrPassword = NULL);
  CHttpConnection* GetHttpConnection(LPCTSTR pstrServer, DWORD dwFlags,
                                     INTERNET_PORT nPort = INTERNET_INVALID_PORT_NUMBER,
                                     LPCTSTR pstrUserName = NULL, LPCTSTR pstrPassword = NULL);
  CGopherConnection* GetGopherConnection(LPCTSTR pstrServer, LPCTSTR pstrUserName = NULL,
                                         LPCTSTR pstrPassword = NULL,
                                         INTERNET_PORT nPort = INTERNET_INVALID_PORT_NUMBER);

  // Turns OnStatusCallback on or off.
  BOOL EnableStatusCallback(BOOL bEnable = TRUE);
  DWORD ServiceTypeFromHandle(HINTERNET hQuery);

  static BOOL SetCookie(LPCTSTR pstrUrl, LPCTSTR pstrCookieName, LPCTSTR pstrCookieData);
  static BOOL GetCookie(LPCTSTR pstrUrl, LPCTSTR pstrCookieName, LPTSTR pstrCookieData,
                        DWORD dwBufLen);
  static BOOL GetCookie(LPCTSTR pstrUrl, LPCTSTR pstrCookieName, CString& strCookieData);
  static DWORD GetCookieLength(LPCTSTR pstrUrl, LPCTSTR pstrCookieName);

  DWORD_PTR GetContext() const { return m_dwContext; }
  operator HINTERNET() const { return m_hSession; }
  virtual void Close();

  virtual void OnStatusCallback(DWORD_PTR dwContext, DWORD dwInternetStatus,
                                LPVOID lpvStatusInformation, DWORD dwStatusInformationLength);

protected:
  DWORD_PTR m_dwContext;
  HINTERNET m_hSession;
  INTERNET_STATUS_CALLBACK m_pOldCallback;
  BOOL m_bCallbackEnabled;

public:
  // The session a WinINet handle belongs to, for the status callback.
  static CInternetSession* PASCAL FromInternetHandle(HINTERNET hInternet);
};

// ---------------------------------------------------------------------
// Connections to one server (InternetConnect).

class CInternetConnection : public CObject {
  DECLARE_DYNAMIC(CInternetConnection)
public:
  CInternetConnection(CInternetSession* pSession, LPCTSTR pstrServer,
                      INTERNET_PORT nPort = INTERNET_INVALID_PORT_NUMBER, DWORD_PTR dwContext = 1);
  virtual ~CInternetConnection();

  BOOL QueryOption(DWORD dwOption, LPVOID lpBuffer, LPDWORD lpdwBufLen) const;
  BOOL QueryOption(DWORD dwOption, DWORD& dwValue) const;
  BOOL QueryOption(DWORD dwOption, CString& refString) const;
  BOOL SetOption(DWORD dwOption, LPVOID lpBuffer, DWORD dwBufferLength, DWORD dwFlags = 0);
  BOOL SetOption(DWORD dwOption, DWORD dwValue, DWORD dwFlags = 0);

  DWORD_PTR GetContext() const { return m_dwContext; }
  CInternetSession* GetSession() const { return m_pSession; }
  CString GetServerName() const { return m_strServerName; }
  INTERNET_PORT GetPort() const { return m_nPort; }
  operator HINTERNET() const { return m_hConnection; }
  virtual void Close();

protected:
  // Makes the connection; throws a CInternetException when it fails.
  void Connect(DWORD dwService, LPCTSTR pstrUserName, LPCTSTR pstrPassword, DWORD dwFlags);

  HINTERNET m_hConnection;
  DWORD_PTR m_dwContext;
  CInternetSession* m_pSession;
  CString m_strServerName;
  INTERNET_PORT m_nPort;
};

class CFtpConnection : public CInternetConnection {
  DECLARE_DYNAMIC(CFtpConnection)
public:
  CFtpConnection(CInternetSession* pSession, LPCTSTR pstrServer, LPCTSTR pstrUserName = NULL,
                 LPCTSTR pstrPassword = NULL, DWORD_PTR dwContext = 0,
                 INTERNET_PORT nPort = INTERNET_INVALID_PORT_NUMBER, BOOL bPassive = FALSE);
  // Takes over a connection made with InternetConnect.
  CFtpConnection(CInternetSession* pSession, HINTERNET hConnected, LPCTSTR pstrServer,
                 DWORD_PTR dwContext);
  virtual ~CFtpConnection();

  BOOL SetCurrentDirectory(LPCTSTR pstrDirName);
  BOOL GetCurrentDirectory(CString& strDirName) const;
  BOOL GetCurrentDirectory(LPTSTR pstrDirName, LPDWORD lpdwLen) const;
  BOOL GetCurrentDirectoryAsURL(CString& strDirName) const;
  BOOL GetCurrentDirectoryAsURL(LPTSTR pstrName, LPDWORD lpdwLen) const;
  BOOL RemoveDirectory(LPCTSTR pstrDirName);
  BOOL CreateDirectory(LPCTSTR pstrDirName);
  BOOL Rename(LPCTSTR pstrExisting, LPCTSTR pstrNew);
  BOOL Remove(LPCTSTR pstrFileName);
  BOOL PutFile(LPCTSTR pstrLocalFile, LPCTSTR pstrRemoteFile,
               DWORD dwFlags = FTP_TRANSFER_TYPE_BINARY, DWORD_PTR dwContext = 1);
  BOOL GetFile(LPCTSTR pstrRemoteFile, LPCTSTR pstrLocalFile, BOOL bFailIfExists = TRUE,
               DWORD dwAttributes = FILE_ATTRIBUTE_NORMAL,
               DWORD dwFlags = FTP_TRANSFER_TYPE_BINARY, DWORD_PTR dwContext = 1);
  CInternetFile* OpenFile(LPCTSTR pstrFileName, DWORD dwAccess = GENERIC_READ,
                          DWORD dwFlags = FTP_TRANSFER_TYPE_BINARY, DWORD_PTR dwContext = 1);
};

class CHttpConnection : public CInternetConnection {
  DECLARE_DYNAMIC(CHttpConnection)
public:
  enum {
    _HTTP_VERB_MIN = 0,
    HTTP_VERB_POST = 0,
    HTTP_VERB_GET = 1,
    HTTP_VERB_HEAD = 2,
    HTTP_VERB_PUT = 3,
    HTTP_VERB_LINK = 4,
    HTTP_VERB_DELETE = 5,
    HTTP_VERB_UNLINK = 6,
    _HTTP_VERB_MAX = 6,
  };

  CHttpConnection(CInternetSession* pSession, LPCTSTR pstrServer,
                  INTERNET_PORT nPort = INTERNET_INVALID_PORT_NUMBER, LPCTSTR pstrUserName = NULL,
                  LPCTSTR pstrPassword = NULL, DWORD_PTR dwContext = 1);
  CHttpConnection(CInternetSession* pSession, LPCTSTR pstrServer, DWORD dwFlags,
                  INTERNET_PORT nPort = INTERNET_INVALID_PORT_NUMBER, LPCTSTR pstrUserName = NULL,
                  LPCTSTR pstrPassword = NULL, DWORD_PTR dwContext = 1);
  CHttpConnection(CInternetSession* pSession, HINTERNET hConnected, LPCTSTR pstrServer,
                  DWORD_PTR dwContext);
  virtual ~CHttpConnection();

  // A request, which CHttpFile::SendRequest sends.
  CHttpFile* OpenRequest(LPCTSTR pstrVerb, LPCTSTR pstrObjectName, LPCTSTR pstrReferer = NULL,
                         DWORD_PTR dwContext = 1, LPCTSTR* ppstrAcceptTypes = NULL,
                         LPCTSTR pstrVersion = NULL, DWORD dwFlags = INTERNET_FLAG_EXISTING_CONNECT);
  CHttpFile* OpenRequest(int nVerb, LPCTSTR pstrObjectName, LPCTSTR pstrReferer = NULL,
                         DWORD_PTR dwContext = 1, LPCTSTR* ppstrAcceptTypes = NULL,
                         LPCTSTR pstrVersion = NULL, DWORD dwFlags = INTERNET_FLAG_EXISTING_CONNECT);

  static const LPCTSTR szHtmlVerbs[];

protected:
  DWORD m_dwFlags;  // INTERNET_FLAG_SECURE for https
};

class CGopherConnection : public CInternetConnection {
  DECLARE_DYNAMIC(CGopherConnection)
public:
  CGopherConnection(CInternetSession* pSession, LPCTSTR pstrServer, LPCTSTR pstrUserName = NULL,
                    LPCTSTR pstrPassword = NULL, DWORD_PTR dwContext = 0,
                    INTERNET_PORT nPort = INTERNET_INVALID_PORT_NUMBER);
  CGopherConnection(CInternetSession* pSession, HINTERNET hConnected, LPCTSTR pstrServer,
                    DWORD_PTR dwContext);
  virtual ~CGopherConnection();

  CGopherFile* OpenFile(CGopherLocator& refLocator, DWORD dwFlags = 0, LPCTSTR pstrView = NULL,
                        DWORD_PTR dwContext = 1);
  CGopherLocator CreateLocator(LPCTSTR pstrDisplayString, LPCTSTR pstrSelectorString,
                               DWORD dwGopherType);
  static CGopherLocator CreateLocator(LPCTSTR pstrLocator);
  static CGopherLocator CreateLocator(LPCTSTR pstrServerName, LPCTSTR pstrDisplayString,
                                      LPCTSTR pstrSelectorString, DWORD dwGopherType,
                                      INTERNET_PORT nPort = INTERNET_INVALID_PORT_NUMBER);
  BOOL GetAttribute(CGopherLocator& refLocator, CString strRequestedAttributes,
                    CString& strResult);
};

// ---------------------------------------------------------------------
// Files read and written over a connection. Reads go through a buffer of
// SetReadBufferSize bytes (none by default), as do writes.

class CInternetFile : public CStdioFile {
  DECLARE_DYNAMIC(CInternetFile)
public:
  virtual ~CInternetFile();

  BOOL SetWriteBufferSize(UINT nWriteSize);
  BOOL SetReadBufferSize(UINT nReadSize);

  BOOL QueryOption(DWORD dwOption, LPVOID lpBuffer, LPDWORD lpdwBufLen) const;
  BOOL QueryOption(DWORD dwOption, DWORD& dwValue) const;
  BOOL QueryOption(DWORD dwOption, CString& refString) const;
  BOOL SetOption(DWORD dwOption, LPVOID lpBuffer, DWORD dwBufferLength, DWORD dwFlags = 0);
  BOOL SetOption(DWORD dwOption, DWORD dwValue, DWORD dwFlags = 0);

  virtual ULONGLONG Seek(LONGLONG lOffset, UINT nFrom);
  virtual UINT Read(void* lpBuf, UINT nCount);
  virtual void Write(const void* lpBuf, UINT nCount);
  virtual void Abort();
  virtual void Flush();
  virtual void Close();
  virtual ULONGLONG GetLength() const;
  virtual ULONGLONG GetPosition() const;
  virtual BOOL ReadString(CString& rString);
  virtual LPTSTR ReadString(LPTSTR pstr, UINT nMax);
  virtual void WriteString(LPCTSTR pstr);

  // The bytes that can be read without waiting (InternetQueryDataAvailable).
  DWORD QueryDataAvailable();

  virtual CString GetFileURL() const;
  DWORD_PTR GetContext() const { return m_dwContext; }
  operator HINTERNET() const { return m_hFile; }

  // Not supported over a connection.
  virtual void SetLength(ULONGLONG dwNewLen);
  virtual void LockRange(ULONGLONG dwPos, ULONGLONG dwCount);
  virtual void UnlockRange(ULONGLONG dwPos, ULONGLONG dwCount);
  virtual CFile* Duplicate() const;

protected:
  CInternetFile(HINTERNET hFile, LPCTSTR pstrFileName, CInternetConnection* pConnection,
                BOOL bReadMode);
  CInternetFile(HINTERNET hFile, HINTERNET hSession, LPCTSTR pstrFileName, LPCTSTR pstrServer,
                DWORD_PTR dwContext, BOOL bReadMode);
  void FlushWrites();
  UINT ReadRaw(void* lpBuf, UINT nCount);

  BYTE* m_pbReadBuffer;
  UINT m_nReadBufferSize;
  UINT m_nReadBufferPos;
  UINT m_nReadBufferBytes;
  BYTE* m_pbWriteBuffer;
  UINT m_nWriteBufferSize;
  UINT m_nWriteBufferPos;
  BOOL m_bReadMode;
  DWORD_PTR m_dwContext;
  HINTERNET m_hConnection;
  CString m_strServerName;
  ULONGLONG m_nPosition;

  friend class CInternetSession;
  friend class CFtpConnection;
  friend class CHttpConnection;
  friend class CGopherConnection;
};

class CHttpFile : public CInternetFile {
  DECLARE_DYNAMIC(CHttpFile)
public:
  CHttpFile(HINTERNET hFile, HINTERNET hSession, LPCTSTR pstrObject, LPCTSTR pstrServer,
            LPCTSTR pstrVerb, DWORD_PTR dwContext);
  CHttpFile(HINTERNET hFile, LPCTSTR pstrVerb, LPCTSTR pstrObject,
            CHttpConnection* pConnection);
  virtual ~CHttpFile();

  BOOL AddRequestHeaders(LPCTSTR pstrHeaders, DWORD dwFlags = HTTP_ADDREQ_FLAG_ADD_IF_NEW,
                         int dwHeadersLen = -1);
  BOOL AddRequestHeaders(CString& str, DWORD dwFlags = HTTP_ADDREQ_FLAG_ADD_IF_NEW);

  BOOL SendRequest(LPCTSTR pstrHeaders = NULL, DWORD dwHeadersLen = 0, LPVOID lpOptional = NULL,
                   DWORD dwOptionalLen = 0);
  BOOL SendRequest(CString& strHeaders, LPVOID lpOptional = NULL, DWORD dwOptionalLen = 0);
  // A request whose body the program then writes, ended with EndRequest.
  BOOL SendRequestEx(DWORD dwTotalLen, DWORD dwFlags = HSR_INITIATE, DWORD_PTR dwContext = 1);
  BOOL SendRequestEx(LPINTERNET_BUFFERS lpBuffIn, LPINTERNET_BUFFERS lpBuffOut,
                     DWORD dwFlags = HSR_INITIATE, DWORD_PTR dwContext = 1);
  BOOL EndRequest(DWORD dwFlags = 0, LPINTERNET_BUFFERS lpBuffIn = NULL, DWORD_PTR dwContext = 1);

  BOOL QueryInfo(DWORD dwInfoLevel, LPVOID lpvBuffer, LPDWORD lpdwBufferLength,
                 LPDWORD lpdwIndex = NULL) const;
  BOOL QueryInfo(DWORD dwInfoLevel, CString& str, LPDWORD dwIndex = NULL) const;
  BOOL QueryInfo(DWORD dwInfoLevel, SYSTEMTIME* pSysTime, LPDWORD dwIndex = NULL) const;
  BOOL QueryInfo(DWORD dwInfoLevel, DWORD& dwResult, LPDWORD dwIndex = NULL) const;
  BOOL QueryInfoStatusCode(DWORD& dwStatusCode) const;

  DWORD ErrorDlg(CWnd* pParent = NULL, DWORD dwError = ERROR_INTERNET_INCORRECT_PASSWORD,
                 DWORD dwFlags = FLAGS_ERROR_UI_FLAGS_GENERATE_DATA |
                                 FLAGS_ERROR_UI_FLAGS_CHANGE_OPTIONS,
                 LPVOID* lppvData = NULL);

  CString GetVerb() const { return m_strVerb; }
  CString GetObject() const { return m_strObject; }
  virtual CString GetFileURL() const;
  virtual void Close();

protected:
  CString m_strObject;
  CString m_strVerb;
};

class CGopherFile : public CInternetFile {
  DECLARE_DYNAMIC(CGopherFile)
public:
  CGopherFile(HINTERNET hFile, CGopherLocator& refLocator, CGopherConnection* pConnection);
  CGopherFile(HINTERNET hFile, HINTERNET hSession, LPCTSTR pstrLocator, DWORD dwLocLen,
              DWORD_PTR dwContext);
  virtual ~CGopherFile();
  virtual void Close();
  virtual void Write(const void* lpBuf, UINT nCount);
  void WriteString(LPCTSTR pstr);
};

// ---------------------------------------------------------------------
// Gopher locators and directory listings.

class CGopherLocator : public CObject {
public:
  CGopherLocator(const CGopherLocator& ref);
  CGopherLocator(LPCTSTR pstrLocator, DWORD dwLocLen);
  ~CGopherLocator();
  CGopherLocator& operator=(const CGopherLocator& ref);
  operator LPCTSTR() const { return m_strLocator; }
  BOOL GetLocatorType(DWORD& dwRef) const;

private:
  CString m_strLocator;
  DWORD m_dwBufferLength;
  friend class CGopherConnection;
  friend class CGopherFile;
};

class CFtpFileFind : public CFileFind {
  DECLARE_DYNAMIC(CFtpFileFind)
public:
  CFtpFileFind(CFtpConnection* pConnection, DWORD_PTR dwContext = 1);
  virtual ~CFtpFileFind();

  virtual BOOL FindFile(LPCTSTR pstrName = NULL, DWORD dwFlags = INTERNET_FLAG_RELOAD);
  virtual BOOL FindNextFile();
  CString GetFileURL() const;

protected:
  virtual void CloseContext();
  CFtpConnection* m_pConnection;
  DWORD_PTR m_dwContext;
};

class CGopherFileFind : public CFileFind {
  DECLARE_DYNAMIC(CGopherFileFind)
public:
  CGopherFileFind(CGopherConnection* pConnection, DWORD_PTR dwContext = 1);
  virtual ~CGopherFileFind();

  virtual BOOL FindFile(CGopherLocator& refLocator, LPCTSTR pstrString,
                        DWORD dwFlags = INTERNET_FLAG_RELOAD);
  virtual BOOL FindFile(LPCTSTR pstrString, DWORD dwFlags = INTERNET_FLAG_RELOAD);
  virtual BOOL FindNextFile();
  CGopherLocator GetLocator() const;
  CString GetScreenName() const;

  virtual BOOL GetLastWriteTime(FILETIME* pTimeStamp) const;
  virtual BOOL GetLastAccessTime(FILETIME* pTimeStamp) const;
  virtual BOOL GetCreationTime(FILETIME* pTimeStamp) const;
  virtual BOOL GetLastWriteTime(CTime& refTime) const;
  virtual BOOL GetLastAccessTime(CTime& refTime) const;
  virtual BOOL GetCreationTime(CTime& refTime) const;
  ULONGLONG GetLength() const;
  virtual CString GetFileName() const;
  virtual CString GetFilePath() const;
  virtual CString GetFileTitle() const;
  virtual CString GetFileURL() const;
  virtual BOOL MatchesMask(DWORD dwMask) const;
  virtual BOOL IsDots() const;

protected:
  virtual void CloseContext();
  CGopherConnection* m_pConnection;
  DWORD_PTR m_dwContext;
  GOPHER_FIND_DATA* m_pGopherFound;
  GOPHER_FIND_DATA* m_pGopherNext;
};

#endif  // __AFXINET_H__
