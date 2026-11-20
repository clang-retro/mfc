// atlenc.h - ATL's encodings: Base64, UUEncode, quoted-printable, hex,
// the "B" encoding of mail headers, XML escaping and UTF-8. Each encoder
// has a GetRequiredLength companion; the encoders take the destination's
// size in *pnDestLen and give back the length they wrote (or need).
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#ifndef __ATLENC_H__
#define __ATLENC_H__

#include <atlbase.h>
#include <string.h>

#define ATL_BASE64_FLAG_NONE 0
#define ATL_BASE64_FLAG_NOPAD 1
#define ATL_BASE64_FLAG_NOCRLF 2

#define ATLSMTP_UUENCODE_HEADER 1
#define ATLSMTP_UUENCODE_END 2
#define ATLSMTP_UUENCODE_DOT 4

#define ATLSMTP_QPENCODE_DOT 1
#define ATLSMTP_QPENCODE_TRAILING_SOFT 2

#define ATL_ESC_FLAG_NONE 0
#define ATL_ESC_FLAG_ATTR 1  // escape quotes too, for attribute values

namespace ATL {

// ---------------------------------------------------------------------
// Hex digits

inline char AtlHexValue(char ch) {
  if (ch >= '0' && ch <= '9') return (char)(ch - '0');
  if (ch >= 'A' && ch <= 'F') return (char)(ch - 'A' + 10);
  if (ch >= 'a' && ch <= 'f') return (char)(ch - 'a' + 10);
  return -1;
}
inline char AtlGetHexValue(char ch) { return AtlHexValue(ch); }
inline wchar_t AtlGetHexValue(wchar_t ch) {
  return ch < 0x80 ? (wchar_t)(signed char)AtlHexValue((char)ch) : (wchar_t)-1;
}

inline BOOL IsExtendedChar(char ch) { return (unsigned char)ch > 126 || (unsigned char)ch < 32; }

// ---------------------------------------------------------------------
// Base64: 76 characters to a line, each ending in CRLF, unless NOCRLF.

inline int Base64EncodeGetRequiredLength(int nSrcLen, DWORD dwFlags = ATL_BASE64_FLAG_NONE) {
  if (nSrcLen < 0) return -1;
  int nChars = (dwFlags & ATL_BASE64_FLAG_NOPAD) ? (nSrcLen * 4 + 2) / 3 : (nSrcLen + 2) / 3 * 4;
  if (!(dwFlags & ATL_BASE64_FLAG_NOCRLF)) nChars += (nChars / 76 + 1) * 2;
  return nChars;
}

inline int Base64DecodeGetRequiredLength(int nSrcLen) { return nSrcLen < 0 ? -1 : nSrcLen; }

inline BOOL Base64Encode(const BYTE* pbSrcData, int nSrcLen, LPSTR szDest, int* pnDestLen,
                         DWORD dwFlags = ATL_BASE64_FLAG_NONE) {
  static const char s_chars[] =
      "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  if (pnDestLen == NULL || (pbSrcData == NULL && nSrcLen > 0) || nSrcLen < 0) return FALSE;
  if (szDest == NULL || *pnDestLen < Base64EncodeGetRequiredLength(nSrcLen, dwFlags)) {
    *pnDestLen = Base64EncodeGetRequiredLength(nSrcLen, dwFlags);
    return FALSE;
  }
  int nOut = 0, nLine = 0;
  for (int i = 0; i < nSrcLen; i += 3) {
    DWORD dw = (DWORD)pbSrcData[i] << 16;
    int nBytes = nSrcLen - i < 3 ? nSrcLen - i : 3;
    if (nBytes > 1) dw |= (DWORD)pbSrcData[i + 1] << 8;
    if (nBytes > 2) dw |= pbSrcData[i + 2];
    int nChars = nBytes + 1;
    for (int k = 0; k < 4; k++) {
      char ch;
      if (k < nChars)
        ch = s_chars[(dw >> (18 - 6 * k)) & 0x3f];
      else if (dwFlags & ATL_BASE64_FLAG_NOPAD)
        break;
      else
        ch = '=';
      szDest[nOut++] = ch;
      if (!(dwFlags & ATL_BASE64_FLAG_NOCRLF) && ++nLine == 76) {
        szDest[nOut++] = '\r';
        szDest[nOut++] = '\n';
        nLine = 0;
      }
    }
  }
  if (!(dwFlags & ATL_BASE64_FLAG_NOCRLF) && nLine != 0) {
    szDest[nOut++] = '\r';
    szDest[nOut++] = '\n';
  }
  *pnDestLen = nOut;
  return TRUE;
}

inline int _AtlBase64Value(char ch) {
  if (ch >= 'A' && ch <= 'Z') return ch - 'A';
  if (ch >= 'a' && ch <= 'z') return ch - 'a' + 26;
  if (ch >= '0' && ch <= '9') return ch - '0' + 52;
  if (ch == '+') return 62;
  if (ch == '/') return 63;
  return -1;
}

// Characters that are not Base64 (line breaks, padding) are skipped.
inline BOOL Base64Decode(LPCSTR szSrc, int nSrcLen, BYTE* pbDest, int* pnDestLen) {
  if (szSrc == NULL || pnDestLen == NULL) return FALSE;
  int nNeeded = 0, nBits = 0;
  for (int i = 0; i < nSrcLen; i++) {
    if (_AtlBase64Value(szSrc[i]) < 0) continue;
    if ((nBits += 6) >= 8) {
      nBits -= 8;
      nNeeded++;
    }
  }
  if (pbDest == NULL || *pnDestLen < nNeeded) {
    *pnDestLen = nNeeded;
    return pbDest == NULL && nNeeded == 0;
  }
  DWORD dwAcc = 0;
  int nOut = 0;
  nBits = 0;
  for (int i = 0; i < nSrcLen; i++) {
    int v = _AtlBase64Value(szSrc[i]);
    if (v < 0) continue;
    dwAcc = (dwAcc << 6) | (DWORD)v;
    if ((nBits += 6) >= 8) {
      nBits -= 8;
      pbDest[nOut++] = (BYTE)(dwAcc >> nBits);
    }
  }
  *pnDestLen = nOut;
  return TRUE;
}

// ---------------------------------------------------------------------
// UUEncode: 45 bytes to a line, each led by its length character.

inline int UUEncodeGetRequiredLength(int nSrcLen) {
  if (nSrcLen < 0) return -1;
  int nLines = (nSrcLen + 44) / 45;
  // With room for a doubled dot on each line, a header and the end.
  return nLines * (1 + 1 + 60 + 2) + MAX_PATH + 16 + 8;
}

inline int UUDecodeGetRequiredLength(int nSrcLen) { return nSrcLen < 0 ? -1 : nSrcLen; }

inline BOOL UUEncode(const BYTE* pbSrcData, int nSrcLen, LPSTR szDest, int* pnDestLen,
                     LPCTSTR lpszFile = _T("file"), DWORD dwFlags = 0) {
  if (pnDestLen == NULL || nSrcLen < 0 || (pbSrcData == NULL && nSrcLen > 0)) return FALSE;
  // The header names the file in plain ASCII.
  char szHeader[MAX_PATH + 16] = "";
  if (dwFlags & ATLSMTP_UUENCODE_HEADER) {
    char szName[MAX_PATH] = "";
    int i = 0;
    for (; lpszFile != NULL && lpszFile[i] != 0 && i < MAX_PATH - 1; i++)
      szName[i] = (char)lpszFile[i];
    szName[i] = 0;
    strcpy(szHeader, "begin 666 ");
    strcat(szHeader, szName);
    strcat(szHeader, "\r\n");
  }
  int nNeeded = (int)strlen(szHeader);
  for (int i = 0; i < nSrcLen; i += 45) {
    int n = nSrcLen - i < 45 ? nSrcLen - i : 45;
    nNeeded += 1 + (n + 2) / 3 * 4 + 2;
    if (dwFlags & ATLSMTP_UUENCODE_DOT) nNeeded++;
  }
  if (dwFlags & ATLSMTP_UUENCODE_END) nNeeded += 8;  // "`\r\nend\r\n"
  if (szDest == NULL || *pnDestLen < nNeeded) {
    *pnDestLen = nNeeded;
    return FALSE;
  }
  int nOut = 0;
  for (const char* p = szHeader; *p; p++) szDest[nOut++] = *p;
#define _ATL_UUCHAR(v) ((char)((v) ? ((v)&0x3f) + 0x20 : '`'))
  for (int i = 0; i < nSrcLen; i += 45) {
    int n = nSrcLen - i < 45 ? nSrcLen - i : 45;
    char chLen = _ATL_UUCHAR(n);
    // A line starting with '.' is doubled for SMTP.
    if ((dwFlags & ATLSMTP_UUENCODE_DOT) && chLen == '.') szDest[nOut++] = '.';
    szDest[nOut++] = chLen;
    for (int k = 0; k < n; k += 3) {
      BYTE b0 = pbSrcData[i + k];
      BYTE b1 = k + 1 < n ? pbSrcData[i + k + 1] : 0;
      BYTE b2 = k + 2 < n ? pbSrcData[i + k + 2] : 0;
      szDest[nOut++] = _ATL_UUCHAR(b0 >> 2);
      szDest[nOut++] = _ATL_UUCHAR(((b0 & 3) << 4) | (b1 >> 4));
      szDest[nOut++] = _ATL_UUCHAR(((b1 & 0xf) << 2) | (b2 >> 6));
      szDest[nOut++] = _ATL_UUCHAR(b2 & 0x3f);
    }
    szDest[nOut++] = '\r';
    szDest[nOut++] = '\n';
  }
#undef _ATL_UUCHAR
  if (dwFlags & ATLSMTP_UUENCODE_END) {
    const char* pszEnd = "`\r\nend\r\n";
    for (const char* p = pszEnd; *p; p++) szDest[nOut++] = *p;
  }
  *pnDestLen = nOut;
  return TRUE;
}

// Decodes the data lines; a "begin" header and the "end" line are skipped.
inline BOOL UUDecode(BYTE* pbSrcData, int nSrcLen, BYTE* pbDest, int* pnDestLen) {
  if (pbSrcData == NULL || pnDestLen == NULL) return FALSE;
  int nOut = 0, nMax = pbDest != NULL ? *pnDestLen : 0;
  BOOL bFits = TRUE;
  int i = 0;
  while (i < nSrcLen) {
    int nLineEnd = i;
    while (nLineEnd < nSrcLen && pbSrcData[nLineEnd] != '\n') nLineEnd++;
    int nLen = nLineEnd - i;
    if (nLen > 0 && pbSrcData[i + nLen - 1] == '\r') nLen--;
    const BYTE* p = pbSrcData + i;
    i = nLineEnd + 1;
    if (nLen == 0) continue;
    if ((nLen >= 6 && memcmp(p, "begin ", 6) == 0) || (nLen == 3 && memcmp(p, "end", 3) == 0))
      continue;
    if (p[0] == '.' && nLen > 1 && p[1] == '.') {  // SMTP's doubled dot
      p++;
      nLen--;
    }
    int nBytes = (p[0] - 0x20) & 0x3f;
    if (nBytes == 0) continue;
    for (int k = 1, nDone = 0; nDone < nBytes; k += 4) {
      BYTE c[4];
      for (int j = 0; j < 4; j++) c[j] = k + j < nLen ? (BYTE)((p[k + j] - 0x20) & 0x3f) : 0;
      BYTE b[3] = {(BYTE)((c[0] << 2) | (c[1] >> 4)), (BYTE)((c[1] << 4) | (c[2] >> 2)),
                   (BYTE)((c[2] << 6) | c[3])};
      for (int j = 0; j < 3 && nDone < nBytes; j++, nDone++) {
        if (nOut < nMax)
          pbDest[nOut] = b[j];
        else
          bFits = FALSE;
        nOut++;
      }
    }
  }
  *pnDestLen = nOut;
  return bFits && pbDest != NULL;
}

// ---------------------------------------------------------------------
// Quoted-printable: lines of at most 76 characters, '=' for soft breaks.

inline int QPEncodeGetRequiredLength(int nSrcLen) {
  if (nSrcLen < 0) return -1;
  return nSrcLen * 3 + (nSrcLen * 3 / 75 + 1) * 3 + 3;
}

inline int QPDecodeGetRequiredLength(int nSrcLen) { return nSrcLen < 0 ? -1 : nSrcLen; }

inline BOOL QPEncode(BYTE* pbSrcData, int nSrcLen, LPSTR szDest, int* pnDestLen,
                     DWORD dwFlags = 0) {
  static const char s_hex[] = "0123456789ABCDEF";
  if (pnDestLen == NULL || nSrcLen < 0 || (pbSrcData == NULL && nSrcLen > 0)) return FALSE;
  int nMax = szDest != NULL ? *pnDestLen : 0;
  int nOut = 0, nLine = 0;
  auto put = [&](char ch) {
    if (nOut < nMax) szDest[nOut] = ch;
    nOut++;
  };
  for (int i = 0; i < nSrcLen; i++) {
    BYTE b = pbSrcData[i];
    if (b == '\r' && i + 1 < nSrcLen && pbSrcData[i + 1] == '\n') {
      put('\r');
      put('\n');
      i++;
      nLine = 0;
      continue;
    }
    // Spaces and tabs before a line break, and '=' and anything not
    // printable ASCII, are written as =XX.
    BOOL bTrailing = (b == ' ' || b == '\t') &&
                     (i + 1 == nSrcLen || pbSrcData[i + 1] == '\r' || pbSrcData[i + 1] == '\n');
    BOOL bEscape = b == '=' || b > 126 || (b < 32 && b != '\t') || bTrailing;
    int nWidth = bEscape ? 3 : 1;
    if (nLine + nWidth > 75) {
      put('=');
      put('\r');
      put('\n');
      nLine = 0;
    }
    if ((dwFlags & ATLSMTP_QPENCODE_DOT) && nLine == 0 && b == '.') {
      put('.');
      nLine++;
    }
    if (bEscape) {
      put('=');
      put(s_hex[b >> 4]);
      put(s_hex[b & 0xf]);
    } else {
      put((char)b);
    }
    nLine += nWidth;
  }
  if ((dwFlags & ATLSMTP_QPENCODE_TRAILING_SOFT) && nLine > 0) {
    put('=');
    put('\r');
    put('\n');
  }
  *pnDestLen = nOut;
  return szDest != NULL && nOut <= nMax;
}

inline BOOL QPDecode(BYTE* pbSrcData, int nSrcLen, LPSTR szDest, int* pnDestLen,
                     DWORD dwFlags = 0) {
  (void)dwFlags;
  if (pbSrcData == NULL || pnDestLen == NULL) return FALSE;
  int nMax = szDest != NULL ? *pnDestLen : 0;
  int nOut = 0;
  for (int i = 0; i < nSrcLen; i++) {
    char ch = (char)pbSrcData[i];
    if (ch == '=') {
      if (i + 1 < nSrcLen && (pbSrcData[i + 1] == '\r' || pbSrcData[i + 1] == '\n')) {
        i++;  // soft line break
        if (pbSrcData[i] == '\r' && i + 1 < nSrcLen && pbSrcData[i + 1] == '\n') i++;
        continue;
      }
      if (i + 2 < nSrcLen && AtlHexValue((char)pbSrcData[i + 1]) >= 0 &&
          AtlHexValue((char)pbSrcData[i + 2]) >= 0) {
        ch = (char)((AtlHexValue((char)pbSrcData[i + 1]) << 4) |
                    AtlHexValue((char)pbSrcData[i + 2]));
        i += 2;
      }
    }
    if (nOut < nMax) szDest[nOut] = ch;
    nOut++;
  }
  *pnDestLen = nOut;
  return szDest != NULL && nOut <= nMax;
}

// ---------------------------------------------------------------------
// Hex: two upper case digits a byte.

inline int AtlHexEncodeGetRequiredLength(int nSrcLen) { return nSrcLen < 0 ? -1 : nSrcLen * 2; }
inline int AtlHexDecodeGetRequiredLength(int nSrcLen) { return nSrcLen < 0 ? -1 : nSrcLen / 2; }

inline BOOL AtlHexEncode(const BYTE* pbSrcData, int nSrcLen, LPSTR szDest, int* pnDestLen) {
  static const char s_hex[] = "0123456789ABCDEF";
  if (pnDestLen == NULL || nSrcLen < 0 || (pbSrcData == NULL && nSrcLen > 0)) return FALSE;
  if (szDest == NULL || *pnDestLen < nSrcLen * 2) {
    *pnDestLen = nSrcLen * 2;
    return FALSE;
  }
  for (int i = 0; i < nSrcLen; i++) {
    szDest[i * 2] = s_hex[pbSrcData[i] >> 4];
    szDest[i * 2 + 1] = s_hex[pbSrcData[i] & 0xf];
  }
  *pnDestLen = nSrcLen * 2;
  return TRUE;
}

inline BOOL AtlHexDecode(LPCSTR pSrcData, int nSrcLen, LPBYTE pbDest, int* pnDestLen) {
  if (pSrcData == NULL || pnDestLen == NULL || nSrcLen < 0 || (nSrcLen & 1)) return FALSE;
  if (pbDest == NULL || *pnDestLen < nSrcLen / 2) {
    *pnDestLen = nSrcLen / 2;
    return FALSE;
  }
  for (int i = 0; i < nSrcLen / 2; i++) {
    char hi = AtlHexValue(pSrcData[i * 2]), lo = AtlHexValue(pSrcData[i * 2 + 1]);
    if (hi < 0 || lo < 0) return FALSE;
    pbDest[i] = (BYTE)((hi << 4) | lo);
  }
  *pnDestLen = nSrcLen / 2;
  return TRUE;
}

// ---------------------------------------------------------------------
// The mail header "B" encoding: =?charset?B?base64?=

inline int BEncodeGetRequiredLength(int nSrcLen, LPCSTR szCharset) {
  if (nSrcLen < 0 || szCharset == NULL) return -1;
  return Base64EncodeGetRequiredLength(nSrcLen, ATL_BASE64_FLAG_NOCRLF) + 7 +
         (int)strlen(szCharset);
}

inline BOOL BEncode(BYTE* pbSrcData, int nSrcLen, LPSTR szDest, int* pnDestLen,
                    LPCSTR szCharset) {
  if (pnDestLen == NULL || szCharset == NULL) return FALSE;
  int nNeeded = BEncodeGetRequiredLength(nSrcLen, szCharset);
  if (szDest == NULL || *pnDestLen < nNeeded) {
    *pnDestLen = nNeeded;
    return FALSE;
  }
  int nCharset = (int)strlen(szCharset);
  int nOut = 0;
  szDest[nOut++] = '=';
  szDest[nOut++] = '?';
  memcpy(szDest + nOut, szCharset, nCharset);
  nOut += nCharset;
  szDest[nOut++] = '?';
  szDest[nOut++] = 'B';
  szDest[nOut++] = '?';
  int nB64 = *pnDestLen - nOut - 2;
  if (!Base64Encode(pbSrcData, nSrcLen, szDest + nOut, &nB64, ATL_BASE64_FLAG_NOCRLF))
    return FALSE;
  nOut += nB64;
  szDest[nOut++] = '?';
  szDest[nOut++] = '=';
  *pnDestLen = nOut;
  return TRUE;
}

// ---------------------------------------------------------------------
// XML escaping: & < > (and " ' with ATL_ESC_FLAG_ATTR) become entities.
// Returns the length written, or 0 when the destination is too small.

inline int EscapeXML(const wchar_t* szIn, int nSrcLen, wchar_t* szEsc, int nDestLen,
                     DWORD dwFlags = ATL_ESC_FLAG_NONE) {
  if (szIn == NULL) return 0;
  int nOut = 0;
  for (int i = 0; i < nSrcLen; i++) {
    const wchar_t* pszRep = NULL;
    switch (szIn[i]) {
      case L'&': pszRep = L"&amp;"; break;
      case L'<': pszRep = L"&lt;"; break;
      case L'>': pszRep = L"&gt;"; break;
      case L'"':
        if (dwFlags & ATL_ESC_FLAG_ATTR) pszRep = L"&quot;";
        break;
      case L'\'':
        if (dwFlags & ATL_ESC_FLAG_ATTR) pszRep = L"&apos;";
        break;
    }
    if (pszRep != NULL) {
      for (; *pszRep; pszRep++) {
        if (nOut >= nDestLen) return 0;
        szEsc[nOut++] = *pszRep;
      }
    } else {
      if (nOut >= nDestLen) return 0;
      szEsc[nOut++] = szIn[i];
    }
  }
  return nOut;
}

// ---------------------------------------------------------------------
// UTF-8. With nDest 0, the length needed.

inline int AtlUnicodeToUTF8(LPCWSTR wszSrc, int nSrc, LPSTR szDest, int nDest) {
  return ::WideCharToMultiByte(CP_UTF8, 0, wszSrc, nSrc, szDest, nDest, NULL, NULL);
}

inline int AtlUTF8ToUnicode(LPCSTR szSrc, int nSrc, LPWSTR wszDest, int nDest) {
  return ::MultiByteToWideChar(CP_UTF8, 0, szSrc, nSrc, wszDest, nDest);
}

}  // namespace ATL

#endif  // __ATLENC_H__
