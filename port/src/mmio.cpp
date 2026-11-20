// mmio.cpp - buffered file I/O for RIFF files: files on disk or in memory,
// the direct buffer access of mmioGetInfo and mmioAdvance, and the chunk
// walking of mmioDescend, mmioAscend and mmioCreateChunk.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#include <windows.h>
#include <mmsystem.h>

#include "winport.h"

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <mutex>
#include <set>
#include <string>

namespace {

struct Mmio {
  MMIOINFO info;     // the buffer as the caller sees it, and the flags
  int fd = -1;       // a file on disk, or -1 for one in memory
  bool ownBuffer = false;
  bool memory = false;
  LONG memSize = 0;  // a memory file's end
  LONG grow = 0;     // how a memory file the layer owns may grow
};

std::mutex& Lock() {
  static std::mutex* m = new std::mutex;
  return *m;
}
std::set<void*>& Live() {
  static std::set<void*>* s = new std::set<void*>;
  return *s;
}
Mmio* Get(HMMIO h) {
  std::lock_guard<std::mutex> g(Lock());
  return h && Live().count(h) ? (Mmio*)h : nullptr;
}

// Where the caller is in the file.
LONG Pos(Mmio* m) {
  if (!m->info.pchBuffer) return m->info.lDiskOffset;
  return m->info.lBufOffset + (LONG)(m->info.pchNext - m->info.pchBuffer);
}

bool Dirty(Mmio* m) { return (m->info.dwFlags & MMIO_DIRTY) != 0; }

// Writes back what the buffer holds that is new.
bool Flush(Mmio* m) {
  if (m->memory || !Dirty(m) || !m->info.pchBuffer) {
    m->info.dwFlags &= ~MMIO_DIRTY;
    return true;
  }
  char* end = std::max(m->info.pchEndRead, m->info.pchNext);
  LONG n = (LONG)(end - m->info.pchBuffer);
  if (lseek(m->fd, m->info.lBufOffset, SEEK_SET) < 0) return false;
  for (LONG done = 0; done < n;) {
    ssize_t w = write(m->fd, m->info.pchBuffer + done, (size_t)(n - done));
    if (w <= 0) return false;
    done += (LONG)w;
  }
  m->info.lDiskOffset = m->info.lBufOffset + n;
  m->info.dwFlags &= ~MMIO_DIRTY;
  return true;
}

// Empties the buffer and puts it at a place in the file, filling it for
// reading.
bool Refill(Mmio* m, LONG at, bool fill) {
  m->info.lBufOffset = at;
  m->info.pchNext = m->info.pchEndRead = m->info.pchBuffer;
  m->info.pchEndWrite = m->info.pchBuffer + m->info.cchBuffer;
  if (!fill) return true;
  if (lseek(m->fd, at, SEEK_SET) < 0) return false;
  ssize_t r = read(m->fd, m->info.pchBuffer, (size_t)m->info.cchBuffer);
  if (r < 0) return false;
  m->info.pchEndRead = m->info.pchBuffer + r;
  m->info.lDiskOffset = at + (LONG)r;
  return true;
}

// A memory file the layer owns grows to hold n more bytes.
bool Grow(Mmio* m, LONG n) {
  if (!m->ownBuffer || m->grow <= 0) return false;
  LONG need = Pos(m) + n;
  LONG size = m->info.cchBuffer;
  while (size < need) size += m->grow;
  LONG next = (LONG)(m->info.pchNext - m->info.pchBuffer);
  LONG endRead = (LONG)(m->info.pchEndRead - m->info.pchBuffer);
  char* b = (char*)realloc(m->info.pchBuffer, (size_t)size);
  if (!b) return false;
  memset(b + m->info.cchBuffer, 0, (size_t)(size - m->info.cchBuffer));
  m->info.pchBuffer = b;
  m->info.cchBuffer = size;
  m->info.pchNext = b + next;
  m->info.pchEndRead = b + endRead;
  m->info.pchEndWrite = b + size;
  return true;
}

int OpenFlags(DWORD flags) {
  int o = 0;
  switch (flags & MMIO_RWMODE) {
    case MMIO_WRITE: o = O_WRONLY; break;
    case MMIO_READWRITE: o = O_RDWR; break;
    default: o = O_RDONLY; break;
  }
  if (flags & MMIO_CREATE) o |= O_CREAT | O_TRUNC;
  return o | O_CLOEXEC;
}

UINT ErrnoError() {
  switch (errno) {
    case ENOENT: return MMIOERR_FILENOTFOUND;
    case ENOTDIR: return MMIOERR_PATHNOTFOUND;
    case EACCES:
    case EPERM: return MMIOERR_ACCESSDENIED;
    case EMFILE:
    case ENFILE: return MMIOERR_TOOMANYOPENFILES;
    case ENOMEM: return MMIOERR_OUTOFMEMORY;
  }
  return MMIOERR_CANNOTOPEN;
}

std::string Path(const char* name) {
  std::string p = name;
  for (char& c : p)
    if (c == '\\') c = '/';
  return p;
}

HMMIO Open(const char* name, LPMMIOINFO in, DWORD flags) {
  MMIOINFO dummy;
  memset(&dummy, 0, sizeof dummy);
  LPMMIOINFO info = in ? in : &dummy;
  info->wErrorRet = 0;
  bool memory = in && in->fccIOProc == FOURCC_MEM;
  if (flags & (MMIO_PARSE | MMIO_EXIST | MMIO_DELETE)) {
    if (!name) {
      info->wErrorRet = MMIOERR_FILENOTFOUND;
      return nullptr;
    }
    std::string p = Path(name);
    if (flags & MMIO_PARSE) {
      // The full path, written back into the name.
      char* full = realpath(p.c_str(), nullptr);
      if (full) {
        lstrcpynA((char*)name, full, MAX_PATH);
        free(full);
      }
      return (HMMIO)(UINT_PTR)TRUE;
    }
    if (flags & MMIO_DELETE) {
      if (unlink(p.c_str()) == 0) return (HMMIO)(UINT_PTR)TRUE;
      info->wErrorRet = ErrnoError();
      return nullptr;
    }
    struct stat st;
    if (stat(p.c_str(), &st) == 0) return (HMMIO)(UINT_PTR)TRUE;
    info->wErrorRet = MMIOERR_FILENOTFOUND;
    return nullptr;
  }
  if (in && in->pIOProc && !memory) {
    info->wErrorRet = MMIOERR_CANNOTOPEN;  // installable I/O procedures are not kept
    return nullptr;
  }
  Mmio* m = new Mmio;
  memset(&m->info, 0, sizeof m->info);
  m->info.dwFlags = flags & ~MMIO_ALLOCBUF;
  m->info.fccIOProc = memory ? FOURCC_MEM : FOURCC_DOS;
  m->memory = memory;
  if (memory) {
    m->info.cchBuffer = in->cchBuffer;
    m->info.pchBuffer = in->pchBuffer;
    m->grow = (LONG)in->adwInfo[0];
    if (!m->info.pchBuffer) {
      if (m->info.cchBuffer <= 0 && m->grow <= 0) {
        delete m;
        info->wErrorRet = MMIOERR_OUTOFMEMORY;
        return nullptr;
      }
      m->info.pchBuffer = (char*)calloc(1, (size_t)std::max<LONG>(1, m->info.cchBuffer));
      m->ownBuffer = true;
    }
    m->memSize = (flags & MMIO_CREATE) ? 0 : m->info.cchBuffer;
    m->info.pchNext = m->info.pchBuffer;
    m->info.pchEndRead = m->info.pchBuffer + m->memSize;
    m->info.pchEndWrite = m->info.pchBuffer + m->info.cchBuffer;
  } else {
    std::string p = Path(name ? name : "");
    if (flags & MMIO_GETTEMP) {
      delete m;
      info->wErrorRet = MMIOERR_CANNOTOPEN;
      return nullptr;
    }
    m->fd = open(p.c_str(), OpenFlags(flags), 0666);
    if (m->fd < 0) {
      info->wErrorRet = ErrnoError();
      delete m;
      return nullptr;
    }
    LONG size = 0;
    if (flags & MMIO_ALLOCBUF)
      size = in && in->cchBuffer ? in->cchBuffer : MMIO_DEFAULTBUFFER;
    else if (in && in->cchBuffer)
      size = in->cchBuffer;
    if (size) {
      m->info.cchBuffer = size;
      if (in && in->pchBuffer && !(flags & MMIO_ALLOCBUF)) {
        m->info.pchBuffer = in->pchBuffer;
      } else {
        m->info.pchBuffer = (char*)malloc((size_t)size);
        m->ownBuffer = true;
      }
      Refill(m, 0, false);
      if ((flags & MMIO_RWMODE) != MMIO_WRITE) Refill(m, 0, true);
    }
  }
  if (flags & MMIO_ALLOCBUF) m->info.dwFlags |= MMIO_ALLOCBUF;
  m->info.hmmio = (HMMIO)m;
  {
    std::lock_guard<std::mutex> g(Lock());
    Live().insert(m);
  }
  return (HMMIO)m;
}

}  // namespace

extern "C" {

HMMIO WINAPI mmioOpenA(LPSTR name, LPMMIOINFO info, DWORD flags) { return Open(name, info, flags); }

HMMIO WINAPI mmioOpenW(LPWSTR name, LPMMIOINFO info, DWORD flags) {
  if (!name) return Open(nullptr, info, flags);
  std::string n = winport::Narrow(name);
  n.resize(std::max<size_t>(n.size() + 1, MAX_PATH));
  HMMIO r = Open(&n[0], info, flags);
  if ((flags & MMIO_PARSE) && r) {
    std::wstring w = winport::Widen(n.c_str());
    lstrcpynW(name, w.c_str(), MAX_PATH);
  }
  return r;
}

MMRESULT WINAPI mmioClose(HMMIO h, UINT flags) {
  Mmio* m = Get(h);
  if (!m) return MMSYSERR_INVALHANDLE;
  MMRESULT r = Flush(m) ? MMSYSERR_NOERROR : MMIOERR_CANNOTWRITE;
  {
    std::lock_guard<std::mutex> g(Lock());
    Live().erase(m);
  }
  if (m->fd >= 0 && !(flags & MMIO_FHOPEN)) close(m->fd);
  if (m->ownBuffer) free(m->info.pchBuffer);
  delete m;
  return r;
}

LONG WINAPI mmioRead(HMMIO h, HPSTR out, LONG n) {
  Mmio* m = Get(h);
  if (!m || n < 0) return -1;
  if (!m->info.pchBuffer) {  // unbuffered
    if (lseek(m->fd, m->info.lDiskOffset, SEEK_SET) < 0) return -1;
    ssize_t r = read(m->fd, out, (size_t)n);
    if (r < 0) return -1;
    m->info.lDiskOffset += (LONG)r;
    return (LONG)r;
  }
  LONG done = 0;
  while (done < n) {
    LONG have = (LONG)(m->info.pchEndRead - m->info.pchNext);
    if (have <= 0) {
      if (m->memory) break;
      LONG at = Pos(m);
      if (!Flush(m) || !Refill(m, at, true)) return -1;
      have = (LONG)(m->info.pchEndRead - m->info.pchNext);
      if (have <= 0) break;
    }
    LONG k = std::min(have, n - done);
    memcpy(out + done, m->info.pchNext, (size_t)k);
    m->info.pchNext += k;
    done += k;
  }
  return done;
}

LONG WINAPI mmioWrite(HMMIO h, const char* data, LONG n) {
  Mmio* m = Get(h);
  if (!m || n < 0) return -1;
  if ((m->info.dwFlags & MMIO_RWMODE) == MMIO_READ) return -1;
  if (!m->info.pchBuffer) {
    if (lseek(m->fd, m->info.lDiskOffset, SEEK_SET) < 0) return -1;
    ssize_t w = write(m->fd, data, (size_t)n);
    if (w < 0) return -1;
    m->info.lDiskOffset += (LONG)w;
    return (LONG)w;
  }
  LONG done = 0;
  while (done < n) {
    LONG room = (LONG)(m->info.pchEndWrite - m->info.pchNext);
    if (room <= 0) {
      if (m->memory) {
        if (!Grow(m, n - done)) break;
        continue;
      }
      LONG at = Pos(m);
      if (!Flush(m) || !Refill(m, at, false)) return -1;
      continue;
    }
    LONG k = std::min(room, n - done);
    memcpy(m->info.pchNext, data + done, (size_t)k);
    m->info.pchNext += k;
    if (m->info.pchNext > m->info.pchEndRead) m->info.pchEndRead = m->info.pchNext;
    m->info.dwFlags |= MMIO_DIRTY;
    done += k;
  }
  if (m->memory) m->memSize = std::max(m->memSize, (LONG)(m->info.pchEndRead - m->info.pchBuffer));
  return done;
}

LONG WINAPI mmioSeek(HMMIO h, LONG offset, int origin) {
  Mmio* m = Get(h);
  if (!m) return -1;
  LONG base;
  switch (origin) {
    case SEEK_SET: base = 0; break;
    case SEEK_CUR: base = Pos(m); break;
    case SEEK_END:
      if (m->memory) {
        base = m->memSize;
      } else {
        if (!Flush(m)) return -1;
        off_t e = lseek(m->fd, 0, SEEK_END);
        if (e < 0) return -1;
        base = (LONG)e;
        if (m->info.pchBuffer) base = std::max(base, m->info.lBufOffset + (LONG)(m->info.pchEndRead - m->info.pchBuffer));
      }
      break;
    default: return -1;
  }
  LONG to = base + offset;
  if (to < 0) return -1;
  if (!m->info.pchBuffer) {
    m->info.lDiskOffset = to;
    return to;
  }
  if (m->memory) {
    if (to > m->info.cchBuffer) return -1;
    m->info.pchNext = m->info.pchBuffer + to;
    return to;
  }
  LONG inBuf = to - m->info.lBufOffset;
  if (inBuf >= 0 && inBuf <= (LONG)(m->info.pchEndRead - m->info.pchBuffer)) {
    m->info.pchNext = m->info.pchBuffer + inBuf;
    return to;
  }
  if (!Flush(m)) return -1;
  // Reading fills the buffer from the new place when it gets there.
  Refill(m, to, false);
  return to;
}

MMRESULT WINAPI mmioGetInfo(HMMIO h, LPMMIOINFO info, UINT) {
  Mmio* m = Get(h);
  if (!m) return MMSYSERR_INVALHANDLE;
  if (!info) return MMSYSERR_INVALPARAM;
  *info = m->info;
  return MMSYSERR_NOERROR;
}

MMRESULT WINAPI mmioSetInfo(HMMIO h, LPCMMIOINFO info, UINT) {
  Mmio* m = Get(h);
  if (!m) return MMSYSERR_INVALHANDLE;
  if (!info) return MMSYSERR_INVALPARAM;
  if (info->pchBuffer != m->info.pchBuffer || info->pchNext < info->pchBuffer ||
      info->pchNext > info->pchBuffer + info->cchBuffer)
    return MMSYSERR_INVALPARAM;
  m->info.pchNext = info->pchNext;
  if (info->pchEndRead > m->info.pchEndRead) m->info.pchEndRead = info->pchEndRead;
  if (info->dwFlags & MMIO_DIRTY) {
    m->info.dwFlags |= MMIO_DIRTY;
    if (m->info.pchNext > m->info.pchEndRead) m->info.pchEndRead = m->info.pchNext;
  }
  if (m->memory) m->memSize = std::max(m->memSize, (LONG)(m->info.pchEndRead - m->info.pchBuffer));
  return MMSYSERR_NOERROR;
}

MMRESULT WINAPI mmioAdvance(HMMIO h, LPMMIOINFO info, UINT flags) {
  Mmio* m = Get(h);
  if (!m) return MMSYSERR_INVALHANDLE;
  if (!m->info.pchBuffer) return MMIOERR_UNBUFFERED;
  if (info) {
    MMRESULT r = mmioSetInfo(h, info, 0);
    if (r) return r;
  }
  if (m->memory) {
    if (flags == MMIO_WRITE && m->info.pchNext >= m->info.pchEndWrite && !Grow(m, MMIO_DEFAULTBUFFER))
      return MMIOERR_CANNOTEXPAND;
  } else {
    LONG at = Pos(m);
    if (!Flush(m)) return MMIOERR_CANNOTWRITE;
    if (!Refill(m, at, flags != MMIO_WRITE)) return flags == MMIO_WRITE ? MMIOERR_CANNOTWRITE : MMIOERR_CANNOTREAD;
  }
  if (info) *info = m->info;
  if (info) info->dwFlags &= ~MMIO_DIRTY;
  return MMSYSERR_NOERROR;
}

MMRESULT WINAPI mmioFlush(HMMIO h, UINT flags) {
  Mmio* m = Get(h);
  if (!m) return MMSYSERR_INVALHANDLE;
  if (!Flush(m)) return MMIOERR_CANNOTWRITE;
  if ((flags & MMIO_EMPTYBUF) && !m->memory && m->info.pchBuffer) Refill(m, Pos(m), false);
  return MMSYSERR_NOERROR;
}

MMRESULT WINAPI mmioDescend(HMMIO h, LPMMCKINFO ck, const MMCKINFO* parent, UINT flags) {
  if (!Get(h)) return MMSYSERR_INVALHANDLE;
  if (!ck) return MMSYSERR_INVALPARAM;
  LONG end = parent ? (LONG)(parent->dwDataOffset + parent->cksize) : 0x7FFFFFFF;
  FOURCC wantId = 0, wantType = 0;
  if (flags & MMIO_FINDCHUNK) wantId = ck->ckid;
  if (flags & MMIO_FINDRIFF) {
    wantId = FOURCC_RIFF;
    wantType = ck->fccType;
  }
  if (flags & MMIO_FINDLIST) {
    wantId = FOURCC_LIST;
    wantType = ck->fccType;
  }
  for (;;) {
    LONG at = mmioSeek(h, 0, SEEK_CUR);
    if (at < 0 || at + 8 > end) return MMIOERR_CHUNKNOTFOUND;
    DWORD head[2];
    if (mmioRead(h, (HPSTR)head, 8) != 8) return MMIOERR_CHUNKNOTFOUND;
    MMCKINFO c;
    c.ckid = head[0];
    c.cksize = head[1];
    c.fccType = 0;
    c.dwDataOffset = (DWORD)(at + 8);
    c.dwFlags = 0;
    bool listy = c.ckid == FOURCC_RIFF || c.ckid == FOURCC_LIST;
    if (listy) {
      if (mmioRead(h, (HPSTR)&c.fccType, 4) != 4) return MMIOERR_CHUNKNOTFOUND;
    }
    bool match = !wantId || (c.ckid == wantId && (!wantType || c.fccType == wantType));
    if (match) {
      *ck = c;
      return MMSYSERR_NOERROR;
    }
    // Over this chunk, to the next (chunks are padded to even sizes).
    LONG next = (LONG)(c.dwDataOffset + c.cksize + (c.cksize & 1));
    if (mmioSeek(h, next, SEEK_SET) < 0) return MMIOERR_CHUNKNOTFOUND;
  }
}

MMRESULT WINAPI mmioAscend(HMMIO h, LPMMCKINFO ck, UINT) {
  if (!Get(h)) return MMSYSERR_INVALHANDLE;
  if (!ck) return MMSYSERR_INVALPARAM;
  if (ck->dwFlags & MMIO_DIRTY) {
    // A chunk being written: its size is where writing stopped.
    LONG at = mmioSeek(h, 0, SEEK_CUR);
    if (at < 0) return MMIOERR_CANNOTSEEK;
    DWORD size = (DWORD)(at - (LONG)ck->dwDataOffset);
    if (size != ck->cksize) {
      ck->cksize = size;
      if (mmioSeek(h, (LONG)ck->dwDataOffset - 4, SEEK_SET) < 0) return MMIOERR_CANNOTSEEK;
      if (mmioWrite(h, (const char*)&size, 4) != 4) return MMIOERR_CANNOTWRITE;
    }
    ck->dwFlags &= ~MMIO_DIRTY;
    if (mmioSeek(h, at, SEEK_SET) < 0) return MMIOERR_CANNOTSEEK;
    if (size & 1) {
      char pad = 0;
      if (mmioWrite(h, &pad, 1) != 1) return MMIOERR_CANNOTWRITE;
    }
    return MMSYSERR_NOERROR;
  }
  LONG next = (LONG)(ck->dwDataOffset + ck->cksize + (ck->cksize & 1));
  if (mmioSeek(h, next, SEEK_SET) < 0) return MMIOERR_CANNOTSEEK;
  return MMSYSERR_NOERROR;
}

MMRESULT WINAPI mmioCreateChunk(HMMIO h, LPMMCKINFO ck, UINT flags) {
  if (!Get(h)) return MMSYSERR_INVALHANDLE;
  if (!ck) return MMSYSERR_INVALPARAM;
  if (flags & MMIO_CREATERIFF) ck->ckid = FOURCC_RIFF;
  if (flags & MMIO_CREATELIST) ck->ckid = FOURCC_LIST;
  LONG at = mmioSeek(h, 0, SEEK_CUR);
  if (at < 0) return MMIOERR_CANNOTSEEK;
  DWORD head[2] = {ck->ckid, ck->cksize};
  if (mmioWrite(h, (const char*)head, 8) != 8) return MMIOERR_CANNOTWRITE;
  ck->dwDataOffset = (DWORD)(at + 8);
  if (ck->ckid == FOURCC_RIFF || ck->ckid == FOURCC_LIST) {
    if (mmioWrite(h, (const char*)&ck->fccType, 4) != 4) return MMIOERR_CANNOTWRITE;
  }
  ck->dwFlags = MMIO_DIRTY;
  return MMSYSERR_NOERROR;
}

FOURCC WINAPI mmioStringToFOURCCA(LPCSTR s, UINT flags) {
  char c[4] = {' ', ' ', ' ', ' '};
  for (int i = 0; i < 4 && s && s[i]; i++) c[i] = (flags & MMIO_TOUPPER) ? (char)toupper((unsigned char)s[i]) : s[i];
  return mmioFOURCC(c[0], c[1], c[2], c[3]);
}

FOURCC WINAPI mmioStringToFOURCCW(LPCWSTR s, UINT flags) {
  return mmioStringToFOURCCA(s ? winport::Narrow(s).c_str() : nullptr, flags);
}

}  // extern "C"
