// httpext.h - ISAPI extensions: the control block a web server passes to
// an extension DLL, and the requests it can make of the server.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#ifndef _WINPORT_HTTPEXT_H
#define _WINPORT_HTTPEXT_H

#include <windows.h>

#define HSE_VERSION_MAJOR 6
#define HSE_VERSION_MINOR 0
#define HSE_VERSION MAKELONG(HSE_VERSION_MINOR, HSE_VERSION_MAJOR)
#define HSE_LOG_BUFFER_LEN 80
#define HSE_MAX_EXT_DLL_NAME_LEN 256

#define HSE_STATUS_SUCCESS 1
#define HSE_STATUS_SUCCESS_AND_KEEP_CONN 2
#define HSE_STATUS_PENDING 3
#define HSE_STATUS_ERROR 4

#define HSE_REQ_BASE 0
#define HSE_REQ_SEND_URL_REDIRECT_RESP (HSE_REQ_BASE + 1)
#define HSE_REQ_SEND_URL (HSE_REQ_BASE + 2)
#define HSE_REQ_SEND_RESPONSE_HEADER (HSE_REQ_BASE + 3)
#define HSE_REQ_DONE_WITH_SESSION (HSE_REQ_BASE + 4)
#define HSE_REQ_END_RESERVED 1000
#define HSE_REQ_MAP_URL_TO_PATH (HSE_REQ_END_RESERVED + 1)
#define HSE_REQ_GET_SSPI_INFO (HSE_REQ_END_RESERVED + 2)
#define HSE_APPEND_LOG_PARAMETER (HSE_REQ_END_RESERVED + 3)
#define HSE_REQ_IO_COMPLETION (HSE_REQ_END_RESERVED + 5)
#define HSE_REQ_TRANSMIT_FILE (HSE_REQ_END_RESERVED + 6)
#define HSE_REQ_REFRESH_ISAPI_ACL (HSE_REQ_END_RESERVED + 7)
#define HSE_REQ_IS_KEEP_CONN (HSE_REQ_END_RESERVED + 8)
#define HSE_REQ_ASYNC_READ_CLIENT (HSE_REQ_END_RESERVED + 10)
#define HSE_REQ_GET_IMPERSONATION_TOKEN (HSE_REQ_END_RESERVED + 11)
#define HSE_REQ_MAP_URL_TO_PATH_EX (HSE_REQ_END_RESERVED + 12)
#define HSE_REQ_ABORTIVE_CLOSE (HSE_REQ_END_RESERVED + 14)
#define HSE_REQ_GET_CERT_INFO_EX (HSE_REQ_END_RESERVED + 15)
#define HSE_REQ_SEND_RESPONSE_HEADER_EX (HSE_REQ_END_RESERVED + 16)

#define HSE_IO_SYNC 0x00000001
#define HSE_IO_ASYNC 0x00000002
#define HSE_IO_DISCONNECT_AFTER_SEND 0x00000004
#define HSE_IO_SEND_HEADERS 0x00000008
#define HSE_IO_NODELAY 0x00001000

typedef LPVOID HCONN;

typedef struct _HSE_VERSION_INFO {
  DWORD dwExtensionVersion;
  CHAR lpszExtensionDesc[HSE_MAX_EXT_DLL_NAME_LEN];
} HSE_VERSION_INFO, *LPHSE_VERSION_INFO;

typedef struct _EXTENSION_CONTROL_BLOCK {
  DWORD cbSize;
  DWORD dwVersion;
  HCONN ConnID;
  DWORD dwHttpStatusCode;
  CHAR lpszLogData[HSE_LOG_BUFFER_LEN];
  LPSTR lpszMethod;
  LPSTR lpszQueryString;
  LPSTR lpszPathInfo;
  LPSTR lpszPathTranslated;
  DWORD cbTotalBytes;
  DWORD cbAvailable;
  LPBYTE lpbData;
  LPSTR lpszContentType;
  BOOL(WINAPI* GetServerVariable)(HCONN hConn, LPSTR lpszVariableName, LPVOID lpvBuffer, LPDWORD lpdwSize);
  BOOL(WINAPI* WriteClient)(HCONN ConnID, LPVOID Buffer, LPDWORD lpdwBytes, DWORD dwReserved);
  BOOL(WINAPI* ReadClient)(HCONN ConnID, LPVOID lpvBuffer, LPDWORD lpdwSize);
  BOOL(WINAPI* ServerSupportFunction)(HCONN hConn, DWORD dwHSERequest, LPVOID lpvBuffer, LPDWORD lpdwSize,
                                      LPDWORD lpdwDataType);
} EXTENSION_CONTROL_BLOCK, *LPEXTENSION_CONTROL_BLOCK;

typedef VOID(WINAPI* PFN_HSE_IO_COMPLETION)(EXTENSION_CONTROL_BLOCK* pECB, PVOID pContext, DWORD cbIO,
                                            DWORD dwError);

typedef struct _HSE_TF_INFO {
  PFN_HSE_IO_COMPLETION pfnHseIO;
  PVOID pContext;
  HANDLE hFile;
  LPCSTR pszStatusCode;
  DWORD BytesToWrite;
  DWORD Offset;
  PVOID pHead;
  DWORD HeadLength;
  PVOID pTail;
  DWORD TailLength;
  DWORD dwFlags;
} HSE_TF_INFO, *LPHSE_TF_INFO;

typedef struct _HSE_SEND_HEADER_EX_INFO {
  LPCSTR pszStatus;
  LPCSTR pszHeader;
  DWORD cchStatus;
  DWORD cchHeader;
  BOOL fKeepConn;
} HSE_SEND_HEADER_EX_INFO, *LPHSE_SEND_HEADER_EX_INFO;

typedef BOOL(WINAPI* PFN_GETEXTENSIONVERSION)(HSE_VERSION_INFO* pVer);
typedef DWORD(WINAPI* PFN_HTTPEXTENSIONPROC)(EXTENSION_CONTROL_BLOCK* pECB);
typedef BOOL(WINAPI* PFN_TERMINATEEXTENSION)(DWORD dwFlags);

#define HSE_TERM_ADVISORY_UNLOAD 0x00000001
#define HSE_TERM_MUST_UNLOAD 0x00000002

#endif
