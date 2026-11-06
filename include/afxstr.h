// afxstr.h - CStringT, CString, CStringA, CStringW.
//
// A CStringT holds one pointer to its characters; a header with the
// reference count and lengths sits in front of them, so copies share one
// buffer until one of them is changed.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
#ifndef __AFXSTR_H__
#define __AFXSTR_H__

#include <windows.h>
#include <stdarg.h>

HINSTANCE AFXAPI AfxGetResourceHandle();
void AFXAPI AfxThrowMemoryException();
void AFXAPI AfxThrowInvalidArgException();

// printf-style formatting with Microsoft's rules (%s is the string type of
// the format, %S the other one, %I64d, and so on) on every platform. Like
// C99 vsnprintf it returns the full length, writing at most nMax-1
// characters and a terminating zero.
int AFX_CDECL AfxVFormatA(char* pszBuf, int nMax, const char* pszFormat, va_list args);
int AFX_CDECL AfxVFormatW(WCHAR* pszBuf, int nMax, const WCHAR* pszFormat, va_list args);

struct CStringData {
  long nRefs;        // -1 locked (never freed); the shared empty string too
  int nDataLength;   // characters, not counting the terminating zero
  int nAllocLength;  // characters that fit, not counting the zero
  void* data() { return this + 1; }
};

CStringData* AFXAPI AfxStrNil();
CStringData* AFXAPI AfxStrAlloc(int nChars, int nCharSize);
void AFXAPI AfxStrRelease(CStringData* pData);

template <typename T> struct ChTraitsMFC;
template <typename C> struct AfxUnsignedCh { typedef C T; };
template <> struct AfxUnsignedCh<char> { typedef unsigned char T; };

template <> struct ChTraitsMFC<char> {
  typedef char XCHAR;
  typedef WCHAR YCHAR;
  static int Len(const char* p) { return p ? lstrlenA(p) : 0; }
  static int LenOther(const WCHAR* p) {
    return p ? WideCharToMultiByte(CP_ACP, 0, p, -1, NULL, 0, NULL, NULL) - 1 : 0;
  }
  static int ConvertFrom(char* d, int nDest, const WCHAR* s, int nSrc) {
    return WideCharToMultiByte(CP_ACP, 0, s, nSrc, d, nDest, NULL, NULL);
  }
  static int ConvLen(const WCHAR* s, int nSrc) {
    return WideCharToMultiByte(CP_ACP, 0, s, nSrc, NULL, 0, NULL, NULL);
  }
  static void Upper(char* p, int n) { CharUpperBuffA(p, n); }
  static void Lower(char* p, int n) { CharLowerBuffA(p, n); }
  static int Collate(const char* a, const char* b) { return lstrcmpA(a, b); }
  static int CollateNoCase(const char* a, const char* b) { return lstrcmpiA(a, b); }
  static int VFormat(char* buf, int n, const char* f, va_list a) {
    return AfxVFormatA(buf, n, f, a);
  }
  static int LoadStr(HINSTANCE h, UINT id, char* buf, int n) {
    return LoadStringA(h, id, buf, n);
  }
  static DWORD GetEnv(const char* name, char* buf, DWORD n) {
    return GetEnvironmentVariableA(name, buf, n);
  }
  static DWORD FmtMsg(DWORD flags, const void* src, DWORD id, char** out, va_list* args) {
    return FormatMessageA(flags, src, id, 0, (char*)out, 0, args);
  }
};

template <> struct ChTraitsMFC<WCHAR> {
  typedef WCHAR XCHAR;
  typedef char YCHAR;
  static int Len(const WCHAR* p) { return p ? lstrlenW(p) : 0; }
  static int LenOther(const char* p) {
    return p ? MultiByteToWideChar(CP_ACP, 0, p, -1, NULL, 0) - 1 : 0;
  }
  static int ConvertFrom(WCHAR* d, int nDest, const char* s, int nSrc) {
    return MultiByteToWideChar(CP_ACP, 0, s, nSrc, d, nDest);
  }
  static int ConvLen(const char* s, int nSrc) {
    return MultiByteToWideChar(CP_ACP, 0, s, nSrc, NULL, 0);
  }
  static void Upper(WCHAR* p, int n) { CharUpperBuffW(p, n); }
  static void Lower(WCHAR* p, int n) { CharLowerBuffW(p, n); }
  static int Collate(const WCHAR* a, const WCHAR* b) { return lstrcmpW(a, b); }
  static int CollateNoCase(const WCHAR* a, const WCHAR* b) { return lstrcmpiW(a, b); }
  static int VFormat(WCHAR* buf, int n, const WCHAR* f, va_list a) {
    return AfxVFormatW(buf, n, f, a);
  }
  static int LoadStr(HINSTANCE h, UINT id, WCHAR* buf, int n) {
    return LoadStringW(h, id, buf, n);
  }
  static DWORD GetEnv(const WCHAR* name, WCHAR* buf, DWORD n) {
    return GetEnvironmentVariableW(name, buf, n);
  }
  static DWORD FmtMsg(DWORD flags, const void* src, DWORD id, WCHAR** out, va_list* args) {
    return FormatMessageW(flags, src, id, 0, (WCHAR*)out, 0, args);
  }
};

template <typename BaseType>
class CStringT {
public:
  typedef BaseType XCHAR;
  typedef XCHAR* PXSTR;
  typedef const XCHAR* PCXSTR;
  typedef typename ChTraitsMFC<BaseType>::YCHAR YCHAR;
  typedef YCHAR* PYSTR;
  typedef const YCHAR* PCYSTR;
  typedef ChTraitsMFC<BaseType> Tr;

  // Construction
  CStringT() { Init(); }
  CStringT(const CStringT& s) {
    CStringData* p = s.GetData();
    if (p->nRefs >= 0) {
      m_pszData = s.m_pszData;
      InterlockedIncrement(&p->nRefs);
    } else {
      Init();
      SetString(s.m_pszData, p->nDataLength);
    }
  }
  CStringT(PCXSTR psz) {
    Init();
    if (psz != NULL && IS_INTRESOURCE(psz))
      LoadString((UINT)(UINT_PTR)psz);
    else
      SetString(psz, Tr::Len(psz));
  }
  CStringT(PCYSTR psz) {
    Init();
    if (psz != NULL && IS_INTRESOURCE(psz))
      LoadString((UINT)(UINT_PTR)psz);
    else
      AssignOther(psz, -1);
  }
  CStringT(const unsigned char* psz) {
    Init();
    *this = (const char*)psz;
  }
  CStringT(PCXSTR pch, int nLength) { Init(); SetString(pch, nLength); }
  CStringT(PCYSTR pch, int nLength) { Init(); AssignOther(pch, nLength); }
  explicit CStringT(XCHAR ch, int nRepeat = 1) {
    Init();
    if (nRepeat > 0) {
      PXSTR p = GetBuffer(nRepeat);
      for (int i = 0; i < nRepeat; i++) p[i] = ch;
      ReleaseBufferSetLength(nRepeat);
    }
  }
  explicit CStringT(YCHAR ch, int nRepeat = 1) {
    Init();
    if (nRepeat > 0) {
      YCHAR s[2] = {ch, 0};
      CStringT one(s);
      XCHAR c = one.IsEmpty() ? XCHAR(0) : one[0];
      PXSTR p = GetBuffer(nRepeat);
      for (int i = 0; i < nRepeat; i++) p[i] = c;
      ReleaseBufferSetLength(nRepeat);
    }
  }
  template <typename Other>
  CStringT(const CStringT<Other>& s) {
    Init();
    AssignOther(s.GetString(), s.GetLength());
  }
  ~CStringT() { AfxStrRelease(GetData()); }

  // Assignment
  CStringT& operator=(const CStringT& s) {
    if (m_pszData != s.m_pszData) {
      CStringData* p = s.GetData();
      if (p->nRefs >= 0 && GetData()->nRefs >= 0) {
        InterlockedIncrement(&p->nRefs);
        AfxStrRelease(GetData());
        m_pszData = s.m_pszData;
      } else {
        SetString(s.m_pszData, p->nDataLength);
      }
    }
    return *this;
  }
  template <typename Other>
  CStringT& operator=(const CStringT<Other>& s) {
    AssignOther(s.GetString(), s.GetLength());
    return *this;
  }
  CStringT& operator=(PCXSTR psz) { SetString(psz, Tr::Len(psz)); return *this; }
  CStringT& operator=(PCYSTR psz) { AssignOther(psz, -1); return *this; }
  CStringT& operator=(const unsigned char* psz) { return *this = (const char*)psz; }
  CStringT& operator=(XCHAR ch) { SetString(&ch, 1); return *this; }
  CStringT& operator=(YCHAR ch) { YCHAR s[2] = {ch, 0}; return *this = s; }

  CStringT& operator+=(const CStringT& s) { Append(s.m_pszData, s.GetLength()); return *this; }
  CStringT& operator+=(PCXSTR psz) { Append(psz, Tr::Len(psz)); return *this; }
  CStringT& operator+=(PCYSTR psz) { CStringT t(psz); return *this += t; }
  template <typename Other>
  CStringT& operator+=(const CStringT<Other>& s) { CStringT t(s); return *this += t; }
  CStringT& operator+=(XCHAR ch) { Append(&ch, 1); return *this; }
  CStringT& operator+=(YCHAR ch) { CStringT t(ch); return *this += t; }

  // Attributes
  int GetLength() const { return GetData()->nDataLength; }
  int GetAllocLength() const { return GetData()->nAllocLength; }
  bool IsEmpty() const { return GetLength() == 0; }
  void Empty() {
    CStringData* p = GetData();
    if (p->nDataLength == 0) return;
    if (p->nRefs > 1 || p->nRefs == 0) {
      AfxStrRelease(p);
      Init();
    } else if (p->nRefs < 0 && p != AfxStrNil()) {
      m_pszData[0] = 0;
      p->nDataLength = 0;
    } else {
      AfxStrRelease(p);
      Init();
    }
  }
  XCHAR GetAt(int i) const { return m_pszData[i]; }
  XCHAR operator[](int i) const { return m_pszData[i]; }
  void SetAt(int i, XCHAR ch) {
    if (i < 0 || i >= GetLength()) AfxThrowInvalidArgException();
    PXSTR p = GetBuffer();
    p[i] = ch;
    ReleaseBufferSetLength(GetLength());
  }
  operator PCXSTR() const { return m_pszData; }
  PCXSTR GetString() const { return m_pszData; }

  void SetString(PCXSTR psz) { SetString(psz, Tr::Len(psz)); }
  void SetString(PCXSTR psz, int nLength) {
    if (nLength <= 0 || psz == NULL) {
      Empty();
      return;
    }
    // psz may point into our own buffer.
    UINT_PTR off = (UINT_PTR)(psz - m_pszData);
    PXSTR pOld = m_pszData;
    bool inside = psz >= pOld && off <= (UINT_PTR)GetLength();
    PXSTR p = GetBuffer(nLength);
    if (inside)
      memmove(p, p + off, nLength * sizeof(XCHAR));
    else
      memcpy(p, psz, nLength * sizeof(XCHAR));
    ReleaseBufferSetLength(nLength);
  }
  void Append(PCXSTR psz, int nLength) {
    if (psz == NULL || nLength <= 0) return;
    int nOld = GetLength();
    UINT_PTR off = (UINT_PTR)(psz - m_pszData);
    bool inside = psz >= m_pszData && off <= (UINT_PTR)nOld;
    PXSTR p = GetBuffer(nOld + nLength);
    memmove(p + nOld, inside ? p + off : psz, nLength * sizeof(XCHAR));
    ReleaseBufferSetLength(nOld + nLength);
  }
  void Append(PCXSTR psz) { Append(psz, Tr::Len(psz)); }
  void Append(const CStringT& s) { Append(s.m_pszData, s.GetLength()); }
  void AppendChar(XCHAR ch) { Append(&ch, 1); }
  static void CopyChars(XCHAR* d, const XCHAR* s, int n) { memcpy(d, s, n * sizeof(XCHAR)); }
  static void CopyChars(XCHAR* d, size_t, const XCHAR* s, int n) { memcpy(d, s, n * sizeof(XCHAR)); }
  static void CopyCharsOverlapped(XCHAR* d, const XCHAR* s, int n) { memmove(d, s, n * sizeof(XCHAR)); }
  static int StringLength(PCXSTR psz) { return Tr::Len(psz); }

  // Comparison
  int Compare(PCXSTR psz) const {
    PCXSTR a = m_pszData;
    if (!psz) psz = EmptyChars();
    while (*a && *a == *psz) { a++; psz++; }
    return (int)(typename AfxUnsignedCh<XCHAR>::T)*a - (int)(typename AfxUnsignedCh<XCHAR>::T)*psz;
  }
  int CompareNoCase(PCXSTR psz) const {
    PCXSTR a = m_pszData;
    if (!psz) psz = EmptyChars();
    for (;; a++, psz++) {
      unsigned x = (typename AfxUnsignedCh<XCHAR>::T)*a, y = (typename AfxUnsignedCh<XCHAR>::T)*psz;
      if (x >= 'A' && x <= 'Z') x += 'a' - 'A';
      if (y >= 'A' && y <= 'Z') y += 'a' - 'A';
      if (x != y || !x) return (int)x - (int)y;
    }
  }
  int Collate(PCXSTR psz) const { return Tr::Collate(m_pszData, psz ? psz : EmptyChars()); }
  int CollateNoCase(PCXSTR psz) const { return Tr::CollateNoCase(m_pszData, psz ? psz : EmptyChars()); }

  // Extraction
  CStringT Mid(int nFirst) const { return Mid(nFirst, GetLength() - nFirst); }
  CStringT Mid(int nFirst, int nCount) const {
    if (nFirst < 0) nFirst = 0;
    if (nCount < 0) nCount = 0;
    int nLen = GetLength();
    if (nFirst > nLen) nFirst = nLen;
    if (nCount > nLen - nFirst) nCount = nLen - nFirst;
    if (nFirst == 0 && nCount == nLen) return *this;
    return CStringT(m_pszData + nFirst, nCount);
  }
  CStringT Left(int nCount) const {
    if (nCount < 0) nCount = 0;
    if (nCount >= GetLength()) return *this;
    return CStringT(m_pszData, nCount);
  }
  CStringT Right(int nCount) const {
    if (nCount < 0) nCount = 0;
    int nLen = GetLength();
    if (nCount >= nLen) return *this;
    return CStringT(m_pszData + nLen - nCount, nCount);
  }
  CStringT SpanIncluding(PCXSTR pszCharSet) const {
    int n = 0;
    while (m_pszData[n] && FindCh(pszCharSet, m_pszData[n])) n++;
    return Left(n);
  }
  CStringT SpanExcluding(PCXSTR pszCharSet) const {
    int n = 0;
    while (m_pszData[n] && !FindCh(pszCharSet, m_pszData[n])) n++;
    return Left(n);
  }

  // Changes
  CStringT& MakeUpper() {
    int n = GetLength();
    if (n) { Tr::Upper(GetBuffer(), n); ReleaseBufferSetLength(n); }
    return *this;
  }
  CStringT& MakeLower() {
    int n = GetLength();
    if (n) { Tr::Lower(GetBuffer(), n); ReleaseBufferSetLength(n); }
    return *this;
  }
  CStringT& MakeReverse() {
    int n = GetLength();
    PXSTR p = GetBuffer();
    for (int i = 0, j = n - 1; i < j; i++, j--) { XCHAR t = p[i]; p[i] = p[j]; p[j] = t; }
    ReleaseBufferSetLength(n);
    return *this;
  }
  int Replace(XCHAR chOld, XCHAR chNew) {
    int nCount = 0, n = GetLength();
    if (chOld == chNew) return 0;
    for (int i = 0; i < n; i++) if (m_pszData[i] == chOld) nCount++;
    if (nCount) {
      PXSTR p = GetBuffer();
      for (int i = 0; i < n; i++) if (p[i] == chOld) p[i] = chNew;
      ReleaseBufferSetLength(n);
    }
    return nCount;
  }
  int Replace(PCXSTR pszOld, PCXSTR pszNew) {
    int nOld = Tr::Len(pszOld);
    if (nOld == 0) return 0;
    int nNew = Tr::Len(pszNew);
    int nCount = 0;
    for (int i = Find(pszOld); i >= 0; i = Find(pszOld, i + nOld)) nCount++;
    if (!nCount) return 0;
    CStringT r;
    PXSTR d = r.GetBuffer(GetLength() + nCount * (nNew - nOld));
    int pos = 0, out = 0;
    for (int i = Find(pszOld); i >= 0; i = Find(pszOld, i + nOld)) {
      memcpy(d + out, m_pszData + pos, (i - pos) * sizeof(XCHAR));
      out += i - pos;
      if (nNew) memcpy(d + out, pszNew, nNew * sizeof(XCHAR));
      out += nNew;
      pos = i + nOld;
    }
    memcpy(d + out, m_pszData + pos, (GetLength() - pos) * sizeof(XCHAR));
    out += GetLength() - pos;
    r.ReleaseBufferSetLength(out);
    *this = r;
    return nCount;
  }
  int Remove(XCHAR chRemove) {
    int n = GetLength();
    PXSTR p = GetBuffer();
    int j = 0;
    for (int i = 0; i < n; i++) if (p[i] != chRemove) p[j++] = p[i];
    ReleaseBufferSetLength(j);
    return n - j;
  }
  int Insert(int nIndex, XCHAR ch) { return Insert(nIndex, CStringT(ch, 1)); }
  int Insert(int nIndex, PCXSTR psz) {
    CStringT ins(psz);
    int n = GetLength(), m = ins.GetLength();
    if (nIndex < 0) nIndex = 0;
    if (nIndex > n) nIndex = n;
    if (m) {
      PXSTR p = GetBuffer(n + m);
      memmove(p + nIndex + m, p + nIndex, (n - nIndex) * sizeof(XCHAR));
      memcpy(p + nIndex, ins.m_pszData, m * sizeof(XCHAR));
      ReleaseBufferSetLength(n + m);
    }
    return n + m;
  }
  int Delete(int nIndex, int nCount = 1) {
    int n = GetLength();
    if (nIndex < 0) nIndex = 0;
    if (nCount > 0 && nIndex < n) {
      if (nCount > n - nIndex) nCount = n - nIndex;
      PXSTR p = GetBuffer();
      memmove(p + nIndex, p + nIndex + nCount, (n - nIndex - nCount) * sizeof(XCHAR));
      ReleaseBufferSetLength(n - nCount);
    }
    return GetLength();
  }

  CStringT& TrimRight() {
    int n = GetLength();
    while (n > 0 && IsSpace(m_pszData[n - 1])) n--;
    if (n != GetLength()) Truncate(n);
    return *this;
  }
  CStringT& TrimRight(XCHAR ch) {
    int n = GetLength();
    while (n > 0 && m_pszData[n - 1] == ch) n--;
    if (n != GetLength()) Truncate(n);
    return *this;
  }
  CStringT& TrimRight(PCXSTR set) {
    int n = GetLength();
    while (n > 0 && FindCh(set, m_pszData[n - 1])) n--;
    if (n != GetLength()) Truncate(n);
    return *this;
  }
  CStringT& TrimLeft() {
    int i = 0;
    while (m_pszData[i] && IsSpace(m_pszData[i])) i++;
    if (i) Delete(0, i);
    return *this;
  }
  CStringT& TrimLeft(XCHAR ch) {
    int i = 0;
    while (m_pszData[i] && m_pszData[i] == ch) i++;
    if (i) Delete(0, i);
    return *this;
  }
  CStringT& TrimLeft(PCXSTR set) {
    int i = 0;
    while (m_pszData[i] && FindCh(set, m_pszData[i])) i++;
    if (i) Delete(0, i);
    return *this;
  }
  CStringT& Trim() { return TrimRight().TrimLeft(); }
  CStringT& Trim(XCHAR ch) { return TrimRight(ch).TrimLeft(ch); }
  CStringT& Trim(PCXSTR set) { return TrimRight(set).TrimLeft(set); }
  void Truncate(int nNewLength) {
    if (nNewLength < GetLength()) {
      GetBuffer();
      ReleaseBufferSetLength(nNewLength);
    }
  }

  // Searching
  int Find(XCHAR ch, int iStart = 0) const {
    int n = GetLength();
    if (iStart < 0) iStart = 0;
    for (int i = iStart; i < n; i++) if (m_pszData[i] == ch) return i;
    return -1;
  }
  int Find(PCXSTR psz, int iStart = 0) const {
    int n = GetLength(), m = Tr::Len(psz);
    if (iStart < 0) iStart = 0;
    if (iStart > n) return -1;
    for (int i = iStart; i + m <= n; i++)
      if (memcmp(m_pszData + i, psz, m * sizeof(XCHAR)) == 0) return i;
    return -1;
  }
  int ReverseFind(XCHAR ch) const {
    for (int i = GetLength() - 1; i >= 0; i--) if (m_pszData[i] == ch) return i;
    return -1;
  }
  int FindOneOf(PCXSTR pszCharSet) const {
    for (int i = 0; m_pszData[i]; i++) if (FindCh(pszCharSet, m_pszData[i])) return i;
    return -1;
  }
  CStringT Tokenize(PCXSTR pszTokens, int& iStart) const {
    if (iStart < 0) return CStringT();
    int n = GetLength();
    if (!pszTokens || !*pszTokens) {
      if (iStart < n) return Mid(iStart);
    } else {
      int i = iStart;
      while (i < n && FindCh(pszTokens, m_pszData[i])) i++;
      if (i < n) {
        int j = i;
        while (j < n && !FindCh(pszTokens, m_pszData[j])) j++;
        iStart = j + 1;
        return Mid(i, j - i);
      }
    }
    iStart = -1;
    return CStringT();
  }

  // Formatting
  void AFX_CDECL Format(PCXSTR pszFormat, ...) {
    va_list a;
    va_start(a, pszFormat);
    FormatV(pszFormat, a);
    va_end(a);
  }
  void AFX_CDECL Format(UINT nFormatID, ...) {
    CStringT f;
    f.LoadString(nFormatID);
    va_list a;
    va_start(a, nFormatID);
    FormatV(f, a);
    va_end(a);
  }
  void AFX_CDECL AppendFormat(PCXSTR pszFormat, ...) {
    va_list a;
    va_start(a, pszFormat);
    AppendFormatV(pszFormat, a);
    va_end(a);
  }
  void AFX_CDECL AppendFormat(UINT nFormatID, ...) {
    CStringT f;
    f.LoadString(nFormatID);
    va_list a;
    va_start(a, nFormatID);
    AppendFormatV(f, a);
    va_end(a);
  }
  void FormatV(PCXSTR pszFormat, va_list args) {
    CStringT r;
    r.AppendFormatV(pszFormat, args);
    *this = r;
  }
  void AppendFormatV(PCXSTR pszFormat, va_list args) {
    va_list a;
    va_copy(a, args);
    int n = Tr::VFormat(NULL, 0, pszFormat, a);
    va_end(a);
    if (n <= 0) return;
    int nOld = GetLength();
    PXSTR p = GetBuffer(nOld + n);
    Tr::VFormat(p + nOld, n + 1, pszFormat, args);
    ReleaseBufferSetLength(nOld + n);
  }
  void AFX_CDECL FormatMessage(PCXSTR pszFormat, ...) {
    va_list a;
    va_start(a, pszFormat);
    FormatMessageV(pszFormat, &a);
    va_end(a);
  }
  void AFX_CDECL FormatMessage(UINT nFormatID, ...) {
    CStringT f;
    f.LoadString(nFormatID);
    va_list a;
    va_start(a, nFormatID);
    FormatMessageV(f, &a);
    va_end(a);
  }
  void FormatMessageV(PCXSTR pszFormat, va_list* pArgs) {
    PXSTR p = NULL;
    if (Tr::FmtMsg(FORMAT_MESSAGE_FROM_STRING | FORMAT_MESSAGE_ALLOCATE_BUFFER,
                   pszFormat, 0, &p, pArgs) == 0 || p == NULL)
      AfxThrowMemoryException();
    *this = p;
    LocalFree(p);
  }

  BOOL LoadString(UINT nID) { return LoadString(AfxGetResourceHandle(), nID); }
  BOOL LoadString(HINSTANCE hInstance, UINT nID) {
    XCHAR buf[256];
    int n = Tr::LoadStr(hInstance, nID, buf, 256);
    if (n < 255) {
      SetString(buf, n);
      return n > 0;
    }
    for (int size = 512;; size *= 2) {
      PXSTR p = GetBuffer(size - 1);
      n = Tr::LoadStr(hInstance, nID, p, size);
      if (n < size - 1) {
        ReleaseBufferSetLength(n);
        return TRUE;
      }
      ReleaseBuffer(0);
    }
  }
  BOOL LoadString(HINSTANCE hInstance, UINT nID, WORD) { return LoadString(hInstance, nID); }
  BOOL GetEnvironmentVariable(PCXSTR pszVar) {
    DWORD n = Tr::GetEnv(pszVar, NULL, 0);
    if (!n) { Empty(); return FALSE; }
    PXSTR p = GetBuffer(n);
    Tr::GetEnv(pszVar, p, n);
    ReleaseBuffer();
    return TRUE;
  }
  void AnsiToOem() {}
  void OemToAnsi() {}

  // Buffer access
  PXSTR GetBuffer() { return GetBuffer(GetLength()); }
  PXSTR GetBuffer(int nMinBufLength) {
    CStringData* p = GetData();
    if (nMinBufLength < p->nDataLength) nMinBufLength = p->nDataLength;
    if (p->nRefs > 1 || p == AfxStrNil() || nMinBufLength > p->nAllocLength) {
      int nAlloc = nMinBufLength;
      if (p->nRefs <= 1 && p != AfxStrNil() && nAlloc > p->nAllocLength) {
        // Growing a string of our own: leave room to grow some more.
        int grow = p->nAllocLength / 2;
        if (nAlloc < p->nAllocLength + grow) nAlloc = p->nAllocLength + grow;
      }
      CStringData* q = AfxStrAlloc(nAlloc, sizeof(XCHAR));
      memcpy(q->data(), m_pszData, (p->nDataLength + 1) * sizeof(XCHAR));
      q->nDataLength = p->nDataLength;
      AfxStrRelease(p);
      m_pszData = (PXSTR)q->data();
    }
    return m_pszData;
  }
  PXSTR GetBufferSetLength(int nNewLength) {
    PXSTR p = GetBuffer(nNewLength);
    ReleaseBufferSetLength(nNewLength);
    return p;
  }
  void ReleaseBuffer(int nNewLength = -1) {
    if (nNewLength < 0) nNewLength = Tr::Len(m_pszData);
    ReleaseBufferSetLength(nNewLength);
  }
  void ReleaseBufferSetLength(int nNewLength) {
    CStringData* p = GetData();
    if (p == AfxStrNil()) return;
    if (nNewLength > p->nAllocLength) nNewLength = p->nAllocLength;
    p->nDataLength = nNewLength;
    m_pszData[nNewLength] = 0;
  }
  void FreeExtra() {
    CStringData* p = GetData();
    if (p->nDataLength != p->nAllocLength && p != AfxStrNil()) {
      CStringT t(m_pszData, p->nDataLength);
      *this = t;
    }
  }
  void Preallocate(int nLength) {
    int n = GetLength();
    GetBuffer(nLength);
    ReleaseBufferSetLength(n);
  }
  PXSTR LockBuffer() {
    PXSTR p = GetBuffer();
    GetData()->nRefs = -1;
    return p;
  }
  void UnlockBuffer() {
    CStringData* p = GetData();
    if (p != AfxStrNil()) p->nRefs = 1;
  }

#ifdef __OLEAUTO_H__
  BSTR AllocSysString() const {
    CStringT<WCHAR> w(*this);
    return ::SysAllocStringLen(w.GetString(), w.GetLength());
  }
  BSTR SetSysString(BSTR* pbstr) const {
    CStringT<WCHAR> w(*this);
    ::SysReAllocStringLen(pbstr, w.GetString(), w.GetLength());
    return *pbstr;
  }
#endif

  // Concatenation and comparison
  friend CStringT operator+(const CStringT& a, const CStringT& b) {
    CStringT r(a); r.Append(b); return r;
  }
  friend CStringT operator+(const CStringT& a, PCXSTR b) {
    CStringT r(a); r.Append(b); return r;
  }
  friend CStringT operator+(PCXSTR a, const CStringT& b) {
    CStringT r(a); r.Append(b); return r;
  }
  friend CStringT operator+(const CStringT& a, PCYSTR b) {
    CStringT r(a); r += b; return r;
  }
  friend CStringT operator+(PCYSTR a, const CStringT& b) {
    CStringT r(a); r.Append(b); return r;
  }
  friend CStringT operator+(const CStringT& a, XCHAR ch) {
    CStringT r(a); r.AppendChar(ch); return r;
  }
  friend CStringT operator+(XCHAR ch, const CStringT& b) {
    CStringT r(ch, 1); r.Append(b); return r;
  }
  friend CStringT operator+(const CStringT& a, YCHAR ch) {
    CStringT r(a); r += ch; return r;
  }
  friend CStringT operator+(YCHAR ch, const CStringT& b) {
    CStringT r(ch, 1); r.Append(b); return r;
  }

  friend bool operator==(const CStringT& a, const CStringT& b) { return a.Compare(b) == 0; }
  friend bool operator==(const CStringT& a, PCXSTR b) { return a.Compare(b) == 0; }
  friend bool operator==(PCXSTR a, const CStringT& b) { return b.Compare(a) == 0; }
  friend bool operator==(const CStringT& a, PCYSTR b) { return a.Compare(CStringT(b)) == 0; }
  friend bool operator==(PCYSTR a, const CStringT& b) { return b.Compare(CStringT(a)) == 0; }
  friend bool operator==(const CStringT& a, XCHAR b) { return a.GetLength() == 1 && a[0] == b; }
  friend bool operator==(XCHAR a, const CStringT& b) { return b.GetLength() == 1 && b[0] == a; }
  friend bool operator!=(const CStringT& a, const CStringT& b) { return a.Compare(b) != 0; }
  friend bool operator!=(const CStringT& a, PCXSTR b) { return a.Compare(b) != 0; }
  friend bool operator!=(PCXSTR a, const CStringT& b) { return b.Compare(a) != 0; }
  friend bool operator!=(const CStringT& a, PCYSTR b) { return a.Compare(CStringT(b)) != 0; }
  friend bool operator!=(PCYSTR a, const CStringT& b) { return b.Compare(CStringT(a)) != 0; }
  friend bool operator!=(const CStringT& a, XCHAR b) { return !(a == b); }
  friend bool operator!=(XCHAR a, const CStringT& b) { return !(b == a); }
  friend bool operator<(const CStringT& a, const CStringT& b) { return a.Compare(b) < 0; }
  friend bool operator<(const CStringT& a, PCXSTR b) { return a.Compare(b) < 0; }
  friend bool operator<(PCXSTR a, const CStringT& b) { return b.Compare(a) > 0; }
  friend bool operator>(const CStringT& a, const CStringT& b) { return a.Compare(b) > 0; }
  friend bool operator>(const CStringT& a, PCXSTR b) { return a.Compare(b) > 0; }
  friend bool operator>(PCXSTR a, const CStringT& b) { return b.Compare(a) < 0; }
  friend bool operator<=(const CStringT& a, const CStringT& b) { return a.Compare(b) <= 0; }
  friend bool operator<=(const CStringT& a, PCXSTR b) { return a.Compare(b) <= 0; }
  friend bool operator<=(PCXSTR a, const CStringT& b) { return b.Compare(a) >= 0; }
  friend bool operator>=(const CStringT& a, const CStringT& b) { return a.Compare(b) >= 0; }
  friend bool operator>=(const CStringT& a, PCXSTR b) { return a.Compare(b) >= 0; }
  friend bool operator>=(PCXSTR a, const CStringT& b) { return b.Compare(a) <= 0; }

private:
  PXSTR m_pszData;

  CStringData* GetData() const { return ((CStringData*)m_pszData) - 1; }
  void Init() { m_pszData = (PXSTR)AfxStrNil()->data(); }
  static PCXSTR EmptyChars() { return (PCXSTR)AfxStrNil()->data(); }
  static bool FindCh(PCXSTR set, XCHAR ch) {
    if (!set) return false;
    for (; *set; set++) if (*set == ch) return true;
    return false;
  }
  static bool IsSpace(XCHAR ch) {
    return ch == ' ' || (ch >= 9 && ch <= 13);
  }
  void AssignOther(PCYSTR psz, int nLength) {
    if (psz == NULL || nLength == 0) { Empty(); return; }
    int n = Tr::ConvLen(psz, nLength);
    if (nLength < 0) n--;  // ConvLen counted the terminating zero
    if (n <= 0) { Empty(); return; }
    // psz is never inside our buffer: it has the other character type.
    PXSTR p = GetBuffer(n);
    Tr::ConvertFrom(p, nLength < 0 ? n + 1 : n, psz, nLength);
    ReleaseBufferSetLength(n);
  }
};

typedef CStringT<char> CStringA;
typedef CStringT<WCHAR> CStringW;
#ifdef _UNICODE
typedef CStringW CString;
#else
typedef CStringA CString;
#endif
typedef CString CAfxString;

template <typename T> class CSimpleStringT;
typedef CString CSimpleString;

// String conversion helpers in the style of ATL's CA2T and friends.
class CA2W {
public:
  CA2W(LPCSTR psz, UINT = CP_ACP) : m_s(psz) {}
  operator LPWSTR() const { return (LPWSTR)m_s.GetString(); }
  CStringW m_s;
};
class CW2A {
public:
  CW2A(LPCWSTR psz, UINT = CP_ACP) : m_s(psz) {}
  operator LPSTR() const { return (LPSTR)m_s.GetString(); }
  CStringA m_s;
};
#ifdef _UNICODE
typedef CA2W CA2T;
typedef CW2A CT2A;
class CW2T { public: CW2T(LPCWSTR p, UINT = 0) : m(p) {} operator LPWSTR() const { return (LPWSTR)m; } LPCWSTR m; };
typedef CW2T CT2W;
typedef CW2T CT2CW;
typedef CW2T CW2CT;
typedef CA2W CA2CT;
typedef CW2A CT2CA;
#else
typedef CW2A CW2T;
typedef CA2W CT2W;
class CA2T { public: CA2T(LPCSTR p, UINT = 0) : m(p) {} operator LPSTR() const { return (LPSTR)m; } LPCSTR m; };
typedef CA2T CT2A;
typedef CA2T CT2CA;
typedef CA2T CA2CT;
typedef CW2A CW2CT;
typedef CA2W CT2CW;
#endif

#endif // __AFXSTR_H__
