// wininet.h - the Internet functions: sessions, connections, URLs, HTTP,
// FTP and Gopher, their options and status callbacks.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#ifndef _WINPORT_WININET_H
#define _WINPORT_WININET_H

#include <windows.h>

extern "C" {

typedef LPVOID HINTERNET;
typedef HINTERNET* LPHINTERNET;
typedef WORD INTERNET_PORT;
typedef INTERNET_PORT* LPINTERNET_PORT;

#define INTERNET_INVALID_PORT_NUMBER 0
#define INTERNET_DEFAULT_FTP_PORT 21
#define INTERNET_DEFAULT_GOPHER_PORT 70
#define INTERNET_DEFAULT_HTTP_PORT 80
#define INTERNET_DEFAULT_HTTPS_PORT 443
#define INTERNET_MAX_HOST_NAME_LENGTH 256
#define INTERNET_MAX_USER_NAME_LENGTH 128
#define INTERNET_MAX_PASSWORD_LENGTH 128
#define INTERNET_MAX_PORT_NUMBER_LENGTH 5
#define INTERNET_MAX_SCHEME_LENGTH 32
#define INTERNET_MAX_PATH_LENGTH 2048
#define INTERNET_MAX_URL_LENGTH (INTERNET_MAX_SCHEME_LENGTH + sizeof("://") + INTERNET_MAX_PATH_LENGTH)

#define INTERNET_OPEN_TYPE_PRECONFIG 0
#define INTERNET_OPEN_TYPE_DIRECT 1
#define INTERNET_OPEN_TYPE_PROXY 3
#define INTERNET_OPEN_TYPE_PRECONFIG_WITH_NO_AUTOPROXY 4
#define PRE_CONFIG_INTERNET_ACCESS INTERNET_OPEN_TYPE_PRECONFIG
#define LOCAL_INTERNET_ACCESS INTERNET_OPEN_TYPE_DIRECT
#define CERN_PROXY_INTERNET_ACCESS INTERNET_OPEN_TYPE_PROXY

#define INTERNET_SERVICE_FTP 1
#define INTERNET_SERVICE_GOPHER 2
#define INTERNET_SERVICE_HTTP 3

#define INTERNET_FLAG_RELOAD 0x80000000
#define INTERNET_FLAG_RAW_DATA 0x40000000
#define INTERNET_FLAG_EXISTING_CONNECT 0x20000000
#define INTERNET_FLAG_ASYNC 0x10000000
#define INTERNET_FLAG_PASSIVE 0x08000000
#define INTERNET_FLAG_NO_CACHE_WRITE 0x04000000
#define INTERNET_FLAG_DONT_CACHE INTERNET_FLAG_NO_CACHE_WRITE
#define INTERNET_FLAG_MAKE_PERSISTENT 0x02000000
#define INTERNET_FLAG_FROM_CACHE 0x01000000
#define INTERNET_FLAG_OFFLINE INTERNET_FLAG_FROM_CACHE
#define INTERNET_FLAG_SECURE 0x00800000
#define INTERNET_FLAG_KEEP_CONNECTION 0x00400000
#define INTERNET_FLAG_NO_AUTO_REDIRECT 0x00200000
#define INTERNET_FLAG_READ_PREFETCH 0x00100000
#define INTERNET_FLAG_NO_COOKIES 0x00080000
#define INTERNET_FLAG_NO_AUTH 0x00040000
#define INTERNET_FLAG_CACHE_IF_NET_FAIL 0x00010000
#define INTERNET_FLAG_IGNORE_REDIRECT_TO_HTTP 0x00008000
#define INTERNET_FLAG_IGNORE_REDIRECT_TO_HTTPS 0x00004000
#define INTERNET_FLAG_IGNORE_CERT_DATE_INVALID 0x00002000
#define INTERNET_FLAG_IGNORE_CERT_CN_INVALID 0x00001000
#define INTERNET_FLAG_RESYNCHRONIZE 0x00000800
#define INTERNET_FLAG_HYPERLINK 0x00000400
#define INTERNET_FLAG_NO_UI 0x00000200
#define INTERNET_FLAG_PRAGMA_NOCACHE 0x00000100
#define INTERNET_FLAG_CACHE_ASYNC 0x00000080
#define INTERNET_FLAG_FORMS_SUBMIT 0x00000040
#define INTERNET_FLAG_NEED_FILE 0x00000010
#define INTERNET_FLAG_TRANSFER_ASCII FTP_TRANSFER_TYPE_ASCII
#define INTERNET_FLAG_TRANSFER_BINARY FTP_TRANSFER_TYPE_BINARY

#define FTP_TRANSFER_TYPE_UNKNOWN 0x00000000
#define FTP_TRANSFER_TYPE_ASCII 0x00000001
#define FTP_TRANSFER_TYPE_BINARY 0x00000002
#define FTP_TRANSFER_TYPE_MASK (FTP_TRANSFER_TYPE_ASCII | FTP_TRANSFER_TYPE_BINARY)

#define INTERNET_OPTION_CALLBACK 1
#define INTERNET_OPTION_CONNECT_TIMEOUT 2
#define INTERNET_OPTION_CONNECT_RETRIES 3
#define INTERNET_OPTION_CONNECT_BACKOFF 4
#define INTERNET_OPTION_SEND_TIMEOUT 5
#define INTERNET_OPTION_CONTROL_SEND_TIMEOUT INTERNET_OPTION_SEND_TIMEOUT
#define INTERNET_OPTION_RECEIVE_TIMEOUT 6
#define INTERNET_OPTION_CONTROL_RECEIVE_TIMEOUT INTERNET_OPTION_RECEIVE_TIMEOUT
#define INTERNET_OPTION_DATA_SEND_TIMEOUT 7
#define INTERNET_OPTION_DATA_RECEIVE_TIMEOUT 8
#define INTERNET_OPTION_HANDLE_TYPE 9
#define INTERNET_OPTION_READ_BUFFER_SIZE 12
#define INTERNET_OPTION_WRITE_BUFFER_SIZE 13
#define INTERNET_OPTION_ASYNC_PRIORITY 15
#define INTERNET_OPTION_PARENT_HANDLE 21
#define INTERNET_OPTION_KEEP_CONNECTION 22
#define INTERNET_OPTION_REQUEST_FLAGS 23
#define INTERNET_OPTION_EXTENDED_ERROR 24
#define INTERNET_OPTION_URL 34
#define INTERNET_OPTION_USER_AGENT 41
#define INTERNET_OPTION_PROXY 38
#define INTERNET_OPTION_USERNAME 28
#define INTERNET_OPTION_PASSWORD 29
#define INTERNET_OPTION_PROXY_USERNAME 43
#define INTERNET_OPTION_PROXY_PASSWORD 44
#define INTERNET_OPTION_CONTEXT_VALUE 45

#define INTERNET_HANDLE_TYPE_INTERNET 1
#define INTERNET_HANDLE_TYPE_CONNECT_FTP 2
#define INTERNET_HANDLE_TYPE_CONNECT_GOPHER 3
#define INTERNET_HANDLE_TYPE_CONNECT_HTTP 4
#define INTERNET_HANDLE_TYPE_FTP_FIND 5
#define INTERNET_HANDLE_TYPE_FTP_FIND_HTML 6
#define INTERNET_HANDLE_TYPE_FTP_FILE 7
#define INTERNET_HANDLE_TYPE_FTP_FILE_HTML 8
#define INTERNET_HANDLE_TYPE_GOPHER_FIND 9
#define INTERNET_HANDLE_TYPE_GOPHER_FIND_HTML 10
#define INTERNET_HANDLE_TYPE_GOPHER_FILE 11
#define INTERNET_HANDLE_TYPE_GOPHER_FILE_HTML 12
#define INTERNET_HANDLE_TYPE_HTTP_REQUEST 13

typedef enum {
  INTERNET_SCHEME_PARTIAL = -2,
  INTERNET_SCHEME_UNKNOWN = -1,
  INTERNET_SCHEME_DEFAULT = 0,
  INTERNET_SCHEME_FTP,
  INTERNET_SCHEME_GOPHER,
  INTERNET_SCHEME_HTTP,
  INTERNET_SCHEME_HTTPS,
  INTERNET_SCHEME_FILE,
  INTERNET_SCHEME_NEWS,
  INTERNET_SCHEME_MAILTO,
  INTERNET_SCHEME_SOCKS,
  INTERNET_SCHEME_FIRST = INTERNET_SCHEME_FTP,
  INTERNET_SCHEME_LAST = INTERNET_SCHEME_SOCKS
} INTERNET_SCHEME, *LPINTERNET_SCHEME;

#define ICU_ESCAPE 0x80000000
#define ICU_USERNAME 0x40000000
#define ICU_NO_ENCODE 0x20000000
#define ICU_DECODE 0x10000000
#define ICU_NO_META 0x08000000
#define ICU_ENCODE_SPACES_ONLY 0x04000000
#define ICU_BROWSER_MODE 0x02000000

#define INTERNET_STATUS_RESOLVING_NAME 10
#define INTERNET_STATUS_NAME_RESOLVED 11
#define INTERNET_STATUS_CONNECTING_TO_SERVER 20
#define INTERNET_STATUS_CONNECTED_TO_SERVER 21
#define INTERNET_STATUS_SENDING_REQUEST 30
#define INTERNET_STATUS_REQUEST_SENT 31
#define INTERNET_STATUS_RECEIVING_RESPONSE 40
#define INTERNET_STATUS_RESPONSE_RECEIVED 41
#define INTERNET_STATUS_CLOSING_CONNECTION 50
#define INTERNET_STATUS_CONNECTION_CLOSED 51
#define INTERNET_STATUS_HANDLE_CREATED 60
#define INTERNET_STATUS_HANDLE_CLOSING 70
#define INTERNET_STATUS_REQUEST_COMPLETE 100
#define INTERNET_STATUS_REDIRECT 110
typedef void(CALLBACK* INTERNET_STATUS_CALLBACK)(HINTERNET, DWORD_PTR, DWORD, LPVOID, DWORD);
typedef INTERNET_STATUS_CALLBACK* LPINTERNET_STATUS_CALLBACK;
#define INTERNET_INVALID_STATUS_CALLBACK ((INTERNET_STATUS_CALLBACK)(-1L))

#define HTTP_QUERY_MIME_VERSION 0
#define HTTP_QUERY_CONTENT_TYPE 1
#define HTTP_QUERY_CONTENT_TRANSFER_ENCODING 2
#define HTTP_QUERY_CONTENT_ID 3
#define HTTP_QUERY_CONTENT_DESCRIPTION 4
#define HTTP_QUERY_CONTENT_LENGTH 5
#define HTTP_QUERY_CONTENT_LANGUAGE 6
#define HTTP_QUERY_ALLOW 7
#define HTTP_QUERY_PUBLIC 8
#define HTTP_QUERY_DATE 9
#define HTTP_QUERY_EXPIRES 10
#define HTTP_QUERY_LAST_MODIFIED 11
#define HTTP_QUERY_MESSAGE_ID 12
#define HTTP_QUERY_URI 13
#define HTTP_QUERY_DERIVED_FROM 14
#define HTTP_QUERY_COST 15
#define HTTP_QUERY_LINK 16
#define HTTP_QUERY_PRAGMA 17
#define HTTP_QUERY_VERSION 18
#define HTTP_QUERY_STATUS_CODE 19
#define HTTP_QUERY_STATUS_TEXT 20
#define HTTP_QUERY_RAW_HEADERS 21
#define HTTP_QUERY_RAW_HEADERS_CRLF 22
#define HTTP_QUERY_CONNECTION 23
#define HTTP_QUERY_ACCEPT 24
#define HTTP_QUERY_LOCATION 33
#define HTTP_QUERY_SERVER 37
#define HTTP_QUERY_USER_AGENT 39
#define HTTP_QUERY_CUSTOM 65535
#define HTTP_QUERY_FLAG_REQUEST_HEADERS 0x80000000
#define HTTP_QUERY_FLAG_SYSTEMTIME 0x40000000
#define HTTP_QUERY_FLAG_NUMBER 0x20000000
#define HTTP_QUERY_FLAG_COALESCE 0x10000000

#define HTTP_ADDREQ_INDEX_MASK 0x0000FFFF
#define HTTP_ADDREQ_FLAGS_MASK 0xFFFF0000
#define HTTP_ADDREQ_FLAG_ADD_IF_NEW 0x10000000
#define HTTP_ADDREQ_FLAG_ADD 0x20000000
#define HTTP_ADDREQ_FLAG_COALESCE_WITH_COMMA 0x40000000
#define HTTP_ADDREQ_FLAG_COALESCE_WITH_SEMICOLON 0x01000000
#define HTTP_ADDREQ_FLAG_COALESCE HTTP_ADDREQ_FLAG_COALESCE_WITH_COMMA
#define HTTP_ADDREQ_FLAG_REPLACE 0x80000000

#define HSR_ASYNC 0x00000001
#define HSR_SYNC 0x00000004
#define HSR_USE_CONTEXT 0x00000008
#define HSR_INITIATE 0x00000008
#define HSR_DOWNLOAD 0x00000010
#define HSR_CHUNKED 0x00000020

#define IRF_ASYNC 0x00000001
#define IRF_SYNC 0x00000004
#define IRF_USE_CONTEXT 0x00000008
#define IRF_NO_WAIT 0x00000008

#define FLAGS_ERROR_UI_FILTER_FOR_ERRORS 0x01
#define FLAGS_ERROR_UI_FLAGS_CHANGE_OPTIONS 0x02
#define FLAGS_ERROR_UI_FLAGS_GENERATE_DATA 0x04
#define FLAGS_ERROR_UI_FLAGS_NO_UI 0x08

#define INTERNET_ERROR_BASE 12000
#define ERROR_INTERNET_OUT_OF_HANDLES (INTERNET_ERROR_BASE + 1)
#define ERROR_INTERNET_TIMEOUT (INTERNET_ERROR_BASE + 2)
#define ERROR_INTERNET_EXTENDED_ERROR (INTERNET_ERROR_BASE + 3)
#define ERROR_INTERNET_INTERNAL_ERROR (INTERNET_ERROR_BASE + 4)
#define ERROR_INTERNET_INVALID_URL (INTERNET_ERROR_BASE + 5)
#define ERROR_INTERNET_UNRECOGNIZED_SCHEME (INTERNET_ERROR_BASE + 6)
#define ERROR_INTERNET_NAME_NOT_RESOLVED (INTERNET_ERROR_BASE + 7)
#define ERROR_INTERNET_PROTOCOL_NOT_FOUND (INTERNET_ERROR_BASE + 8)
#define ERROR_INTERNET_INVALID_OPTION (INTERNET_ERROR_BASE + 9)
#define ERROR_INTERNET_BAD_OPTION_LENGTH (INTERNET_ERROR_BASE + 10)
#define ERROR_INTERNET_OPTION_NOT_SETTABLE (INTERNET_ERROR_BASE + 11)
#define ERROR_INTERNET_SHUTDOWN (INTERNET_ERROR_BASE + 12)
#define ERROR_INTERNET_INCORRECT_USER_NAME (INTERNET_ERROR_BASE + 13)
#define ERROR_INTERNET_INCORRECT_PASSWORD (INTERNET_ERROR_BASE + 14)
#define ERROR_INTERNET_LOGIN_FAILURE (INTERNET_ERROR_BASE + 15)
#define ERROR_INTERNET_INVALID_OPERATION (INTERNET_ERROR_BASE + 16)
#define ERROR_INTERNET_OPERATION_CANCELLED (INTERNET_ERROR_BASE + 17)
#define ERROR_INTERNET_INCORRECT_HANDLE_TYPE (INTERNET_ERROR_BASE + 18)
#define ERROR_INTERNET_INCORRECT_HANDLE_STATE (INTERNET_ERROR_BASE + 19)
#define ERROR_INTERNET_NOT_PROXY_REQUEST (INTERNET_ERROR_BASE + 20)
#define ERROR_INTERNET_ITEM_NOT_FOUND (INTERNET_ERROR_BASE + 28)
#define ERROR_INTERNET_CANNOT_CONNECT (INTERNET_ERROR_BASE + 29)
#define ERROR_INTERNET_CONNECTION_ABORTED (INTERNET_ERROR_BASE + 30)
#define ERROR_INTERNET_CONNECTION_RESET (INTERNET_ERROR_BASE + 31)
#define ERROR_FTP_TRANSFER_IN_PROGRESS (INTERNET_ERROR_BASE + 110)
#define ERROR_FTP_DROPPED (INTERNET_ERROR_BASE + 111)
#define ERROR_GOPHER_PROTOCOL_ERROR (INTERNET_ERROR_BASE + 130)
#define ERROR_GOPHER_NOT_FILE (INTERNET_ERROR_BASE + 131)
#define ERROR_GOPHER_DATA_ERROR (INTERNET_ERROR_BASE + 132)
#define ERROR_GOPHER_END_OF_DATA (INTERNET_ERROR_BASE + 133)
#define ERROR_GOPHER_INVALID_LOCATOR (INTERNET_ERROR_BASE + 134)
#define ERROR_GOPHER_INCORRECT_LOCATOR_TYPE (INTERNET_ERROR_BASE + 135)
#define ERROR_GOPHER_NOT_GOPHER_PLUS (INTERNET_ERROR_BASE + 136)
#define ERROR_GOPHER_ATTRIBUTE_NOT_FOUND (INTERNET_ERROR_BASE + 137)
#define ERROR_GOPHER_UNKNOWN_LOCATOR (INTERNET_ERROR_BASE + 138)
#define ERROR_HTTP_HEADER_NOT_FOUND (INTERNET_ERROR_BASE + 150)
#define ERROR_HTTP_DOWNLEVEL_SERVER (INTERNET_ERROR_BASE + 151)
#define ERROR_HTTP_INVALID_SERVER_RESPONSE (INTERNET_ERROR_BASE + 152)
#define ERROR_HTTP_INVALID_HEADER (INTERNET_ERROR_BASE + 153)
#define ERROR_HTTP_INVALID_QUERY_REQUEST (INTERNET_ERROR_BASE + 154)
#define ERROR_HTTP_HEADER_ALREADY_EXISTS (INTERNET_ERROR_BASE + 155)
#define ERROR_HTTP_REDIRECT_FAILED (INTERNET_ERROR_BASE + 156)

// Gopher
#define MAX_GOPHER_DISPLAY_TEXT 128
#define MAX_GOPHER_SELECTOR_TEXT 256
#define MAX_GOPHER_HOST_NAME INTERNET_MAX_HOST_NAME_LENGTH
#define MAX_GOPHER_LOCATOR_LENGTH \
  (1 + MAX_GOPHER_DISPLAY_TEXT + 1 + MAX_GOPHER_SELECTOR_TEXT + 1 + MAX_GOPHER_HOST_NAME + 1 + \
   INTERNET_MAX_PORT_NUMBER_LENGTH + 1 + 1 + 2)
#define GOPHER_TYPE_TEXT_FILE 0x00000001
#define GOPHER_TYPE_DIRECTORY 0x00000002
#define GOPHER_TYPE_CSO 0x00000004
#define GOPHER_TYPE_ERROR 0x00000008
#define GOPHER_TYPE_MAC_BINHEX 0x00000010
#define GOPHER_TYPE_DOS_ARCHIVE 0x00000020
#define GOPHER_TYPE_UNIX_UUENCODED 0x00000040
#define GOPHER_TYPE_INDEX_SERVER 0x00000080
#define GOPHER_TYPE_TELNET 0x00000100
#define GOPHER_TYPE_BINARY 0x00000200
#define GOPHER_TYPE_REDUNDANT 0x00000400
#define GOPHER_TYPE_TN3270 0x00000800
#define GOPHER_TYPE_GIF 0x00001000
#define GOPHER_TYPE_IMAGE 0x00002000
#define GOPHER_TYPE_BITMAP 0x00004000
#define GOPHER_TYPE_MOVIE 0x00008000
#define GOPHER_TYPE_SOUND 0x00010000
#define GOPHER_TYPE_HTML 0x00020000
#define GOPHER_TYPE_PDF 0x00040000
#define GOPHER_TYPE_CALENDAR 0x00080000
#define GOPHER_TYPE_INLINE 0x00100000
#define GOPHER_TYPE_UNKNOWN 0x20000000
#define GOPHER_TYPE_ASK 0x40000000
#define GOPHER_TYPE_GOPHER_PLUS 0x80000000

}  // extern "C"

extern "C" {

typedef struct {
  DWORD dwStructSize;
  LPSTR lpszScheme;
  DWORD dwSchemeLength;
  INTERNET_SCHEME nScheme;
  LPSTR lpszHostName;
  DWORD dwHostNameLength;
  INTERNET_PORT nPort;
  LPSTR lpszUserName;
  DWORD dwUserNameLength;
  LPSTR lpszPassword;
  DWORD dwPasswordLength;
  LPSTR lpszUrlPath;
  DWORD dwUrlPathLength;
  LPSTR lpszExtraInfo;
  DWORD dwExtraInfoLength;
} URL_COMPONENTSA, *LPURL_COMPONENTSA;

typedef struct _INTERNET_BUFFERSA {
  DWORD dwStructSize;
  struct _INTERNET_BUFFERSA* Next;
  LPCSTR lpcszHeader;
  DWORD dwHeadersLength;
  DWORD dwHeadersTotal;
  LPVOID lpvBuffer;
  DWORD dwBufferLength;
  DWORD dwBufferTotal;
  DWORD dwOffsetLow;
  DWORD dwOffsetHigh;
} INTERNET_BUFFERSA, *LPINTERNET_BUFFERSA;

typedef struct {
  CHAR DisplayString[MAX_GOPHER_DISPLAY_TEXT + 1];
  DWORD GopherType;
  DWORD SizeLow;
  DWORD SizeHigh;
  FILETIME LastModificationTime;
  CHAR Locator[MAX_GOPHER_LOCATOR_LENGTH + 1];
} GOPHER_FIND_DATAA, *LPGOPHER_FIND_DATAA;

HINTERNET WINAPI InternetOpenA(LPCSTR lpszAgent, DWORD dwAccessType, LPCSTR lpszProxy, LPCSTR lpszProxyBypass,
                               DWORD dwFlags);
HINTERNET WINAPI InternetConnectA(HINTERNET hInternet, LPCSTR lpszServerName, INTERNET_PORT nServerPort,
                                  LPCSTR lpszUserName, LPCSTR lpszPassword, DWORD dwService, DWORD dwFlags,
                                  DWORD_PTR dwContext);
HINTERNET WINAPI InternetOpenUrlA(HINTERNET hInternet, LPCSTR lpszUrl, LPCSTR lpszHeaders, DWORD dwHeadersLength,
                                  DWORD dwFlags, DWORD_PTR dwContext);
BOOL WINAPI InternetCrackUrlA(LPCSTR lpszUrl, DWORD dwUrlLength, DWORD dwFlags,
                              LPURL_COMPONENTSA lpUrlComponents);
BOOL WINAPI InternetQueryOptionA(HINTERNET hInternet, DWORD dwOption, LPVOID lpBuffer, LPDWORD lpdwBufferLength);
BOOL WINAPI InternetSetOptionA(HINTERNET hInternet, DWORD dwOption, LPVOID lpBuffer, DWORD dwBufferLength);
BOOL WINAPI InternetGetLastResponseInfoA(LPDWORD lpdwError, LPSTR lpszBuffer, LPDWORD lpdwBufferLength);
INTERNET_STATUS_CALLBACK WINAPI InternetSetStatusCallbackA(HINTERNET hInternet,
                                                          INTERNET_STATUS_CALLBACK lpfnInternetCallback);
BOOL WINAPI InternetFindNextFileA(HINTERNET hFind, LPVOID lpvFindData);
BOOL WINAPI InternetGetCookieA(LPCSTR lpszUrl, LPCSTR lpszCookieName, LPSTR lpszCookieData, LPDWORD lpdwSize);
BOOL WINAPI InternetSetCookieA(LPCSTR lpszUrl, LPCSTR lpszCookieName, LPCSTR lpszCookieData);

HINTERNET WINAPI HttpOpenRequestA(HINTERNET hConnect, LPCSTR lpszVerb, LPCSTR lpszObjectName, LPCSTR lpszVersion,
                                  LPCSTR lpszReferrer, LPCSTR* lplpszAcceptTypes, DWORD dwFlags,
                                  DWORD_PTR dwContext);
BOOL WINAPI HttpAddRequestHeadersA(HINTERNET hRequest, LPCSTR lpszHeaders, DWORD dwHeadersLength,
                                   DWORD dwModifiers);
BOOL WINAPI HttpSendRequestA(HINTERNET hRequest, LPCSTR lpszHeaders, DWORD dwHeadersLength, LPVOID lpOptional,
                             DWORD dwOptionalLength);
BOOL WINAPI HttpSendRequestExA(HINTERNET hRequest, LPINTERNET_BUFFERSA lpBuffersIn,
                               LPINTERNET_BUFFERSA lpBuffersOut, DWORD dwFlags, DWORD_PTR dwContext);
BOOL WINAPI HttpEndRequestA(HINTERNET hRequest, LPINTERNET_BUFFERSA lpBuffersOut, DWORD dwFlags,
                            DWORD_PTR dwContext);
BOOL WINAPI HttpQueryInfoA(HINTERNET hRequest, DWORD dwInfoLevel, LPVOID lpBuffer, LPDWORD lpdwBufferLength,
                           LPDWORD lpdwIndex);

HINTERNET WINAPI FtpFindFirstFileA(HINTERNET hConnect, LPCSTR lpszSearchFile, LPWIN32_FIND_DATAA lpFindFileData,
                                   DWORD dwFlags, DWORD_PTR dwContext);
BOOL WINAPI FtpGetFileA(HINTERNET hConnect, LPCSTR lpszRemoteFile, LPCSTR lpszNewFile, BOOL fFailIfExists,
                        DWORD dwFlagsAndAttributes, DWORD dwFlags, DWORD_PTR dwContext);
BOOL WINAPI FtpPutFileA(HINTERNET hConnect, LPCSTR lpszLocalFile, LPCSTR lpszNewRemoteFile, DWORD dwFlags,
                        DWORD_PTR dwContext);
BOOL WINAPI FtpDeleteFileA(HINTERNET hConnect, LPCSTR lpszFileName);
BOOL WINAPI FtpRenameFileA(HINTERNET hConnect, LPCSTR lpszExisting, LPCSTR lpszNew);
HINTERNET WINAPI FtpOpenFileA(HINTERNET hConnect, LPCSTR lpszFileName, DWORD dwAccess, DWORD dwFlags,
                              DWORD_PTR dwContext);
BOOL WINAPI FtpCreateDirectoryA(HINTERNET hConnect, LPCSTR lpszDirectory);
BOOL WINAPI FtpRemoveDirectoryA(HINTERNET hConnect, LPCSTR lpszDirectory);
BOOL WINAPI FtpSetCurrentDirectoryA(HINTERNET hConnect, LPCSTR lpszDirectory);
BOOL WINAPI FtpGetCurrentDirectoryA(HINTERNET hConnect, LPSTR lpszCurrentDirectory, LPDWORD lpdwCurrentDirectory);

BOOL WINAPI GopherCreateLocatorA(LPCSTR lpszHost, INTERNET_PORT nServerPort, LPCSTR lpszDisplayString,
                                 LPCSTR lpszSelectorString, DWORD dwGopherType, LPSTR lpszLocator,
                                 LPDWORD lpdwBufferLength);
BOOL WINAPI GopherGetLocatorTypeA(LPCSTR lpszLocator, LPDWORD lpdwGopherType);
HINTERNET WINAPI GopherFindFirstFileA(HINTERNET hConnect, LPCSTR lpszLocator, LPCSTR lpszSearchString,
                                      LPGOPHER_FIND_DATAA lpFindData, DWORD dwFlags, DWORD_PTR dwContext);
HINTERNET WINAPI GopherOpenFileA(HINTERNET hConnect, LPCSTR lpszLocator, LPCSTR lpszView, DWORD dwFlags,
                                 DWORD_PTR dwContext);
BOOL WINAPI GopherGetAttributeA(HINTERNET hConnect, LPCSTR lpszLocator, LPCSTR lpszAttributeName, LPBYTE lpBuffer,
                                DWORD dwBufferLength, LPDWORD lpdwCharactersReturned, LPVOID lpfnEnumerator,
                                DWORD_PTR dwContext);

typedef struct {
  DWORD dwStructSize;
  LPWSTR lpszScheme;
  DWORD dwSchemeLength;
  INTERNET_SCHEME nScheme;
  LPWSTR lpszHostName;
  DWORD dwHostNameLength;
  INTERNET_PORT nPort;
  LPWSTR lpszUserName;
  DWORD dwUserNameLength;
  LPWSTR lpszPassword;
  DWORD dwPasswordLength;
  LPWSTR lpszUrlPath;
  DWORD dwUrlPathLength;
  LPWSTR lpszExtraInfo;
  DWORD dwExtraInfoLength;
} URL_COMPONENTSW, *LPURL_COMPONENTSW;

typedef struct _INTERNET_BUFFERSW {
  DWORD dwStructSize;
  struct _INTERNET_BUFFERSW* Next;
  LPCWSTR lpcszHeader;
  DWORD dwHeadersLength;
  DWORD dwHeadersTotal;
  LPVOID lpvBuffer;
  DWORD dwBufferLength;
  DWORD dwBufferTotal;
  DWORD dwOffsetLow;
  DWORD dwOffsetHigh;
} INTERNET_BUFFERSW, *LPINTERNET_BUFFERSW;

typedef struct {
  WCHAR DisplayString[MAX_GOPHER_DISPLAY_TEXT + 1];
  DWORD GopherType;
  DWORD SizeLow;
  DWORD SizeHigh;
  FILETIME LastModificationTime;
  WCHAR Locator[MAX_GOPHER_LOCATOR_LENGTH + 1];
} GOPHER_FIND_DATAW, *LPGOPHER_FIND_DATAW;

HINTERNET WINAPI InternetOpenW(LPCWSTR lpszAgent, DWORD dwAccessType, LPCWSTR lpszProxy, LPCWSTR lpszProxyBypass,
                               DWORD dwFlags);
HINTERNET WINAPI InternetConnectW(HINTERNET hInternet, LPCWSTR lpszServerName, INTERNET_PORT nServerPort,
                                  LPCWSTR lpszUserName, LPCWSTR lpszPassword, DWORD dwService, DWORD dwFlags,
                                  DWORD_PTR dwContext);
HINTERNET WINAPI InternetOpenUrlW(HINTERNET hInternet, LPCWSTR lpszUrl, LPCWSTR lpszHeaders, DWORD dwHeadersLength,
                                  DWORD dwFlags, DWORD_PTR dwContext);
BOOL WINAPI InternetCrackUrlW(LPCWSTR lpszUrl, DWORD dwUrlLength, DWORD dwFlags,
                              LPURL_COMPONENTSW lpUrlComponents);
BOOL WINAPI InternetQueryOptionW(HINTERNET hInternet, DWORD dwOption, LPVOID lpBuffer, LPDWORD lpdwBufferLength);
BOOL WINAPI InternetSetOptionW(HINTERNET hInternet, DWORD dwOption, LPVOID lpBuffer, DWORD dwBufferLength);
BOOL WINAPI InternetGetLastResponseInfoW(LPDWORD lpdwError, LPWSTR lpszBuffer, LPDWORD lpdwBufferLength);
INTERNET_STATUS_CALLBACK WINAPI InternetSetStatusCallbackW(HINTERNET hInternet,
                                                          INTERNET_STATUS_CALLBACK lpfnInternetCallback);
BOOL WINAPI InternetFindNextFileW(HINTERNET hFind, LPVOID lpvFindData);
BOOL WINAPI InternetGetCookieW(LPCWSTR lpszUrl, LPCWSTR lpszCookieName, LPWSTR lpszCookieData, LPDWORD lpdwSize);
BOOL WINAPI InternetSetCookieW(LPCWSTR lpszUrl, LPCWSTR lpszCookieName, LPCWSTR lpszCookieData);

HINTERNET WINAPI HttpOpenRequestW(HINTERNET hConnect, LPCWSTR lpszVerb, LPCWSTR lpszObjectName, LPCWSTR lpszVersion,
                                  LPCWSTR lpszReferrer, LPCWSTR* lplpszAcceptTypes, DWORD dwFlags,
                                  DWORD_PTR dwContext);
BOOL WINAPI HttpAddRequestHeadersW(HINTERNET hRequest, LPCWSTR lpszHeaders, DWORD dwHeadersLength,
                                   DWORD dwModifiers);
BOOL WINAPI HttpSendRequestW(HINTERNET hRequest, LPCWSTR lpszHeaders, DWORD dwHeadersLength, LPVOID lpOptional,
                             DWORD dwOptionalLength);
BOOL WINAPI HttpSendRequestExW(HINTERNET hRequest, LPINTERNET_BUFFERSW lpBuffersIn,
                               LPINTERNET_BUFFERSW lpBuffersOut, DWORD dwFlags, DWORD_PTR dwContext);
BOOL WINAPI HttpEndRequestW(HINTERNET hRequest, LPINTERNET_BUFFERSW lpBuffersOut, DWORD dwFlags,
                            DWORD_PTR dwContext);
BOOL WINAPI HttpQueryInfoW(HINTERNET hRequest, DWORD dwInfoLevel, LPVOID lpBuffer, LPDWORD lpdwBufferLength,
                           LPDWORD lpdwIndex);

HINTERNET WINAPI FtpFindFirstFileW(HINTERNET hConnect, LPCWSTR lpszSearchFile, LPWIN32_FIND_DATAW lpFindFileData,
                                   DWORD dwFlags, DWORD_PTR dwContext);
BOOL WINAPI FtpGetFileW(HINTERNET hConnect, LPCWSTR lpszRemoteFile, LPCWSTR lpszNewFile, BOOL fFailIfExists,
                        DWORD dwFlagsAndAttributes, DWORD dwFlags, DWORD_PTR dwContext);
BOOL WINAPI FtpPutFileW(HINTERNET hConnect, LPCWSTR lpszLocalFile, LPCWSTR lpszNewRemoteFile, DWORD dwFlags,
                        DWORD_PTR dwContext);
BOOL WINAPI FtpDeleteFileW(HINTERNET hConnect, LPCWSTR lpszFileName);
BOOL WINAPI FtpRenameFileW(HINTERNET hConnect, LPCWSTR lpszExisting, LPCWSTR lpszNew);
HINTERNET WINAPI FtpOpenFileW(HINTERNET hConnect, LPCWSTR lpszFileName, DWORD dwAccess, DWORD dwFlags,
                              DWORD_PTR dwContext);
BOOL WINAPI FtpCreateDirectoryW(HINTERNET hConnect, LPCWSTR lpszDirectory);
BOOL WINAPI FtpRemoveDirectoryW(HINTERNET hConnect, LPCWSTR lpszDirectory);
BOOL WINAPI FtpSetCurrentDirectoryW(HINTERNET hConnect, LPCWSTR lpszDirectory);
BOOL WINAPI FtpGetCurrentDirectoryW(HINTERNET hConnect, LPWSTR lpszCurrentDirectory, LPDWORD lpdwCurrentDirectory);

BOOL WINAPI GopherCreateLocatorW(LPCWSTR lpszHost, INTERNET_PORT nServerPort, LPCWSTR lpszDisplayString,
                                 LPCWSTR lpszSelectorString, DWORD dwGopherType, LPWSTR lpszLocator,
                                 LPDWORD lpdwBufferLength);
BOOL WINAPI GopherGetLocatorTypeW(LPCWSTR lpszLocator, LPDWORD lpdwGopherType);
HINTERNET WINAPI GopherFindFirstFileW(HINTERNET hConnect, LPCWSTR lpszLocator, LPCWSTR lpszSearchString,
                                      LPGOPHER_FIND_DATAW lpFindData, DWORD dwFlags, DWORD_PTR dwContext);
HINTERNET WINAPI GopherOpenFileW(HINTERNET hConnect, LPCWSTR lpszLocator, LPCWSTR lpszView, DWORD dwFlags,
                                 DWORD_PTR dwContext);
BOOL WINAPI GopherGetAttributeW(HINTERNET hConnect, LPCWSTR lpszLocator, LPCWSTR lpszAttributeName, LPBYTE lpBuffer,
                                DWORD dwBufferLength, LPDWORD lpdwCharactersReturned, LPVOID lpfnEnumerator,
                                DWORD_PTR dwContext);

BOOL WINAPI InternetCloseHandle(HINTERNET hInternet);
BOOL WINAPI InternetReadFile(HINTERNET hFile, LPVOID lpBuffer, DWORD dwNumberOfBytesToRead,
                             LPDWORD lpdwNumberOfBytesRead);
BOOL WINAPI InternetWriteFile(HINTERNET hFile, LPCVOID lpBuffer, DWORD dwNumberOfBytesToWrite,
                              LPDWORD lpdwNumberOfBytesWritten);
DWORD WINAPI InternetSetFilePointer(HINTERNET hFile, LONG lDistanceToMove, PLONG pReserved, DWORD dwMoveMethod,
                                    DWORD_PTR dwContext);
BOOL WINAPI InternetQueryDataAvailable(HINTERNET hFile, LPDWORD lpdwNumberOfBytesAvailable, DWORD dwFlags,
                                       DWORD_PTR dwContext);
DWORD WINAPI InternetErrorDlg(HWND hWnd, HINTERNET hRequest, DWORD dwError, DWORD dwFlags, LPVOID* lppvData);
DWORD WINAPI FtpGetFileSize(HINTERNET hFile, LPDWORD lpdwFileSizeHigh);
}  // extern "C"

#ifdef UNICODE
#define URL_COMPONENTS URL_COMPONENTSW
#define LPURL_COMPONENTS LPURL_COMPONENTSW
#define INTERNET_BUFFERS INTERNET_BUFFERSW
#define LPINTERNET_BUFFERS LPINTERNET_BUFFERSW
#define GOPHER_FIND_DATA GOPHER_FIND_DATAW
#define LPGOPHER_FIND_DATA LPGOPHER_FIND_DATAW
#define InternetOpen InternetOpenW
#define InternetConnect InternetConnectW
#define InternetOpenUrl InternetOpenUrlW
#define InternetCrackUrl InternetCrackUrlW
#define InternetQueryOption InternetQueryOptionW
#define InternetSetOption InternetSetOptionW
#define InternetGetLastResponseInfo InternetGetLastResponseInfoW
#define InternetSetStatusCallback InternetSetStatusCallbackW
#define InternetFindNextFile InternetFindNextFileW
#define InternetGetCookie InternetGetCookieW
#define InternetSetCookie InternetSetCookieW
#define HttpOpenRequest HttpOpenRequestW
#define HttpAddRequestHeaders HttpAddRequestHeadersW
#define HttpSendRequest HttpSendRequestW
#define HttpSendRequestEx HttpSendRequestExW
#define HttpEndRequest HttpEndRequestW
#define HttpQueryInfo HttpQueryInfoW
#define FtpFindFirstFile FtpFindFirstFileW
#define FtpGetFile FtpGetFileW
#define FtpPutFile FtpPutFileW
#define FtpDeleteFile FtpDeleteFileW
#define FtpRenameFile FtpRenameFileW
#define FtpOpenFile FtpOpenFileW
#define FtpCreateDirectory FtpCreateDirectoryW
#define FtpRemoveDirectory FtpRemoveDirectoryW
#define FtpSetCurrentDirectory FtpSetCurrentDirectoryW
#define FtpGetCurrentDirectory FtpGetCurrentDirectoryW
#define GopherCreateLocator GopherCreateLocatorW
#define GopherGetLocatorType GopherGetLocatorTypeW
#define GopherFindFirstFile GopherFindFirstFileW
#define GopherOpenFile GopherOpenFileW
#define GopherGetAttribute GopherGetAttributeW
#else
#define URL_COMPONENTS URL_COMPONENTSA
#define LPURL_COMPONENTS LPURL_COMPONENTSA
#define INTERNET_BUFFERS INTERNET_BUFFERSA
#define LPINTERNET_BUFFERS LPINTERNET_BUFFERSA
#define GOPHER_FIND_DATA GOPHER_FIND_DATAA
#define LPGOPHER_FIND_DATA LPGOPHER_FIND_DATAA
#define InternetOpen InternetOpenA
#define InternetConnect InternetConnectA
#define InternetOpenUrl InternetOpenUrlA
#define InternetCrackUrl InternetCrackUrlA
#define InternetQueryOption InternetQueryOptionA
#define InternetSetOption InternetSetOptionA
#define InternetGetLastResponseInfo InternetGetLastResponseInfoA
#define InternetSetStatusCallback InternetSetStatusCallbackA
#define InternetFindNextFile InternetFindNextFileA
#define InternetGetCookie InternetGetCookieA
#define InternetSetCookie InternetSetCookieA
#define HttpOpenRequest HttpOpenRequestA
#define HttpAddRequestHeaders HttpAddRequestHeadersA
#define HttpSendRequest HttpSendRequestA
#define HttpSendRequestEx HttpSendRequestExA
#define HttpEndRequest HttpEndRequestA
#define HttpQueryInfo HttpQueryInfoA
#define FtpFindFirstFile FtpFindFirstFileA
#define FtpGetFile FtpGetFileA
#define FtpPutFile FtpPutFileA
#define FtpDeleteFile FtpDeleteFileA
#define FtpRenameFile FtpRenameFileA
#define FtpOpenFile FtpOpenFileA
#define FtpCreateDirectory FtpCreateDirectoryA
#define FtpRemoveDirectory FtpRemoveDirectoryA
#define FtpSetCurrentDirectory FtpSetCurrentDirectoryA
#define FtpGetCurrentDirectory FtpGetCurrentDirectoryA
#define GopherCreateLocator GopherCreateLocatorA
#define GopherGetLocatorType GopherGetLocatorTypeA
#define GopherFindFirstFile GopherFindFirstFileA
#define GopherOpenFile GopherOpenFileA
#define GopherGetAttribute GopherGetAttributeA
#endif

#endif
