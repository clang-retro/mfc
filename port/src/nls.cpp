// nls.cpp - code pages, character conversion and classification,
// string comparison, locales and the lstr and Char string helpers.
//
// The ANSI code page is UTF-8. Code pages 1252 and 437 and Latin-1
// convert too. WCHAR is the system's wchar_t, which holds a whole code
// point, so there are no surrogate pairs here.
//
// The locale is English (United States), whatever the system's is: the
// formats match what Windows programs expect from it.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#include "winport.h"

#include <string.h>
#include <wchar.h>
#include <wctype.h>

#include <string>

using namespace winport;

namespace {

// The upper halves of code pages 1252 and 437. In 1252, the five unused
// bytes map to the control characters of the same value, as Windows
// maps them.
const unsigned short kCp1252[32] = {
    0x20AC, 0x0081, 0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021, 0x02C6, 0x2030, 0x0160,
    0x2039, 0x0152, 0x008D, 0x017D, 0x008F, 0x0090, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022,
    0x2013, 0x2014, 0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0x009D, 0x017E, 0x0178};

const unsigned short kCp437[128] = {
    0x00C7, 0x00FC, 0x00E9, 0x00E2, 0x00E4, 0x00E0, 0x00E5, 0x00E7, 0x00EA, 0x00EB, 0x00E8, 0x00EF, 0x00EE,
    0x00EC, 0x00C4, 0x00C5, 0x00C9, 0x00E6, 0x00C6, 0x00F4, 0x00F6, 0x00F2, 0x00FB, 0x00F9, 0x00FF, 0x00D6,
    0x00DC, 0x00A2, 0x00A3, 0x00A5, 0x20A7, 0x0192, 0x00E1, 0x00ED, 0x00F3, 0x00FA, 0x00F1, 0x00D1, 0x00AA,
    0x00BA, 0x00BF, 0x2310, 0x00AC, 0x00BD, 0x00BC, 0x00A1, 0x00AB, 0x00BB, 0x2591, 0x2592, 0x2593, 0x2502,
    0x2524, 0x2561, 0x2562, 0x2556, 0x2555, 0x2563, 0x2551, 0x2557, 0x255D, 0x255C, 0x255B, 0x2510, 0x2514,
    0x2534, 0x252C, 0x251C, 0x2500, 0x253C, 0x255E, 0x255F, 0x255A, 0x2554, 0x2569, 0x2566, 0x2560, 0x2550,
    0x256C, 0x2567, 0x2568, 0x2564, 0x2565, 0x2559, 0x2558, 0x2552, 0x2553, 0x256B, 0x256A, 0x2518, 0x250C,
    0x2588, 0x2584, 0x258C, 0x2590, 0x2580, 0x03B1, 0x00DF, 0x0393, 0x03C0, 0x03A3, 0x03C3, 0x00B5, 0x03C4,
    0x03A6, 0x0398, 0x03A9, 0x03B4, 0x221E, 0x03C6, 0x03B5, 0x2229, 0x2261, 0x00B1, 0x2265, 0x2264, 0x2320,
    0x2321, 0x00F7, 0x2248, 0x00B0, 0x2219, 0x00B7, 0x221A, 0x207F, 0x00B2, 0x25A0, 0x00A0};

enum Kind { kUtf8, k1252, k437, kLatin1, kBad };

Kind KindOf(UINT cp) {
  switch (cp) {
    case CP_ACP: case CP_THREAD_ACP: case CP_UTF8: case CP_MACCP: return kUtf8;
    case 1252: return k1252;
    case CP_OEMCP: case 437: return k437;
    case 28591: case CP_SYMBOL: case 20127: return kLatin1;
    default: return kBad;
  }
}

unsigned int ByteToCp(Kind k, unsigned char b) {
  if (b < 0x80) return b;
  if (k == k1252) return b < 0xa0 ? kCp1252[b - 0x80] : b;
  if (k == k437) return kCp437[b - 0x80];
  return b;
}

// The byte for a code point, or -1 when the code page has none.
int CpToByte(Kind k, unsigned int c) {
  if (c < 0x80) return (int)c;
  if (k == kLatin1) return c < 0x100 ? (int)c : -1;
  if (k == k1252) {
    if (c >= 0xa0 && c < 0x100) return (int)c;
    for (int i = 0; i < 32; i++)
      if (kCp1252[i] == c) return 0x80 + i;
    return -1;
  }
  for (int i = 0; i < 128; i++)
    if (kCp437[i] == c) return 0x80 + i;
  return -1;
}

// Decodes one UTF-8 sequence; returns its length, or 0 when invalid
// (one byte is then consumed and read as U+FFFD).
int DecodeUtf8(const unsigned char* s, int n, unsigned int* out) {
  unsigned char c = s[0];
  int len;
  unsigned int cp, min;
  if (c < 0x80) {
    *out = c;
    return 1;
  } else if ((c & 0xe0) == 0xc0) {
    len = 2; cp = c & 0x1f; min = 0x80;
  } else if ((c & 0xf0) == 0xe0) {
    len = 3; cp = c & 0x0f; min = 0x800;
  } else if ((c & 0xf8) == 0xf0) {
    len = 4; cp = c & 0x07; min = 0x10000;
  } else {
    return 0;
  }
  if (len > n) return 0;
  for (int i = 1; i < len; i++) {
    if ((s[i] & 0xc0) != 0x80) return 0;
    cp = (cp << 6) | (s[i] & 0x3f);
  }
  if (cp < min || cp > 0x10ffff || (cp >= 0xd800 && cp < 0xe000)) return 0;
  *out = cp;
  return len;
}

int EncodeUtf8(unsigned int c, char* out) {
  if (c < 0x80) { out[0] = (char)c; return 1; }
  if (c < 0x800) { out[0] = (char)(0xc0 | (c >> 6)); out[1] = (char)(0x80 | (c & 0x3f)); return 2; }
  if (c < 0x10000) {
    out[0] = (char)(0xe0 | (c >> 12)); out[1] = (char)(0x80 | ((c >> 6) & 0x3f)); out[2] = (char)(0x80 | (c & 0x3f));
    return 3;
  }
  out[0] = (char)(0xf0 | (c >> 18)); out[1] = (char)(0x80 | ((c >> 12) & 0x3f));
  out[2] = (char)(0x80 | ((c >> 6) & 0x3f)); out[3] = (char)(0x80 | (c & 0x3f));
  return 4;
}

}  // namespace

extern "C" {

UINT WINAPI GetACP(void) { return CP_UTF8; }
UINT WINAPI GetOEMCP(void) { return 437; }

BOOL WINAPI IsValidCodePage(UINT cp) { return cp != CP_ACP && cp != CP_OEMCP && KindOf(cp) != kBad; }

BOOL WINAPI GetCPInfo(UINT cp, LPCPINFO info) {
  Kind k = KindOf(cp);
  if (k == kBad) {
    SetLastError(ERROR_INVALID_PARAMETER);
    return FALSE;
  }
  memset(info, 0, sizeof(*info));
  info->MaxCharSize = k == kUtf8 ? 4 : 1;
  info->DefaultChar[0] = '?';
  return TRUE;
}

// UTF-8 has no lead bytes in the DBCS sense.
BOOL WINAPI IsDBCSLeadByte(BYTE) { return FALSE; }
BOOL WINAPI IsDBCSLeadByteEx(UINT, BYTE) { return FALSE; }

int WINAPI MultiByteToWideChar(UINT cp, DWORD flags, LPCSTR src, int n, LPWSTR dst, int cch) {
  Kind k = KindOf(cp);
  if (k == kBad || !src || cch < 0 || (dst == NULL && cch != 0)) {
    SetLastError(k == kBad ? ERROR_INVALID_PARAMETER : ERROR_INVALID_PARAMETER);
    return 0;
  }
  if (n < 0) n = (int)strlen(src) + 1;
  const unsigned char* s = (const unsigned char*)src;
  int out = 0;
  for (int i = 0; i < n;) {
    unsigned int c;
    if (k == kUtf8) {
      int len = DecodeUtf8(s + i, n - i, &c);
      if (len == 0) {
        if (flags & MB_ERR_INVALID_CHARS) {
          SetLastError(ERROR_NO_UNICODE_TRANSLATION);
          return 0;
        }
        c = 0xfffd;
        len = 1;
      }
      i += len;
    } else {
      c = ByteToCp(k, s[i++]);
    }
    if (cch) {
      if (out >= cch) {
        SetLastError(ERROR_INSUFFICIENT_BUFFER);
        return 0;
      }
      dst[out] = (wchar_t)c;
    }
    out++;
  }
  return out;
}

int WINAPI WideCharToMultiByte(UINT cp, DWORD, LPCWSTR src, int n, LPSTR dst, int cb, LPCSTR defChar,
                               LPBOOL usedDef) {
  Kind k = KindOf(cp);
  if (k == kBad || !src || cb < 0 || (dst == NULL && cb != 0) || (k == kUtf8 && (defChar || usedDef))) {
    SetLastError(ERROR_INVALID_PARAMETER);
    return 0;
  }
  if (usedDef) *usedDef = FALSE;
  if (n < 0) n = (int)wcslen(src) + 1;
  int out = 0;
  char buf[4];
  for (int i = 0; i < n; i++) {
    unsigned int c = (unsigned int)src[i];
    int len;
    if (k == kUtf8) {
      if (c > 0x10ffff || (c >= 0xd800 && c < 0xe000)) c = 0xfffd;
      len = EncodeUtf8(c, buf);
    } else {
      int b = CpToByte(k, c);
      if (b < 0) {
        b = (unsigned char)(defChar ? defChar[0] : '?');
        if (usedDef) *usedDef = TRUE;
      }
      buf[0] = (char)b;
      len = 1;
    }
    if (cb) {
      if (out + len > cb) {
        SetLastError(ERROR_INSUFFICIENT_BUFFER);
        return 0;
      }
      memcpy(dst + out, buf, (size_t)len);
    }
    out += len;
  }
  return out;
}

}  // extern "C"

// ---------------------------------------------------------------------
// Comparison
//
// Strings compare by a word sort like Windows': hyphens and apostrophes
// count only when everything else is equal (unless SORT_STRINGSORT),
// punctuation sorts before digits and digits before letters, letters
// compare without case and accents first, and then lower case comes
// before upper case.

namespace {

// Removes the accent from the Latin-1 letters.
unsigned int BaseLetter(unsigned int c) {
  static const char base[] =
      "AAAAAAACEEEEIIIIDNOOOOO*OUUUUYTsaaaaaaaceeeeiiiidnooooo/ouuuuyty";
  if (c >= 0xc0 && c <= 0xff && base[c - 0xc0] != '*' && base[c - 0xc0] != '/') {
    if (c == 0xc6) return 'A';
    if (c == 0xe6) return 'a';
    if (c == 0xdf) return 's';
    return (unsigned char)base[c - 0xc0];
  }
  return c;
}

// Primary weight: the group (symbols, digits, letters) and the folded
// letter.
unsigned int Primary(unsigned int c, DWORD flags) {
  if (!(flags & NORM_IGNORENONSPACE) || true) c = BaseLetter(c);
  unsigned int lower = (unsigned int)towlower((wint_t)c);
  if (iswdigit((wint_t)c)) return 0x20000 + c;
  if (iswalpha((wint_t)c)) return 0x30000 + lower;
  if (iswspace((wint_t)c)) return 0x08000 + c;
  return 0x10000 + c;
}

bool Ignorable(unsigned int c, DWORD flags) {
  if (flags & NORM_IGNORESYMBOLS)
    if (!iswalnum((wint_t)c)) return true;
  if (!(flags & SORT_STRINGSORT) && (c == '-' || c == '\'' || c == 0xad)) return true;
  return false;
}

int Compare(DWORD flags, const wchar_t* a, int na, const wchar_t* b, int nb) {
  // Primary pass.
  int i = 0, j = 0;
  for (;;) {
    while (i < na && Ignorable((unsigned)a[i], flags)) i++;
    while (j < nb && Ignorable((unsigned)b[j], flags)) j++;
    if (i >= na || j >= nb) break;
    unsigned int pa = Primary((unsigned)a[i], flags), pb = Primary((unsigned)b[j], flags);
    if (pa != pb) return pa < pb ? CSTR_LESS_THAN : CSTR_GREATER_THAN;
    i++;
    j++;
  }
  if (i < na) return CSTR_GREATER_THAN;
  if (j < nb) return CSTR_LESS_THAN;
  // Accents.
  if (!(flags & NORM_IGNORENONSPACE)) {
    for (i = 0, j = 0; i < na && j < nb; i++, j++) {
      while (i < na && Ignorable((unsigned)a[i], flags)) i++;
      while (j < nb && Ignorable((unsigned)b[j], flags)) j++;
      if (i >= na || j >= nb) break;
      unsigned int x = (unsigned)towlower((wint_t)a[i]), y = (unsigned)towlower((wint_t)b[j]);
      if (x != y) return x < y ? CSTR_LESS_THAN : CSTR_GREATER_THAN;
    }
  }
  // Case: lower before upper.
  if (!(flags & NORM_IGNORECASE)) {
    for (i = 0, j = 0; i < na && j < nb; i++, j++) {
      while (i < na && Ignorable((unsigned)a[i], flags)) i++;
      while (j < nb && Ignorable((unsigned)b[j], flags)) j++;
      if (i >= na || j >= nb) break;
      bool ua = iswupper((wint_t)a[i]) != 0, ub = iswupper((wint_t)b[j]) != 0;
      if (ua != ub) return ua ? CSTR_GREATER_THAN : CSTR_LESS_THAN;
    }
  }
  // The ignored characters break the last ties.
  if (!(flags & NORM_IGNORESYMBOLS)) {
    std::wstring x, y;
    for (int k = 0; k < na; k++)
      if (Ignorable((unsigned)a[k], flags)) x += a[k];
    for (int k = 0; k < nb; k++)
      if (Ignorable((unsigned)b[k], flags)) y += b[k];
    int c = x.compare(y);
    if (c) return c < 0 ? CSTR_LESS_THAN : CSTR_GREATER_THAN;
  }
  return CSTR_EQUAL;
}

int Len(const wchar_t* s, int n) {
  if (n >= 0) {
    // A count stops at an embedded zero, as Windows does.
    int k = 0;
    while (k < n && s[k]) k++;
    return k;
  }
  return (int)wcslen(s);
}

}  // namespace

extern "C" {

int WINAPI CompareStringW(LCID, DWORD flags, LPCWSTR a, int na, LPCWSTR b, int nb) {
  if (!a || !b) {
    SetLastError(ERROR_INVALID_PARAMETER);
    return 0;
  }
  return Compare(flags, a, Len(a, na), b, Len(b, nb));
}

int WINAPI CompareStringA(LCID lcid, DWORD flags, LPCSTR a, int na, LPCSTR b, int nb) {
  if (!a || !b) {
    SetLastError(ERROR_INVALID_PARAMETER);
    return 0;
  }
  if (na >= 0) na = (int)strnlen(a, (size_t)na);
  if (nb >= 0) nb = (int)strnlen(b, (size_t)nb);
  std::wstring wa = Widen(a, na), wb = Widen(b, nb);
  return CompareStringW(lcid, flags, wa.c_str(), (int)wa.size(), wb.c_str(), (int)wb.size());
}

int WINAPI LCMapStringW(LCID, DWORD flags, LPCWSTR src, int n, LPWSTR dst, int cch) {
  if (!src) {
    SetLastError(ERROR_INVALID_PARAMETER);
    return 0;
  }
  if (n < 0) n = (int)wcslen(src) + 1;
  if (cch == 0) return n;
  if (cch < n) {
    SetLastError(ERROR_INSUFFICIENT_BUFFER);
    return 0;
  }
  for (int i = 0; i < n; i++) {
    wchar_t c = src[i];
    if (flags & LCMAP_UPPERCASE) c = (wchar_t)towupper((wint_t)c);
    else if (flags & LCMAP_LOWERCASE) c = (wchar_t)towlower((wint_t)c);
    dst[i] = c;
  }
  return n;
}

int WINAPI LCMapStringA(LCID lcid, DWORD flags, LPCSTR src, int n, LPSTR dst, int cb) {
  if (!src) {
    SetLastError(ERROR_INVALID_PARAMETER);
    return 0;
  }
  bool withZero = n < 0;
  std::wstring w = Widen(src, n < 0 ? (int)strlen(src) : n);
  std::wstring m(w.size(), 0);
  if (!w.empty()) LCMapStringW(lcid, flags, w.c_str(), (int)w.size(), &m[0], (int)m.size());
  std::string r = Narrow(m.c_str(), (int)m.size());
  if (withZero) r.push_back(0);
  if (cb == 0) return (int)r.size();
  if (cb < (int)r.size()) {
    SetLastError(ERROR_INSUFFICIENT_BUFFER);
    return 0;
  }
  memcpy(dst, r.data(), r.size());
  return (int)r.size();
}

BOOL WINAPI GetStringTypeW(DWORD type, LPCWSTR s, int n, LPWORD out) {
  if (!s || !out) {
    SetLastError(ERROR_INVALID_PARAMETER);
    return FALSE;
  }
  if (n < 0) n = (int)wcslen(s) + 1;
  for (int i = 0; i < n; i++) {
    wint_t c = (wint_t)s[i];
    WORD t = 0;
    if (type == CT_CTYPE1) {
      if (iswupper(c)) t |= C1_UPPER;
      if (iswlower(c)) t |= C1_LOWER;
      if (iswdigit(c)) t |= C1_DIGIT;
      if (iswspace(c)) t |= C1_SPACE;
      if (iswpunct(c)) t |= C1_PUNCT;
      if (iswcntrl(c)) t |= C1_CNTRL;
      if (c == ' ' || c == '\t') t |= C1_BLANK;
      if (iswxdigit(c)) t |= C1_XDIGIT;
      if (iswalpha(c)) t |= C1_ALPHA;
    } else if (type == CT_CTYPE2) {
      t = iswdigit(c) ? 3 : iswalpha(c) ? 1 : iswspace(c) ? 10 : 0;  // C2_EUROPENUMBER, C2_LEFTTORIGHT, C2_WHITESPACE
    }
    out[i] = t;
  }
  return TRUE;
}

BOOL WINAPI GetStringTypeExW(LCID, DWORD type, LPCWSTR s, int n, LPWORD out) { return GetStringTypeW(type, s, n, out); }

// The narrow form gives one result per byte, as Windows does: the
// bytes of a UTF-8 sequence all get the character's type.
BOOL WINAPI GetStringTypeExA(LCID, DWORD type, LPCSTR s, int n, LPWORD out) {
  if (!s || !out) {
    SetLastError(ERROR_INVALID_PARAMETER);
    return FALSE;
  }
  if (n < 0) n = (int)strlen(s) + 1;
  const unsigned char* p = (const unsigned char*)s;
  for (int i = 0; i < n;) {
    unsigned int c;
    int len = DecodeUtf8(p + i, n - i, &c);
    if (len == 0) {
      c = 0xfffd;
      len = 1;
    }
    wchar_t w = (wchar_t)c;
    WORD t;
    GetStringTypeW(type, &w, 1, &t);
    for (int k = 0; k < len; k++) out[i + k] = t;
    i += len;
  }
  return TRUE;
}

// ---------------------------------------------------------------------
// Locales

LCID WINAPI GetUserDefaultLCID(void) { return 0x0409; }
LCID WINAPI GetSystemDefaultLCID(void) { return 0x0409; }
LANGID WINAPI GetUserDefaultLangID(void) { return 0x0409; }
LANGID WINAPI GetSystemDefaultLangID(void) { return 0x0409; }
LANGID WINAPI GetUserDefaultUILanguage(void) { return 0x0409; }

}  // extern "C"

namespace {
thread_local LCID t_locale = 0x0409;

const char* const kDays[7] = {"Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"};
const char* const kMonths[12] = {"January", "February", "March",     "April",   "May",      "June",
                                 "July",    "August",   "September", "October", "November", "December"};

std::string LocaleString(LCTYPE t) {
  t &= ~(LOCALE_NOUSEROVERRIDE | LOCALE_RETURN_NUMBER);
  if (t >= LOCALE_SDAYNAME1 && t < LOCALE_SDAYNAME1 + 7) return kDays[(t - LOCALE_SDAYNAME1 + 1) % 7];
  if (t >= LOCALE_SABBREVDAYNAME1 && t < LOCALE_SABBREVDAYNAME1 + 7)
    return std::string(kDays[(t - LOCALE_SABBREVDAYNAME1 + 1) % 7], 3);
  if (t >= LOCALE_SMONTHNAME1 && t < LOCALE_SMONTHNAME1 + 12) return kMonths[t - LOCALE_SMONTHNAME1];
  if (t == LOCALE_SMONTHNAME1 + 12) return "";
  if (t >= LOCALE_SABBREVMONTHNAME1 && t < LOCALE_SABBREVMONTHNAME1 + 12)
    return std::string(kMonths[t - LOCALE_SABBREVMONTHNAME1], 3);
  switch (t) {
    case LOCALE_ILANGUAGE: return "0409";
    case LOCALE_SLANGUAGE: return "English (United States)";
    case LOCALE_SENGLANGUAGE: return "English";
    case LOCALE_SABBREVLANGNAME: return "ENU";
    case LOCALE_SNATIVELANGNAME: return "English";
    case LOCALE_ICOUNTRY: return "1";
    case LOCALE_SCOUNTRY: return "United States";
    case LOCALE_SENGCOUNTRY: return "United States";
    case LOCALE_SABBREVCTRYNAME: return "USA";
    case LOCALE_IDEFAULTLANGUAGE: return "0409";
    case LOCALE_IDEFAULTCOUNTRY: return "1";
    case LOCALE_IDEFAULTCODEPAGE: return "437";
    case LOCALE_IDEFAULTANSICODEPAGE: return "65001";
    case LOCALE_SLIST: return ",";
    case LOCALE_IMEASURE: return "1";
    case LOCALE_SDECIMAL: return ".";
    case LOCALE_STHOUSAND: return ",";
    case LOCALE_SGROUPING: return "3;0";
    case LOCALE_IDIGITS: return "2";
    case LOCALE_ILZERO: return "1";
    case LOCALE_INEGNUMBER: return "1";
    case LOCALE_SNATIVEDIGITS: return "0123456789";
    case LOCALE_SCURRENCY: return "$";
    case LOCALE_SINTLSYMBOL: return "USD";
    case LOCALE_SMONDECIMALSEP: return ".";
    case LOCALE_SMONTHOUSANDSEP: return ",";
    case LOCALE_SMONGROUPING: return "3;0";
    case LOCALE_ICURRDIGITS: return "2";
    case LOCALE_IINTLCURRDIGITS: return "2";
    case LOCALE_ICURRENCY: return "0";
    case LOCALE_INEGCURR: return "0";
    case LOCALE_SDATE: return "/";
    case LOCALE_STIME: return ":";
    case LOCALE_SSHORTDATE: return "M/d/yyyy";
    case LOCALE_SLONGDATE: return "dddd, MMMM dd, yyyy";
    case LOCALE_STIMEFORMAT: return "h:mm:ss tt";
    case LOCALE_IDATE: return "0";
    case LOCALE_ILDATE: return "0";
    case LOCALE_ITIME: return "0";
    case LOCALE_ITIMEMARKPOSN: return "0";
    case LOCALE_ICENTURY: return "1";
    case LOCALE_ITLZERO: return "0";
    case LOCALE_IDAYLZERO: return "0";
    case LOCALE_IMONLZERO: return "0";
    case LOCALE_S1159: return "AM";
    case LOCALE_S2359: return "PM";
    case LOCALE_ICALENDARTYPE: return "1";
    case LOCALE_IFIRSTDAYOFWEEK: return "6";
    case LOCALE_SPOSITIVESIGN: return "";
    case LOCALE_SNEGATIVESIGN: return "-";
    default: return std::string("\x01");  // not known
  }
}

template <class C>
int CopyResult(const std::basic_string<C>& s, C* buf, int n) {
  int need = (int)s.size() + 1;
  if (n == 0) return need;
  if (n < need) {
    SetLastError(ERROR_INSUFFICIENT_BUFFER);
    return 0;
  }
  memcpy(buf, s.c_str(), need * sizeof(C));
  return need;
}

}  // namespace

extern "C" {

LCID WINAPI GetThreadLocale(void) { return t_locale; }
BOOL WINAPI SetThreadLocale(LCID l) {
  t_locale = l;
  return TRUE;
}
BOOL WINAPI IsValidLocale(LCID l, DWORD) { return (l & 0x3ff) == 0x09 || l == 0 || l == 0x400 || l == 0x800; }

int WINAPI GetLocaleInfoA(LCID, LCTYPE t, LPSTR buf, int n) {
  std::string s = LocaleString(t);
  if (s == "\x01") {
    SetLastError(ERROR_INVALID_FLAGS);
    return 0;
  }
  if (t & LOCALE_RETURN_NUMBER) {
    if (n == 0) return sizeof(DWORD);
    if (n < (int)sizeof(DWORD)) {
      SetLastError(ERROR_INSUFFICIENT_BUFFER);
      return 0;
    }
    DWORD v = (DWORD)strtoul(s.c_str(), NULL, (t & 0xffff) == LOCALE_ILANGUAGE ? 16 : 10);
    memcpy(buf, &v, sizeof v);
    return sizeof(DWORD);
  }
  return CopyResult(s, buf, n);
}

int WINAPI GetLocaleInfoW(LCID l, LCTYPE t, LPWSTR buf, int n) {
  if (t & LOCALE_RETURN_NUMBER) {
    int r = GetLocaleInfoA(l, t, (LPSTR)buf, n == 0 ? 0 : n * (int)sizeof(WCHAR));
    return r ? (int)(sizeof(DWORD) / sizeof(WCHAR)) : 0;
  }
  std::string s = LocaleString(t);
  if (s == "\x01") {
    SetLastError(ERROR_INVALID_FLAGS);
    return 0;
  }
  return CopyResult(Widen(s.c_str()), buf, n);
}

}  // extern "C"

// ---------------------------------------------------------------------
// Date, time and number formats

namespace {

std::string Pad(int v, int w) {
  char b[16];
  snprintf(b, sizeof b, "%0*d", w, v);
  return b;
}

// Expands a picture: runs of a letter are fields, quoted text is copied.
std::string Picture(const std::string& pic, const SYSTEMTIME& st, bool date) {
  std::string r;
  for (size_t i = 0; i < pic.size();) {
    char c = pic[i];
    if (c == '\'') {
      size_t e = i + 1;
      while (e < pic.size()) {
        if (pic[e] == '\'') {
          if (e + 1 < pic.size() && pic[e + 1] == '\'') {
            r += '\'';
            e += 2;
            continue;
          }
          break;
        }
        r += pic[e++];
      }
      i = e + 1;
      continue;
    }
    size_t n = 1;
    while (i + n < pic.size() && pic[i + n] == c) n++;
    bool field = true;
    if (date) {
      if (c == 'd') {
        if (n == 1) r += std::to_string(st.wDay);
        else if (n == 2) r += Pad(st.wDay, 2);
        else if (n == 3) r += std::string(kDays[st.wDayOfWeek % 7], 3);
        else r += kDays[st.wDayOfWeek % 7];
      } else if (c == 'M') {
        if (n == 1) r += std::to_string(st.wMonth);
        else if (n == 2) r += Pad(st.wMonth, 2);
        else if (n == 3) r += std::string(kMonths[(st.wMonth + 11) % 12], 3);
        else r += kMonths[(st.wMonth + 11) % 12];
      } else if (c == 'y') {
        if (n == 1) r += std::to_string(st.wYear % 100);
        else if (n == 2) r += Pad(st.wYear % 100, 2);
        else r += Pad(st.wYear, 4);
      } else if (c == 'g') {
        r += "A.D.";
      } else {
        field = false;
      }
    } else {
      int h12 = st.wHour % 12 == 0 ? 12 : st.wHour % 12;
      if (c == 'h') r += n == 1 ? std::to_string(h12) : Pad(h12, 2);
      else if (c == 'H') r += n == 1 ? std::to_string(st.wHour) : Pad(st.wHour, 2);
      else if (c == 'm') r += n == 1 ? std::to_string(st.wMinute) : Pad(st.wMinute, 2);
      else if (c == 's') r += n == 1 ? std::to_string(st.wSecond) : Pad(st.wSecond, 2);
      else if (c == 't') r += n == 1 ? std::string(st.wHour < 12 ? "A" : "P") : std::string(st.wHour < 12 ? "AM" : "PM");
      else field = false;
    }
    if (!field) r.append(n, c);
    i += n;
  }
  return r;
}

// Takes out the fields TIME_NOSECONDS and TIME_NOMINUTESORSECONDS drop,
// with the separators before them.
std::string StripTime(const std::string& pic, DWORD flags) {
  std::string r;
  for (size_t i = 0; i < pic.size(); i++) {
    char c = pic[i];
    bool drop = (c == 's' && (flags & (TIME_NOSECONDS | TIME_NOMINUTESORSECONDS))) ||
                (c == 'm' && (flags & TIME_NOMINUTESORSECONDS)) || (c == 't' && (flags & TIME_NOTIMEMARKER));
    if (drop) {
      while (!r.empty() && !isalpha((unsigned char)r.back()) && r.back() != '\'') r.pop_back();
      while (i + 1 < pic.size() && pic[i + 1] == c) i++;
      continue;
    }
    r += c;
  }
  while (!r.empty() && r.back() == ' ') r.pop_back();
  return r;
}

bool CurrentIfNull(const SYSTEMTIME* in, SYSTEMTIME* st) {
  if (in) {
    *st = *in;
    // The day of the week comes from the date.
    FILETIME ft;
    SYSTEMTIME tmp = *in;
    tmp.wHour = tmp.wMinute = tmp.wSecond = tmp.wMilliseconds = 0;
    if (!SystemTimeToFileTime(&tmp, &ft)) return false;
    FileTimeToSystemTime(&ft, &tmp);
    st->wDayOfWeek = tmp.wDayOfWeek;
    return true;
  }
  GetLocalTime(st);
  return true;
}

std::string DateFormat(DWORD flags, const SYSTEMTIME* t, const char* fmt, bool* ok) {
  SYSTEMTIME st;
  *ok = CurrentIfNull(t, &st);
  if (!*ok) return std::string();
  std::string pic = fmt ? fmt : LocaleString((flags & DATE_LONGDATE) ? LOCALE_SLONGDATE : LOCALE_SSHORTDATE);
  return Picture(pic, st, true);
}

std::string TimeFormat(DWORD flags, const SYSTEMTIME* t, const char* fmt, bool* ok) {
  SYSTEMTIME st;
  if (t && (t->wHour > 23 || t->wMinute > 59 || t->wSecond > 59)) {
    *ok = false;
    return std::string();
  }
  *ok = true;
  if (t) st = *t;
  else GetLocalTime(&st);
  std::string pic = fmt ? fmt : LocaleString(LOCALE_STIMEFORMAT);
  if (flags & TIME_FORCE24HOURFORMAT)
    for (char& c : pic)
      if (c == 'h') c = 'H';
  pic = StripTime(pic, flags);
  return Picture(pic, st, false);
}

// A number string ("-1234.5") in the locale's form: two decimals and
// groups of three.
std::string NumberFormat(const char* v, bool* ok) {
  std::string s(v ? v : "");
  *ok = false;
  bool neg = false;
  size_t i = 0;
  if (i < s.size() && s[i] == '-') {
    neg = true;
    i++;
  }
  std::string ip, fp;
  while (i < s.size() && isdigit((unsigned char)s[i])) ip += s[i++];
  if (i < s.size() && s[i] == '.') {
    i++;
    while (i < s.size() && isdigit((unsigned char)s[i])) fp += s[i++];
  }
  if (i != s.size() || (ip.empty() && fp.empty())) return std::string();
  *ok = true;
  // Rounds to two places.
  while (fp.size() < 3) fp += '0';
  bool up = fp[2] >= '5';
  fp.resize(2);
  std::string digits = (ip.empty() ? "0" : ip) + fp;
  if (up) {
    int k = (int)digits.size() - 1;
    while (k >= 0) {
      if (digits[k] == '9') {
        digits[k] = '0';
        k--;
      } else {
        digits[k]++;
        break;
      }
    }
    if (k < 0) digits.insert(0, "1");
  }
  ip = digits.substr(0, digits.size() - 2);
  fp = digits.substr(digits.size() - 2);
  size_t nz = ip.find_first_not_of('0');
  ip = nz == std::string::npos ? "0" : ip.substr(nz);
  std::string g;
  for (size_t k = 0; k < ip.size(); k++) {
    if (k && (ip.size() - k) % 3 == 0) g += ',';
    g += ip[k];
  }
  return (neg ? "-" : "") + g + "." + fp;
}

}  // namespace

extern "C" {

int WINAPI GetDateFormatA(LCID, DWORD flags, const SYSTEMTIME* t, LPCSTR fmt, LPSTR buf, int n) {
  bool ok;
  std::string r = DateFormat(flags, t, fmt, &ok);
  if (!ok) {
    SetLastError(ERROR_INVALID_PARAMETER);
    return 0;
  }
  return CopyResult(r, buf, n);
}

int WINAPI GetDateFormatW(LCID, DWORD flags, const SYSTEMTIME* t, LPCWSTR fmt, LPWSTR buf, int n) {
  bool ok;
  std::string f = Narrow(fmt);
  std::string r = DateFormat(flags, t, fmt ? f.c_str() : NULL, &ok);
  if (!ok) {
    SetLastError(ERROR_INVALID_PARAMETER);
    return 0;
  }
  return CopyResult(Widen(r.c_str()), buf, n);
}

int WINAPI GetTimeFormatA(LCID, DWORD flags, const SYSTEMTIME* t, LPCSTR fmt, LPSTR buf, int n) {
  bool ok;
  std::string r = TimeFormat(flags, t, fmt, &ok);
  if (!ok) {
    SetLastError(ERROR_INVALID_PARAMETER);
    return 0;
  }
  return CopyResult(r, buf, n);
}

int WINAPI GetTimeFormatW(LCID, DWORD flags, const SYSTEMTIME* t, LPCWSTR fmt, LPWSTR buf, int n) {
  bool ok;
  std::string f = Narrow(fmt);
  std::string r = TimeFormat(flags, t, fmt ? f.c_str() : NULL, &ok);
  if (!ok) {
    SetLastError(ERROR_INVALID_PARAMETER);
    return 0;
  }
  return CopyResult(Widen(r.c_str()), buf, n);
}

int WINAPI GetNumberFormatA(LCID, DWORD, LPCSTR v, const void*, LPSTR buf, int n) {
  bool ok;
  std::string r = NumberFormat(v, &ok);
  if (!ok) {
    SetLastError(ERROR_INVALID_PARAMETER);
    return 0;
  }
  return CopyResult(r, buf, n);
}

int WINAPI GetNumberFormatW(LCID, DWORD, LPCWSTR v, const void*, LPWSTR buf, int n) {
  bool ok;
  std::string r = NumberFormat(Narrow(v).c_str(), &ok);
  if (!ok) {
    SetLastError(ERROR_INVALID_PARAMETER);
    return 0;
  }
  return CopyResult(Widen(r.c_str()), buf, n);
}

// ---------------------------------------------------------------------
// lstr helpers. lstrcmp and lstrcmpi use the word sort, as on Windows.

int WINAPI lstrlenA(LPCSTR s) { return s ? (int)strlen(s) : 0; }
int WINAPI lstrlenW(LPCWSTR s) { return s ? (int)wcslen(s) : 0; }
LPSTR WINAPI lstrcpyA(LPSTR d, LPCSTR s) { return strcpy(d, s ? s : ""); }
LPWSTR WINAPI lstrcpyW(LPWSTR d, LPCWSTR s) { return wcscpy(d, s ? s : L""); }
LPSTR WINAPI lstrcatA(LPSTR d, LPCSTR s) { return strcat(d, s ? s : ""); }
LPWSTR WINAPI lstrcatW(LPWSTR d, LPCWSTR s) { return wcscat(d, s ? s : L""); }

LPSTR WINAPI lstrcpynA(LPSTR d, LPCSTR s, int n) {
  if (!d || n <= 0) return d;
  int i = 0;
  if (s)
    for (; i < n - 1 && s[i]; i++) d[i] = s[i];
  d[i] = 0;
  return d;
}

LPWSTR WINAPI lstrcpynW(LPWSTR d, LPCWSTR s, int n) {
  if (!d || n <= 0) return d;
  int i = 0;
  if (s)
    for (; i < n - 1 && s[i]; i++) d[i] = s[i];
  d[i] = 0;
  return d;
}

int WINAPI lstrcmpW(LPCWSTR a, LPCWSTR b) {
  if (!a || !b) return a ? 1 : b ? -1 : 0;
  return CompareStringW(0x0409, 0, a, -1, b, -1) - 2;
}
int WINAPI lstrcmpiW(LPCWSTR a, LPCWSTR b) {
  if (!a || !b) return a ? 1 : b ? -1 : 0;
  return CompareStringW(0x0409, NORM_IGNORECASE, a, -1, b, -1) - 2;
}
int WINAPI lstrcmpA(LPCSTR a, LPCSTR b) {
  if (!a || !b) return a ? 1 : b ? -1 : 0;
  return CompareStringA(0x0409, 0, a, -1, b, -1) - 2;
}
int WINAPI lstrcmpiA(LPCSTR a, LPCSTR b) {
  if (!a || !b) return a ? 1 : b ? -1 : 0;
  return CompareStringA(0x0409, NORM_IGNORECASE, a, -1, b, -1) - 2;
}

// ---------------------------------------------------------------------
// Char helpers. The narrow ones work on UTF-8: case changes keep the
// length, so letters whose other case has a different length in UTF-8
// stay as they are.

}  // extern "C"

namespace {

// Maps one UTF-8 character in place when the result has the same
// length; returns the length.
int MapUtf8(char* p, int n, bool upper) {
  unsigned int c;
  int len = DecodeUtf8((const unsigned char*)p, n, &c);
  if (len == 0) return 1;
  unsigned int m = (unsigned int)(upper ? towupper((wint_t)c) : towlower((wint_t)c));
  char buf[4];
  if (m != c && EncodeUtf8(m, buf) == len) memcpy(p, buf, (size_t)len);
  return len;
}

DWORD MapBuffA(LPSTR s, DWORD n, bool upper) {
  if (!s) return 0;
  DWORD i = 0;
  while (i < n) i += (DWORD)MapUtf8(s + i, (int)(n - i), upper);
  return n;
}

}  // namespace

extern "C" {

DWORD WINAPI CharUpperBuffA(LPSTR s, DWORD n) { return MapBuffA(s, n, true); }
DWORD WINAPI CharLowerBuffA(LPSTR s, DWORD n) { return MapBuffA(s, n, false); }

DWORD WINAPI CharUpperBuffW(LPWSTR s, DWORD n) {
  if (!s) return 0;
  for (DWORD i = 0; i < n; i++) s[i] = (wchar_t)towupper((wint_t)s[i]);
  return n;
}

DWORD WINAPI CharLowerBuffW(LPWSTR s, DWORD n) {
  if (!s) return 0;
  for (DWORD i = 0; i < n; i++) s[i] = (wchar_t)towlower((wint_t)s[i]);
  return n;
}

// A pointer below 64K is one character, which is changed and returned.
LPSTR WINAPI CharUpperA(LPSTR s) {
  if (IS_INTRESOURCE(s)) return (LPSTR)(UINT_PTR)(unsigned char)toupper((unsigned char)(UINT_PTR)s);
  MapBuffA(s, (DWORD)strlen(s), true);
  return s;
}

LPSTR WINAPI CharLowerA(LPSTR s) {
  if (IS_INTRESOURCE(s)) return (LPSTR)(UINT_PTR)(unsigned char)tolower((unsigned char)(UINT_PTR)s);
  MapBuffA(s, (DWORD)strlen(s), false);
  return s;
}

LPWSTR WINAPI CharUpperW(LPWSTR s) {
  if (IS_INTRESOURCE(s)) return (LPWSTR)(UINT_PTR)towupper((wint_t)(UINT_PTR)s);
  CharUpperBuffW(s, (DWORD)wcslen(s));
  return s;
}

LPWSTR WINAPI CharLowerW(LPWSTR s) {
  if (IS_INTRESOURCE(s)) return (LPWSTR)(UINT_PTR)towlower((wint_t)(UINT_PTR)s);
  CharLowerBuffW(s, (DWORD)wcslen(s));
  return s;
}

LPSTR WINAPI CharNextA(LPCSTR s) {
  if (!*s) return (LPSTR)s;
  unsigned int c;
  int len = DecodeUtf8((const unsigned char*)s, (int)strnlen(s, 4), &c);
  return (LPSTR)(s + (len ? len : 1));
}

LPWSTR WINAPI CharNextW(LPCWSTR s) { return (LPWSTR)(*s ? s + 1 : s); }

LPSTR WINAPI CharPrevA(LPCSTR start, LPCSTR s) {
  if (s <= start) return (LPSTR)start;
  const char* p = s - 1;
  while (p > start && ((unsigned char)*p & 0xc0) == 0x80 && s - p < 4) p--;
  unsigned int c;
  if (DecodeUtf8((const unsigned char*)p, (int)(s - p), &c) != (int)(s - p)) p = s - 1;
  return (LPSTR)p;
}

LPWSTR WINAPI CharPrevW(LPCWSTR start, LPCWSTR s) { return (LPWSTR)(s > start ? s - 1 : start); }

static BOOL ConvertBuff(LPCSTR src, LPSTR dst, DWORD n, UINT from, UINT to) {
  if (!src || !dst) return FALSE;
  std::wstring w(n, 0);
  int k = MultiByteToWideChar(from, 0, src, (int)n, &w[0], (int)n);
  std::string out(n * 4 + 1, 0);
  int m = WideCharToMultiByte(to, 0, w.c_str(), k, &out[0], (int)out.size(), to == CP_UTF8 ? NULL : "?", NULL);
  // The buffer forms keep the length; UTF-8 may need more, and is cut.
  memcpy(dst, out.data(), (size_t)(m < (int)n ? m : (int)n));
  if (m < (int)n) memset(dst + m, 0, n - (DWORD)m);
  return TRUE;
}

BOOL WINAPI CharToOemBuffA(LPCSTR s, LPSTR d, DWORD n) { return ConvertBuff(s, d, n, CP_UTF8, 437); }
BOOL WINAPI OemToCharBuffA(LPCSTR s, LPSTR d, DWORD n) { return ConvertBuff(s, d, n, 437, CP_UTF8); }
BOOL WINAPI CharToOemA(LPCSTR s, LPSTR d) { return s ? CharToOemBuffA(s, d, (DWORD)strlen(s) + 1) : FALSE; }
BOOL WINAPI OemToCharA(LPCSTR s, LPSTR d) {
  if (!s || !d) return FALSE;
  // OEM to UTF-8 can grow; the caller's buffer is assumed big enough,
  // as Windows assumes.
  std::wstring w = std::wstring(strlen(s) + 1, 0);
  int k = MultiByteToWideChar(437, 0, s, -1, &w[0], (int)w.size());
  WideCharToMultiByte(CP_UTF8, 0, w.c_str(), k, d, k * 4, NULL, NULL);
  return TRUE;
}

BOOL WINAPI IsCharAlphaW(WCHAR c) { return iswalpha((wint_t)c) != 0; }
BOOL WINAPI IsCharAlphaNumericW(WCHAR c) { return iswalnum((wint_t)c) != 0; }
BOOL WINAPI IsCharUpperW(WCHAR c) { return iswupper((wint_t)c) != 0; }
BOOL WINAPI IsCharLowerW(WCHAR c) { return iswlower((wint_t)c) != 0; }
// One byte of UTF-8 is a letter only when it is ASCII.
BOOL WINAPI IsCharAlphaA(CHAR c) { return isalpha((unsigned char)c) && (unsigned char)c < 0x80; }
BOOL WINAPI IsCharAlphaNumericA(CHAR c) { return isalnum((unsigned char)c) && (unsigned char)c < 0x80; }
BOOL WINAPI IsCharUpperA(CHAR c) { return isupper((unsigned char)c) && (unsigned char)c < 0x80; }
BOOL WINAPI IsCharLowerA(CHAR c) { return islower((unsigned char)c) && (unsigned char)c < 0x80; }

}  // extern "C"
