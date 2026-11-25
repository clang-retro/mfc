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
#ifdef __cplusplus
#define REFGUID const GUID&
#define REFIID const IID&
#define REFCLSID const CLSID&
#define REFFMTID const FMTID&
#define InlineIsEqualGUID(a, b) IsEqualGUID(a, b)
__WINPORT_INLINE bool IsEqualGUID(REFGUID a, REFGUID b) {
  const DWORD* x = (const DWORD*)&a;
  const DWORD* y = (const DWORD*)&b;
  return x[0] == y[0] && x[1] == y[1] && x[2] == y[2] && x[3] == y[3];
}
__WINPORT_INLINE bool operator==(REFGUID a, REFGUID b) { return IsEqualGUID(a, b); }
__WINPORT_INLINE bool operator!=(REFGUID a, REFGUID b) { return !IsEqualGUID(a, b); }
#else
// In C, as in the Windows SDK, a GUID goes by pointer.
#define REFGUID const GUID*
#define REFIID const IID*
#define REFCLSID const CLSID*
#define REFFMTID const FMTID*
#define IsEqualGUID(a, b) (!memcmp((a), (b), sizeof(GUID)))
#define InlineIsEqualGUID(a, b) IsEqualGUID(a, b)
#endif
#define IsEqualIID(a, b) IsEqualGUID(a, b)
#define IsEqualCLSID(a, b) IsEqualGUID(a, b)
#define DEFINE_GUID(name, l, w1, w2, b1, b2, b3, b4, b5, b6, b7, b8) \
  __WINPORT_EXTERN_C __attribute__((weak)) const GUID name = {l, w1, w2, {b1, b2, b3, b4, b5, b6, b7, b8}}
#define DEFINE_OLEGUID(name, l, w1, w2) DEFINE_GUID(name, l, w1, w2, 0xC0, 0, 0, 0, 0, 0, 0, 0x46)
#define GUID_NULL IID_NULL
__WINPORT_EXTERN_C const GUID IID_NULL;
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
#define PAGE_WRITECOPY 0x08
#define PAGE_EXECUTE 0x10
#define PAGE_EXECUTE_READ 0x20
#define PAGE_EXECUTE_READWRITE 0x40
#define PAGE_EXECUTE_WRITECOPY 0x80
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
  __attribute__((aligned(16))) unsigned char _Mutex[64];
} RTL_CRITICAL_SECTION, *PRTL_CRITICAL_SECTION, CRITICAL_SECTION, *PCRITICAL_SECTION,
    *LPCRITICAL_SECTION;

__WINPORT_INLINE LONG InterlockedIncrement(LONG volatile* p) { return __atomic_add_fetch(p, 1, __ATOMIC_SEQ_CST); }
__WINPORT_INLINE LONG InterlockedDecrement(LONG volatile* p) { return __atomic_sub_fetch(p, 1, __ATOMIC_SEQ_CST); }
__WINPORT_INLINE LONG InterlockedExchange(LONG volatile* p, LONG v) {
  return __atomic_exchange_n(p, v, __ATOMIC_SEQ_CST);
}
__WINPORT_INLINE LONG InterlockedExchangeAdd(LONG volatile* p, LONG v) {
  return __atomic_fetch_add(p, v, __ATOMIC_SEQ_CST);
}
__WINPORT_INLINE LONG InterlockedCompareExchange(LONG volatile* p, LONG v, LONG cmp) {
  __atomic_compare_exchange_n(p, &cmp, v, 0, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
  return cmp;
}
__WINPORT_INLINE LONGLONG InterlockedIncrement64(LONGLONG volatile* p) {
  return __atomic_add_fetch(p, 1, __ATOMIC_SEQ_CST);
}
__WINPORT_INLINE LONGLONG InterlockedDecrement64(LONGLONG volatile* p) {
  return __atomic_sub_fetch(p, 1, __ATOMIC_SEQ_CST);
}
__WINPORT_INLINE PVOID InterlockedExchangePointer(PVOID volatile* p, PVOID v) {
  return __atomic_exchange_n(p, v, __ATOMIC_SEQ_CST);
}
__WINPORT_INLINE PVOID InterlockedCompareExchangePointer(PVOID volatile* p, PVOID v, PVOID cmp) {
  __atomic_compare_exchange_n(p, &cmp, v, 0, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
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

// Security: identifiers, access control lists, security descriptors and
// access tokens, laid out as on Windows.
#define FILE_READ_EA 0x0008
#define FILE_WRITE_EA 0x0010
#define FILE_EXECUTE 0x0020
#define FILE_TRAVERSE 0x0020
#define FILE_DELETE_CHILD 0x0040
#define FILE_READ_ATTRIBUTES 0x0080
#define FILE_WRITE_ATTRIBUTES 0x0100
#define FILE_ALL_ACCESS (STANDARD_RIGHTS_REQUIRED | SYNCHRONIZE | 0x1FF)
#define FILE_GENERIC_READ (STANDARD_RIGHTS_READ | 0x0001 | FILE_READ_ATTRIBUTES | FILE_READ_EA | SYNCHRONIZE)
#define FILE_GENERIC_WRITE \
  (STANDARD_RIGHTS_WRITE | 0x0002 | FILE_WRITE_ATTRIBUTES | FILE_WRITE_EA | 0x0004 | SYNCHRONIZE)
#define FILE_GENERIC_EXECUTE (STANDARD_RIGHTS_EXECUTE | FILE_READ_ATTRIBUTES | FILE_EXECUTE | SYNCHRONIZE)
#define ACCESS_SYSTEM_SECURITY 0x01000000

typedef struct _LUID {
  DWORD LowPart;
  LONG HighPart;
} LUID, *PLUID;

typedef struct _SID_IDENTIFIER_AUTHORITY {
  BYTE Value[6];
} SID_IDENTIFIER_AUTHORITY, *PSID_IDENTIFIER_AUTHORITY;
typedef struct _SID {
  BYTE Revision;
  BYTE SubAuthorityCount;
  SID_IDENTIFIER_AUTHORITY IdentifierAuthority;
  DWORD SubAuthority[1];
} SID, *PISID;
typedef void* PSID;
#define SID_REVISION 1
#define SID_MAX_SUB_AUTHORITIES 15
#define SECURITY_MAX_SID_SIZE (sizeof(SID) - sizeof(DWORD) + SID_MAX_SUB_AUTHORITIES * sizeof(DWORD))
typedef enum _SID_NAME_USE {
  SidTypeUser = 1,
  SidTypeGroup,
  SidTypeDomain,
  SidTypeAlias,
  SidTypeWellKnownGroup,
  SidTypeDeletedAccount,
  SidTypeInvalid,
  SidTypeUnknown,
  SidTypeComputer,
  SidTypeLabel
} SID_NAME_USE, *PSID_NAME_USE;
typedef struct _SID_AND_ATTRIBUTES {
  PSID Sid;
  DWORD Attributes;
} SID_AND_ATTRIBUTES, *PSID_AND_ATTRIBUTES;

#define SECURITY_NULL_SID_AUTHORITY {{0, 0, 0, 0, 0, 0}}
#define SECURITY_WORLD_SID_AUTHORITY {{0, 0, 0, 0, 0, 1}}
#define SECURITY_LOCAL_SID_AUTHORITY {{0, 0, 0, 0, 0, 2}}
#define SECURITY_CREATOR_SID_AUTHORITY {{0, 0, 0, 0, 0, 3}}
#define SECURITY_NON_UNIQUE_AUTHORITY {{0, 0, 0, 0, 0, 4}}
#define SECURITY_NT_AUTHORITY {{0, 0, 0, 0, 0, 5}}
#define SECURITY_MANDATORY_LABEL_AUTHORITY {{0, 0, 0, 0, 0, 16}}
#define SECURITY_NULL_RID 0
#define SECURITY_WORLD_RID 0
#define SECURITY_LOCAL_RID 0
#define SECURITY_CREATOR_OWNER_RID 0
#define SECURITY_CREATOR_GROUP_RID 1
#define SECURITY_DIALUP_RID 1
#define SECURITY_NETWORK_RID 2
#define SECURITY_BATCH_RID 3
#define SECURITY_INTERACTIVE_RID 4
#define SECURITY_LOGON_IDS_RID 5
#define SECURITY_SERVICE_RID 6
#define SECURITY_ANONYMOUS_LOGON_RID 7
#define SECURITY_PROXY_RID 8
#define SECURITY_ENTERPRISE_CONTROLLERS_RID 9
#define SECURITY_SERVER_LOGON_RID SECURITY_ENTERPRISE_CONTROLLERS_RID
#define SECURITY_PRINCIPAL_SELF_RID 10
#define SECURITY_AUTHENTICATED_USER_RID 11
#define SECURITY_RESTRICTED_CODE_RID 12
#define SECURITY_TERMINAL_SERVER_RID 13
#define SECURITY_LOCAL_SYSTEM_RID 18
#define SECURITY_LOCAL_SERVICE_RID 19
#define SECURITY_NETWORK_SERVICE_RID 20
#define SECURITY_NT_NON_UNIQUE 21
#define SECURITY_BUILTIN_DOMAIN_RID 32
#define DOMAIN_USER_RID_ADMIN 500
#define DOMAIN_USER_RID_GUEST 501
#define DOMAIN_GROUP_RID_ADMINS 512
#define DOMAIN_GROUP_RID_USERS 513
#define DOMAIN_GROUP_RID_GUESTS 514
#define DOMAIN_ALIAS_RID_ADMINS 544
#define DOMAIN_ALIAS_RID_USERS 545
#define DOMAIN_ALIAS_RID_GUESTS 546
#define DOMAIN_ALIAS_RID_POWER_USERS 547
#define DOMAIN_ALIAS_RID_ACCOUNT_OPS 548
#define DOMAIN_ALIAS_RID_SYSTEM_OPS 549
#define DOMAIN_ALIAS_RID_PRINT_OPS 550
#define DOMAIN_ALIAS_RID_BACKUP_OPS 551
#define DOMAIN_ALIAS_RID_REPLICATOR 552
#define DOMAIN_ALIAS_RID_RAS_SERVERS 553
#define DOMAIN_ALIAS_RID_PREW2KCOMPACCESS 554
#define DOMAIN_ALIAS_RID_REMOTE_DESKTOP_USERS 555
#define DOMAIN_ALIAS_RID_NETWORK_CONFIGURATION_OPS 556

typedef struct _ACL {
  BYTE AclRevision;
  BYTE Sbz1;
  WORD AclSize;
  WORD AceCount;
  WORD Sbz2;
} ACL, *PACL;
#define ACL_REVISION 2
#define ACL_REVISION_DS 4
#define ACL_REVISION2 2
#define ACL_REVISION4 4
#define MIN_ACL_REVISION ACL_REVISION2
#define MAX_ACL_REVISION ACL_REVISION4
typedef struct _ACE_HEADER {
  BYTE AceType;
  BYTE AceFlags;
  WORD AceSize;
} ACE_HEADER, *PACE_HEADER;
#define ACCESS_ALLOWED_ACE_TYPE 0x0
#define ACCESS_DENIED_ACE_TYPE 0x1
#define SYSTEM_AUDIT_ACE_TYPE 0x2
#define SYSTEM_ALARM_ACE_TYPE 0x3
#define ACCESS_ALLOWED_COMPOUND_ACE_TYPE 0x4
#define ACCESS_ALLOWED_OBJECT_ACE_TYPE 0x5
#define ACCESS_DENIED_OBJECT_ACE_TYPE 0x6
#define SYSTEM_AUDIT_OBJECT_ACE_TYPE 0x7
#define SYSTEM_ALARM_OBJECT_ACE_TYPE 0x8
#define SYSTEM_MANDATORY_LABEL_ACE_TYPE 0x11
#define OBJECT_INHERIT_ACE 0x1
#define CONTAINER_INHERIT_ACE 0x2
#define NO_PROPAGATE_INHERIT_ACE 0x4
#define INHERIT_ONLY_ACE 0x8
#define INHERITED_ACE 0x10
#define VALID_INHERIT_FLAGS 0x1F
#define SUCCESSFUL_ACCESS_ACE_FLAG 0x40
#define FAILED_ACCESS_ACE_FLAG 0x80
typedef struct _ACCESS_ALLOWED_ACE {
  ACE_HEADER Header;
  ACCESS_MASK Mask;
  DWORD SidStart;
} ACCESS_ALLOWED_ACE, *PACCESS_ALLOWED_ACE;
typedef struct _ACCESS_DENIED_ACE {
  ACE_HEADER Header;
  ACCESS_MASK Mask;
  DWORD SidStart;
} ACCESS_DENIED_ACE, *PACCESS_DENIED_ACE;
typedef struct _SYSTEM_AUDIT_ACE {
  ACE_HEADER Header;
  ACCESS_MASK Mask;
  DWORD SidStart;
} SYSTEM_AUDIT_ACE, *PSYSTEM_AUDIT_ACE;
typedef struct _ACCESS_ALLOWED_OBJECT_ACE {
  ACE_HEADER Header;
  ACCESS_MASK Mask;
  DWORD Flags;
  GUID ObjectType;
  GUID InheritedObjectType;
  DWORD SidStart;
} ACCESS_ALLOWED_OBJECT_ACE, *PACCESS_ALLOWED_OBJECT_ACE;
typedef ACCESS_ALLOWED_OBJECT_ACE ACCESS_DENIED_OBJECT_ACE, *PACCESS_DENIED_OBJECT_ACE;
typedef ACCESS_ALLOWED_OBJECT_ACE SYSTEM_AUDIT_OBJECT_ACE, *PSYSTEM_AUDIT_OBJECT_ACE;
#define ACE_OBJECT_TYPE_PRESENT 0x1
#define ACE_INHERITED_OBJECT_TYPE_PRESENT 0x2
typedef enum _ACL_INFORMATION_CLASS { AclRevisionInformation = 1, AclSizeInformation } ACL_INFORMATION_CLASS;
typedef struct _ACL_REVISION_INFORMATION {
  DWORD AclRevision;
} ACL_REVISION_INFORMATION;
typedef struct _ACL_SIZE_INFORMATION {
  DWORD AceCount;
  DWORD AclBytesInUse;
  DWORD AclBytesFree;
} ACL_SIZE_INFORMATION;

typedef WORD SECURITY_DESCRIPTOR_CONTROL, *PSECURITY_DESCRIPTOR_CONTROL;
typedef DWORD SECURITY_INFORMATION, *PSECURITY_INFORMATION;
typedef void* PSECURITY_DESCRIPTOR;
#define SECURITY_DESCRIPTOR_REVISION 1
#define SECURITY_DESCRIPTOR_REVISION1 1
#define SECURITY_DESCRIPTOR_MIN_LENGTH 20
#define SE_OWNER_DEFAULTED 0x0001
#define SE_GROUP_DEFAULTED 0x0002
#define SE_DACL_PRESENT 0x0004
#define SE_DACL_DEFAULTED 0x0008
#define SE_SACL_PRESENT 0x0010
#define SE_SACL_DEFAULTED 0x0020
#define SE_DACL_AUTO_INHERIT_REQ 0x0100
#define SE_SACL_AUTO_INHERIT_REQ 0x0200
#define SE_DACL_AUTO_INHERITED 0x0400
#define SE_SACL_AUTO_INHERITED 0x0800
#define SE_DACL_PROTECTED 0x1000
#define SE_SACL_PROTECTED 0x2000
#define SE_RM_CONTROL_VALID 0x4000
#define SE_SELF_RELATIVE 0x8000
typedef struct _SECURITY_DESCRIPTOR {
  BYTE Revision;
  BYTE Sbz1;
  SECURITY_DESCRIPTOR_CONTROL Control;
  PSID Owner;
  PSID Group;
  PACL Sacl;
  PACL Dacl;
} SECURITY_DESCRIPTOR, *PISECURITY_DESCRIPTOR;
typedef struct _SECURITY_DESCRIPTOR_RELATIVE {
  BYTE Revision;
  BYTE Sbz1;
  SECURITY_DESCRIPTOR_CONTROL Control;
  DWORD Owner;
  DWORD Group;
  DWORD Sacl;
  DWORD Dacl;
} SECURITY_DESCRIPTOR_RELATIVE, *PISECURITY_DESCRIPTOR_RELATIVE;
#define OWNER_SECURITY_INFORMATION 0x00000001
#define GROUP_SECURITY_INFORMATION 0x00000002
#define DACL_SECURITY_INFORMATION 0x00000004
#define SACL_SECURITY_INFORMATION 0x00000008
#define LABEL_SECURITY_INFORMATION 0x00000010
#define PROTECTED_DACL_SECURITY_INFORMATION 0x80000000
#define PROTECTED_SACL_SECURITY_INFORMATION 0x40000000
#define UNPROTECTED_DACL_SECURITY_INFORMATION 0x20000000
#define UNPROTECTED_SACL_SECURITY_INFORMATION 0x10000000

typedef struct _GENERIC_MAPPING {
  ACCESS_MASK GenericRead;
  ACCESS_MASK GenericWrite;
  ACCESS_MASK GenericExecute;
  ACCESS_MASK GenericAll;
} GENERIC_MAPPING, *PGENERIC_MAPPING;

// Access tokens.
#define TOKEN_ASSIGN_PRIMARY 0x0001
#define TOKEN_DUPLICATE 0x0002
#define TOKEN_IMPERSONATE 0x0004
#define TOKEN_QUERY 0x0008
#define TOKEN_QUERY_SOURCE 0x0010
#define TOKEN_ADJUST_PRIVILEGES 0x0020
#define TOKEN_ADJUST_GROUPS 0x0040
#define TOKEN_ADJUST_DEFAULT 0x0080
#define TOKEN_ADJUST_SESSIONID 0x0100
#define TOKEN_ALL_ACCESS (STANDARD_RIGHTS_REQUIRED | 0x1FF)
#define TOKEN_READ (STANDARD_RIGHTS_READ | TOKEN_QUERY)
#define TOKEN_WRITE (STANDARD_RIGHTS_WRITE | TOKEN_ADJUST_PRIVILEGES | TOKEN_ADJUST_GROUPS | TOKEN_ADJUST_DEFAULT)
#define TOKEN_EXECUTE STANDARD_RIGHTS_EXECUTE
typedef enum _TOKEN_TYPE { TokenPrimary = 1, TokenImpersonation } TOKEN_TYPE, *PTOKEN_TYPE;
typedef enum _SECURITY_IMPERSONATION_LEVEL {
  SecurityAnonymous,
  SecurityIdentification,
  SecurityImpersonation,
  SecurityDelegation
} SECURITY_IMPERSONATION_LEVEL, *PSECURITY_IMPERSONATION_LEVEL;
typedef enum _TOKEN_INFORMATION_CLASS {
  TokenUser = 1,
  TokenGroups,
  TokenPrivileges,
  TokenOwner,
  TokenPrimaryGroup,
  TokenDefaultDacl,
  TokenSource,
  TokenType,
  TokenImpersonationLevel,
  TokenStatistics,
  TokenRestrictedSids,
  TokenSessionId,
  TokenGroupsAndPrivileges,
  TokenSessionReference,
  TokenSandBoxInert,
  TokenAuditPolicy,
  TokenOrigin,
  TokenElevationType,
  TokenLinkedToken,
  TokenElevation,
} TOKEN_INFORMATION_CLASS, *PTOKEN_INFORMATION_CLASS;
typedef struct _TOKEN_USER {
  SID_AND_ATTRIBUTES User;
} TOKEN_USER, *PTOKEN_USER;
typedef struct _TOKEN_GROUPS {
  DWORD GroupCount;
  SID_AND_ATTRIBUTES Groups[1];
} TOKEN_GROUPS, *PTOKEN_GROUPS;
typedef struct _TOKEN_OWNER {
  PSID Owner;
} TOKEN_OWNER, *PTOKEN_OWNER;
typedef struct _TOKEN_PRIMARY_GROUP {
  PSID PrimaryGroup;
} TOKEN_PRIMARY_GROUP, *PTOKEN_PRIMARY_GROUP;
typedef struct _TOKEN_DEFAULT_DACL {
  PACL DefaultDacl;
} TOKEN_DEFAULT_DACL, *PTOKEN_DEFAULT_DACL;
typedef struct _LUID_AND_ATTRIBUTES {
  LUID Luid;
  DWORD Attributes;
} LUID_AND_ATTRIBUTES, *PLUID_AND_ATTRIBUTES;
typedef struct _TOKEN_PRIVILEGES {
  DWORD PrivilegeCount;
  LUID_AND_ATTRIBUTES Privileges[1];
} TOKEN_PRIVILEGES, *PTOKEN_PRIVILEGES;
typedef struct _TOKEN_SOURCE {
  CHAR SourceName[8];
  LUID SourceIdentifier;
} TOKEN_SOURCE, *PTOKEN_SOURCE;
typedef struct _TOKEN_STATISTICS {
  LUID TokenId;
  LUID AuthenticationId;
  LARGE_INTEGER ExpirationTime;
  TOKEN_TYPE TokenType;
  SECURITY_IMPERSONATION_LEVEL ImpersonationLevel;
  DWORD DynamicCharged;
  DWORD DynamicAvailable;
  DWORD GroupCount;
  DWORD PrivilegeCount;
  LUID ModifiedId;
} TOKEN_STATISTICS, *PTOKEN_STATISTICS;
typedef struct _TOKEN_ELEVATION {
  DWORD TokenIsElevated;
} TOKEN_ELEVATION, *PTOKEN_ELEVATION;
#define SE_GROUP_MANDATORY 0x00000001
#define SE_GROUP_ENABLED_BY_DEFAULT 0x00000002
#define SE_GROUP_ENABLED 0x00000004
#define SE_GROUP_OWNER 0x00000008
#define SE_GROUP_USE_FOR_DENY_ONLY 0x00000010
#define SE_GROUP_LOGON_ID 0xC0000000
#define SE_GROUP_RESOURCE 0x20000000
#define SE_PRIVILEGE_ENABLED_BY_DEFAULT 0x00000001
#define SE_PRIVILEGE_ENABLED 0x00000002
#define SE_PRIVILEGE_REMOVED 0x00000004
#define SE_PRIVILEGE_USED_FOR_ACCESS 0x80000000
#define SE_CREATE_TOKEN_NAME TEXT("SeCreateTokenPrivilege")
#define SE_ASSIGNPRIMARYTOKEN_NAME TEXT("SeAssignPrimaryTokenPrivilege")
#define SE_LOCK_MEMORY_NAME TEXT("SeLockMemoryPrivilege")
#define SE_INCREASE_QUOTA_NAME TEXT("SeIncreaseQuotaPrivilege")
#define SE_MACHINE_ACCOUNT_NAME TEXT("SeMachineAccountPrivilege")
#define SE_TCB_NAME TEXT("SeTcbPrivilege")
#define SE_SECURITY_NAME TEXT("SeSecurityPrivilege")
#define SE_TAKE_OWNERSHIP_NAME TEXT("SeTakeOwnershipPrivilege")
#define SE_LOAD_DRIVER_NAME TEXT("SeLoadDriverPrivilege")
#define SE_SYSTEM_PROFILE_NAME TEXT("SeSystemProfilePrivilege")
#define SE_SYSTEMTIME_NAME TEXT("SeSystemtimePrivilege")
#define SE_PROF_SINGLE_PROCESS_NAME TEXT("SeProfileSingleProcessPrivilege")
#define SE_INC_BASE_PRIORITY_NAME TEXT("SeIncreaseBasePriorityPrivilege")
#define SE_CREATE_PAGEFILE_NAME TEXT("SeCreatePagefilePrivilege")
#define SE_CREATE_PERMANENT_NAME TEXT("SeCreatePermanentPrivilege")
#define SE_BACKUP_NAME TEXT("SeBackupPrivilege")
#define SE_RESTORE_NAME TEXT("SeRestorePrivilege")
#define SE_SHUTDOWN_NAME TEXT("SeShutdownPrivilege")
#define SE_DEBUG_NAME TEXT("SeDebugPrivilege")
#define SE_AUDIT_NAME TEXT("SeAuditPrivilege")
#define SE_SYSTEM_ENVIRONMENT_NAME TEXT("SeSystemEnvironmentPrivilege")
#define SE_CHANGE_NOTIFY_NAME TEXT("SeChangeNotifyPrivilege")
#define SE_REMOTE_SHUTDOWN_NAME TEXT("SeRemoteShutdownPrivilege")
#define SE_UNDOCK_NAME TEXT("SeUndockPrivilege")
#define SE_IMPERSONATE_NAME TEXT("SeImpersonatePrivilege")
#define SE_CREATE_GLOBAL_NAME TEXT("SeCreateGlobalPrivilege")

#endif
