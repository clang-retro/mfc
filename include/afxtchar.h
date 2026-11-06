// afxtchar.h - the parts of <tchar.h> MFC programs rely on, for C
// libraries that do not have one (picolibc). Included by afx.h only
// when the system has no <tchar.h>.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
#ifndef __AFXTCHAR_H__
#define __AFXTCHAR_H__

#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>

#ifdef _UNICODE
typedef wchar_t _TCHAR;
typedef wchar_t _TSCHAR;
typedef wchar_t _TUCHAR;
typedef wchar_t _TXCHAR;
typedef wint_t _TINT;
#define __T(x) L##x
#define _tcslen wcslen
#define _tcscpy wcscpy
#define _tcsncpy wcsncpy
#define _tcscat wcscat
#define _tcscmp wcscmp
#define _tcsncmp wcsncmp
#define _tcschr wcschr
#define _tcsrchr wcsrchr
#define _tcsstr wcsstr
#define _tcsspn wcsspn
#define _tcscspn wcscspn
#define _tcspbrk wcspbrk
#define _tcstok wcstok
#define _tcstol wcstol
#define _tcstoul wcstoul
#define _tcstod wcstod
#define _stprintf swprintf
#define _sntprintf swprintf
#define _vsntprintf vswprintf
#define _tprintf wprintf
#define _ftprintf fwprintf
#define _istspace iswspace
#define _istdigit iswdigit
#define _istalpha iswalpha
#define _istalnum iswalnum
#define _istupper iswupper
#define _istlower iswlower
#define _totupper towupper
#define _totlower towlower
#else
typedef char _TCHAR;
typedef char _TSCHAR;
typedef unsigned char _TUCHAR;
typedef unsigned char _TXCHAR;
typedef int _TINT;
#define __T(x) x
#define _tcslen strlen
#define _tcscpy strcpy
#define _tcsncpy strncpy
#define _tcscat strcat
#define _tcscmp strcmp
#define _tcsncmp strncmp
#define _tcschr strchr
#define _tcsrchr strrchr
#define _tcsstr strstr
#define _tcsspn strspn
#define _tcscspn strcspn
#define _tcspbrk strpbrk
#define _tcstok strtok
#define _tcstol strtol
#define _tcstoul strtoul
#define _tcstod strtod
#define _stprintf sprintf
#define _sntprintf snprintf
#define _vsntprintf vsnprintf
#define _tprintf printf
#define _ftprintf fprintf
#define _istspace isspace
#define _istdigit isdigit
#define _istalpha isalpha
#define _istalnum isalnum
#define _istupper isupper
#define _istlower islower
#define _totupper toupper
#define _totlower tolower
#endif

#define _T(x) __T(x)
#define _TEXT(x) __T(x)

// Case-insensitive comparisons, by the system's case mapping.
inline int _afx_tcsnicmp(const _TCHAR* a, const _TCHAR* b, size_t n) {
  for (; n > 0; n--, a++, b++) {
    _TCHAR ca = (_TCHAR)(UINT_PTR)::CharLower((LPTSTR)(UINT_PTR)(_TUCHAR)*a);
    _TCHAR cb = (_TCHAR)(UINT_PTR)::CharLower((LPTSTR)(UINT_PTR)(_TUCHAR)*b);
    if (ca != cb) return (int)(_TUCHAR)ca - (int)(_TUCHAR)cb;
    if (ca == 0) return 0;
  }
  return 0;
}
inline int _afx_tcsicmp(const _TCHAR* a, const _TCHAR* b) { return _afx_tcsnicmp(a, b, (size_t)-1); }
#define _tcsnicmp _afx_tcsnicmp
#define _tcsicmp _afx_tcsicmp

// Opens a file by a TCHAR name; Unicode names go through the ANSI code page.
inline FILE* _afx_tfopen(const _TCHAR* name, const _TCHAR* mode) {
#ifdef _UNICODE
  char szName[MAX_PATH * 2], szMode[16];
  if (!::WideCharToMultiByte(CP_ACP, 0, name, -1, szName, sizeof(szName), NULL, NULL) ||
      !::WideCharToMultiByte(CP_ACP, 0, mode, -1, szMode, sizeof(szMode), NULL, NULL))
    return NULL;
  return fopen(szName, szMode);
#else
  return fopen(name, mode);
#endif
}
#define _tfopen _afx_tfopen

#endif  // __AFXTCHAR_H__
