// security.cpp - Windows security on a POSIX system: security identifiers
// for Unix users and groups, access tokens for the process, access
// control lists and security descriptors (as the Windows layouts), SDDL,
// and the security of kernel objects and of files.
//
// A Unix user is S-1-22-1-<uid> and a Unix group S-1-22-2-<gid>; their
// domain is the computer's name. A process's token holds its user, its
// groups, and the well-known groups every user is in (Everyone, Users,
// Authenticated Users; Administrators for root and the sudo, wheel and
// admin groups). A file's security is its owner, its group and its mode
// bits: its DACL shows them as entries for the owner, the group and
// Everyone, and a DACL set on a file becomes mode bits the same way, so
// entries for other users and groups are not kept.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#include <errno.h>
#include <grp.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <wctype.h>

#include <mutex>
#include <string>
#include <vector>

#include "winport.h"

#include <aclapi.h>
#include <sddl.h>

using namespace winport;

namespace {

typedef std::vector<BYTE> Bytes;

// ---------------------------------------------------------------------
// Identifiers.

const BYTE kUnixAuthority = 22;  // S-1-22-1-uid, S-1-22-2-gid

SID* AsSid(PSID p) { return (SID*)p; }

bool ValidSid(PSID p) {
  SID* s = AsSid(p);
  return s && s->Revision == SID_REVISION && s->SubAuthorityCount <= SID_MAX_SUB_AUTHORITIES;
}

DWORD SidLen(PSID p) { return 8 + 4 * AsSid(p)->SubAuthorityCount; }

Bytes MakeSid(BYTE authority, std::initializer_list<DWORD> subs) {
  Bytes b(8 + 4 * subs.size());
  SID* s = (SID*)b.data();
  s->Revision = SID_REVISION;
  s->SubAuthorityCount = (BYTE)subs.size();
  s->IdentifierAuthority.Value[5] = authority;
  int i = 0;
  for (DWORD d : subs) s->SubAuthority[i++] = d;
  return b;
}

Bytes SidBytes(PSID p) { return Bytes((BYTE*)p, (BYTE*)p + SidLen(p)); }

bool SameSid(PSID a, PSID b) {
  return ValidSid(a) && ValidSid(b) && SidLen(a) == SidLen(b) && memcmp(a, b, SidLen(a)) == 0;
}

ULONGLONG Authority(const SID* s) {
  ULONGLONG v = 0;
  for (int i = 0; i < 6; i++) v = v << 8 | s->IdentifierAuthority.Value[i];
  return v;
}

std::string SidString(PSID p) {
  SID* s = AsSid(p);
  char buf[32];
  ULONGLONG a = Authority(s);
  if (a >> 32)
    snprintf(buf, sizeof buf, "S-1-0x%012llX", a);
  else
    snprintf(buf, sizeof buf, "S-1-%llu", a);
  std::string r = buf;
  for (int i = 0; i < s->SubAuthorityCount; i++) r += "-" + std::to_string(s->SubAuthority[i]);
  return r;
}

bool ParseSidString(const std::string& str, Bytes* out) {
  if (str.size() < 4 || (str[0] != 'S' && str[0] != 's') || str[1] != '-') return false;
  const char* p = str.c_str() + 2;
  char* e;
  unsigned long rev = strtoul(p, &e, 10);
  if (e == p || rev != 1 || *e != '-') return false;
  p = e + 1;
  ULONGLONG a = strtoull(p, &e, 0);
  if (e == p) return false;
  std::vector<DWORD> subs;
  while (*e == '-') {
    p = e + 1;
    unsigned long long v = strtoull(p, &e, 10);
    if (e == p || v > 0xFFFFFFFFull) return false;
    subs.push_back((DWORD)v);
  }
  if (*e || subs.size() > SID_MAX_SUB_AUTHORITIES) return false;
  out->assign(8 + 4 * subs.size(), 0);
  SID* s = (SID*)out->data();
  s->Revision = 1;
  s->SubAuthorityCount = (BYTE)subs.size();
  for (int i = 5; i >= 0; i--, a >>= 8) s->IdentifierAuthority.Value[i] = (BYTE)a;
  for (size_t i = 0; i < subs.size(); i++) s->SubAuthority[i] = subs[i];
  return true;
}

// The well-known identifiers: their SDDL names, account names and kinds.
struct WellKnown {
  const char* sid;
  const char* alias;  // in SDDL, or ""
  const char* name;
  const char* domain;
  SID_NAME_USE use;
};
const WellKnown kWellKnown[] = {
    {"S-1-0-0", "", "NULL SID", "", SidTypeWellKnownGroup},
    {"S-1-1-0", "WD", "Everyone", "", SidTypeWellKnownGroup},
    {"S-1-2-0", "", "LOCAL", "", SidTypeWellKnownGroup},
    {"S-1-3-0", "CO", "CREATOR OWNER", "", SidTypeWellKnownGroup},
    {"S-1-3-1", "CG", "CREATOR GROUP", "", SidTypeWellKnownGroup},
    {"S-1-3-4", "OW", "OWNER RIGHTS", "", SidTypeWellKnownGroup},
    {"S-1-5-1", "", "DIALUP", "NT AUTHORITY", SidTypeWellKnownGroup},
    {"S-1-5-2", "NU", "NETWORK", "NT AUTHORITY", SidTypeWellKnownGroup},
    {"S-1-5-3", "", "BATCH", "NT AUTHORITY", SidTypeWellKnownGroup},
    {"S-1-5-4", "IU", "INTERACTIVE", "NT AUTHORITY", SidTypeWellKnownGroup},
    {"S-1-5-6", "SU", "SERVICE", "NT AUTHORITY", SidTypeWellKnownGroup},
    {"S-1-5-7", "AN", "ANONYMOUS LOGON", "NT AUTHORITY", SidTypeWellKnownGroup},
    {"S-1-5-8", "", "PROXY", "NT AUTHORITY", SidTypeWellKnownGroup},
    {"S-1-5-9", "ED", "ENTERPRISE DOMAIN CONTROLLERS", "NT AUTHORITY", SidTypeWellKnownGroup},
    {"S-1-5-10", "PS", "SELF", "NT AUTHORITY", SidTypeWellKnownGroup},
    {"S-1-5-11", "AU", "Authenticated Users", "NT AUTHORITY", SidTypeWellKnownGroup},
    {"S-1-5-12", "RC", "RESTRICTED", "NT AUTHORITY", SidTypeWellKnownGroup},
    {"S-1-5-13", "", "TERMINAL SERVER USER", "NT AUTHORITY", SidTypeWellKnownGroup},
    {"S-1-5-18", "SY", "SYSTEM", "NT AUTHORITY", SidTypeWellKnownGroup},
    {"S-1-5-19", "LS", "LOCAL SERVICE", "NT AUTHORITY", SidTypeWellKnownGroup},
    {"S-1-5-20", "NS", "NETWORK SERVICE", "NT AUTHORITY", SidTypeWellKnownGroup},
    {"S-1-5-32", "", "BUILTIN", "BUILTIN", SidTypeDomain},
    {"S-1-5-32-544", "BA", "Administrators", "BUILTIN", SidTypeAlias},
    {"S-1-5-32-545", "BU", "Users", "BUILTIN", SidTypeAlias},
    {"S-1-5-32-546", "BG", "Guests", "BUILTIN", SidTypeAlias},
    {"S-1-5-32-547", "PU", "Power Users", "BUILTIN", SidTypeAlias},
    {"S-1-5-32-548", "AO", "Account Operators", "BUILTIN", SidTypeAlias},
    {"S-1-5-32-549", "SO", "Server Operators", "BUILTIN", SidTypeAlias},
    {"S-1-5-32-550", "PO", "Print Operators", "BUILTIN", SidTypeAlias},
    {"S-1-5-32-551", "BO", "Backup Operators", "BUILTIN", SidTypeAlias},
    {"S-1-5-32-552", "RE", "Replicator", "BUILTIN", SidTypeAlias},
    {"S-1-5-32-554", "RU", "Pre-Windows 2000 Compatible Access", "BUILTIN", SidTypeAlias},
    {"S-1-5-32-555", "RD", "Remote Desktop Users", "BUILTIN", SidTypeAlias},
    {"S-1-5-32-556", "NO", "Network Configuration Operators", "BUILTIN", SidTypeAlias},
};

std::string ComputerName() {
  char buf[MAX_COMPUTERNAME_LENGTH + 1];
  DWORD n = sizeof buf;
  if (!GetComputerNameA(buf, &n)) return "localhost";
  return buf;
}

bool UserName(uid_t uid, std::string* out) {
  passwd pw, *r = NULL;
  char buf[4096];
  if (getpwuid_r(uid, &pw, buf, sizeof buf, &r) != 0 || !r) return false;
  *out = pw.pw_name;
  return true;
}

bool GroupName(gid_t gid, std::string* out) {
  group gr, *r = NULL;
  std::vector<char> buf(16384);
  if (getgrgid_r(gid, &gr, buf.data(), buf.size(), &r) != 0 || !r) return false;
  *out = gr.gr_name;
  return true;
}

bool IsUnixSid(PSID p, DWORD kind, DWORD* id) {
  SID* s = AsSid(p);
  if (!ValidSid(p) || Authority(s) != kUnixAuthority || s->SubAuthorityCount != 2 || s->SubAuthority[0] != kind)
    return false;
  if (id) *id = s->SubAuthority[1];
  return true;
}

Bytes UserSid(uid_t uid) { return MakeSid(kUnixAuthority, {1, (DWORD)uid}); }
Bytes GroupSid(gid_t gid) { return MakeSid(kUnixAuthority, {2, (DWORD)gid}); }
Bytes WellKnownSid(const char* s) {
  Bytes b;
  ParseSidString(s, &b);
  return b;
}

// An account's name, domain and kind, from its identifier.
bool NameOf(PSID p, std::string* name, std::string* domain, SID_NAME_USE* use) {
  std::string str = SidString(p);
  for (const WellKnown& w : kWellKnown)
    if (str == w.sid) {
      *name = w.name;
      *domain = w.domain;
      *use = w.use;
      return true;
    }
  DWORD id;
  if (IsUnixSid(p, 1, &id) && UserName((uid_t)id, name)) {
    *domain = ComputerName();
    *use = SidTypeUser;
    return true;
  }
  if (IsUnixSid(p, 2, &id) && GroupName((gid_t)id, name)) {
    *domain = ComputerName();
    *use = SidTypeAlias;
    return true;
  }
  return false;
}

bool EqualNoCase(const std::string& a, const std::string& b) { return strcasecmp(a.c_str(), b.c_str()) == 0; }

// An identifier from an account's name, "name" or "domain\name".
bool SidOf(const std::string& full, Bytes* sid, std::string* domain, SID_NAME_USE* use) {
  std::string dom, name = full;
  size_t bs = full.find('\\');
  bool hasDomain = bs != std::string::npos;
  if (hasDomain) dom = full.substr(0, bs), name = full.substr(bs + 1);
  for (const WellKnown& w : kWellKnown)
    if (EqualNoCase(name, w.name) && (!hasDomain || EqualNoCase(dom, w.domain))) {
      *sid = WellKnownSid(w.sid);
      *domain = w.domain;
      *use = w.use;
      return true;
    }
  std::string computer = ComputerName();
  if (hasDomain && !EqualNoCase(dom, computer)) return false;
  passwd pw, *pr = NULL;
  char buf[4096];
  if (getpwnam_r(name.c_str(), &pw, buf, sizeof buf, &pr) == 0 && pr) {
    *sid = UserSid(pw.pw_uid);
    *domain = computer;
    *use = SidTypeUser;
    return true;
  }
  group gr, *gr2 = NULL;
  std::vector<char> gbuf(16384);
  if (getgrnam_r(name.c_str(), &gr, gbuf.data(), gbuf.size(), &gr2) == 0 && gr2) {
    *sid = GroupSid(gr.gr_gid);
    *domain = computer;
    *use = SidTypeAlias;
    return true;
  }
  if (EqualNoCase(name, computer)) {
    *sid = MakeSid(kUnixAuthority, {1});
    *domain = computer;
    *use = SidTypeDomain;
    return true;
  }
  return false;
}

void* LocalCopy(const void* p, size_t n) {
  void* r = LocalAlloc(LMEM_FIXED, n ? n : 1);
  if (r && n) memcpy(r, p, n);
  return r;
}

// ---------------------------------------------------------------------
// Access control lists.

ACE_HEADER* AceAt(PACL acl, DWORD i) {
  BYTE* p = (BYTE*)acl + sizeof(ACL);
  BYTE* end = (BYTE*)acl + acl->AclSize;
  for (DWORD k = 0; k < acl->AceCount; k++) {
    if (p + sizeof(ACE_HEADER) > end) return NULL;
    ACE_HEADER* h = (ACE_HEADER*)p;
    if (h->AceSize < sizeof(ACE_HEADER) || p + h->AceSize > end) return NULL;
    if (k == i) return h;
    p += h->AceSize;
  }
  return NULL;
}

DWORD AclUsed(PACL acl) {
  DWORD n = sizeof(ACL);
  for (DWORD i = 0; i < acl->AceCount; i++) {
    ACE_HEADER* h = AceAt(acl, i);
    if (!h) break;
    n += h->AceSize;
  }
  return n;
}

bool ValidAcl(PACL acl) {
  if (!acl || acl->AclRevision < MIN_ACL_REVISION || acl->AclRevision > MAX_ACL_REVISION ||
      acl->AclSize < sizeof(ACL) || (acl->AclSize & 3))
    return false;
  for (DWORD i = 0; i < acl->AceCount; i++)
    if (!AceAt(acl, i)) return false;
  return true;
}

bool IsObjectAce(BYTE type) { return type >= 5 && type <= 8; }

// The identifier an ACE names.
PSID AceSid(ACE_HEADER* h) {
  if (IsObjectAce(h->AceType)) {
    ACCESS_ALLOWED_OBJECT_ACE* o = (ACCESS_ALLOWED_OBJECT_ACE*)h;
    BYTE* p = (BYTE*)&o->ObjectType;
    if (o->Flags & ACE_OBJECT_TYPE_PRESENT) p += sizeof(GUID);
    if (o->Flags & ACE_INHERITED_OBJECT_TYPE_PRESENT) p += sizeof(GUID);
    return p;
  }
  return &((ACCESS_ALLOWED_ACE*)h)->SidStart;
}

ACCESS_MASK& AceMask(ACE_HEADER* h) { return ((ACCESS_ALLOWED_ACE*)h)->Mask; }

BOOL InsertAce(PACL acl, DWORD index, const void* ace, DWORD len) {
  if (!ValidAcl(acl)) {
    SetLastError(ERROR_INVALID_ACL);
    return FALSE;
  }
  DWORD used = AclUsed(acl);
  if (used + len > acl->AclSize) {
    SetLastError(ERROR_ALLOTTED_SPACE_EXCEEDED);
    return FALSE;
  }
  BYTE* at = (BYTE*)acl + used;
  if (index < acl->AceCount) at = (BYTE*)AceAt(acl, index);
  memmove(at + len, at, (BYTE*)acl + used - at);
  memcpy(at, ace, len);
  // Count the ACEs in the inserted block.
  DWORD count = 0;
  for (const BYTE* p = (const BYTE*)ace; p < (const BYTE*)ace + len; p += ((const ACE_HEADER*)p)->AceSize) {
    count++;
    if (((const ACE_HEADER*)p)->AceSize == 0) break;
  }
  acl->AceCount += count;
  return TRUE;
}

BOOL AddSimpleAce(PACL acl, DWORD rev, BYTE type, DWORD flags, DWORD mask, PSID sid) {
  if (!ValidSid(sid)) {
    SetLastError(ERROR_INVALID_SID);
    return FALSE;
  }
  if (!acl) {
    SetLastError(ERROR_INVALID_ACL);
    return FALSE;
  }
  DWORD len = 8 + SidLen(sid);
  Bytes ace(len);
  ACE_HEADER* h = (ACE_HEADER*)ace.data();
  h->AceType = type;
  h->AceFlags = (BYTE)flags;
  h->AceSize = (WORD)len;
  AceMask(h) = mask;
  memcpy(AceSid(h), sid, SidLen(sid));
  if (rev > acl->AclRevision) acl->AclRevision = (BYTE)rev;
  return InsertAce(acl, MAXDWORD, ace.data(), len);
}

Bytes NewAcl(DWORD size) {
  Bytes b(size < sizeof(ACL) ? sizeof(ACL) : (size + 3) & ~3u);
  PACL a = (PACL)b.data();
  a->AclRevision = ACL_REVISION;
  a->AclSize = (WORD)b.size();
  return b;
}

// ---------------------------------------------------------------------
// Security descriptors: absolute (pointers) or self-relative (offsets).

bool SelfRelative(PSECURITY_DESCRIPTOR sd) { return ((SECURITY_DESCRIPTOR*)sd)->Control & SE_SELF_RELATIVE; }

struct SdParts {
  WORD control = 0;
  PSID owner = NULL, group = NULL;
  PACL sacl = NULL, dacl = NULL;
};

SdParts Parts(PSECURITY_DESCRIPTOR p) {
  SdParts r;
  if (!p) return r;
  if (SelfRelative(p)) {
    SECURITY_DESCRIPTOR_RELATIVE* s = (SECURITY_DESCRIPTOR_RELATIVE*)p;
    r.control = s->Control;
    if (s->Owner) r.owner = (BYTE*)p + s->Owner;
    if (s->Group) r.group = (BYTE*)p + s->Group;
    if (s->Sacl) r.sacl = (PACL)((BYTE*)p + s->Sacl);
    if (s->Dacl) r.dacl = (PACL)((BYTE*)p + s->Dacl);
  } else {
    SECURITY_DESCRIPTOR* s = (SECURITY_DESCRIPTOR*)p;
    r.control = s->Control;
    r.owner = s->Owner, r.group = s->Group, r.sacl = s->Sacl, r.dacl = s->Dacl;
  }
  return r;
}

Bytes MakeRelative(const SdParts& p) {
  DWORD n = sizeof(SECURITY_DESCRIPTOR_RELATIVE);
  DWORD o = 0, g = 0, s = 0, d = 0;
  bool sacl = (p.control & SE_SACL_PRESENT) && p.sacl, dacl = (p.control & SE_DACL_PRESENT) && p.dacl;
  if (sacl) s = n, n += p.sacl->AclSize;
  if (dacl) d = n, n += p.dacl->AclSize;
  if (p.owner) o = n, n += SidLen(p.owner);
  if (p.group) g = n, n += SidLen(p.group);
  Bytes b(n);
  SECURITY_DESCRIPTOR_RELATIVE* r = (SECURITY_DESCRIPTOR_RELATIVE*)b.data();
  r->Revision = SECURITY_DESCRIPTOR_REVISION;
  r->Control = p.control | SE_SELF_RELATIVE;
  r->Owner = o, r->Group = g, r->Sacl = s, r->Dacl = d;
  if (sacl) memcpy(b.data() + s, p.sacl, p.sacl->AclSize);
  if (dacl) memcpy(b.data() + d, p.dacl, p.dacl->AclSize);
  if (p.owner) memcpy(b.data() + o, p.owner, SidLen(p.owner));
  if (p.group) memcpy(b.data() + g, p.group, SidLen(p.group));
  return b;
}

DWORD SdLength(PSECURITY_DESCRIPTOR p) {
  SdParts s = Parts(p);
  if (SelfRelative(p)) return (DWORD)MakeRelative(s).size();
  DWORD n = sizeof(SECURITY_DESCRIPTOR);
  if (s.owner) n += SidLen(s.owner);
  if (s.group) n += SidLen(s.group);
  if (s.sacl) n += s.sacl->AclSize;
  if (s.dacl) n += s.dacl->AclSize;
  return n;
}

// ---------------------------------------------------------------------
// Tokens.

struct PrivilegeName {
  const char* name;
  DWORD luid;
};
const PrivilegeName kPrivileges[] = {
    {"SeCreateTokenPrivilege", 2},       {"SeAssignPrimaryTokenPrivilege", 3},
    {"SeLockMemoryPrivilege", 4},        {"SeIncreaseQuotaPrivilege", 5},
    {"SeMachineAccountPrivilege", 6},    {"SeTcbPrivilege", 7},
    {"SeSecurityPrivilege", 8},          {"SeTakeOwnershipPrivilege", 9},
    {"SeLoadDriverPrivilege", 10},       {"SeSystemProfilePrivilege", 11},
    {"SeSystemtimePrivilege", 12},       {"SeProfileSingleProcessPrivilege", 13},
    {"SeIncreaseBasePriorityPrivilege", 14}, {"SeCreatePagefilePrivilege", 15},
    {"SeCreatePermanentPrivilege", 16},  {"SeBackupPrivilege", 17},
    {"SeRestorePrivilege", 18},          {"SeShutdownPrivilege", 19},
    {"SeDebugPrivilege", 20},            {"SeAuditPrivilege", 21},
    {"SeSystemEnvironmentPrivilege", 22}, {"SeChangeNotifyPrivilege", 23},
    {"SeRemoteShutdownPrivilege", 24},   {"SeUndockPrivilege", 25},
    {"SeSyncAgentPrivilege", 26},        {"SeEnableDelegationPrivilege", 27},
    {"SeManageVolumePrivilege", 28},     {"SeImpersonatePrivilege", 29},
    {"SeCreateGlobalPrivilege", 30},     {"SeTrustedCredManAccessPrivilege", 31},
    {"SeRelabelPrivilege", 32},          {"SeIncreaseWorkingSetPrivilege", 33},
    {"SeTimeZonePrivilege", 34},         {"SeCreateSymbolicLinkPrivilege", 35},
};

struct Token : Object {
  std::mutex lock;
  Bytes user;
  std::vector<std::pair<Bytes, DWORD>> groups;  // and their attributes
  std::vector<LUID_AND_ATTRIBUTES> privileges;
  Bytes owner, primaryGroup, defaultDacl;
  TOKEN_TYPE type = TokenPrimary;
  SECURITY_IMPERSONATION_LEVEL level = SecurityAnonymous;
  LUID id, modified;
  bool elevated = false;
  Token() : Object(kToken) {
    AllocateLocallyUniqueId(&id);
    modified = id;
  }
};

bool Administrator(uid_t uid, const std::vector<gid_t>& gids) {
  if (uid == 0) return true;
  for (gid_t g : gids) {
    std::string n;
    if (GroupName(g, &n) && (n == "sudo" || n == "wheel" || n == "admin")) return true;
  }
  return false;
}

Token* MakeProcessToken() {
  Token* t = new Token;
  uid_t uid = geteuid();
  gid_t gid = getegid();
  t->user = UserSid(uid);
  t->owner = t->user;
  t->primaryGroup = GroupSid(gid);
  std::vector<gid_t> gids(1, gid);
  int n = getgroups(0, NULL);
  if (n > 0) {
    std::vector<gid_t> more(n);
    n = getgroups(n, more.data());
    for (int i = 0; i < n; i++)
      if (more[i] != gid) gids.push_back(more[i]);
  }
  const DWORD on = SE_GROUP_MANDATORY | SE_GROUP_ENABLED_BY_DEFAULT | SE_GROUP_ENABLED;
  t->groups.push_back({WellKnownSid("S-1-1-0"), on});
  for (gid_t g : gids) t->groups.push_back({GroupSid(g), on});
  t->elevated = Administrator(uid, gids);
  if (t->elevated) t->groups.push_back({WellKnownSid("S-1-5-32-544"), on | SE_GROUP_OWNER});
  t->groups.push_back({WellKnownSid("S-1-5-32-545"), on});
  t->groups.push_back({WellKnownSid("S-1-5-4"), on});
  t->groups.push_back({WellKnownSid("S-1-5-11"), on});
  t->groups.push_back({WellKnownSid("S-1-2-0"), on});
  // The privileges: a user's few, and an administrator's, disabled.
  auto add = [&](DWORD luid, DWORD attrs) { t->privileges.push_back({{luid, 0}, attrs}); };
  add(23, SE_PRIVILEGE_ENABLED_BY_DEFAULT | SE_PRIVILEGE_ENABLED);  // change notify
  add(19, 0);                                                         // shutdown
  add(25, 0);                                                         // undock
  add(33, 0);                                                         // working set
  add(34, 0);                                                         // time zone
  if (t->elevated) {
    for (DWORD l : {5u, 8u, 9u, 10u, 11u, 12u, 13u, 14u, 15u, 17u, 18u, 20u, 22u, 24u, 28u, 35u}) add(l, 0);
    add(29, SE_PRIVILEGE_ENABLED_BY_DEFAULT | SE_PRIVILEGE_ENABLED);  // impersonate
    add(30, SE_PRIVILEGE_ENABLED_BY_DEFAULT | SE_PRIVILEGE_ENABLED);  // create global
  }
  // The default DACL: the user and SYSTEM, all access.
  Bytes system = WellKnownSid("S-1-5-18");
  t->defaultDacl = NewAcl(sizeof(ACL) + 2 * (8 + 28));
  AddSimpleAce((PACL)t->defaultDacl.data(), ACL_REVISION, ACCESS_ALLOWED_ACE_TYPE, 0, GENERIC_ALL, t->user.data());
  AddSimpleAce((PACL)t->defaultDacl.data(), ACL_REVISION, ACCESS_ALLOWED_ACE_TYPE, 0, GENERIC_ALL, system.data());
  return t;
}

Token* ProcessToken() {
  static Token* t = MakeProcessToken();
  return t;
}

thread_local Token* t_impersonation = NULL;

Token* GetToken(HANDLE h) { return (Token*)FromHandle(h, kToken); }

Token* CopyToken(Token* src, TOKEN_TYPE type, SECURITY_IMPERSONATION_LEVEL level) {
  Token* t = new Token;
  std::lock_guard<std::mutex> g(src->lock);
  t->user = src->user;
  t->groups = src->groups;
  t->privileges = src->privileges;
  t->owner = src->owner;
  t->primaryGroup = src->primaryGroup;
  t->defaultDacl = src->defaultDacl;
  t->elevated = src->elevated;
  t->type = type;
  t->level = level;
  return t;
}

// The token a thread acts with: its impersonation token, or the process's.
Token* Effective() { return t_impersonation ? t_impersonation : ProcessToken(); }

bool InToken(Token* t, PSID sid, bool denyToo = false) {
  if (SameSid(t->user.data(), sid)) return true;
  for (auto& g : t->groups)
    if (SameSid(g.first.data(), sid) && ((g.second & SE_GROUP_ENABLED) || (denyToo && (g.second & SE_GROUP_USE_FOR_DENY_ONLY))))
      return true;
  return false;
}

// Fills a caller's buffer of n bytes from a block built with pointers
// relative to its start: copies, then fixes each pointer up.
struct Out {
  Bytes data;
  std::vector<size_t> fixups;  // offsets of pointer fields holding offsets
  size_t Add(const void* p, size_t n) {
    size_t at = (data.size() + 3) & ~(size_t)3;
    data.resize(at + n);
    memcpy(data.data() + at, p, n);
    return at;
  }
  size_t Reserve(size_t n) {
    size_t at = (data.size() + 7) & ~(size_t)7;
    data.resize(at + n);
    return at;
  }
  void Pointer(size_t field, size_t target) {
    *(size_t*)(data.data() + field) = target;
    fixups.push_back(field);
  }
  BOOL Copy(LPVOID buf, DWORD n, PDWORD needed) {
    if (needed) *needed = (DWORD)data.size();
    if (!buf || n < data.size()) {
      SetLastError(ERROR_INSUFFICIENT_BUFFER);
      return FALSE;
    }
    memcpy(buf, data.data(), data.size());
    for (size_t f : fixups) {
      size_t off = *(size_t*)(data.data() + f);
      *(void**)((BYTE*)buf + f) = (BYTE*)buf + off;
    }
    return TRUE;
  }
};

// ---------------------------------------------------------------------
// Objects: the security descriptor of a kernel object, and the default.

Bytes DefaultObjectSecurity() {
  Token* t = Effective();
  std::lock_guard<std::mutex> g(t->lock);
  SdParts p;
  p.control = SE_DACL_PRESENT;
  p.owner = t->owner.data();
  p.group = t->primaryGroup.data();
  p.dacl = (PACL)t->defaultDacl.data();
  return MakeRelative(p);
}

// Merges the parts that info names from a new descriptor into an old one.
Bytes Merge(const Bytes& old, SECURITY_INFORMATION info, const SdParts& in) {
  SdParts p = Parts((PSECURITY_DESCRIPTOR)old.data());
  if (info & OWNER_SECURITY_INFORMATION) p.owner = in.owner;
  if (info & GROUP_SECURITY_INFORMATION) p.group = in.group;
  if (info & DACL_SECURITY_INFORMATION) {
    p.control = (p.control & ~(SE_DACL_PRESENT | SE_DACL_DEFAULTED | SE_DACL_PROTECTED | SE_DACL_AUTO_INHERITED)) |
                (in.control & (SE_DACL_PRESENT | SE_DACL_DEFAULTED | SE_DACL_PROTECTED | SE_DACL_AUTO_INHERITED));
    p.dacl = in.dacl;
    if (info & PROTECTED_DACL_SECURITY_INFORMATION) p.control |= SE_DACL_PROTECTED;
    if (info & UNPROTECTED_DACL_SECURITY_INFORMATION) p.control &= ~SE_DACL_PROTECTED;
  }
  if (info & SACL_SECURITY_INFORMATION) {
    p.control = (p.control & ~(SE_SACL_PRESENT | SE_SACL_DEFAULTED | SE_SACL_PROTECTED | SE_SACL_AUTO_INHERITED)) |
                (in.control & (SE_SACL_PRESENT | SE_SACL_DEFAULTED | SE_SACL_PROTECTED | SE_SACL_AUTO_INHERITED));
    p.sacl = in.sacl;
  }
  p.control &= ~SE_SELF_RELATIVE;
  return MakeRelative(p);
}

std::mutex g_objectLock;

Bytes ObjectSecurity(Object* o) {
  std::lock_guard<std::mutex> g(g_objectLock);
  return o->security.empty() ? DefaultObjectSecurity() : o->security;
}

// Only the parts info asks for, as a new self-relative descriptor.
Bytes Select(const Bytes& sd, SECURITY_INFORMATION info) {
  SdParts p = Parts((PSECURITY_DESCRIPTOR)sd.data()), r;
  if (info & OWNER_SECURITY_INFORMATION) r.owner = p.owner;
  if (info & GROUP_SECURITY_INFORMATION) r.group = p.group;
  if (info & DACL_SECURITY_INFORMATION) {
    r.dacl = p.dacl;
    r.control |= p.control & (SE_DACL_PRESENT | SE_DACL_DEFAULTED | SE_DACL_PROTECTED | SE_DACL_AUTO_INHERITED);
  }
  if (info & SACL_SECURITY_INFORMATION) {
    r.sacl = p.sacl;
    r.control |= p.control & (SE_SACL_PRESENT | SE_SACL_DEFAULTED | SE_SACL_PROTECTED | SE_SACL_AUTO_INHERITED);
  }
  return MakeRelative(r);
}

// ---------------------------------------------------------------------
// Files: owner, group and mode bits.

const ACCESS_MASK kRead = FILE_GENERIC_READ, kWrite = FILE_GENERIC_WRITE, kExecute = FILE_GENERIC_EXECUTE;

ACCESS_MASK RightsOf(mode_t bits, bool owner) {
  ACCESS_MASK m = 0;
  if (bits & 4) m |= kRead;
  if (bits & 2) m |= kWrite | DELETE | FILE_DELETE_CHILD;
  if (bits & 1) m |= kExecute;
  if (owner) m |= READ_CONTROL | WRITE_DAC | WRITE_OWNER | FILE_READ_ATTRIBUTES | FILE_WRITE_ATTRIBUTES;
  if ((bits & 7) == 7 && owner) m = FILE_ALL_ACCESS;
  return m;
}

ACCESS_MASK MapFileGeneric(ACCESS_MASK m) {
  if (m & GENERIC_READ) m |= kRead;
  if (m & GENERIC_WRITE) m |= kWrite;
  if (m & GENERIC_EXECUTE) m |= kExecute;
  if (m & GENERIC_ALL) m |= FILE_ALL_ACCESS;
  return m & ~(GENERIC_READ | GENERIC_WRITE | GENERIC_EXECUTE | GENERIC_ALL);
}

mode_t BitsOf(ACCESS_MASK m) {
  m = MapFileGeneric(m);
  mode_t b = 0;
  if (m & FILE_READ_DATA) b |= 4;
  if (m & (FILE_WRITE_DATA | FILE_APPEND_DATA)) b |= 2;
  if (m & FILE_EXECUTE) b |= 1;
  return b;
}

Bytes FileSecurity(const struct stat& st) {
  Bytes owner = UserSid(st.st_uid), group = GroupSid(st.st_gid), world = WellKnownSid("S-1-1-0");
  Bytes acl = NewAcl(sizeof(ACL) + 3 * (8 + 16));
  PACL a = (PACL)acl.data();
  ACCESS_MASK mo = RightsOf((st.st_mode >> 6) & 7, true);
  ACCESS_MASK mg = RightsOf((st.st_mode >> 3) & 7, false);
  ACCESS_MASK mw = RightsOf(st.st_mode & 7, false);
  BYTE inherit = S_ISDIR(st.st_mode) ? OBJECT_INHERIT_ACE | CONTAINER_INHERIT_ACE : 0;
  AddSimpleAce(a, ACL_REVISION, ACCESS_ALLOWED_ACE_TYPE, inherit, mo, owner.data());
  if (mg) AddSimpleAce(a, ACL_REVISION, ACCESS_ALLOWED_ACE_TYPE, inherit, mg, group.data());
  if (mw) AddSimpleAce(a, ACL_REVISION, ACCESS_ALLOWED_ACE_TYPE, inherit, mw, world.data());
  SdParts p;
  p.control = SE_DACL_PRESENT;
  p.owner = owner.data();
  p.group = group.data();
  p.dacl = a;
  return MakeRelative(p);
}

// A DACL as mode bits for a file's owner and group.
mode_t ModeOf(PACL dacl, const struct stat& st) {
  if (!dacl) return 0777;  // no DACL: everyone may do anything
  Bytes owner = UserSid(st.st_uid), group = GroupSid(st.st_gid);
  static const char* const kOthers[] = {"S-1-1-0", "S-1-5-11", "S-1-5-32-545", "S-1-5-4"};
  mode_t allow[3] = {0, 0, 0}, deny[3] = {0, 0, 0};  // owner, group, others
  for (DWORD i = 0; i < dacl->AceCount; i++) {
    ACE_HEADER* h = AceAt(dacl, i);
    if (!h || (h->AceFlags & INHERIT_ONLY_ACE)) continue;
    if (h->AceType != ACCESS_ALLOWED_ACE_TYPE && h->AceType != ACCESS_DENIED_ACE_TYPE) continue;
    PSID s = AceSid(h);
    mode_t b = BitsOf(AceMask(h));
    mode_t* to = h->AceType == ACCESS_ALLOWED_ACE_TYPE ? allow : deny;
    bool other = false;
    for (const char* w : kOthers)
      if (SidString(s) == w) other = true;
    if (SameSid(s, owner.data()) || SidString(s) == "S-1-3-0") to[0] |= b;
    if (SameSid(s, group.data()) || SidString(s) == "S-1-3-1") to[1] |= b;
    if (other) to[0] |= b, to[1] |= b, to[2] |= b;
  }
  mode_t m = 0;
  for (int i = 0; i < 3; i++) m = m << 3 | (allow[i] & ~deny[i] & 7);
  return m;
}

DWORD SetFile(const std::string& path, int fd, SECURITY_INFORMATION info, PSID owner, PSID group, PACL dacl) {
  struct stat st;
  if ((fd >= 0 ? fstat(fd, &st) : stat(path.c_str(), &st)) != 0) return ErrorFromErrno(errno);
  uid_t uid = (uid_t)-1;
  gid_t gid = (gid_t)-1;
  DWORD id;
  if (info & OWNER_SECURITY_INFORMATION) {
    if (!IsUnixSid(owner, 1, &id)) return ERROR_INVALID_OWNER;
    uid = (uid_t)id;
  }
  if (info & GROUP_SECURITY_INFORMATION) {
    if (!IsUnixSid(group, 2, &id)) return ERROR_INVALID_PRIMARY_GROUP;
    gid = (gid_t)id;
  }
  if ((uid != (uid_t)-1 && uid != st.st_uid) || (gid != (gid_t)-1 && gid != st.st_gid)) {
    int r = fd >= 0 ? fchown(fd, uid, gid) : chown(path.c_str(), uid, gid);
    if (r != 0) return errno == EPERM ? ERROR_INVALID_OWNER : ErrorFromErrno(errno);
    if (uid != (uid_t)-1) st.st_uid = uid;
    if (gid != (gid_t)-1) st.st_gid = gid;
  }
  if (info & DACL_SECURITY_INFORMATION) {
    mode_t m = (st.st_mode & ~(mode_t)0777) | ModeOf(dacl, st);
    m &= 07777;
    int r = fd >= 0 ? fchmod(fd, m) : chmod(path.c_str(), m);
    if (r != 0) return ErrorFromErrno(errno);
  }
  return ERROR_SUCCESS;
}

// Hands back a descriptor and pointers into it, as GetSecurityInfo does.
DWORD GiveOut(const Bytes& whole, SECURITY_INFORMATION info, PSID* owner, PSID* group, PACL* dacl, PACL* sacl,
              PSECURITY_DESCRIPTOR* sd) {
  Bytes sel = Select(whole, info);
  void* mem = LocalCopy(sel.data(), sel.size());
  if (!mem) return ERROR_NOT_ENOUGH_MEMORY;
  SdParts p = Parts(mem);
  if (owner) *owner = p.owner;
  if (group) *group = p.group;
  if (dacl) *dacl = p.dacl;
  if (sacl) *sacl = p.sacl;
  if (sd)
    *sd = mem;
  else {
    // Without the descriptor to free, the pointers stay valid until this
    // thread's next call.
    static thread_local void* last = NULL;
    if (last) LocalFree(last);
    last = mem;
  }
  return ERROR_SUCCESS;
}

SdParts PartsFrom(SECURITY_INFORMATION info, PSID owner, PSID group, PACL dacl, PACL sacl) {
  SdParts in;
  in.owner = owner;
  in.group = group;
  if (info & DACL_SECURITY_INFORMATION) in.control |= SE_DACL_PRESENT, in.dacl = dacl;
  if (info & SACL_SECURITY_INFORMATION) in.control |= SE_SACL_PRESENT, in.sacl = sacl;
  return in;
}

bool IsCurrentProcess(HANDLE h) { return h == GetCurrentProcess(); }

// ---------------------------------------------------------------------
// SDDL.

struct Right {
  const char* alias;
  ACCESS_MASK mask;
};
// Whole sets first; a mask that is none of them is written as single
// rights, or in hex.
const Right kSets[] = {
    {"GA", GENERIC_ALL},      {"GR", GENERIC_READ},       {"GW", GENERIC_WRITE},     {"GX", GENERIC_EXECUTE},
    {"FA", FILE_ALL_ACCESS},  {"FR", FILE_GENERIC_READ},  {"FW", FILE_GENERIC_WRITE}, {"FX", FILE_GENERIC_EXECUTE},
    {"KA", KEY_ALL_ACCESS},   {"KR", KEY_READ},           {"KW", KEY_WRITE},         {"KX", KEY_EXECUTE},
};
const Right kRights[] = {
    {"GA", GENERIC_ALL}, {"GR", GENERIC_READ}, {"GW", GENERIC_WRITE}, {"GX", GENERIC_EXECUTE},
    {"CC", 0x1},         {"DC", 0x2},          {"LC", 0x4},           {"SW", 0x8},
    {"RP", 0x10},        {"WP", 0x20},         {"DT", 0x40},          {"LO", 0x80},
    {"CR", 0x100},       {"SD", DELETE},       {"RC", READ_CONTROL},  {"WD", WRITE_DAC},
    {"WO", WRITE_OWNER},
};
struct Flag {
  const char* alias;
  BYTE flag;
};
const Flag kAceFlags[] = {{"CI", CONTAINER_INHERIT_ACE}, {"OI", OBJECT_INHERIT_ACE},     {"NP", NO_PROPAGATE_INHERIT_ACE},
                          {"IO", INHERIT_ONLY_ACE},      {"ID", INHERITED_ACE},          {"SA", SUCCESSFUL_ACCESS_ACE_FLAG},
                          {"FA", FAILED_ACCESS_ACE_FLAG}};
const Flag kAceTypes[] = {{"A", ACCESS_ALLOWED_ACE_TYPE},         {"D", ACCESS_DENIED_ACE_TYPE},
                          {"AU", SYSTEM_AUDIT_ACE_TYPE},          {"AL", SYSTEM_ALARM_ACE_TYPE},
                          {"OA", ACCESS_ALLOWED_OBJECT_ACE_TYPE}, {"OD", ACCESS_DENIED_OBJECT_ACE_TYPE},
                          {"OU", SYSTEM_AUDIT_OBJECT_ACE_TYPE},   {"OL", SYSTEM_ALARM_OBJECT_ACE_TYPE},
                          {"ML", SYSTEM_MANDATORY_LABEL_ACE_TYPE}};

std::string SidToSddl(PSID p) {
  std::string s = SidString(p);
  for (const WellKnown& w : kWellKnown)
    if (*w.alias && s == w.sid) return w.alias;
  return s;
}

std::string GuidString(const GUID& g) {
  char b[40];
  snprintf(b, sizeof b, "%08x-%04x-%04x-%02x%02x-%02x%02x%02x%02x%02x%02x", (unsigned)g.Data1, g.Data2, g.Data3,
           g.Data4[0], g.Data4[1], g.Data4[2], g.Data4[3], g.Data4[4], g.Data4[5], g.Data4[6], g.Data4[7]);
  return b;
}

std::string RightsToSddl(ACCESS_MASK m) {
  for (const Right& r : kSets)
    if (m == r.mask) return r.alias;
  std::string s;
  ACCESS_MASK left = m;
  for (const Right& r : kRights)
    if ((left & r.mask) == r.mask) s += r.alias, left &= ~r.mask;
  if (left || m == 0) {
    char b[16];
    snprintf(b, sizeof b, "0x%x", (unsigned)m);
    return b;
  }
  return s;
}

std::string AclToSddl(PACL acl, WORD control, bool dacl) {
  std::string s;
  if (control & (dacl ? SE_DACL_PROTECTED : SE_SACL_PROTECTED)) s += "P";
  if (control & (dacl ? SE_DACL_AUTO_INHERIT_REQ : SE_SACL_AUTO_INHERIT_REQ)) s += "AR";
  if (control & (dacl ? SE_DACL_AUTO_INHERITED : SE_SACL_AUTO_INHERITED)) s += "AI";
  if (!acl) return s + "NO_ACCESS_CONTROL";
  for (DWORD i = 0; i < acl->AceCount; i++) {
    ACE_HEADER* h = AceAt(acl, i);
    if (!h) break;
    std::string type;
    for (const Flag& f : kAceTypes)
      if (f.flag == h->AceType) type = f.alias;
    if (type.empty()) continue;
    std::string flags;
    for (const Flag& f : kAceFlags)
      if (h->AceFlags & f.flag) flags += f.alias;
    std::string obj, inh;
    if (IsObjectAce(h->AceType)) {
      ACCESS_ALLOWED_OBJECT_ACE* o = (ACCESS_ALLOWED_OBJECT_ACE*)h;
      const GUID* g = &o->ObjectType;
      if (o->Flags & ACE_OBJECT_TYPE_PRESENT) obj = GuidString(*g++);
      if (o->Flags & ACE_INHERITED_OBJECT_TYPE_PRESENT) inh = GuidString(*g);
    }
    s += "(" + type + ";" + flags + ";" + RightsToSddl(AceMask(h)) + ";" + obj + ";" + inh + ";" +
         SidToSddl(AceSid(h)) + ")";
  }
  return s;
}

std::string SdToSddl(PSECURITY_DESCRIPTOR sd, SECURITY_INFORMATION info) {
  SdParts p = Parts(sd);
  std::string s;
  if ((info & OWNER_SECURITY_INFORMATION) && p.owner) s += "O:" + SidToSddl(p.owner);
  if ((info & GROUP_SECURITY_INFORMATION) && p.group) s += "G:" + SidToSddl(p.group);
  if ((info & DACL_SECURITY_INFORMATION) && (p.control & SE_DACL_PRESENT))
    s += "D:" + AclToSddl(p.dacl, p.control, true);
  if ((info & SACL_SECURITY_INFORMATION) && (p.control & SE_SACL_PRESENT))
    s += "S:" + AclToSddl(p.sacl, p.control, false);
  return s;
}

bool SidFromSddl(const std::string& s, Bytes* out) {
  for (const WellKnown& w : kWellKnown)
    if (*w.alias && s == w.alias) {
      *out = WellKnownSid(w.sid);
      return true;
    }
  return ParseSidString(s, out);
}

bool ParseGuid(const std::string& s, GUID* g) {
  unsigned d[11];
  if (s.size() != 36 || sscanf(s.c_str(), "%8x-%4x-%4x-%2x%2x-%2x%2x%2x%2x%2x%2x", &d[0], &d[1], &d[2], &d[3], &d[4],
                               &d[5], &d[6], &d[7], &d[8], &d[9], &d[10]) != 11)
    return false;
  g->Data1 = d[0];
  g->Data2 = (WORD)d[1];
  g->Data3 = (WORD)d[2];
  for (int i = 0; i < 8; i++) g->Data4[i] = (BYTE)d[3 + i];
  return true;
}

bool RightsFromSddl(const std::string& s, ACCESS_MASK* m) {
  *m = 0;
  if (s.size() > 2 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
    char* e;
    *m = (ACCESS_MASK)strtoul(s.c_str(), &e, 16);
    return !*e;
  }
  if (!s.empty() && isdigit((unsigned char)s[0])) {
    char* e;
    *m = (ACCESS_MASK)strtoul(s.c_str(), &e, 10);
    return !*e;
  }
  for (size_t i = 0; i < s.size(); i += 2) {
    std::string a = s.substr(i, 2);
    bool found = false;
    for (const Right& r : kSets)
      if (a == r.alias) *m |= r.mask, found = true;
    for (const Right& r : kRights)
      if (!found && a == r.alias) *m |= r.mask, found = true;
    if (!found) return false;
  }
  return true;
}

// Splits "a;b;c" into its fields.
std::vector<std::string> Fields(const std::string& s) {
  std::vector<std::string> v;
  size_t at = 0;
  for (;;) {
    size_t e = s.find(';', at);
    v.push_back(s.substr(at, e == std::string::npos ? std::string::npos : e - at));
    if (e == std::string::npos) break;
    at = e + 1;
  }
  return v;
}

bool AclFromSddl(const std::string& s, bool dacl, WORD* control, Bytes* acl, bool* null) {
  size_t i = 0;
  *null = false;
  for (;;) {
    if (s.compare(i, 1, "P") == 0 && s.compare(i, 2, "PS") != 0) {
      *control |= dacl ? SE_DACL_PROTECTED : SE_SACL_PROTECTED;
      i += 1;
    } else if (s.compare(i, 2, "AR") == 0) {
      *control |= dacl ? SE_DACL_AUTO_INHERIT_REQ : SE_SACL_AUTO_INHERIT_REQ;
      i += 2;
    } else if (s.compare(i, 2, "AI") == 0) {
      *control |= dacl ? SE_DACL_AUTO_INHERITED : SE_SACL_AUTO_INHERITED;
      i += 2;
    } else
      break;
  }
  if (s.compare(i, std::string::npos, "NO_ACCESS_CONTROL") == 0) {
    *null = true;
    return true;
  }
  *acl = NewAcl(sizeof(ACL));
  std::vector<Bytes> aces;
  while (i < s.size()) {
    if (s[i] != '(') return false;
    size_t e = s.find(')', i);
    if (e == std::string::npos) return false;
    std::vector<std::string> f = Fields(s.substr(i + 1, e - i - 1));
    i = e + 1;
    if (f.size() < 6) return false;
    int type = -1;
    for (const Flag& t : kAceTypes)
      if (f[0] == t.alias) type = t.flag;
    if (type < 0) return false;
    BYTE flags = 0;
    for (size_t k = 0; k < f[1].size(); k += 2) {
      bool found = false;
      for (const Flag& t : kAceFlags)
        if (f[1].compare(k, 2, t.alias) == 0) flags |= t.flag, found = true;
      if (!found) return false;
    }
    ACCESS_MASK m;
    if (!RightsFromSddl(f[2], &m)) return false;
    Bytes sid;
    if (!SidFromSddl(f[5], &sid)) return false;
    Bytes ace;
    if (IsObjectAce((BYTE)type)) {
      GUID g1, g2;
      DWORD oflags = 0;
      if (!f[3].empty()) {
        if (!ParseGuid(f[3], &g1)) return false;
        oflags |= ACE_OBJECT_TYPE_PRESENT;
      }
      if (!f[4].empty()) {
        if (!ParseGuid(f[4], &g2)) return false;
        oflags |= ACE_INHERITED_OBJECT_TYPE_PRESENT;
      }
      size_t n = 12 + (oflags & 1 ? 16 : 0) + (oflags & 2 ? 16 : 0) + sid.size();
      ace.assign(n, 0);
      BYTE* p = ace.data() + 12;
      *(DWORD*)(ace.data() + 8) = oflags;
      if (oflags & 1) memcpy(p, &g1, 16), p += 16;
      if (oflags & 2) memcpy(p, &g2, 16), p += 16;
      memcpy(p, sid.data(), sid.size());
    } else {
      ace.assign(8 + sid.size(), 0);
      memcpy(ace.data() + 8, sid.data(), sid.size());
    }
    ACE_HEADER* h = (ACE_HEADER*)ace.data();
    h->AceType = (BYTE)type;
    h->AceFlags = flags;
    h->AceSize = (WORD)ace.size();
    AceMask(h) = m;
    aces.push_back(ace);
  }
  size_t total = sizeof(ACL);
  bool objects = false;
  for (auto& a : aces) total += a.size(), objects |= IsObjectAce(a[0]);
  *acl = NewAcl((DWORD)total);
  PACL p = (PACL)acl->data();
  if (objects) p->AclRevision = ACL_REVISION_DS;
  for (auto& a : aces) InsertAce(p, MAXDWORD, a.data(), (DWORD)a.size());
  return true;
}

bool SdFromSddl(const std::string& s, Bytes* out) {
  SdParts p;
  Bytes owner, group, dacl, sacl;
  size_t i = 0;
  while (i < s.size()) {
    if (i + 1 >= s.size() || s[i + 1] != ':') return false;
    char kind = s[i];
    i += 2;
    // The section runs to the next "X:" outside parentheses.
    size_t e = i;
    int depth = 0;
    while (e < s.size()) {
      if (s[e] == '(') depth++;
      if (s[e] == ')') depth--;
      if (depth == 0 && e + 1 < s.size() && s[e + 1] == ':' && (s[e] == 'O' || s[e] == 'G' || s[e] == 'D' || s[e] == 'S') &&
          (e == i || s[e - 1] == ')' || kind == 'O' || kind == 'G'))
        break;
      e++;
    }
    std::string part = s.substr(i, e - i);
    i = e;
    bool null;
    switch (kind) {
      case 'O':
        if (!SidFromSddl(part, &owner)) return false;
        p.owner = owner.data();
        break;
      case 'G':
        if (!SidFromSddl(part, &group)) return false;
        p.group = group.data();
        break;
      case 'D':
        if (!AclFromSddl(part, true, &p.control, &dacl, &null)) return false;
        p.control |= SE_DACL_PRESENT;
        p.dacl = null ? NULL : (PACL)dacl.data();
        break;
      case 'S':
        if (!AclFromSddl(part, false, &p.control, &sacl, &null)) return false;
        p.control |= SE_SACL_PRESENT;
        p.sacl = null ? NULL : (PACL)sacl.data();
        break;
      default:
        return false;
    }
  }
  *out = MakeRelative(p);
  return true;
}

template <typename C>
BOOL CopyName(const std::string& s, C* buf, LPDWORD cch, bool* small) {
  std::basic_string<C> w;
  if (sizeof(C) == 1)
    w.assign(s.begin(), s.end());
  else {
    std::wstring ws = Widen(s.c_str());
    w.assign(ws.begin(), ws.end());
  }
  if (!cch) return FALSE;
  if (!buf || *cch <= w.size()) {
    *cch = (DWORD)w.size() + 1;
    *small = true;
    return FALSE;
  }
  memcpy(buf, w.c_str(), (w.size() + 1) * sizeof(C));
  *cch = (DWORD)w.size();
  return TRUE;
}

template <typename C>
BOOL LookupSid(PSID sid, C* name, LPDWORD cchName, C* domain, LPDWORD cchDomain, PSID_NAME_USE use) {
  if (!ValidSid(sid)) {
    SetLastError(ERROR_INVALID_SID);
    return FALSE;
  }
  std::string n, d;
  SID_NAME_USE u;
  if (!NameOf(sid, &n, &d, &u)) {
    SetLastError(ERROR_NONE_MAPPED);
    return FALSE;
  }
  bool small = false;
  CopyName(n, name, cchName, &small);
  CopyName(d, domain, cchDomain, &small);
  if (small) {
    SetLastError(ERROR_INSUFFICIENT_BUFFER);
    return FALSE;
  }
  if (use) *use = u;
  return TRUE;
}

template <typename C>
BOOL LookupName(const std::string& account, PSID sid, LPDWORD cbSid, C* domain, LPDWORD cchDomain,
                PSID_NAME_USE use) {
  Bytes s;
  std::string d;
  SID_NAME_USE u;
  if (!SidOf(account, &s, &d, &u)) {
    SetLastError(ERROR_NONE_MAPPED);
    return FALSE;
  }
  bool small = false;
  if (!cbSid) return FALSE;
  if (!sid || *cbSid < s.size()) small = true;
  else memcpy(sid, s.data(), s.size());
  *cbSid = (DWORD)s.size();
  CopyName(d, domain, cchDomain, &small);
  if (small) {
    SetLastError(ERROR_INSUFFICIENT_BUFFER);
    return FALSE;
  }
  if (use) *use = u;
  return TRUE;
}

DWORD SetEntries(ULONG n, PEXPLICIT_ACCESS_W entries, PACL old, PACL* out) {
  // The old entries, then each new one: denials before grants.
  std::vector<Bytes> denied, allowed, others;
  auto split = [&](ACE_HEADER* h) {
    Bytes a((BYTE*)h, (BYTE*)h + h->AceSize);
    if (h->AceType == ACCESS_DENIED_ACE_TYPE || h->AceType == ACCESS_DENIED_OBJECT_ACE_TYPE)
      denied.push_back(a);
    else if (h->AceType == ACCESS_ALLOWED_ACE_TYPE || h->AceType == ACCESS_ALLOWED_OBJECT_ACE_TYPE)
      allowed.push_back(a);
    else
      others.push_back(a);
  };
  if (old)
    for (DWORD i = 0; i < old->AceCount; i++)
      if (ACE_HEADER* h = AceAt(old, i)) split(h);
  auto removeFor = [](std::vector<Bytes>& v, const Bytes& sid) {
    for (size_t i = 0; i < v.size();)
      if (SameSid(AceSid((ACE_HEADER*)v[i].data()), (PSID)sid.data()) && !(v[i][1] & INHERITED_ACE))
        v.erase(v.begin() + i);
      else
        i++;
  };
  for (ULONG k = 0; k < n; k++) {
    EXPLICIT_ACCESS_W& e = entries[k];
    Bytes sid;
    if (e.Trustee.TrusteeForm == TRUSTEE_IS_SID) {
      if (!ValidSid((PSID)e.Trustee.ptstrName)) return ERROR_INVALID_SID;
      sid = SidBytes((PSID)e.Trustee.ptstrName);
    } else if (e.Trustee.TrusteeForm == TRUSTEE_IS_NAME) {
      std::string d;
      SID_NAME_USE u;
      std::string name = Narrow(e.Trustee.ptstrName);
      if (name == "CURRENT_USER")
        sid = ProcessToken()->user;
      else if (!SidOf(name, &sid, &d, &u))
        return ERROR_NONE_MAPPED;
    } else {
      return ERROR_INVALID_PARAMETER;
    }
    BYTE flags = (BYTE)(e.grfInheritance & VALID_INHERIT_FLAGS);
    auto make = [&](BYTE type) {
      Bytes a(8 + sid.size());
      ACE_HEADER* h = (ACE_HEADER*)a.data();
      h->AceType = type;
      h->AceFlags = flags;
      h->AceSize = (WORD)a.size();
      AceMask(h) = e.grfAccessPermissions;
      memcpy(a.data() + 8, sid.data(), sid.size());
      return a;
    };
    switch (e.grfAccessMode) {
      case GRANT_ACCESS: allowed.push_back(make(ACCESS_ALLOWED_ACE_TYPE)); break;
      case SET_ACCESS:
        removeFor(allowed, sid);
        removeFor(denied, sid);
        allowed.push_back(make(ACCESS_ALLOWED_ACE_TYPE));
        break;
      case DENY_ACCESS: denied.push_back(make(ACCESS_DENIED_ACE_TYPE)); break;
      case REVOKE_ACCESS:
        removeFor(allowed, sid);
        removeFor(denied, sid);
        break;
      case SET_AUDIT_SUCCESS:
      case SET_AUDIT_FAILURE: {
        Bytes a = make(SYSTEM_AUDIT_ACE_TYPE);
        a[1] |= e.grfAccessMode == SET_AUDIT_SUCCESS ? SUCCESSFUL_ACCESS_ACE_FLAG : FAILED_ACCESS_ACE_FLAG;
        others.push_back(a);
        break;
      }
      default: break;
    }
  }
  size_t total = sizeof(ACL);
  for (auto* v : {&denied, &allowed, &others})
    for (auto& a : *v) total += a.size();
  Bytes acl = NewAcl((DWORD)total);
  for (auto* v : {&denied, &allowed, &others})
    for (auto& a : *v) InsertAce((PACL)acl.data(), MAXDWORD, a.data(), (DWORD)a.size());
  *out = (PACL)LocalCopy(acl.data(), acl.size());
  return *out ? ERROR_SUCCESS : ERROR_NOT_ENOUGH_MEMORY;
}

}  // namespace

void winport::SetObjectSecurity(Object* o, const SECURITY_ATTRIBUTES* sa) {
  if (!o || !sa || !sa->lpSecurityDescriptor) return;
  SdParts in = Parts(sa->lpSecurityDescriptor);
  Bytes def = DefaultObjectSecurity();
  SECURITY_INFORMATION info = 0;
  if (in.owner) info |= OWNER_SECURITY_INFORMATION;
  if (in.group) info |= GROUP_SECURITY_INFORMATION;
  if (in.control & SE_DACL_PRESENT) info |= DACL_SECURITY_INFORMATION;
  if (in.control & SE_SACL_PRESENT) info |= SACL_SECURITY_INFORMATION;
  Bytes sd = Merge(def, info, in);
  std::lock_guard<std::mutex> g(g_objectLock);
  o->security = sd;
}

extern "C" {

// Identifiers.

BOOL WINAPI AllocateAndInitializeSid(PSID_IDENTIFIER_AUTHORITY auth, BYTE count, DWORD s0, DWORD s1, DWORD s2,
                                     DWORD s3, DWORD s4, DWORD s5, DWORD s6, DWORD s7, PSID* out) {
  if (!auth || !out || count > 8) {
    SetLastError(ERROR_INVALID_PARAMETER);
    return FALSE;
  }
  DWORD subs[8] = {s0, s1, s2, s3, s4, s5, s6, s7};
  SID* s = (SID*)LocalAlloc(LMEM_FIXED | LMEM_ZEROINIT, 8 + 4 * (count ? count : 1));
  if (!s) return FALSE;
  s->Revision = SID_REVISION;
  s->SubAuthorityCount = count;
  s->IdentifierAuthority = *auth;
  for (int i = 0; i < count; i++) s->SubAuthority[i] = subs[i];
  *out = s;
  return TRUE;
}

PVOID WINAPI FreeSid(PSID p) {
  if (p) LocalFree(p);
  return NULL;
}

BOOL WINAPI InitializeSid(PSID p, PSID_IDENTIFIER_AUTHORITY auth, BYTE count) {
  if (!p || !auth || count > SID_MAX_SUB_AUTHORITIES) return FALSE;
  SID* s = AsSid(p);
  s->Revision = SID_REVISION;
  s->SubAuthorityCount = count;
  s->IdentifierAuthority = *auth;
  for (int i = 0; i < count; i++) s->SubAuthority[i] = 0;
  return TRUE;
}

BOOL WINAPI IsValidSid(PSID p) { return ValidSid(p); }

BOOL WINAPI EqualSid(PSID a, PSID b) {
  SetLastError(ERROR_SUCCESS);
  return SameSid(a, b);
}

BOOL WINAPI EqualPrefixSid(PSID a, PSID b) {
  if (!ValidSid(a) || !ValidSid(b)) return FALSE;
  SID *x = AsSid(a), *y = AsSid(b);
  if (x->SubAuthorityCount != y->SubAuthorityCount || x->SubAuthorityCount == 0) return FALSE;
  return memcmp(x, y, 8 + 4 * (x->SubAuthorityCount - 1)) == 0;
}

DWORD WINAPI GetLengthSid(PSID p) { return ValidSid(p) ? SidLen(p) : 0; }
DWORD WINAPI GetSidLengthRequired(UCHAR n) { return 8 + 4 * n; }

BOOL WINAPI CopySid(DWORD n, PSID dst, PSID src) {
  if (!ValidSid(src)) {
    SetLastError(ERROR_INVALID_SID);
    return FALSE;
  }
  if (!dst || n < SidLen(src)) {
    SetLastError(ERROR_INSUFFICIENT_BUFFER);
    return FALSE;
  }
  memcpy(dst, src, SidLen(src));
  return TRUE;
}

PSID_IDENTIFIER_AUTHORITY WINAPI GetSidIdentifierAuthority(PSID p) { return &AsSid(p)->IdentifierAuthority; }
PDWORD WINAPI GetSidSubAuthority(PSID p, DWORD i) { return &AsSid(p)->SubAuthority[i]; }
PUCHAR WINAPI GetSidSubAuthorityCount(PSID p) { return &AsSid(p)->SubAuthorityCount; }

BOOL WINAPI LookupAccountSidA(LPCSTR, PSID sid, LPSTR name, LPDWORD cn, LPSTR dom, LPDWORD cd, PSID_NAME_USE use) {
  return LookupSid(sid, name, cn, dom, cd, use);
}
BOOL WINAPI LookupAccountSidW(LPCWSTR, PSID sid, LPWSTR name, LPDWORD cn, LPWSTR dom, LPDWORD cd,
                              PSID_NAME_USE use) {
  return LookupSid(sid, name, cn, dom, cd, use);
}
BOOL WINAPI LookupAccountNameA(LPCSTR, LPCSTR account, PSID sid, LPDWORD cb, LPSTR dom, LPDWORD cd,
                               PSID_NAME_USE use) {
  return LookupName(account ? account : "", sid, cb, dom, cd, use);
}
BOOL WINAPI LookupAccountNameW(LPCWSTR, LPCWSTR account, PSID sid, LPDWORD cb, LPWSTR dom, LPDWORD cd,
                               PSID_NAME_USE use) {
  return LookupName(account ? Narrow(account) : std::string(), sid, cb, dom, cd, use);
}

BOOL WINAPI LookupPrivilegeValueA(LPCSTR, LPCSTR name, PLUID luid) {
  for (const PrivilegeName& p : kPrivileges)
    if (name && strcasecmp(name, p.name) == 0) {
      luid->LowPart = p.luid;
      luid->HighPart = 0;
      return TRUE;
    }
  SetLastError(ERROR_NO_SUCH_PRIVILEGE);
  return FALSE;
}
BOOL WINAPI LookupPrivilegeValueW(LPCWSTR sys, LPCWSTR name, PLUID luid) {
  return LookupPrivilegeValueA(sys ? Narrow(sys).c_str() : NULL, name ? Narrow(name).c_str() : NULL, luid);
}
BOOL WINAPI LookupPrivilegeNameA(LPCSTR, PLUID luid, LPSTR name, LPDWORD cch) {
  for (const PrivilegeName& p : kPrivileges)
    if (luid && luid->HighPart == 0 && luid->LowPart == p.luid) {
      bool small = false;
      if (CopyName(p.name, name, cch, &small)) return TRUE;
      SetLastError(ERROR_INSUFFICIENT_BUFFER);
      return FALSE;
    }
  SetLastError(ERROR_NO_SUCH_PRIVILEGE);
  return FALSE;
}
BOOL WINAPI LookupPrivilegeNameW(LPCWSTR, PLUID luid, LPWSTR name, LPDWORD cch) {
  for (const PrivilegeName& p : kPrivileges)
    if (luid && luid->HighPart == 0 && luid->LowPart == p.luid) {
      bool small = false;
      if (CopyName(p.name, name, cch, &small)) return TRUE;
      SetLastError(ERROR_INSUFFICIENT_BUFFER);
      return FALSE;
    }
  SetLastError(ERROR_NO_SUCH_PRIVILEGE);
  return FALSE;
}

BOOL WINAPI AllocateLocallyUniqueId(PLUID luid) {
  static std::atomic<unsigned long long> next{0x10000};
  unsigned long long v = next.fetch_add(1);
  luid->LowPart = (DWORD)v;
  luid->HighPart = (LONG)(v >> 32);
  return TRUE;
}

// Access control lists.

BOOL WINAPI InitializeAcl(PACL acl, DWORD n, DWORD rev) {
  if (!acl || n < sizeof(ACL) || n > 0xFFFF || rev < MIN_ACL_REVISION || rev > MAX_ACL_REVISION) {
    SetLastError(n < sizeof(ACL) ? ERROR_INSUFFICIENT_BUFFER : ERROR_INVALID_PARAMETER);
    return FALSE;
  }
  memset(acl, 0, sizeof(ACL));
  acl->AclRevision = (BYTE)rev;
  acl->AclSize = (WORD)(n & ~3u);
  return TRUE;
}

BOOL WINAPI IsValidAcl(PACL acl) { return ValidAcl(acl); }

BOOL WINAPI GetAclInformation(PACL acl, LPVOID info, DWORD n, ACL_INFORMATION_CLASS c) {
  if (!ValidAcl(acl)) {
    SetLastError(ERROR_INVALID_ACL);
    return FALSE;
  }
  if (c == AclRevisionInformation && n >= sizeof(ACL_REVISION_INFORMATION)) {
    ((ACL_REVISION_INFORMATION*)info)->AclRevision = acl->AclRevision;
    return TRUE;
  }
  if (c == AclSizeInformation && n >= sizeof(ACL_SIZE_INFORMATION)) {
    ACL_SIZE_INFORMATION* s = (ACL_SIZE_INFORMATION*)info;
    s->AceCount = acl->AceCount;
    s->AclBytesInUse = AclUsed(acl);
    s->AclBytesFree = acl->AclSize - s->AclBytesInUse;
    return TRUE;
  }
  SetLastError(ERROR_INVALID_PARAMETER);
  return FALSE;
}

BOOL WINAPI AddAce(PACL acl, DWORD rev, DWORD start, LPVOID list, DWORD n) {
  if (!acl || !list) {
    SetLastError(ERROR_INVALID_PARAMETER);
    return FALSE;
  }
  if (rev > acl->AclRevision) acl->AclRevision = (BYTE)rev;
  return InsertAce(acl, start, list, n);
}

BOOL WINAPI DeleteAce(PACL acl, DWORD i) {
  ACE_HEADER* h = ValidAcl(acl) ? AceAt(acl, i) : NULL;
  if (!h) {
    SetLastError(ERROR_INVALID_PARAMETER);
    return FALSE;
  }
  DWORD used = AclUsed(acl), len = h->AceSize;
  BYTE* end = (BYTE*)acl + used;
  memmove(h, (BYTE*)h + len, end - ((BYTE*)h + len));
  memset(end - len, 0, len);
  acl->AceCount--;
  return TRUE;
}

BOOL WINAPI GetAce(PACL acl, DWORD i, LPVOID* out) {
  ACE_HEADER* h = acl ? AceAt(acl, i) : NULL;
  if (!h || !out) {
    SetLastError(ERROR_INVALID_PARAMETER);
    return FALSE;
  }
  *out = h;
  return TRUE;
}

BOOL WINAPI AddAccessAllowedAce(PACL acl, DWORD rev, DWORD mask, PSID sid) {
  return AddSimpleAce(acl, rev, ACCESS_ALLOWED_ACE_TYPE, 0, mask, sid);
}
BOOL WINAPI AddAccessAllowedAceEx(PACL acl, DWORD rev, DWORD flags, DWORD mask, PSID sid) {
  return AddSimpleAce(acl, rev, ACCESS_ALLOWED_ACE_TYPE, flags, mask, sid);
}
BOOL WINAPI AddAccessDeniedAce(PACL acl, DWORD rev, DWORD mask, PSID sid) {
  return AddSimpleAce(acl, rev, ACCESS_DENIED_ACE_TYPE, 0, mask, sid);
}
BOOL WINAPI AddAccessDeniedAceEx(PACL acl, DWORD rev, DWORD flags, DWORD mask, PSID sid) {
  return AddSimpleAce(acl, rev, ACCESS_DENIED_ACE_TYPE, flags, mask, sid);
}
BOOL WINAPI AddAuditAccessAce(PACL acl, DWORD rev, DWORD mask, PSID sid, BOOL success, BOOL failure) {
  return AddAuditAccessAceEx(acl, rev, 0, mask, sid, success, failure);
}
BOOL WINAPI AddAuditAccessAceEx(PACL acl, DWORD rev, DWORD flags, DWORD mask, PSID sid, BOOL success,
                                BOOL failure) {
  flags &= VALID_INHERIT_FLAGS;
  if (success) flags |= SUCCESSFUL_ACCESS_ACE_FLAG;
  if (failure) flags |= FAILED_ACCESS_ACE_FLAG;
  return AddSimpleAce(acl, rev, SYSTEM_AUDIT_ACE_TYPE, flags, mask, sid);
}

void WINAPI MapGenericMask(PDWORD mask, PGENERIC_MAPPING map) {
  if (!mask || !map) return;
  DWORD m = *mask;
  if (m & GENERIC_READ) m |= map->GenericRead;
  if (m & GENERIC_WRITE) m |= map->GenericWrite;
  if (m & GENERIC_EXECUTE) m |= map->GenericExecute;
  if (m & GENERIC_ALL) m |= map->GenericAll;
  *mask = m & ~(GENERIC_READ | GENERIC_WRITE | GENERIC_EXECUTE | GENERIC_ALL);
}

// Security descriptors.

BOOL WINAPI InitializeSecurityDescriptor(PSECURITY_DESCRIPTOR p, DWORD rev) {
  if (!p || rev != SECURITY_DESCRIPTOR_REVISION) {
    SetLastError(ERROR_INVALID_PARAMETER);
    return FALSE;
  }
  memset(p, 0, sizeof(SECURITY_DESCRIPTOR));
  ((SECURITY_DESCRIPTOR*)p)->Revision = (BYTE)rev;
  return TRUE;
}

BOOL WINAPI IsValidSecurityDescriptor(PSECURITY_DESCRIPTOR p) {
  if (!p || ((SECURITY_DESCRIPTOR*)p)->Revision != SECURITY_DESCRIPTOR_REVISION) {
    SetLastError(ERROR_INVALID_SECURITY_DESCR);
    return FALSE;
  }
  SdParts s = Parts(p);
  if ((s.owner && !ValidSid(s.owner)) || (s.group && !ValidSid(s.group)) || (s.dacl && !ValidAcl(s.dacl)) ||
      (s.sacl && !ValidAcl(s.sacl))) {
    SetLastError(ERROR_INVALID_SECURITY_DESCR);
    return FALSE;
  }
  return TRUE;
}

DWORD WINAPI GetSecurityDescriptorLength(PSECURITY_DESCRIPTOR p) { return p ? SdLength(p) : 0; }

BOOL WINAPI GetSecurityDescriptorControl(PSECURITY_DESCRIPTOR p, PSECURITY_DESCRIPTOR_CONTROL c, LPDWORD rev) {
  if (!p) return FALSE;
  if (c) *c = ((SECURITY_DESCRIPTOR*)p)->Control;
  if (rev) *rev = ((SECURITY_DESCRIPTOR*)p)->Revision;
  return TRUE;
}

BOOL WINAPI SetSecurityDescriptorControl(PSECURITY_DESCRIPTOR p, SECURITY_DESCRIPTOR_CONTROL which,
                                         SECURITY_DESCRIPTOR_CONTROL bits) {
  const WORD settable = SE_DACL_AUTO_INHERIT_REQ | SE_SACL_AUTO_INHERIT_REQ | SE_DACL_AUTO_INHERITED |
                        SE_SACL_AUTO_INHERITED | SE_DACL_PROTECTED | SE_SACL_PROTECTED;
  if (!p || (which & ~settable)) {
    SetLastError(ERROR_INVALID_PARAMETER);
    return FALSE;
  }
  SECURITY_DESCRIPTOR* s = (SECURITY_DESCRIPTOR*)p;
  s->Control = (WORD)((s->Control & ~which) | (bits & which));
  return TRUE;
}

static BOOL Absolute(PSECURITY_DESCRIPTOR p) {
  if (!p || SelfRelative(p)) {
    SetLastError(ERROR_INVALID_SECURITY_DESCR);
    return FALSE;
  }
  return TRUE;
}

BOOL WINAPI GetSecurityDescriptorOwner(PSECURITY_DESCRIPTOR p, PSID* owner, LPBOOL defaulted) {
  if (!p || !owner) return FALSE;
  *owner = Parts(p).owner;
  if (defaulted) *defaulted = (((SECURITY_DESCRIPTOR*)p)->Control & SE_OWNER_DEFAULTED) != 0;
  return TRUE;
}
BOOL WINAPI SetSecurityDescriptorOwner(PSECURITY_DESCRIPTOR p, PSID owner, BOOL defaulted) {
  if (!Absolute(p)) return FALSE;
  SECURITY_DESCRIPTOR* s = (SECURITY_DESCRIPTOR*)p;
  s->Owner = owner;
  s->Control = (WORD)((s->Control & ~SE_OWNER_DEFAULTED) | (defaulted ? SE_OWNER_DEFAULTED : 0));
  return TRUE;
}
BOOL WINAPI GetSecurityDescriptorGroup(PSECURITY_DESCRIPTOR p, PSID* group, LPBOOL defaulted) {
  if (!p || !group) return FALSE;
  *group = Parts(p).group;
  if (defaulted) *defaulted = (((SECURITY_DESCRIPTOR*)p)->Control & SE_GROUP_DEFAULTED) != 0;
  return TRUE;
}
BOOL WINAPI SetSecurityDescriptorGroup(PSECURITY_DESCRIPTOR p, PSID group, BOOL defaulted) {
  if (!Absolute(p)) return FALSE;
  SECURITY_DESCRIPTOR* s = (SECURITY_DESCRIPTOR*)p;
  s->Group = group;
  s->Control = (WORD)((s->Control & ~SE_GROUP_DEFAULTED) | (defaulted ? SE_GROUP_DEFAULTED : 0));
  return TRUE;
}
BOOL WINAPI GetSecurityDescriptorDacl(PSECURITY_DESCRIPTOR p, LPBOOL present, PACL* dacl, LPBOOL defaulted) {
  if (!p || !present || !dacl) return FALSE;
  SdParts s = Parts(p);
  *present = (s.control & SE_DACL_PRESENT) != 0;
  *dacl = *present ? s.dacl : NULL;
  if (defaulted) *defaulted = (s.control & SE_DACL_DEFAULTED) != 0;
  return TRUE;
}
BOOL WINAPI SetSecurityDescriptorDacl(PSECURITY_DESCRIPTOR p, BOOL present, PACL dacl, BOOL defaulted) {
  if (!Absolute(p)) return FALSE;
  SECURITY_DESCRIPTOR* s = (SECURITY_DESCRIPTOR*)p;
  s->Control &= ~(SE_DACL_PRESENT | SE_DACL_DEFAULTED);
  if (present) {
    s->Control |= SE_DACL_PRESENT;
    if (defaulted) s->Control |= SE_DACL_DEFAULTED;
    s->Dacl = dacl;
  } else {
    s->Dacl = NULL;
  }
  return TRUE;
}
BOOL WINAPI GetSecurityDescriptorSacl(PSECURITY_DESCRIPTOR p, LPBOOL present, PACL* sacl, LPBOOL defaulted) {
  if (!p || !present || !sacl) return FALSE;
  SdParts s = Parts(p);
  *present = (s.control & SE_SACL_PRESENT) != 0;
  *sacl = *present ? s.sacl : NULL;
  if (defaulted) *defaulted = (s.control & SE_SACL_DEFAULTED) != 0;
  return TRUE;
}
BOOL WINAPI SetSecurityDescriptorSacl(PSECURITY_DESCRIPTOR p, BOOL present, PACL sacl, BOOL defaulted) {
  if (!Absolute(p)) return FALSE;
  SECURITY_DESCRIPTOR* s = (SECURITY_DESCRIPTOR*)p;
  s->Control &= ~(SE_SACL_PRESENT | SE_SACL_DEFAULTED);
  if (present) {
    s->Control |= SE_SACL_PRESENT;
    if (defaulted) s->Control |= SE_SACL_DEFAULTED;
    s->Sacl = sacl;
  } else {
    s->Sacl = NULL;
  }
  return TRUE;
}

BOOL WINAPI MakeSelfRelativeSD(PSECURITY_DESCRIPTOR abs, PSECURITY_DESCRIPTOR rel, LPDWORD n) {
  if (!abs || !n) {
    SetLastError(ERROR_INVALID_PARAMETER);
    return FALSE;
  }
  if (SelfRelative(abs)) {
    SetLastError(ERROR_BAD_DESCRIPTOR_FORMAT);
    return FALSE;
  }
  SdParts p = Parts(abs);
  p.control &= ~SE_SELF_RELATIVE;
  Bytes b = MakeRelative(p);
  if (!rel || *n < b.size()) {
    *n = (DWORD)b.size();
    SetLastError(ERROR_INSUFFICIENT_BUFFER);
    return FALSE;
  }
  memcpy(rel, b.data(), b.size());
  return TRUE;
}

BOOL WINAPI MakeAbsoluteSD(PSECURITY_DESCRIPTOR rel, PSECURITY_DESCRIPTOR abs, LPDWORD nAbs, PACL dacl,
                           LPDWORD nDacl, PACL sacl, LPDWORD nSacl, PSID owner, LPDWORD nOwner, PSID group,
                           LPDWORD nGroup) {
  if (!rel || !SelfRelative(rel)) {
    SetLastError(ERROR_BAD_DESCRIPTOR_FORMAT);
    return FALSE;
  }
  SdParts p = Parts(rel);
  DWORD need[5] = {sizeof(SECURITY_DESCRIPTOR), p.dacl ? p.dacl->AclSize : 0u, p.sacl ? p.sacl->AclSize : 0u,
                   p.owner ? SidLen(p.owner) : 0u, p.group ? SidLen(p.group) : 0u};
  LPDWORD have[5] = {nAbs, nDacl, nSacl, nOwner, nGroup};
  bool small = false;
  for (int i = 0; i < 5; i++) {
    if (!have[i]) return FALSE;
    if (*have[i] < need[i]) small = true;
  }
  if (small) {
    for (int i = 0; i < 5; i++) *have[i] = need[i];
    SetLastError(ERROR_INSUFFICIENT_BUFFER);
    return FALSE;
  }
  SECURITY_DESCRIPTOR* s = (SECURITY_DESCRIPTOR*)abs;
  memset(s, 0, sizeof *s);
  s->Revision = SECURITY_DESCRIPTOR_REVISION;
  s->Control = p.control & ~SE_SELF_RELATIVE;
  if (p.dacl) memcpy(dacl, p.dacl, need[1]), s->Dacl = dacl;
  if (p.sacl) memcpy(sacl, p.sacl, need[2]), s->Sacl = sacl;
  if (p.owner) memcpy(owner, p.owner, need[3]), s->Owner = owner;
  if (p.group) memcpy(group, p.group, need[4]), s->Group = group;
  return TRUE;
}

// Tokens.

BOOL WINAPI OpenProcessToken(HANDLE process, DWORD, PHANDLE out) {
  if (!out) {
    SetLastError(ERROR_INVALID_PARAMETER);
    return FALSE;
  }
  if (!IsCurrentProcess(process)) {
    Object* o = FromHandle(process, kProcess);
    if (!o) return FALSE;
    // Another process: it runs as this user, as a child does.
  }
  Token* t = ProcessToken();
  t->refs.fetch_add(1);
  *out = NewHandle(t);
  return TRUE;
}

BOOL WINAPI OpenThreadToken(HANDLE thread, DWORD, BOOL, PHANDLE out) {
  if (!out) {
    SetLastError(ERROR_INVALID_PARAMETER);
    return FALSE;
  }
  *out = NULL;
  if (thread != GetCurrentThread() || !t_impersonation) {
    SetLastError(ERROR_NO_TOKEN);
    return FALSE;
  }
  t_impersonation->refs.fetch_add(1);
  *out = NewHandle(t_impersonation);
  return TRUE;
}

BOOL WINAPI GetTokenInformation(HANDLE h, TOKEN_INFORMATION_CLASS c, LPVOID buf, DWORD n, PDWORD needed) {
  Token* t = GetToken(h);
  if (!t) return FALSE;
  std::lock_guard<std::mutex> g(t->lock);
  Out o;
  switch (c) {
    case TokenUser: {
      size_t at = o.Reserve(sizeof(TOKEN_USER));
      size_t s = o.Add(t->user.data(), t->user.size());
      o.Pointer(at + offsetof(TOKEN_USER, User.Sid), s);
      break;
    }
    case TokenOwner:
    case TokenPrimaryGroup: {
      size_t at = o.Reserve(sizeof(PSID));
      const Bytes& sid = c == TokenOwner ? t->owner : t->primaryGroup;
      o.Pointer(at, o.Add(sid.data(), sid.size()));
      break;
    }
    case TokenGroups: {
      size_t count = t->groups.size();
      size_t at = o.Reserve(offsetof(TOKEN_GROUPS, Groups) + (count ? count : 1) * sizeof(SID_AND_ATTRIBUTES));
      *(DWORD*)(o.data.data() + at) = (DWORD)count;
      for (size_t i = 0; i < count; i++) {
        size_t e = at + offsetof(TOKEN_GROUPS, Groups) + i * sizeof(SID_AND_ATTRIBUTES);
        ((SID_AND_ATTRIBUTES*)(o.data.data() + e))->Attributes = t->groups[i].second;
        size_t s = o.Add(t->groups[i].first.data(), t->groups[i].first.size());
        o.Pointer(e + offsetof(SID_AND_ATTRIBUTES, Sid), s);
      }
      break;
    }
    case TokenPrivileges: {
      size_t count = t->privileges.size();
      size_t at = o.Reserve(offsetof(TOKEN_PRIVILEGES, Privileges) + (count ? count : 1) * sizeof(LUID_AND_ATTRIBUTES));
      TOKEN_PRIVILEGES* p = (TOKEN_PRIVILEGES*)(o.data.data() + at);
      p->PrivilegeCount = (DWORD)count;
      for (size_t i = 0; i < count; i++) p->Privileges[i] = t->privileges[i];
      break;
    }
    case TokenDefaultDacl: {
      size_t at = o.Reserve(sizeof(PACL));
      o.Pointer(at, o.Add(t->defaultDacl.data(), t->defaultDacl.size()));
      break;
    }
    case TokenSource: {
      TOKEN_SOURCE s = {};
      memcpy(s.SourceName, "winport", 7);
      s.SourceIdentifier = t->id;
      o.Add(&s, sizeof s);
      break;
    }
    case TokenType: {
      TOKEN_TYPE v = t->type;
      o.Add(&v, sizeof v);
      break;
    }
    case TokenImpersonationLevel: {
      if (t->type != TokenImpersonation) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
      }
      SECURITY_IMPERSONATION_LEVEL v = t->level;
      o.Add(&v, sizeof v);
      break;
    }
    case TokenStatistics: {
      TOKEN_STATISTICS s = {};
      s.TokenId = t->id;
      s.AuthenticationId.LowPart = (DWORD)getsid(0);
      s.ExpirationTime.QuadPart = 0x7FFFFFFFFFFFFFFFLL;
      s.TokenType = t->type;
      s.ImpersonationLevel = t->level;
      s.GroupCount = (DWORD)t->groups.size();
      s.PrivilegeCount = (DWORD)t->privileges.size();
      s.ModifiedId = t->modified;
      o.Add(&s, sizeof s);
      break;
    }
    case TokenSessionId: {
      DWORD v = 1;
      o.Add(&v, sizeof v);
      break;
    }
    case TokenElevation: {
      TOKEN_ELEVATION v = {t->elevated ? 1u : 0u};
      o.Add(&v, sizeof v);
      break;
    }
    case TokenElevationType: {
      DWORD v = 1;  // TokenElevationTypeDefault
      o.Add(&v, sizeof v);
      break;
    }
    default:
      SetLastError(ERROR_INVALID_PARAMETER);
      return FALSE;
  }
  return o.Copy(buf, n, needed);
}

BOOL WINAPI SetTokenInformation(HANDLE h, TOKEN_INFORMATION_CLASS c, LPVOID info, DWORD n) {
  Token* t = GetToken(h);
  if (!t) return FALSE;
  std::lock_guard<std::mutex> g(t->lock);
  switch (c) {
    case TokenOwner: {
      if (n < sizeof(TOKEN_OWNER) || !ValidSid(((TOKEN_OWNER*)info)->Owner)) break;
      PSID s = ((TOKEN_OWNER*)info)->Owner;
      if (!InToken(t, s)) {
        SetLastError(ERROR_INVALID_OWNER);
        return FALSE;
      }
      t->owner = SidBytes(s);
      AllocateLocallyUniqueId(&t->modified);
      return TRUE;
    }
    case TokenPrimaryGroup: {
      if (n < sizeof(TOKEN_PRIMARY_GROUP) || !ValidSid(((TOKEN_PRIMARY_GROUP*)info)->PrimaryGroup)) break;
      t->primaryGroup = SidBytes(((TOKEN_PRIMARY_GROUP*)info)->PrimaryGroup);
      AllocateLocallyUniqueId(&t->modified);
      return TRUE;
    }
    case TokenDefaultDacl: {
      if (n < sizeof(TOKEN_DEFAULT_DACL)) break;
      PACL a = ((TOKEN_DEFAULT_DACL*)info)->DefaultDacl;
      if (a && !ValidAcl(a)) break;
      t->defaultDacl = a ? Bytes((BYTE*)a, (BYTE*)a + a->AclSize) : NewAcl(sizeof(ACL));
      AllocateLocallyUniqueId(&t->modified);
      return TRUE;
    }
    default: break;
  }
  SetLastError(ERROR_INVALID_PARAMETER);
  return FALSE;
}

BOOL WINAPI AdjustTokenPrivileges(HANDLE h, BOOL disableAll, PTOKEN_PRIVILEGES in, DWORD n, PTOKEN_PRIVILEGES prev,
                                  PDWORD needed) {
  Token* t = GetToken(h);
  if (!t) return FALSE;
  std::lock_guard<std::mutex> g(t->lock);
  std::vector<LUID_AND_ATTRIBUTES> changed;
  bool all = true;
  auto set = [&](LUID_AND_ATTRIBUTES& p, bool on) {
    bool was = (p.Attributes & SE_PRIVILEGE_ENABLED) != 0;
    if (was == on) return;
    changed.push_back({p.Luid, p.Attributes & SE_PRIVILEGE_ENABLED});
    if (on)
      p.Attributes |= SE_PRIVILEGE_ENABLED;
    else
      p.Attributes &= ~SE_PRIVILEGE_ENABLED;
  };
  if (disableAll) {
    for (auto& p : t->privileges) set(p, false);
  } else if (in) {
    for (DWORD i = 0; i < in->PrivilegeCount; i++) {
      bool found = false;
      for (size_t k = 0; k < t->privileges.size(); k++) {
        auto& p = t->privileges[k];
        if (p.Luid.LowPart != in->Privileges[i].Luid.LowPart || p.Luid.HighPart != in->Privileges[i].Luid.HighPart)
          continue;
        found = true;
        if (in->Privileges[i].Attributes & SE_PRIVILEGE_REMOVED) {
          changed.push_back({p.Luid, p.Attributes & SE_PRIVILEGE_ENABLED});
          t->privileges.erase(t->privileges.begin() + k);
        } else {
          set(p, (in->Privileges[i].Attributes & SE_PRIVILEGE_ENABLED) != 0);
        }
        break;
      }
      if (!found) all = false;
    }
  }
  if (prev || needed) {
    DWORD size = (DWORD)(offsetof(TOKEN_PRIVILEGES, Privileges) +
                         (changed.empty() ? 1 : changed.size()) * sizeof(LUID_AND_ATTRIBUTES));
    if (needed) *needed = size;
    if (prev) {
      if (n < size) {
        SetLastError(ERROR_INSUFFICIENT_BUFFER);
        return FALSE;
      }
      prev->PrivilegeCount = (DWORD)changed.size();
      for (size_t i = 0; i < changed.size(); i++) prev->Privileges[i] = changed[i];
    }
  }
  if (!changed.empty()) AllocateLocallyUniqueId(&t->modified);
  SetLastError(all ? ERROR_SUCCESS : ERROR_NOT_ALL_ASSIGNED);
  return TRUE;
}

BOOL WINAPI AdjustTokenGroups(HANDLE h, BOOL reset, PTOKEN_GROUPS in, DWORD, PTOKEN_GROUPS prev, PDWORD needed) {
  Token* t = GetToken(h);
  if (!t) return FALSE;
  std::lock_guard<std::mutex> g(t->lock);
  if (needed) *needed = 0;
  if (prev) prev->GroupCount = 0;
  for (auto& gr : t->groups) {
    if (gr.second & SE_GROUP_MANDATORY) continue;
    bool on = (gr.second & SE_GROUP_ENABLED_BY_DEFAULT) != 0;
    if (!reset && in)
      for (DWORD i = 0; i < in->GroupCount; i++)
        if (SameSid(in->Groups[i].Sid, gr.first.data())) on = (in->Groups[i].Attributes & SE_GROUP_ENABLED) != 0;
    gr.second = on ? gr.second | SE_GROUP_ENABLED : gr.second & ~SE_GROUP_ENABLED;
  }
  SetLastError(ERROR_SUCCESS);
  return TRUE;
}

BOOL WINAPI DuplicateTokenEx(HANDLE h, DWORD, LPSECURITY_ATTRIBUTES sa, SECURITY_IMPERSONATION_LEVEL level,
                             TOKEN_TYPE type, PHANDLE out) {
  Token* t = GetToken(h);
  if (!t || !out) return FALSE;
  Token* c = CopyToken(t, type, level);
  SetObjectSecurity(c, sa);
  *out = NewHandle(c);
  return TRUE;
}

BOOL WINAPI DuplicateToken(HANDLE h, SECURITY_IMPERSONATION_LEVEL level, PHANDLE out) {
  return DuplicateTokenEx(h, 0, NULL, level, TokenImpersonation, out);
}

BOOL WINAPI SetThreadToken(PHANDLE thread, HANDLE token) {
  if (thread && *thread != GetCurrentThread()) {
    SetLastError(ERROR_ACCESS_DENIED);
    return FALSE;
  }
  Token* t = NULL;
  if (token) {
    t = GetToken(token);
    if (!t) return FALSE;
    if (t->type != TokenImpersonation) {
      SetLastError(ERROR_BAD_TOKEN_TYPE);
      return FALSE;
    }
    t->refs.fetch_add(1);
  }
  if (t_impersonation) Release(t_impersonation);
  t_impersonation = t;
  return TRUE;
}

BOOL WINAPI ImpersonateSelf(SECURITY_IMPERSONATION_LEVEL level) {
  Token* t = CopyToken(ProcessToken(), TokenImpersonation, level);
  if (t_impersonation) Release(t_impersonation);
  t_impersonation = t;
  return TRUE;
}

BOOL WINAPI RevertToSelf(void) {
  if (t_impersonation) Release(t_impersonation);
  t_impersonation = NULL;
  return TRUE;
}

BOOL WINAPI CheckTokenMembership(HANDLE h, PSID sid, PBOOL is) {
  Token* t = h ? GetToken(h) : Effective();
  if (!t || !is) return FALSE;
  if (h && t->type != TokenImpersonation) {
    SetLastError(ERROR_NO_IMPERSONATION_TOKEN);
    return FALSE;
  }
  std::lock_guard<std::mutex> g(t->lock);
  *is = InToken(t, sid);
  return TRUE;
}

BOOL WINAPI AccessCheck(PSECURITY_DESCRIPTOR sd, HANDLE h, DWORD desired, PGENERIC_MAPPING map, PVOID, LPDWORD,
                        LPDWORD granted, LPBOOL status) {
  Token* t = GetToken(h);
  if (!t || !sd || !map || !granted || !status) return FALSE;
  if (t->type != TokenImpersonation) {
    SetLastError(ERROR_NO_IMPERSONATION_TOKEN);
    return FALSE;
  }
  if (!IsValidSecurityDescriptor(sd)) return FALSE;
  SdParts p = Parts(sd);
  if (!p.owner || !p.group) {
    SetLastError(ERROR_INVALID_SECURITY_DESCR);
    return FALSE;
  }
  std::lock_guard<std::mutex> g(t->lock);
  DWORD want = desired;
  MapGenericMask(&want, map);
  bool maximum = (want & MAXIMUM_ALLOWED) != 0;
  want &= ~MAXIMUM_ALLOWED;
  DWORD all = map->GenericAll | STANDARD_RIGHTS_ALL;
  DWORD allowed = 0, denied = 0;
  if (!(p.control & SE_DACL_PRESENT) || !p.dacl) {
    allowed = all;
  } else {
    for (DWORD i = 0; i < p.dacl->AceCount; i++) {
      ACE_HEADER* a = AceAt(p.dacl, i);
      if (!a || (a->AceFlags & INHERIT_ONLY_ACE)) continue;
      DWORD m = AceMask(a);
      MapGenericMask(&m, map);
      if (a->AceType == ACCESS_DENIED_ACE_TYPE && InToken(t, AceSid(a), true))
        denied |= m & ~allowed;
      else if (a->AceType == ACCESS_ALLOWED_ACE_TYPE && InToken(t, AceSid(a)))
        allowed |= m & ~denied;
    }
  }
  // The owner may always read and change the DACL.
  if (InToken(t, p.owner)) allowed |= (READ_CONTROL | WRITE_DAC) & ~denied;
  if (maximum) {
    *granted = allowed | (want & allowed);
    *status = allowed != 0 && (want & ~allowed) == 0;
  } else {
    *granted = (want & ~allowed) == 0 ? want : 0;
    *status = (want & ~allowed) == 0 && want != 0;
  }
  if (!*status) {
    *granted = 0;
    SetLastError(ERROR_ACCESS_DENIED);
  }
  return TRUE;
}

// SDDL.

BOOL WINAPI ConvertSidToStringSidA(PSID sid, LPSTR* out) {
  if (!ValidSid(sid) || !out) {
    SetLastError(ERROR_INVALID_SID);
    return FALSE;
  }
  std::string s = SidString(sid);
  *out = (LPSTR)LocalCopy(s.c_str(), s.size() + 1);
  return *out != NULL;
}
BOOL WINAPI ConvertSidToStringSidW(PSID sid, LPWSTR* out) {
  if (!ValidSid(sid) || !out) {
    SetLastError(ERROR_INVALID_SID);
    return FALSE;
  }
  std::wstring s = Widen(SidString(sid).c_str());
  *out = (LPWSTR)LocalCopy(s.c_str(), (s.size() + 1) * sizeof(wchar_t));
  return *out != NULL;
}
BOOL WINAPI ConvertStringSidToSidA(LPCSTR str, PSID* out) {
  Bytes b;
  if (!str || !out || !SidFromSddl(str, &b)) {
    SetLastError(ERROR_INVALID_SID);
    return FALSE;
  }
  *out = LocalCopy(b.data(), b.size());
  return *out != NULL;
}
BOOL WINAPI ConvertStringSidToSidW(LPCWSTR str, PSID* out) {
  return ConvertStringSidToSidA(str ? Narrow(str).c_str() : NULL, out);
}

BOOL WINAPI ConvertSecurityDescriptorToStringSecurityDescriptorA(PSECURITY_DESCRIPTOR sd, DWORD rev,
                                                                 SECURITY_INFORMATION info, LPSTR* out,
                                                                 PULONG len) {
  if (!sd || !out || rev != SDDL_REVISION_1) {
    SetLastError(ERROR_INVALID_PARAMETER);
    return FALSE;
  }
  std::string s = SdToSddl(sd, info);
  *out = (LPSTR)LocalCopy(s.c_str(), s.size() + 1);
  if (len) *len = (ULONG)s.size() + 1;
  return *out != NULL;
}
BOOL WINAPI ConvertSecurityDescriptorToStringSecurityDescriptorW(PSECURITY_DESCRIPTOR sd, DWORD rev,
                                                                 SECURITY_INFORMATION info, LPWSTR* out,
                                                                 PULONG len) {
  if (!sd || !out || rev != SDDL_REVISION_1) {
    SetLastError(ERROR_INVALID_PARAMETER);
    return FALSE;
  }
  std::wstring s = Widen(SdToSddl(sd, info).c_str());
  *out = (LPWSTR)LocalCopy(s.c_str(), (s.size() + 1) * sizeof(wchar_t));
  if (len) *len = (ULONG)s.size() + 1;
  return *out != NULL;
}
BOOL WINAPI ConvertStringSecurityDescriptorToSecurityDescriptorA(LPCSTR str, DWORD rev, PSECURITY_DESCRIPTOR* out,
                                                                 PULONG len) {
  Bytes b;
  if (!str || !out || rev != SDDL_REVISION_1) {
    SetLastError(ERROR_INVALID_PARAMETER);
    return FALSE;
  }
  // Spaces between the parts are allowed.
  std::string s;
  for (const char* p = str; *p; p++)
    if (*p != ' ' && *p != '\t' && *p != '\n' && *p != '\r') s += *p;
  if (!SdFromSddl(s, &b)) {
    SetLastError(ERROR_INVALID_PARAMETER);
    return FALSE;
  }
  *out = LocalCopy(b.data(), b.size());
  if (len) *len = (ULONG)b.size();
  return *out != NULL;
}
BOOL WINAPI ConvertStringSecurityDescriptorToSecurityDescriptorW(LPCWSTR str, DWORD rev, PSECURITY_DESCRIPTOR* out,
                                                                 PULONG len) {
  return ConvertStringSecurityDescriptorToSecurityDescriptorA(str ? Narrow(str).c_str() : NULL, rev, out, len);
}

// Objects.

DWORD WINAPI GetSecurityInfo(HANDLE h, SE_OBJECT_TYPE type, SECURITY_INFORMATION info, PSID* owner, PSID* group,
                             PACL* dacl, PACL* sacl, PSECURITY_DESCRIPTOR* sd) {
  if (type == SE_FILE_OBJECT) {
    int fd = FileDescriptor(h);
    struct stat st;
    if (fd < 0) return ERROR_INVALID_HANDLE;
    if (fstat(fd, &st) != 0) return ErrorFromErrno(errno);
    return GiveOut(FileSecurity(st), info, owner, group, dacl, sacl, sd);
  }
  if (type == SE_KERNEL_OBJECT || type == SE_UNKNOWN_OBJECT_TYPE) {
    if (h == GetCurrentProcess() || h == GetCurrentThread())
      return GiveOut(DefaultObjectSecurity(), info, owner, group, dacl, sacl, sd);
    Object* o = FromHandle(h);
    if (!o) return ERROR_INVALID_HANDLE;
    return GiveOut(ObjectSecurity(o), info, owner, group, dacl, sacl, sd);
  }
  return ERROR_INVALID_PARAMETER;
}

DWORD WINAPI SetSecurityInfo(HANDLE h, SE_OBJECT_TYPE type, SECURITY_INFORMATION info, PSID owner, PSID group,
                             PACL dacl, PACL sacl) {
  if (type == SE_FILE_OBJECT) {
    int fd = FileDescriptor(h);
    if (fd < 0) return ERROR_INVALID_HANDLE;
    return SetFile(std::string(), fd, info, owner, group, dacl);
  }
  if (type == SE_KERNEL_OBJECT || type == SE_UNKNOWN_OBJECT_TYPE) {
    Object* o = FromHandle(h);
    if (!o) return ERROR_INVALID_HANDLE;
    if ((info & OWNER_SECURITY_INFORMATION) && !ValidSid(owner)) return ERROR_INVALID_OWNER;
    if ((info & GROUP_SECURITY_INFORMATION) && !ValidSid(group)) return ERROR_INVALID_PRIMARY_GROUP;
    Bytes old = ObjectSecurity(o);
    Bytes sd = Merge(old, info, PartsFrom(info, owner, group, dacl, sacl));
    std::lock_guard<std::mutex> g(g_objectLock);
    o->security = sd;
    return ERROR_SUCCESS;
  }
  return ERROR_INVALID_PARAMETER;
}

DWORD WINAPI GetNamedSecurityInfoA(LPCSTR name, SE_OBJECT_TYPE type, SECURITY_INFORMATION info, PSID* owner,
                                   PSID* group, PACL* dacl, PACL* sacl, PSECURITY_DESCRIPTOR* sd) {
  if (!name) return ERROR_INVALID_PARAMETER;
  if (type == SE_FILE_OBJECT) {
    struct stat st;
    if (stat(PosixPath(name).c_str(), &st) != 0) return ErrorFromErrno(errno);
    return GiveOut(FileSecurity(st), info, owner, group, dacl, sacl, sd);
  }
  if (type == SE_REGISTRY_KEY || type == SE_SERVICE) {
    // Keys and services carry the default: the user, SYSTEM and
    // Administrators may do anything.
    Bytes def = DefaultObjectSecurity();
    return GiveOut(def, info, owner, group, dacl, sacl, sd);
  }
  return ERROR_INVALID_PARAMETER;
}
DWORD WINAPI GetNamedSecurityInfoW(LPCWSTR name, SE_OBJECT_TYPE type, SECURITY_INFORMATION info, PSID* owner,
                                   PSID* group, PACL* dacl, PACL* sacl, PSECURITY_DESCRIPTOR* sd) {
  return GetNamedSecurityInfoA(name ? Narrow(name).c_str() : NULL, type, info, owner, group, dacl, sacl, sd);
}
DWORD WINAPI SetNamedSecurityInfoA(LPSTR name, SE_OBJECT_TYPE type, SECURITY_INFORMATION info, PSID owner,
                                   PSID group, PACL dacl, PACL sacl) {
  (void)sacl;
  if (!name) return ERROR_INVALID_PARAMETER;
  if (type == SE_FILE_OBJECT) return SetFile(PosixPath(name), -1, info, owner, group, dacl);
  return ERROR_CALL_NOT_IMPLEMENTED;
}
DWORD WINAPI SetNamedSecurityInfoW(LPWSTR name, SE_OBJECT_TYPE type, SECURITY_INFORMATION info, PSID owner,
                                   PSID group, PACL dacl, PACL sacl) {
  std::string n = name ? Narrow(name) : std::string();
  return SetNamedSecurityInfoA(name ? &n[0] : NULL, type, info, owner, group, dacl, sacl);
}

BOOL WINAPI GetKernelObjectSecurity(HANDLE h, SECURITY_INFORMATION info, PSECURITY_DESCRIPTOR sd, DWORD n,
                                    LPDWORD needed) {
  Object* o = FromHandle(h);
  if (!o) return FALSE;
  Bytes b = Select(ObjectSecurity(o), info);
  if (needed) *needed = (DWORD)b.size();
  if (!sd || n < b.size()) {
    SetLastError(ERROR_INSUFFICIENT_BUFFER);
    return FALSE;
  }
  memcpy(sd, b.data(), b.size());
  return TRUE;
}
BOOL WINAPI SetKernelObjectSecurity(HANDLE h, SECURITY_INFORMATION info, PSECURITY_DESCRIPTOR sd) {
  if (!sd) return FALSE;
  SdParts p = Parts(sd);
  DWORD r = SetSecurityInfo(h, SE_KERNEL_OBJECT, info, p.owner, p.group, p.dacl, p.sacl);
  if (r) SetLastError(r);
  return r == ERROR_SUCCESS;
}
BOOL WINAPI GetFileSecurityA(LPCSTR name, SECURITY_INFORMATION info, PSECURITY_DESCRIPTOR sd, DWORD n,
                             LPDWORD needed) {
  struct stat st;
  if (!name || stat(PosixPath(name).c_str(), &st) != 0) {
    SetErrorFromErrno();
    return FALSE;
  }
  Bytes b = Select(FileSecurity(st), info);
  if (needed) *needed = (DWORD)b.size();
  if (!sd || n < b.size()) {
    SetLastError(ERROR_INSUFFICIENT_BUFFER);
    return FALSE;
  }
  memcpy(sd, b.data(), b.size());
  return TRUE;
}
BOOL WINAPI GetFileSecurityW(LPCWSTR name, SECURITY_INFORMATION info, PSECURITY_DESCRIPTOR sd, DWORD n,
                             LPDWORD needed) {
  return GetFileSecurityA(name ? Narrow(name).c_str() : NULL, info, sd, n, needed);
}
BOOL WINAPI SetFileSecurityA(LPCSTR name, SECURITY_INFORMATION info, PSECURITY_DESCRIPTOR sd) {
  if (!name || !sd) return FALSE;
  SdParts p = Parts(sd);
  DWORD r = SetFile(PosixPath(name), -1, info, p.owner, p.group, p.dacl);
  if (r) SetLastError(r);
  return r == ERROR_SUCCESS;
}
BOOL WINAPI SetFileSecurityW(LPCWSTR name, SECURITY_INFORMATION info, PSECURITY_DESCRIPTOR sd) {
  return SetFileSecurityA(name ? Narrow(name).c_str() : NULL, info, sd);
}

DWORD WINAPI SetEntriesInAclW(ULONG n, PEXPLICIT_ACCESS_W e, PACL old, PACL* out) {
  if (!out || (n && !e)) return ERROR_INVALID_PARAMETER;
  return SetEntries(n, e, old, out);
}
DWORD WINAPI SetEntriesInAclA(ULONG n, PEXPLICIT_ACCESS_A e, PACL old, PACL* out) {
  if (!out || (n && !e)) return ERROR_INVALID_PARAMETER;
  std::vector<EXPLICIT_ACCESS_W> w(n);
  std::vector<std::wstring> names(n);
  for (ULONG i = 0; i < n; i++) {
    w[i].grfAccessPermissions = e[i].grfAccessPermissions;
    w[i].grfAccessMode = e[i].grfAccessMode;
    w[i].grfInheritance = e[i].grfInheritance;
    w[i].Trustee.pMultipleTrustee = NULL;
    w[i].Trustee.MultipleTrusteeOperation = NO_MULTIPLE_TRUSTEE;
    w[i].Trustee.TrusteeForm = e[i].Trustee.TrusteeForm;
    w[i].Trustee.TrusteeType = e[i].Trustee.TrusteeType;
    if (e[i].Trustee.TrusteeForm == TRUSTEE_IS_NAME) {
      names[i] = Widen(e[i].Trustee.ptstrName ? e[i].Trustee.ptstrName : "");
      w[i].Trustee.ptstrName = &names[i][0];
    } else {
      w[i].Trustee.ptstrName = (LPWSTR)e[i].Trustee.ptstrName;
    }
  }
  return SetEntries(n, w.data(), old, out);
}
void WINAPI BuildTrusteeWithSidA(PTRUSTEE_A t, PSID sid) {
  memset(t, 0, sizeof *t);
  t->TrusteeForm = TRUSTEE_IS_SID;
  t->TrusteeType = TRUSTEE_IS_UNKNOWN;
  t->ptstrName = (LPSTR)sid;
}
void WINAPI BuildTrusteeWithSidW(PTRUSTEE_W t, PSID sid) {
  memset(t, 0, sizeof *t);
  t->TrusteeForm = TRUSTEE_IS_SID;
  t->TrusteeType = TRUSTEE_IS_UNKNOWN;
  t->ptstrName = (LPWSTR)sid;
}
void WINAPI BuildTrusteeWithNameA(PTRUSTEE_A t, LPSTR name) {
  memset(t, 0, sizeof *t);
  t->TrusteeForm = TRUSTEE_IS_NAME;
  t->TrusteeType = TRUSTEE_IS_UNKNOWN;
  t->ptstrName = name;
}
void WINAPI BuildTrusteeWithNameW(PTRUSTEE_W t, LPWSTR name) {
  memset(t, 0, sizeof *t);
  t->TrusteeForm = TRUSTEE_IS_NAME;
  t->TrusteeType = TRUSTEE_IS_UNKNOWN;
  t->ptstrName = name;
}

}  // extern "C"
