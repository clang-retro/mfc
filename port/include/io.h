// io.h - the C runtime's low-level file functions (_open, _read and the
// rest) under their Microsoft names, over the POSIX ones.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#ifndef _WINPORT_IO_H
#define _WINPORT_IO_H

#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

// Files are always binary here.
#ifndef O_BINARY
#define O_BINARY 0
#endif
#ifndef O_TEXT
#define O_TEXT 0
#endif
#define _O_RDONLY O_RDONLY
#define _O_WRONLY O_WRONLY
#define _O_RDWR O_RDWR
#define _O_APPEND O_APPEND
#define _O_CREAT O_CREAT
#define _O_TRUNC O_TRUNC
#define _O_EXCL O_EXCL
#define _O_BINARY O_BINARY
#define _O_TEXT O_TEXT
#define _S_IREAD S_IRUSR
#define _S_IWRITE S_IWUSR

#define _open open
#define _creat creat
#define _read read
#define _write write
#define _close close
#define _lseek lseek
#define _access access
#define _unlink unlink
#define _dup dup
#define _dup2 dup2
#define _isatty isatty
#define _chmod chmod
#define _umask umask

#ifndef _WINPORT_IO_INLINE
#ifdef __cplusplus
#define _WINPORT_IO_INLINE inline
#else
#define _WINPORT_IO_INLINE static inline
#endif
#endif

// The length of an open file, or -1.
_WINPORT_IO_INLINE long _filelength(int fd) {
  struct stat st;
  return fstat(fd, &st) == 0 ? (long)st.st_size : -1L;
}

// The position in an open file, or -1.
_WINPORT_IO_INLINE long _tell(int fd) { return (long)lseek(fd, 0, SEEK_CUR); }

// Nonzero when the position is at the end of the file, -1 on an error.
_WINPORT_IO_INLINE int _eof(int fd) {
  long len = _filelength(fd), pos = _tell(fd);
  if (len < 0 || pos < 0) return -1;
  return pos >= len;
}

#endif
