// atlfile.h - ATL's files: CAtlFile (a file handle with Create, Read,
// Write, Seek and the rest returning HRESULTs), CAtlTemporaryFile, and
// CAtlFileMapping.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#ifndef __ATLFILE_H__
#define __ATLFILE_H__

#include <atlbase.h>

namespace ATL {

class CAtlFile : public CHandle {
public:
  CAtlFile() {}
  CAtlFile(CAtlFile& file) : CHandle(file) {}
  explicit CAtlFile(HANDLE hFile) : CHandle(hFile) {}

  HRESULT Create(LPCTSTR szFilename, DWORD dwDesiredAccess, DWORD dwShareMode,
                 DWORD dwCreationDisposition, DWORD dwFlagsAndAttributes = FILE_ATTRIBUTE_NORMAL,
                 LPSECURITY_ATTRIBUTES lpsa = NULL, HANDLE hTemplateFile = NULL) {
    HANDLE hFile = ::CreateFile(szFilename, dwDesiredAccess, dwShareMode, lpsa,
                                dwCreationDisposition, dwFlagsAndAttributes, hTemplateFile);
    if (hFile == INVALID_HANDLE_VALUE) return AtlHresultFromLastError();
    Attach(hFile);
    return S_OK;
  }

  HRESULT Read(LPVOID pBuffer, DWORD nBufSize) {
    DWORD nBytesRead = 0;
    if (!::ReadFile(m_h, pBuffer, nBufSize, &nBytesRead, NULL)) return AtlHresultFromLastError();
    if (nBytesRead != nBufSize) return HRESULT_FROM_WIN32(ERROR_HANDLE_EOF);
    return S_OK;
  }
  HRESULT Read(LPVOID pBuffer, DWORD nBufSize, DWORD& nBytesRead) {
    if (!::ReadFile(m_h, pBuffer, nBufSize, &nBytesRead, NULL)) return AtlHresultFromLastError();
    return S_OK;
  }
  HRESULT Read(LPVOID pBuffer, DWORD nBufSize, LPOVERLAPPED pOverlapped) {
    if (!::ReadFile(m_h, pBuffer, nBufSize, NULL, pOverlapped)) return AtlHresultFromLastError();
    return S_OK;
  }
  HRESULT Write(LPCVOID pBuffer, DWORD nBufSize, DWORD* pnBytesWritten = NULL) {
    DWORD nBytesWritten;
    if (pnBytesWritten == NULL) pnBytesWritten = &nBytesWritten;
    if (!::WriteFile(m_h, pBuffer, nBufSize, pnBytesWritten, NULL)) return AtlHresultFromLastError();
    return S_OK;
  }
  HRESULT Write(LPCVOID pBuffer, DWORD nBufSize, LPOVERLAPPED pOverlapped) {
    if (!::WriteFile(m_h, pBuffer, nBufSize, NULL, pOverlapped)) return AtlHresultFromLastError();
    return S_OK;
  }
  HRESULT GetOverlappedResult(LPOVERLAPPED pOverlapped, DWORD& dwBytesTransferred, BOOL bWait) {
    if (!::GetOverlappedResult(m_h, pOverlapped, &dwBytesTransferred, bWait))
      return AtlHresultFromLastError();
    return S_OK;
  }

  HRESULT Seek(LONGLONG nOffset, DWORD dwFrom = FILE_CURRENT) {
    LARGE_INTEGER li;
    li.QuadPart = nOffset;
    li.LowPart = ::SetFilePointer(m_h, (LONG)li.LowPart, &li.HighPart, dwFrom);
    if (li.LowPart == INVALID_SET_FILE_POINTER && ::GetLastError() != NO_ERROR)
      return AtlHresultFromLastError();
    return S_OK;
  }
  HRESULT GetPosition(ULONGLONG& nPos) const {
    LARGE_INTEGER li;
    li.QuadPart = 0;
    li.LowPart = ::SetFilePointer(m_h, 0, &li.HighPart, FILE_CURRENT);
    if (li.LowPart == INVALID_SET_FILE_POINTER && ::GetLastError() != NO_ERROR)
      return AtlHresultFromLastError();
    nPos = (ULONGLONG)li.QuadPart;
    return S_OK;
  }
  HRESULT GetSize(ULONGLONG& nLen) const {
    ULARGE_INTEGER li;
    li.LowPart = ::GetFileSize(m_h, &li.HighPart);
    if (li.LowPart == INVALID_FILE_SIZE && ::GetLastError() != NO_ERROR)
      return AtlHresultFromLastError();
    nLen = li.QuadPart;
    return S_OK;
  }
  HRESULT SetSize(ULONGLONG nNewLen) {
    HRESULT hr = Seek((LONGLONG)nNewLen, FILE_BEGIN);
    if (FAILED(hr)) return hr;
    if (!::SetEndOfFile(m_h)) return AtlHresultFromLastError();
    return S_OK;
  }
  HRESULT Flush() {
    if (!::FlushFileBuffers(m_h)) return AtlHresultFromLastError();
    return S_OK;
  }
  HRESULT LockRange(ULONGLONG nPos, ULONGLONG nCount) {
    if (!::LockFile(m_h, (DWORD)nPos, (DWORD)(nPos >> 32), (DWORD)nCount, (DWORD)(nCount >> 32)))
      return AtlHresultFromLastError();
    return S_OK;
  }
  HRESULT UnlockRange(ULONGLONG nPos, ULONGLONG nCount) {
    if (!::UnlockFile(m_h, (DWORD)nPos, (DWORD)(nPos >> 32), (DWORD)nCount, (DWORD)(nCount >> 32)))
      return AtlHresultFromLastError();
    return S_OK;
  }
};

// A file in the temporary folder that is deleted when it closes, unless
// Close names a file to move it to.
class CAtlTemporaryFile {
public:
  CAtlTemporaryFile() { m_szTempFileName[0] = 0; }
  CAtlTemporaryFile(const CAtlTemporaryFile&) = delete;
  CAtlTemporaryFile& operator=(const CAtlTemporaryFile&) = delete;
  ~CAtlTemporaryFile() {
    if (m_file.m_h != NULL) Close();
  }

  HRESULT Create(LPCTSTR pszDir = NULL, DWORD dwDesiredAccess = GENERIC_WRITE) {
    TCHAR szPath[MAX_PATH];
    if (pszDir == NULL) {
      if (::GetTempPath(MAX_PATH, szPath) == 0) return AtlHresultFromLastError();
      pszDir = szPath;
    }
    if (::GetTempFileName(pszDir, TEXT("TFR"), 0, m_szTempFileName) == 0)
      return AtlHresultFromLastError();
    return m_file.Create(m_szTempFileName, dwDesiredAccess, 0, CREATE_ALWAYS,
                         FILE_ATTRIBUTE_NOT_CONTENT_INDEXED | FILE_ATTRIBUTE_TEMPORARY);
  }
  // Closes the file and moves it to szNewName, or deletes it.
  HRESULT Close(LPCTSTR szNewName = NULL) {
    m_file.Close();
    HRESULT hr = S_OK;
    if (szNewName != NULL) {
      ::DeleteFile(szNewName);
      if (!::MoveFile(m_szTempFileName, szNewName)) hr = AtlHresultFromLastError();
    }
    if (szNewName == NULL || FAILED(hr)) ::DeleteFile(m_szTempFileName);
    m_szTempFileName[0] = 0;
    return hr;
  }
  HRESULT HandsOff() {
    m_file.Close();
    return S_OK;
  }
  HRESULT HandsOn() {
    return m_file.Create(m_szTempFileName, GENERIC_READ | GENERIC_WRITE, 0, OPEN_EXISTING);
  }

  HRESULT Read(LPVOID pBuffer, DWORD nBufSize, DWORD& nBytesRead) {
    return m_file.Read(pBuffer, nBufSize, nBytesRead);
  }
  HRESULT Write(LPCVOID pBuffer, DWORD nBufSize, DWORD* pnBytesWritten = NULL) {
    return m_file.Write(pBuffer, nBufSize, pnBytesWritten);
  }
  HRESULT Seek(LONGLONG nOffset, DWORD dwFrom = FILE_CURRENT) { return m_file.Seek(nOffset, dwFrom); }
  HRESULT GetPosition(ULONGLONG& nPos) const { return m_file.GetPosition(nPos); }
  HRESULT Flush() { return m_file.Flush(); }
  HRESULT LockRange(ULONGLONG nPos, ULONGLONG nCount) { return m_file.LockRange(nPos, nCount); }
  HRESULT UnlockRange(ULONGLONG nPos, ULONGLONG nCount) { return m_file.UnlockRange(nPos, nCount); }
  HRESULT SetSize(ULONGLONG nNewLen) { return m_file.SetSize(nNewLen); }
  HRESULT GetSize(ULONGLONG& nLen) const { return m_file.GetSize(nLen); }

  operator HANDLE() { return m_file; }
  LPCTSTR TempFileName() { return m_szTempFileName; }

private:
  CAtlFile m_file;
  TCHAR m_szTempFileName[MAX_PATH + 1];
};

// A view of a file mapping.
class CAtlFileMappingBase {
public:
  CAtlFileMappingBase() : m_pData(NULL), m_nMappingSize(0), m_hMapping(NULL), m_dwViewDesiredAccess(0) {
    m_nOffset.QuadPart = 0;
  }
  CAtlFileMappingBase(const CAtlFileMappingBase&) = delete;
  CAtlFileMappingBase& operator=(const CAtlFileMappingBase&) = delete;
  ~CAtlFileMappingBase() { Unmap(); }

  HRESULT MapFile(HANDLE hFile, SIZE_T nMappingSize = 0, ULONGLONG nOffset = 0,
                  DWORD dwMappingProtection = PAGE_READONLY,
                  DWORD dwViewDesiredAccess = FILE_MAP_READ) {
    if (m_pData != NULL) return E_UNEXPECTED;
    m_nOffset.QuadPart = (LONGLONG)nOffset;
    m_dwViewDesiredAccess = dwViewDesiredAccess;
    ULARGE_INTEGER liFileSize;
    liFileSize.LowPart = ::GetFileSize(hFile, &liFileSize.HighPart);
    if (liFileSize.QuadPart < nMappingSize) liFileSize.QuadPart = nMappingSize;
    m_hMapping = ::CreateFileMapping(hFile, NULL, dwMappingProtection, liFileSize.HighPart,
                                     liFileSize.LowPart, NULL);
    if (m_hMapping == NULL) return AtlHresultFromLastError();
    m_nMappingSize = nMappingSize == 0 ? (SIZE_T)(liFileSize.QuadPart - nOffset) : nMappingSize;
    return _MapView();
  }
  HRESULT MapSharedMem(SIZE_T nMappingSize, LPCTSTR szName, BOOL* pbAlreadyExisted = NULL,
                       LPSECURITY_ATTRIBUTES lpsa = NULL, DWORD dwMappingProtection = PAGE_READWRITE,
                       DWORD dwViewDesiredAccess = FILE_MAP_ALL_ACCESS) {
    if (m_pData != NULL) return E_UNEXPECTED;
    m_nMappingSize = nMappingSize;
    m_dwViewDesiredAccess = dwViewDesiredAccess;
    m_nOffset.QuadPart = 0;
    ULARGE_INTEGER liSize;
    liSize.QuadPart = nMappingSize;
    m_hMapping = ::CreateFileMapping(INVALID_HANDLE_VALUE, lpsa, dwMappingProtection,
                                     liSize.HighPart, liSize.LowPart, szName);
    if (m_hMapping == NULL) return AtlHresultFromLastError();
    if (pbAlreadyExisted != NULL) *pbAlreadyExisted = ::GetLastError() == ERROR_ALREADY_EXISTS;
    return _MapView();
  }
  HRESULT OpenMapping(LPCTSTR szName, SIZE_T nMappingSize, ULONGLONG nOffset = 0,
                      DWORD dwViewDesiredAccess = FILE_MAP_ALL_ACCESS) {
    if (m_pData != NULL) return E_UNEXPECTED;
    m_nMappingSize = nMappingSize;
    m_dwViewDesiredAccess = dwViewDesiredAccess;
    m_nOffset.QuadPart = (LONGLONG)nOffset;
    m_hMapping = ::OpenFileMapping(dwViewDesiredAccess, FALSE, szName);
    if (m_hMapping == NULL) return AtlHresultFromLastError();
    return _MapView();
  }
  HRESULT Unmap() {
    HRESULT hr = S_OK;
    if (m_pData != NULL) {
      if (!::UnmapViewOfFile(m_pData)) hr = AtlHresultFromLastError();
      m_pData = NULL;
    }
    if (m_hMapping != NULL) {
      ::CloseHandle(m_hMapping);
      m_hMapping = NULL;
    }
    return hr;
  }

  void* GetData() const { return m_pData; }
  HANDLE GetHandle() const { return m_hMapping; }
  SIZE_T GetMappingSize() { return m_nMappingSize; }

private:
  void* m_pData;
  SIZE_T m_nMappingSize;
  HANDLE m_hMapping;
  ULARGE_INTEGER m_nOffset;
  DWORD m_dwViewDesiredAccess;

  HRESULT _MapView() {
    m_pData = ::MapViewOfFile(m_hMapping, m_dwViewDesiredAccess, m_nOffset.HighPart,
                              m_nOffset.LowPart, m_nMappingSize);
    if (m_pData == NULL) {
      HRESULT hr = AtlHresultFromLastError();
      ::CloseHandle(m_hMapping);
      m_hMapping = NULL;
      return hr;
    }
    return S_OK;
  }
};

template <typename T = char>
class CAtlFileMapping : public CAtlFileMappingBase {
public:
  operator T*() const { return reinterpret_cast<T*>(GetData()); }
};

}  // namespace ATL

#endif  // __ATLFILE_H__
