// winnt.h - Windows NT types the layer uses: GUIDs, large integers,
// access rights, languages, critical sections and interlocked calls.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#ifndef _WINPORT_WINNT_H
#define _WINPORT_WINNT_H

#include <windef.h>

typedef union _LARGE_INTEGER {
  struct {
#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
    LONG HighPart;
    DWORD LowPart;
#else
    DWORD LowPart;
    LONG HighPart;
#endif
  };
  struct {
#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
    LONG HighPart;
    DWORD LowPart;
#else
    DWORD LowPart;
    LONG HighPart;
#endif
  } u;
  LONGLONG QuadPart;
} LARGE_INTEGER, *PLARGE_INTEGER;
typedef union _ULARGE_INTEGER {
  struct {
#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
    DWORD HighPart;
    DWORD LowPart;
#else
    DWORD LowPart;
    DWORD HighPart;
#endif
  };
  struct {
#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
    DWORD HighPart;
    DWORD LowPart;
#else
    DWORD LowPart;
    DWORD HighPart;
#endif
  } u;
  ULONGLONG QuadPart;
} ULARGE_INTEGER, *PULARGE_INTEGER;

#ifndef GUID_DEFINED
#define GUID_DEFINED
typedef struct _GUID {
  DWORD Data1;
  WORD Data2;
  WORD Data3;
  BYTE Data4[8];
} GUID;
#endif
typedef GUID* LPGUID;
typedef const GUID* LPCGUID;
typedef GUID IID;
typedef IID* LPIID;
typedef GUID CLSID;
typedef CLSID* LPCLSID;
typedef GUID FMTID;
typedef FMTID* LPFMTID;
#define REFGUID const GUID&
#define REFIID const IID&
#define REFCLSID const CLSID&
#define REFFMTID const FMTID&
inline bool IsEqualGUID(REFGUID a, REFGUID b) {
  const DWORD* x = (const DWORD*)&a;
  const DWORD* y = (const DWORD*)&b;
  return x[0] == y[0] && x[1] == y[1] && x[2] == y[2] && x[3] == y[3];
}
#define IsEqualIID(a, b) IsEqualGUID(a, b)
#define IsEqualCLSID(a, b) IsEqualGUID(a, b)
inline bool operator==(REFGUID a, REFGUID b) { return IsEqualGUID(a, b); }
inline bool operator!=(REFGUID a, REFGUID b) { return !IsEqualGUID(a, b); }
#define DEFINE_GUID(name, l, w1, w2, b1, b2, b3, b4, b5, b6, b7, b8) \
  extern "C" __attribute__((weak)) const GUID name = {l, w1, w2, {b1, b2, b3, b4, b5, b6, b7, b8}}
#define DEFINE_OLEGUID(name, l, w1, w2) DEFINE_GUID(name, l, w1, w2, 0xC0, 0, 0, 0, 0, 0, 0, 0x46)
#define GUID_NULL IID_NULL
extern "C" const GUID IID_NULL;
#define CLSID_NULL IID_NULL
#define FMTID_NULL IID_NULL

// Interfaces are C++ classes with virtual functions, so their layout
// matches the COM binary standard on these systems' C++ ABIs.
#define interface struct
#define MIDL_INTERFACE(x) struct
#define DECLARE_INTERFACE(iface) interface iface
#define DECLARE_INTERFACE_(iface, base) interface iface : public base
#define STDMETHOD(method) virtual HRESULT STDMETHODCALLTYPE method
#define STDMETHOD_(type, method) virtual type STDMETHODCALLTYPE method
#define STDMETHODV(method) virtual HRESULT STDMETHODVCALLTYPE method
#define STDMETHODV_(type, method) virtual type STDMETHODVCALLTYPE method
#define STDMETHODIMP HRESULT STDMETHODCALLTYPE
#define STDMETHODIMP_(type) type STDMETHODCALLTYPE
#define STDMETHODIMPV HRESULT STDMETHODVCALLTYPE
#define STDMETHODIMPV_(type) type STDMETHODVCALLTYPE
#define PURE = 0
#define THIS_
#define THIS void
#define BEGIN_INTERFACE
#define END_INTERFACE

#define MAKELANGID(p, s) ((((WORD)(s)) << 10) | (WORD)(p))
#define PRIMARYLANGID(lgid) ((WORD)(lgid) & 0x3ff)
#define SUBLANGID(lgid) ((WORD)(lgid) >> 10)
#define MAKELCID(lgid, srtid) ((DWORD)((((DWORD)((WORD)(srtid))) << 16) | ((DWORD)((WORD)(lgid)))))
#define LANGIDFROMLCID(lcid) ((WORD)(lcid))
#define SORT_DEFAULT 0
#define LANG_NEUTRAL 0x00
#define LANG_INVARIANT 0x7f
#define LANG_ENGLISH 0x09
#define LANG_FRENCH 0x0c
#define LANG_GERMAN 0x07
#define LANG_JAPANESE 0x11
#define SUBLANG_NEUTRAL 0x00
#define SUBLANG_DEFAULT 0x01
#define SUBLANG_SYS_DEFAULT 0x02
#define SUBLANG_ENGLISH_US 0x01
#define LANG_SYSTEM_DEFAULT MAKELANGID(LANG_NEUTRAL, SUBLANG_SYS_DEFAULT)
#define LANG_USER_DEFAULT MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT)
#define LOCALE_SYSTEM_DEFAULT MAKELCID(LANG_SYSTEM_DEFAULT, SORT_DEFAULT)
#define LOCALE_USER_DEFAULT MAKELCID(LANG_USER_DEFAULT, SORT_DEFAULT)
#define LOCALE_NEUTRAL MAKELCID(MAKELANGID(LANG_NEUTRAL, SUBLANG_NEUTRAL), SORT_DEFAULT)
#define LOCALE_INVARIANT MAKELCID(MAKELANGID(LANG_INVARIANT, SUBLANG_NEUTRAL), SORT_DEFAULT)

#define DELETE 0x00010000
#define READ_CONTROL 0x00020000
#define WRITE_DAC 0x00040000
#define WRITE_OWNER 0x00080000
#define SYNCHRONIZE 0x00100000
#define STANDARD_RIGHTS_REQUIRED 0x000F0000
#define STANDARD_RIGHTS_READ READ_CONTROL
#define STANDARD_RIGHTS_WRITE READ_CONTROL
#define STANDARD_RIGHTS_EXECUTE READ_CONTROL
#define STANDARD_RIGHTS_ALL 0x001F0000
#define SPECIFIC_RIGHTS_ALL 0x0000FFFF
#define MAXIMUM_ALLOWED 0x02000000
#define GENERIC_READ 0x80000000
#define GENERIC_WRITE 0x40000000
#define GENERIC_EXECUTE 0x20000000
#define GENERIC_ALL 0x10000000

#define FILE_SHARE_READ 0x00000001
#define FILE_SHARE_WRITE 0x00000002
#define FILE_SHARE_DELETE 0x00000004
#define FILE_ATTRIBUTE_READONLY 0x00000001
#define FILE_ATTRIBUTE_HIDDEN 0x00000002
#define FILE_ATTRIBUTE_SYSTEM 0x00000004
#define FILE_ATTRIBUTE_DIRECTORY 0x00000010
#define FILE_ATTRIBUTE_ARCHIVE 0x00000020
#define FILE_ATTRIBUTE_DEVICE 0x00000040
#define FILE_ATTRIBUTE_NORMAL 0x00000080
#define FILE_ATTRIBUTE_TEMPORARY 0x00000100
#define FILE_ATTRIBUTE_SPARSE_FILE 0x00000200
#define FILE_ATTRIBUTE_REPARSE_POINT 0x00000400
#define FILE_ATTRIBUTE_COMPRESSED 0x00000800
#define FILE_ATTRIBUTE_OFFLINE 0x00001000
#define FILE_ATTRIBUTE_NOT_CONTENT_INDEXED 0x00002000
#define FILE_ATTRIBUTE_ENCRYPTED 0x00004000

#define KEY_QUERY_VALUE 0x0001
#define KEY_SET_VALUE 0x0002
#define KEY_CREATE_SUB_KEY 0x0004
#define KEY_ENUMERATE_SUB_KEYS 0x0008
#define KEY_NOTIFY 0x0010
#define KEY_CREATE_LINK 0x0020
#define KEY_WOW64_64KEY 0x0100
#define KEY_WOW64_32KEY 0x0200
#define KEY_READ ((STANDARD_RIGHTS_READ | KEY_QUERY_VALUE | KEY_ENUMERATE_SUB_KEYS | KEY_NOTIFY) & ~SYNCHRONIZE)
#define KEY_WRITE ((STANDARD_RIGHTS_WRITE | KEY_SET_VALUE | KEY_CREATE_SUB_KEY) & ~SYNCHRONIZE)
#define KEY_EXECUTE (KEY_READ & ~SYNCHRONIZE)
#define KEY_ALL_ACCESS ((STANDARD_RIGHTS_ALL | KEY_QUERY_VALUE | KEY_SET_VALUE | KEY_CREATE_SUB_KEY | \
                         KEY_ENUMERATE_SUB_KEYS | KEY_NOTIFY | KEY_CREATE_LINK) & ~SYNCHRONIZE)
#define REG_NONE 0
#define REG_SZ 1
#define REG_EXPAND_SZ 2
#define REG_BINARY 3
#define REG_DWORD 4
#define REG_DWORD_LITTLE_ENDIAN 4
#define REG_DWORD_BIG_ENDIAN 5
#define REG_LINK 6
#define REG_MULTI_SZ 7
#define REG_QWORD 11
#define REG_OPTION_NON_VOLATILE 0
#define REG_OPTION_VOLATILE 1
#define REG_CREATED_NEW_KEY 1
#define REG_OPENED_EXISTING_KEY 2

#define DUPLICATE_CLOSE_SOURCE 0x00000001
#define DUPLICATE_SAME_ACCESS 0x00000002

#define PAGE_NOACCESS 0x01
#define PAGE_READONLY 0x02
#define PAGE_READWRITE 0x04
#define MEM_COMMIT 0x1000
#define MEM_RESERVE 0x2000
#define MEM_RELEASE 0x8000

#define VER_PLATFORM_WIN32s 0
#define VER_PLATFORM_WIN32_WINDOWS 1
#define VER_PLATFORM_WIN32_NT 2

#define MAXCHAR 0x7f
#define MAXSHORT 0x7fff
#define MAXLONG 0x7fffffff
#define MAXBYTE 0xff
#define MAXWORD 0xffff
#define MAXDWORD 0xffffffff

#define UNREFERENCED_PARAMETER(P) (void)(P)
#define DBG_UNREFERENCED_PARAMETER(P) (void)(P)
#define DBG_UNREFERENCED_LOCAL_VARIABLE(V) (void)(V)
#define FIELD_OFFSET(type, field) ((LONG)offsetof(type, field))
#define RTL_FIELD_SIZE(type, field) (sizeof(((type*)0)->field))
#define CONTAINING_RECORD(address, type, field) \
  ((type*)((PCHAR)(address) - (ULONG_PTR)(&((type*)0)->field)))
#define C_ASSERT(e) static_assert(e, #e)

typedef struct _LIST_ENTRY {
  struct _LIST_ENTRY* Flink;
  struct _LIST_ENTRY* Blink;
} LIST_ENTRY, *PLIST_ENTRY;

// A critical section is a recursive mutex; the structure has room for
// the mutex of either system and keeps the documented fields.
typedef struct _RTL_CRITICAL_SECTION {
  void* DebugInfo;
  LONG LockCount;
  LONG RecursionCount;
  HANDLE OwningThread;
  HANDLE LockSemaphore;
  ULONG_PTR SpinCount;
  alignas(16) unsigned char _Mutex[64];
} RTL_CRITICAL_SECTION, *PRTL_CRITICAL_SECTION, CRITICAL_SECTION, *PCRITICAL_SECTION,
    *LPCRITICAL_SECTION;

inline LONG InterlockedIncrement(LONG volatile* p) { return __atomic_add_fetch(p, 1, __ATOMIC_SEQ_CST); }
inline LONG InterlockedDecrement(LONG volatile* p) { return __atomic_sub_fetch(p, 1, __ATOMIC_SEQ_CST); }
inline LONG InterlockedExchange(LONG volatile* p, LONG v) {
  return __atomic_exchange_n(p, v, __ATOMIC_SEQ_CST);
}
inline LONG InterlockedExchangeAdd(LONG volatile* p, LONG v) {
  return __atomic_fetch_add(p, v, __ATOMIC_SEQ_CST);
}
inline LONG InterlockedCompareExchange(LONG volatile* p, LONG v, LONG cmp) {
  __atomic_compare_exchange_n(p, &cmp, v, false, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
  return cmp;
}
inline LONGLONG InterlockedIncrement64(LONGLONG volatile* p) {
  return __atomic_add_fetch(p, 1, __ATOMIC_SEQ_CST);
}
inline LONGLONG InterlockedDecrement64(LONGLONG volatile* p) {
  return __atomic_sub_fetch(p, 1, __ATOMIC_SEQ_CST);
}
inline PVOID InterlockedExchangePointer(PVOID volatile* p, PVOID v) {
  return __atomic_exchange_n(p, v, __ATOMIC_SEQ_CST);
}
inline PVOID InterlockedCompareExchangePointer(PVOID volatile* p, PVOID v, PVOID cmp) {
  __atomic_compare_exchange_n(p, &cmp, v, false, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
  return cmp;
}
#define MemoryBarrier() __atomic_thread_fence(__ATOMIC_SEQ_CST)

#define RtlMoveMemory(d, s, n) memmove((d), (s), (n))
#define RtlCopyMemory(d, s, n) memcpy((d), (s), (n))
#define RtlFillMemory(d, n, f) memset((d), (f), (n))
#define RtlZeroMemory(d, n) memset((d), 0, (n))
#define MoveMemory RtlMoveMemory
#define CopyMemory RtlCopyMemory
#define FillMemory RtlFillMemory
#define ZeroMemory RtlZeroMemory
#define SecureZeroMemory RtlZeroMemory

typedef struct _MEMORY_BASIC_INFORMATION {
  PVOID BaseAddress;
  PVOID AllocationBase;
  DWORD AllocationProtect;
  SIZE_T RegionSize;
  DWORD State;
  DWORD Protect;
  DWORD Type;
} MEMORY_BASIC_INFORMATION, *PMEMORY_BASIC_INFORMATION;

#endif
