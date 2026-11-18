// atlpath.h - CPath (CPathT over a CString), a file path with the
// shell's path functions (shlwapi's Path*) as members, and the ATLPath
// namespace of the same functions for either character type.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#ifndef __ATLPATH_H__
#define __ATLPATH_H__

#include <atlbase.h>
#ifndef __AFX_H__
#include <atlstr.h>
#endif
#include <shlwapi.h>

#if !defined(_ATL_NO_DEFAULT_LIBS) && defined(_WIN32)
__asm__(".section .drectve,\"yn\"\n"
        ".ascii \" -defaultlib:shlwapi\"\n"
        ".text");
#endif

namespace ATL {

// The shell's path functions by character type.
namespace ATLPath {

inline char* AddBackslash(char* psz) { return ::PathAddBackslashA(psz); }
inline wchar_t* AddBackslash(wchar_t* psz) { return ::PathAddBackslashW(psz); }
inline BOOL AddExtension(char* psz, const char* pszExt) { return ::PathAddExtensionA(psz, pszExt); }
inline BOOL AddExtension(wchar_t* psz, const wchar_t* pszExt) { return ::PathAddExtensionW(psz, pszExt); }
inline BOOL Append(char* psz, const char* pszMore) { return ::PathAppendA(psz, pszMore); }
inline BOOL Append(wchar_t* psz, const wchar_t* pszMore) { return ::PathAppendW(psz, pszMore); }
inline char* BuildRoot(char* psz, int iDrive) { return ::PathBuildRootA(psz, iDrive); }
inline wchar_t* BuildRoot(wchar_t* psz, int iDrive) { return ::PathBuildRootW(psz, iDrive); }
inline BOOL Canonicalize(char* pszDest, const char* psz) { return ::PathCanonicalizeA(pszDest, psz); }
inline BOOL Canonicalize(wchar_t* pszDest, const wchar_t* psz) { return ::PathCanonicalizeW(pszDest, psz); }
inline char* Combine(char* pszDest, const char* pszDir, const char* pszFile) {
  return ::PathCombineA(pszDest, pszDir, pszFile);
}
inline wchar_t* Combine(wchar_t* pszDest, const wchar_t* pszDir, const wchar_t* pszFile) {
  return ::PathCombineW(pszDest, pszDir, pszFile);
}
inline int CommonPrefix(const char* psz1, const char* psz2, char* pszCommon) {
  return ::PathCommonPrefixA(psz1, psz2, pszCommon);
}
inline int CommonPrefix(const wchar_t* psz1, const wchar_t* psz2, wchar_t* pszCommon) {
  return ::PathCommonPrefixW(psz1, psz2, pszCommon);
}
inline BOOL CompactPath(HDC hDC, char* psz, UINT dx) { return ::PathCompactPathA(hDC, psz, dx); }
inline BOOL CompactPath(HDC hDC, wchar_t* psz, UINT dx) { return ::PathCompactPathW(hDC, psz, dx); }
inline BOOL CompactPathEx(char* pszDest, const char* psz, UINT nMaxChars, DWORD dwFlags) {
  return ::PathCompactPathExA(pszDest, psz, nMaxChars, dwFlags);
}
inline BOOL CompactPathEx(wchar_t* pszDest, const wchar_t* psz, UINT nMaxChars, DWORD dwFlags) {
  return ::PathCompactPathExW(pszDest, psz, nMaxChars, dwFlags);
}
inline BOOL FileExists(const char* psz) { return ::PathFileExistsA(psz); }
inline BOOL FileExists(const wchar_t* psz) { return ::PathFileExistsW(psz); }
inline char* FindExtension(const char* psz) { return ::PathFindExtensionA(psz); }
inline wchar_t* FindExtension(const wchar_t* psz) { return ::PathFindExtensionW(psz); }
inline char* FindFileName(const char* psz) { return ::PathFindFileNameA(psz); }
inline wchar_t* FindFileName(const wchar_t* psz) { return ::PathFindFileNameW(psz); }
inline int GetDriveNumber(const char* psz) { return ::PathGetDriveNumberA(psz); }
inline int GetDriveNumber(const wchar_t* psz) { return ::PathGetDriveNumberW(psz); }
inline BOOL IsDirectory(const char* psz) { return ::PathIsDirectoryA(psz); }
inline BOOL IsDirectory(const wchar_t* psz) { return ::PathIsDirectoryW(psz); }
inline BOOL IsFileSpec(const char* psz) { return ::PathIsFileSpecA(psz); }
inline BOOL IsFileSpec(const wchar_t* psz) { return ::PathIsFileSpecW(psz); }
inline BOOL IsPrefix(const char* pszPrefix, const char* psz) { return ::PathIsPrefixA(pszPrefix, psz); }
inline BOOL IsPrefix(const wchar_t* pszPrefix, const wchar_t* psz) { return ::PathIsPrefixW(pszPrefix, psz); }
inline BOOL IsRelative(const char* psz) { return ::PathIsRelativeA(psz); }
inline BOOL IsRelative(const wchar_t* psz) { return ::PathIsRelativeW(psz); }
inline BOOL IsRoot(const char* psz) { return ::PathIsRootA(psz); }
inline BOOL IsRoot(const wchar_t* psz) { return ::PathIsRootW(psz); }
inline BOOL IsSameRoot(const char* psz1, const char* psz2) { return ::PathIsSameRootA(psz1, psz2); }
inline BOOL IsSameRoot(const wchar_t* psz1, const wchar_t* psz2) { return ::PathIsSameRootW(psz1, psz2); }
inline BOOL IsUNC(const char* psz) { return ::PathIsUNCA(psz); }
inline BOOL IsUNC(const wchar_t* psz) { return ::PathIsUNCW(psz); }
inline BOOL IsUNCServer(const char* psz) { return ::PathIsUNCServerA(psz); }
inline BOOL IsUNCServer(const wchar_t* psz) { return ::PathIsUNCServerW(psz); }
inline BOOL IsUNCServerShare(const char* psz) { return ::PathIsUNCServerShareA(psz); }
inline BOOL IsUNCServerShare(const wchar_t* psz) { return ::PathIsUNCServerShareW(psz); }
inline BOOL MakePretty(char* psz) { return ::PathMakePrettyA(psz); }
inline BOOL MakePretty(wchar_t* psz) { return ::PathMakePrettyW(psz); }
inline BOOL MatchSpec(const char* psz, const char* pszSpec) { return ::PathMatchSpecA(psz, pszSpec); }
inline BOOL MatchSpec(const wchar_t* psz, const wchar_t* pszSpec) { return ::PathMatchSpecW(psz, pszSpec); }
inline void QuoteSpaces(char* psz) { ::PathQuoteSpacesA(psz); }
inline void QuoteSpaces(wchar_t* psz) { ::PathQuoteSpacesW(psz); }
inline BOOL RelativePathTo(char* pszPath, const char* pszFrom, DWORD dwAttrFrom, const char* pszTo,
                           DWORD dwAttrTo) {
  return ::PathRelativePathToA(pszPath, pszFrom, dwAttrFrom, pszTo, dwAttrTo);
}
inline BOOL RelativePathTo(wchar_t* pszPath, const wchar_t* pszFrom, DWORD dwAttrFrom,
                           const wchar_t* pszTo, DWORD dwAttrTo) {
  return ::PathRelativePathToW(pszPath, pszFrom, dwAttrFrom, pszTo, dwAttrTo);
}
inline void RemoveArgs(char* psz) { ::PathRemoveArgsA(psz); }
inline void RemoveArgs(wchar_t* psz) { ::PathRemoveArgsW(psz); }
inline char* RemoveBackslash(char* psz) { return ::PathRemoveBackslashA(psz); }
inline wchar_t* RemoveBackslash(wchar_t* psz) { return ::PathRemoveBackslashW(psz); }
inline void RemoveBlanks(char* psz) { ::PathRemoveBlanksA(psz); }
inline void RemoveBlanks(wchar_t* psz) { ::PathRemoveBlanksW(psz); }
inline void RemoveExtension(char* psz) { ::PathRemoveExtensionA(psz); }
inline void RemoveExtension(wchar_t* psz) { ::PathRemoveExtensionW(psz); }
inline BOOL RemoveFileSpec(char* psz) { return ::PathRemoveFileSpecA(psz); }
inline BOOL RemoveFileSpec(wchar_t* psz) { return ::PathRemoveFileSpecW(psz); }
inline BOOL RenameExtension(char* psz, const char* pszExt) { return ::PathRenameExtensionA(psz, pszExt); }
inline BOOL RenameExtension(wchar_t* psz, const wchar_t* pszExt) {
  return ::PathRenameExtensionW(psz, pszExt);
}
inline char* SkipRoot(const char* psz) { return ::PathSkipRootA(psz); }
inline wchar_t* SkipRoot(const wchar_t* psz) { return ::PathSkipRootW(psz); }
inline void StripPath(char* psz) { ::PathStripPathA(psz); }
inline void StripPath(wchar_t* psz) { ::PathStripPathW(psz); }
inline BOOL StripToRoot(char* psz) { return ::PathStripToRootA(psz); }
inline BOOL StripToRoot(wchar_t* psz) { return ::PathStripToRootW(psz); }
inline void UnquoteSpaces(char* psz) { ::PathUnquoteSpacesA(psz); }
inline void UnquoteSpaces(wchar_t* psz) { ::PathUnquoteSpacesW(psz); }

}  // namespace ATLPath

// A path in a string. The functions that change it work in a buffer of
// MAX_PATH characters (more when the path is longer).
template <typename StringType>
class CPathT {
public:
  typedef typename StringType::XCHAR XCHAR;
  typedef typename StringType::PCXSTR PCXSTR;
  typedef typename StringType::PXSTR PXSTR;

  StringType m_strPath;

  CPathT() {}
  CPathT(const CPathT& path) : m_strPath(path.m_strPath) {}
  CPathT(PCXSTR pszPath) : m_strPath(pszPath) {}

  operator const StringType&() const { return m_strPath; }
  operator StringType&() { return m_strPath; }
  operator PCXSTR() const { return m_strPath; }
  CPathT& operator=(const CPathT& path) {
    m_strPath = path.m_strPath;
    return *this;
  }
  CPathT& operator+=(PCXSTR pszMore) {
    Append(pszMore);
    return *this;
  }

  void AddBackslash() { _Edit(0, [](PXSTR p) { ATLPath::AddBackslash(p); }); }
  BOOL AddExtension(PCXSTR pszExtension) {
    BOOL b = FALSE;
    _Edit(_Len(pszExtension), [&](PXSTR p) { b = ATLPath::AddExtension(p, pszExtension); });
    return b;
  }
  BOOL Append(PCXSTR pszMore) {
    BOOL b = FALSE;
    _Edit(_Len(pszMore) + 1, [&](PXSTR p) { b = ATLPath::Append(p, pszMore); });
    return b;
  }
  void BuildRoot(int iDrive) {
    XCHAR sz[4];
    ATLPath::BuildRoot(sz, iDrive);
    m_strPath = sz;
  }
  void Canonicalize() {
    XCHAR sz[MAX_PATH];
    if (m_strPath.GetLength() < MAX_PATH && ATLPath::Canonicalize(sz, (PCXSTR)m_strPath)) m_strPath = sz;
  }
  void Combine(PCXSTR pszDir, PCXSTR pszFile) {
    XCHAR sz[MAX_PATH];
    if (ATLPath::Combine(sz, pszDir, pszFile) != NULL) m_strPath = sz;
  }
  CPathT CommonPrefix(PCXSTR pszOther) {
    XCHAR sz[MAX_PATH];
    sz[0] = 0;
    ATLPath::CommonPrefix((PCXSTR)m_strPath, pszOther, sz);
    return CPathT(sz);
  }
  BOOL CompactPath(HDC hDC, UINT nWidth) {
    BOOL b = FALSE;
    _Edit(0, [&](PXSTR p) { b = ATLPath::CompactPath(hDC, p, nWidth); });
    return b;
  }
  BOOL CompactPathEx(UINT nMaxChars, DWORD dwFlags = 0) {
    XCHAR sz[MAX_PATH];
    if (nMaxChars > MAX_PATH) nMaxChars = MAX_PATH;
    BOOL b = ATLPath::CompactPathEx(sz, (PCXSTR)m_strPath, nMaxChars, dwFlags);
    if (b) m_strPath = sz;
    return b;
  }
  BOOL FileExists() const { return ATLPath::FileExists((PCXSTR)m_strPath); }
  int FindExtension() const {
    PCXSTR p = m_strPath;
    PCXSTR pExt = ATLPath::FindExtension(p);
    return pExt == NULL || *pExt == 0 ? -1 : int(pExt - p);
  }
  int FindFileName() const {
    PCXSTR p = m_strPath;
    PCXSTR pName = ATLPath::FindFileName(p);
    return pName == NULL || *pName == 0 ? -1 : int(pName - p);
  }
  int GetDriveNumber() const { return ATLPath::GetDriveNumber((PCXSTR)m_strPath); }
  StringType GetExtension() const {
    int i = FindExtension();
    return i < 0 ? StringType() : m_strPath.Mid(i);
  }
  BOOL IsDirectory() const { return ATLPath::IsDirectory((PCXSTR)m_strPath); }
  BOOL IsFileSpec() const { return ATLPath::IsFileSpec((PCXSTR)m_strPath); }
  BOOL IsPrefix(PCXSTR pszPrefix) const { return ATLPath::IsPrefix(pszPrefix, (PCXSTR)m_strPath); }
  BOOL IsRelative() const { return ATLPath::IsRelative((PCXSTR)m_strPath); }
  BOOL IsRoot() const { return ATLPath::IsRoot((PCXSTR)m_strPath); }
  BOOL IsSameRoot(PCXSTR pszOther) const { return ATLPath::IsSameRoot((PCXSTR)m_strPath, pszOther); }
  BOOL IsUNC() const { return ATLPath::IsUNC((PCXSTR)m_strPath); }
  BOOL IsUNCServer() const { return ATLPath::IsUNCServer((PCXSTR)m_strPath); }
  BOOL IsUNCServerShare() const { return ATLPath::IsUNCServerShare((PCXSTR)m_strPath); }
  BOOL MakePretty() {
    BOOL b = FALSE;
    _Edit(0, [&](PXSTR p) { b = ATLPath::MakePretty(p); });
    return b;
  }
  BOOL MatchSpec(PCXSTR pszSpec) const { return ATLPath::MatchSpec((PCXSTR)m_strPath, pszSpec); }
  void QuoteSpaces() { _Edit(2, [](PXSTR p) { ATLPath::QuoteSpaces(p); }); }
  BOOL RelativePathTo(PCXSTR pszFrom, DWORD dwAttrFrom, PCXSTR pszTo, DWORD dwAttrTo) {
    XCHAR sz[MAX_PATH];
    BOOL b = ATLPath::RelativePathTo(sz, pszFrom, dwAttrFrom, pszTo, dwAttrTo);
    if (b) m_strPath = sz;
    return b;
  }
  void RemoveArgs() { _Edit(0, [](PXSTR p) { ATLPath::RemoveArgs(p); }); }
  void RemoveBackslash() { _Edit(0, [](PXSTR p) { ATLPath::RemoveBackslash(p); }); }
  void RemoveBlanks() { _Edit(0, [](PXSTR p) { ATLPath::RemoveBlanks(p); }); }
  void RemoveExtension() { _Edit(0, [](PXSTR p) { ATLPath::RemoveExtension(p); }); }
  BOOL RemoveFileSpec() {
    BOOL b = FALSE;
    _Edit(0, [&](PXSTR p) { b = ATLPath::RemoveFileSpec(p); });
    return b;
  }
  BOOL RenameExtension(PCXSTR pszExtension) {
    BOOL b = FALSE;
    _Edit(_Len(pszExtension), [&](PXSTR p) { b = ATLPath::RenameExtension(p, pszExtension); });
    return b;
  }
  int SkipRoot() const {
    PCXSTR p = m_strPath;
    PCXSTR pRest = ATLPath::SkipRoot(p);
    return pRest == NULL ? -1 : int(pRest - p);
  }
  void StripPath() { _Edit(0, [](PXSTR p) { ATLPath::StripPath(p); }); }
  BOOL StripToRoot() {
    BOOL b = FALSE;
    _Edit(0, [&](PXSTR p) { b = ATLPath::StripToRoot(p); });
    return b;
  }
  void UnquoteSpaces() { _Edit(0, [](PXSTR p) { ATLPath::UnquoteSpaces(p); }); }

private:
  static int _Len(PCXSTR psz) { return StringType(psz).GetLength(); }
  // Runs f on the path in a buffer with room for nGrow more characters
  // (and at least MAX_PATH), then takes the result back.
  template <typename F>
  void _Edit(int nGrow, F f) {
    int nLen = m_strPath.GetLength();
    int nBuf = nLen + nGrow + 1;
    if (nBuf < MAX_PATH) nBuf = MAX_PATH;
    PXSTR p = m_strPath.GetBuffer(nBuf);
    f(p);
    m_strPath.ReleaseBuffer();
  }
};

typedef CPathT<CString> CPath;
typedef CPathT<CStringA> CPathA;
typedef CPathT<CStringW> CPathW;

}  // namespace ATL

#endif  // __ATLPATH_H__
