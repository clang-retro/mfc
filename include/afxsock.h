// afxsock.h - Windows Sockets: CAsyncSocket, which turns socket events
// into virtual function calls, CSocket, whose calls block while the
// thread's messages keep flowing, and CSocketFile, which lets a CArchive
// read and write through a CSocket.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
#ifndef __AFXSOCK_H__
#define __AFXSOCK_H__

#include "afxwin.h"
// <windows.h> brings in <winsock.h> unless WIN32_LEAN_AND_MEAN is
// defined; a program that wants Winsock 2 includes <winsock2.h> first.
#if !defined(_WINSOCKAPI_) && !defined(_WINSOCK2API_)
#include <winsock2.h>
#endif

#ifndef _AFX_NOFORCE_LIBS
__asm__(".section .drectve,\"yn\"\n"
        ".ascii \" -defaultlib:ws2_32\"\n"
        ".text");
#endif

// Sent by Winsock to the thread's socket window (see WSAAsyncSelect).
#define WM_SOCKET_NOTIFY 0x0373
#define WM_SOCKET_DEAD 0x0374

class CAsyncSocket;
class CSocket;
class CSocketFile;

BOOL AFXAPI AfxSocketInit(WSADATA* lpwsaData = NULL);
void AFXAPI AfxSocketTerm();

class CAsyncSocket : public CObject {
  DECLARE_DYNAMIC(CAsyncSocket)
public:
  CAsyncSocket();
  virtual ~CAsyncSocket();

  BOOL Create(UINT nSocketPort = 0, int nSocketType = SOCK_STREAM,
              long lEvent = FD_READ | FD_WRITE | FD_OOB | FD_ACCEPT | FD_CONNECT | FD_CLOSE,
              LPCTSTR lpszSocketAddress = NULL);
  BOOL Socket(int nSocketType = SOCK_STREAM,
              long lEvent = FD_READ | FD_WRITE | FD_OOB | FD_ACCEPT | FD_CONNECT | FD_CLOSE,
              int nProtocolType = 0, int nAddressFormat = PF_INET);

  SOCKET m_hSocket;
  operator SOCKET() const { return m_hSocket; }

  BOOL Attach(SOCKET hSocket,
              long lEvent = FD_READ | FD_WRITE | FD_OOB | FD_ACCEPT | FD_CONNECT | FD_CLOSE);
  SOCKET Detach();
  // The object attached to a socket handle in this thread, or NULL.
  static CAsyncSocket* PASCAL FromHandle(SOCKET hSocket);
  static CAsyncSocket* PASCAL LookupHandle(SOCKET hSocket, BOOL bDead = FALSE);

  BOOL GetPeerName(CString& rPeerAddress, UINT& rPeerPort);
  BOOL GetPeerName(SOCKADDR* lpSockAddr, int* lpSockAddrLen);
  BOOL GetSockName(CString& rSocketAddress, UINT& rSocketPort);
  BOOL GetSockName(SOCKADDR* lpSockAddr, int* lpSockAddrLen);
  BOOL SetSockOpt(int nOptionName, const void* lpOptionValue, int nOptionLen,
                  int nLevel = SOL_SOCKET);
  BOOL GetSockOpt(int nOptionName, void* lpOptionValue, int* lpOptionLen,
                  int nLevel = SOL_SOCKET);

  static int PASCAL GetLastError() { return ::WSAGetLastError(); }

  virtual BOOL Accept(CAsyncSocket& rConnectedSocket, SOCKADDR* lpSockAddr = NULL,
                      int* lpSockAddrLen = NULL);
  BOOL Bind(UINT nSocketPort, LPCTSTR lpszSocketAddress = NULL);
  BOOL Bind(const SOCKADDR* lpSockAddr, int nSockAddrLen);
  virtual void Close();
  BOOL Connect(LPCTSTR lpszHostAddress, UINT nHostPort);
  BOOL Connect(const SOCKADDR* lpSockAddr, int nSockAddrLen);
  BOOL IOCtl(long lCommand, DWORD* lpArgument);
  BOOL Listen(int nConnectionBacklog = 5);
  virtual int Receive(void* lpBuf, int nBufLen, int nFlags = 0);
  int ReceiveFrom(void* lpBuf, int nBufLen, CString& rSocketAddress, UINT& rSocketPort,
                  int nFlags = 0);
  int ReceiveFrom(void* lpBuf, int nBufLen, SOCKADDR* lpSockAddr, int* lpSockAddrLen,
                  int nFlags = 0);
  enum { receives = 0, sends = 1, both = 2 };
  BOOL ShutDown(int nHow = sends);
  virtual int Send(const void* lpBuf, int nBufLen, int nFlags = 0);
  int SendTo(const void* lpBuf, int nBufLen, UINT nHostPort, LPCTSTR lpszHostAddress = NULL,
             int nFlags = 0);
  int SendTo(const void* lpBuf, int nBufLen, const SOCKADDR* lpSockAddr, int nSockAddrLen,
             int nFlags = 0);
  BOOL AsyncSelect(long lEvent = FD_READ | FD_WRITE | FD_OOB | FD_ACCEPT | FD_CONNECT |
                                 FD_CLOSE);

  // Called from the thread's message loop when Winsock reports an event.
  virtual void OnReceive(int nErrorCode);
  virtual void OnSend(int nErrorCode);
  virtual void OnOutOfBandData(int nErrorCode);
  virtual void OnAccept(int nErrorCode);
  virtual void OnConnect(int nErrorCode);
  virtual void OnClose(int nErrorCode);

  static void PASCAL DoCallBack(WPARAM wParam, LPARAM lParam);

protected:
  virtual BOOL ConnectHelper(const SOCKADDR* lpSockAddr, int nSockAddrLen);
  virtual int ReceiveFromHelper(void* lpBuf, int nBufLen, SOCKADDR* lpSockAddr,
                                int* lpSockAddrLen, int nFlags);
  virtual int SendToHelper(const void* lpBuf, int nBufLen, const SOCKADDR* lpSockAddr,
                           int nSockAddrLen, int nFlags);

private:
  CAsyncSocket(const CAsyncSocket&);
  void operator=(const CAsyncSocket&);
};

class CSocket : public CAsyncSocket {
  DECLARE_DYNAMIC(CSocket)
public:
  CSocket();
  virtual ~CSocket();

  BOOL Create(UINT nSocketPort = 0, int nSocketType = SOCK_STREAM,
              LPCTSTR lpszSocketAddress = NULL);
  BOOL Attach(SOCKET hSocket);
  static CSocket* PASCAL FromHandle(SOCKET hSocket);

  BOOL IsBlocking() const { return m_pbBlocking != NULL; }
  void CancelBlockingCall();

  virtual BOOL Accept(CAsyncSocket& rConnectedSocket, SOCKADDR* lpSockAddr = NULL,
                      int* lpSockAddrLen = NULL);
  virtual void Close();
  virtual int Receive(void* lpBuf, int nBufLen, int nFlags = 0);
  virtual int Send(const void* lpBuf, int nBufLen, int nFlags = 0);

  // How often, in milliseconds, OnMessagePending runs while a call waits.
  int m_nTimeOut;

protected:
  // Called while a call waits; the default handles WM_PAINT messages so
  // the program's windows keep drawing. Return TRUE if it handled one.
  virtual BOOL OnMessagePending();
  virtual BOOL ConnectHelper(const SOCKADDR* lpSockAddr, int nSockAddrLen);
  virtual int ReceiveFromHelper(void* lpBuf, int nBufLen, SOCKADDR* lpSockAddr,
                                int* lpSockAddrLen, int nFlags);
  virtual int SendToHelper(const void* lpBuf, int nBufLen, const SOCKADDR* lpSockAddr,
                           int nSockAddrLen, int nFlags);
  int SendChunk(const void* lpBuf, int nBufLen, int nFlags);
  BOOL PumpMessages(UINT uStopFlag);

  BOOL* m_pbBlocking;
  int m_nConnectError;

  friend class CAsyncSocket;
};

class CSocketFile : public CFile {
  DECLARE_DYNAMIC(CSocketFile)
public:
  // With bArchiveCompatible, a read returns once some data has arrived,
  // as CArchive expects; without it, a read waits for the whole count.
  explicit CSocketFile(CSocket* pSocket, BOOL bArchiveCompatible = TRUE);
  virtual ~CSocketFile();

  virtual UINT Read(void* lpBuf, UINT nCount);
  virtual void Write(const void* lpBuf, UINT nCount);
  virtual void Close();
  virtual void Flush() {}
  virtual void Abort() { Close(); }

  // Sockets have no position or length.
  virtual ULONGLONG GetPosition() const;
  virtual BOOL Open(LPCTSTR lpszFileName, UINT nOpenFlags, CFileException* pError = NULL);
  virtual CFile* Duplicate() const;
  virtual ULONGLONG Seek(LONGLONG lOff, UINT nFrom);
  virtual void SetLength(ULONGLONG dwNewLen);
  virtual ULONGLONG GetLength() const;
  virtual void LockRange(ULONGLONG dwPos, ULONGLONG dwCount);
  virtual void UnlockRange(ULONGLONG dwPos, ULONGLONG dwCount);
  virtual UINT GetBufferPtr(UINT nCommand, UINT nCount = 0, void** ppBufStart = NULL,
                            void** ppBufMax = NULL);

  CSocket* m_pSocket;
  BOOL m_bArchiveCompatible;
};

#endif  // __AFXSOCK_H__
