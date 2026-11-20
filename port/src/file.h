// file.h - the layer's file objects, shared by kernel_file.cpp (files and
// pipes) and kernel_comm.cpp (serial ports).
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#ifndef _WINPORT_FILE_H
#define _WINPORT_FILE_H

#include "winport.h"

#include <string>

namespace winport {

struct Comm;  // a serial port's state, in kernel_comm.cpp

struct File : Object {
  int fd;
  std::string path;
  bool deleteOnClose = false;
  bool append = false;
  bool pipe = false;        // from CreatePipe: the other end gone is a broken pipe
  bool overlapped = false;  // opened with FILE_FLAG_OVERLAPPED
  std::string peeked;       // read by PeekNamedPipe and not yet by ReadFile
  Comm* comm = nullptr;     // a serial port
  HANDLE port = NULL;       // the completion port its overlapped results go to
  ULONG_PTR portKey = 0;
  explicit File(int f) : Object(kFile), fd(f) {}
  ~File() override;
};

File* GetFile(HANDLE h);

// Serial ports: the device a name like "COM1" or "\\.\COM12" stands for
// ("" when it is not one), setting a newly opened port up, ending it, and
// its reads and writes, which keep to its time-outs.
std::string CommDevice(const std::string& name);
bool IsTerminal(int fd);
void OpenComm(File* f);
void CloseComm(File* f);
BOOL CommRead(File* f, void* buf, DWORD n, LPDWORD done, LPOVERLAPPED ov);
BOOL CommWrite(File* f, const void* buf, DWORD n, LPDWORD done, LPOVERLAPPED ov);
void CancelComm(File* f);

// Overlapped results: a finished operation's error and count, and its
// event set, and a packet on the file's completion port if it has one.
void CompleteOverlapped(LPOVERLAPPED ov, DWORD error, DWORD n, File* f = nullptr);
// Queues a packet on a completion port.
BOOL PostCompletion(HANDLE port, DWORD bytes, ULONG_PTR key, LPOVERLAPPED ov, DWORD error);

}  // namespace winport

#endif
