// atlsecurity.h - ATL's security classes: CSid (and the well-known ones
// in Sids), CDacl and CSacl, CSecurityDesc, CSecurityAttributes,
// CAccessToken, and AtlGetDacl, AtlSetDacl and their kin for kernel
// objects, files and registry keys.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#ifndef __ATLSECURITY_H__
#define __ATLSECURITY_H__

#include <atlbase.h>
#ifndef __AFX_H__
#include <atlstr.h>
#endif
#include <atlcoll.h>
#include <aclapi.h>
#include <sddl.h>
#include <vector>

namespace ATL {

// ---------------------------------------------------------------------
// CSid

class CSid {
public:
  typedef CAtlArray<CSid> CSidArray;

  CSid() : m_bValid(false), m_sidnameuse(SidTypeInvalid) {}
  CSid(const CSid& rhs) { *this = rhs; }
  CSid(const SID& rhs) : m_bValid(false), m_sidnameuse(SidTypeInvalid) { _Copy(&rhs); }
  explicit CSid(const SID* pSid, LPCTSTR pszSystem = NULL)
      : m_bValid(false), m_sidnameuse(SidTypeInvalid) {
    if (!LoadAccount(pSid, pszSystem)) _Copy(pSid);
  }
  explicit CSid(LPCTSTR pszAccountName, LPCTSTR pszSystem = NULL)
      : m_bValid(false), m_sidnameuse(SidTypeInvalid) {
    if (!LoadAccount(pszAccountName, pszSystem)) AtlThrowLastWin32();
  }
  // From an authority and up to eight subauthorities. A variadic
  // template rather than C varargs: a BYTE count before "..." is undefined
  // behaviour, and the template accepts the same call sites.
  template <class... _Subs>
  CSid(const SID_IDENTIFIER_AUTHORITY& IdentifierAuthority, BYTE nSubAuthorityCount,
       _Subs... subs)
      : m_bValid(false), m_sidnameuse(SidTypeInvalid) {
    const DWORD rgSub[] = {0, (DWORD)subs...};
    if (nSubAuthorityCount > SID_MAX_SUB_AUTHORITIES || nSubAuthorityCount > sizeof...(subs))
      AtlThrow(E_INVALIDARG);
    SID* pSid = (SID*)m_buf;
    ::InitializeSid(pSid, const_cast<SID_IDENTIFIER_AUTHORITY*>(&IdentifierAuthority),
                    nSubAuthorityCount);
    for (BYTE i = 0; i < nSubAuthorityCount; i++) *::GetSidSubAuthority(pSid, i) = rgSub[i + 1];
    m_bValid = true;
  }

  CSid& operator=(const CSid& rhs) {
    if (this == &rhs) return *this;
    ::memcpy(m_buf, rhs.m_buf, sizeof(m_buf));
    m_bValid = rhs.m_bValid;
    m_sidnameuse = rhs.m_sidnameuse;
    m_strSid = rhs.m_strSid;
    m_strAccountName = rhs.m_strAccountName;
    m_strDomain = rhs.m_strDomain;
    m_strSystem = rhs.m_strSystem;
    return *this;
  }
  CSid& operator=(const SID& rhs) {
    _Clear();
    _Copy(&rhs);
    return *this;
  }

  bool LoadAccount(LPCTSTR pszAccountName, LPCTSTR pszSystem = NULL) {
    _Clear();
    DWORD cbSid = sizeof(m_buf), cchDomain = 0;
    SID_NAME_USE use;
    ::LookupAccountName(pszSystem, pszAccountName, m_buf, &cbSid, NULL, &cchDomain, &use);
    if (cchDomain == 0) cchDomain = 1;
    std::vector<TCHAR> domain(cchDomain + 1);
    cbSid = sizeof(m_buf);
    if (!::LookupAccountName(pszSystem, pszAccountName, m_buf, &cbSid, domain.data(), &cchDomain,
                             &use))
      return false;
    m_bValid = true;
    m_sidnameuse = use;
    m_strDomain = domain.data();
    m_strSystem = pszSystem;
    // The account's name as the system spells it.
    _LookupSid();
    if (m_strAccountName.IsEmpty()) m_strAccountName = pszAccountName;
    return true;
  }
  bool LoadAccount(const SID* pSid, LPCTSTR pszSystem = NULL) {
    _Clear();
    if (!_Copy(pSid)) return false;
    m_strSystem = pszSystem;
    return _LookupSid();
  }

  LPCTSTR AccountName() const {
    if (m_strAccountName.IsEmpty() && m_bValid) const_cast<CSid*>(this)->_LookupSid();
    return m_strAccountName;
  }
  LPCTSTR Domain() const {
    if (m_strDomain.IsEmpty() && m_bValid) const_cast<CSid*>(this)->_LookupSid();
    return m_strDomain;
  }
  // The SID's string form, S-1-5-....
  LPCTSTR Sid() const {
    if (m_strSid.IsEmpty() && m_bValid) {
      LPTSTR psz = NULL;
      if (::ConvertSidToStringSid((PSID)m_buf, &psz)) {
        const_cast<CSid*>(this)->m_strSid = psz;
        ::LocalFree(psz);
      }
    }
    return m_strSid;
  }
  const SID* GetPSID() const { return m_bValid ? (const SID*)m_buf : NULL; }
  operator const SID*() const { return GetPSID(); }
  SID_NAME_USE SidNameUse() const { return m_sidnameuse; }
  UINT GetLength() const { return m_bValid ? ::GetLengthSid((PSID)m_buf) : 0; }
  bool IsValid() const { return m_bValid && ::IsValidSid((PSID)m_buf); }
  const SID_IDENTIFIER_AUTHORITY* GetPSID_IDENTIFIER_AUTHORITY() const {
    return m_bValid ? ::GetSidIdentifierAuthority((PSID)m_buf) : NULL;
  }
  DWORD GetSubAuthority(DWORD nSubAuthority) const {
    return m_bValid ? *::GetSidSubAuthority((PSID)m_buf, nSubAuthority) : 0;
  }
  UCHAR GetSubAuthorityCount() const {
    return m_bValid ? *::GetSidSubAuthorityCount((PSID)m_buf) : 0;
  }
  bool EqualPrefix(const SID& rhs) const {
    return m_bValid && ::EqualPrefixSid((PSID)m_buf, (PSID)&rhs);
  }
  bool EqualPrefix(const CSid& rhs) const { return rhs.m_bValid && EqualPrefix(*rhs.GetPSID()); }

  bool operator==(const CSid& rhs) const {
    if (!m_bValid || !rhs.m_bValid) return m_bValid == rhs.m_bValid;
    return ::EqualSid((PSID)m_buf, (PSID)rhs.m_buf) != 0;
  }
  bool operator!=(const CSid& rhs) const { return !(*this == rhs); }
  // Ordered by their bytes (CRBMap keys).
  bool operator<(const CSid& rhs) const {
    UINT n1 = GetLength(), n2 = rhs.GetLength();
    int c = ::memcmp(m_buf, rhs.m_buf, n1 < n2 ? n1 : n2);
    return c < 0 || (c == 0 && n1 < n2);
  }
  bool operator>(const CSid& rhs) const { return rhs < *this; }

private:
  BYTE m_buf[SECURITY_MAX_SID_SIZE];
  bool m_bValid;
  SID_NAME_USE m_sidnameuse;
  CString m_strSid, m_strAccountName, m_strDomain, m_strSystem;

  void _Clear() {
    m_bValid = false;
    m_sidnameuse = SidTypeInvalid;
    m_strSid.Empty();
    m_strAccountName.Empty();
    m_strDomain.Empty();
    m_strSystem.Empty();
  }
  bool _Copy(const SID* pSid) {
    if (pSid == NULL || !::IsValidSid((PSID)pSid)) return false;
    DWORD cb = ::GetLengthSid((PSID)pSid);
    if (cb > sizeof(m_buf)) return false;
    ::CopySid(sizeof(m_buf), m_buf, (PSID)pSid);
    m_bValid = true;
    return true;
  }
  bool _LookupSid() {
    TCHAR szName[256], szDomain[256];
    DWORD cchName = 256, cchDomain = 256;
    SID_NAME_USE use;
    LPCTSTR pszSystem = m_strSystem.IsEmpty() ? NULL : (LPCTSTR)m_strSystem;
    if (!::LookupAccountSid(pszSystem, (PSID)m_buf, szName, &cchName, szDomain, &cchDomain, &use))
      return false;
    m_strAccountName = szName;
    m_strDomain = szDomain;
    m_sidnameuse = use;
    return true;
  }
};

template <>
class CElementTraits<CSid> : public CElementTraitsBase<CSid> {
public:
  static ULONG Hash(const CSid& sid) {
    const BYTE* p = (const BYTE*)sid.GetPSID();
    ULONG h = 0;
    for (UINT i = 0; i < sid.GetLength(); i++) h = (h << 5) + h + p[i];
    return h;
  }
  static bool CompareElements(const CSid& s1, const CSid& s2) { return s1 == s2; }
  static int CompareElementsOrdered(const CSid& s1, const CSid& s2) {
    return s1 < s2 ? -1 : s2 < s1 ? 1 : 0;
  }
};

// The well-known SIDs.
namespace Sids {
inline CSid _Nt(BYTE n, DWORD a1, DWORD a2 = 0) {
  SID_IDENTIFIER_AUTHORITY nt = SECURITY_NT_AUTHORITY;
  return n == 1 ? CSid(nt, 1, a1) : CSid(nt, 2, a1, a2);
}
inline CSid World() {
  SID_IDENTIFIER_AUTHORITY a = SECURITY_WORLD_SID_AUTHORITY;
  return CSid(a, 1, SECURITY_WORLD_RID);
}
inline CSid CreatorOwner() {
  SID_IDENTIFIER_AUTHORITY a = SECURITY_CREATOR_SID_AUTHORITY;
  return CSid(a, 1, SECURITY_CREATOR_OWNER_RID);
}
inline CSid CreatorGroup() {
  SID_IDENTIFIER_AUTHORITY a = SECURITY_CREATOR_SID_AUTHORITY;
  return CSid(a, 1, SECURITY_CREATOR_GROUP_RID);
}
inline CSid Dialup() { return _Nt(1, SECURITY_DIALUP_RID); }
inline CSid Network() { return _Nt(1, SECURITY_NETWORK_RID); }
inline CSid Batch() { return _Nt(1, SECURITY_BATCH_RID); }
inline CSid Interactive() { return _Nt(1, SECURITY_INTERACTIVE_RID); }
inline CSid Service() { return _Nt(1, SECURITY_SERVICE_RID); }
inline CSid AnonymousLogon() { return _Nt(1, SECURITY_ANONYMOUS_LOGON_RID); }
inline CSid Proxy() { return _Nt(1, SECURITY_PROXY_RID); }
inline CSid ServerLogon() { return _Nt(1, SECURITY_SERVER_LOGON_RID); }
inline CSid Self() { return _Nt(1, SECURITY_PRINCIPAL_SELF_RID); }
inline CSid AuthenticatedUser() { return _Nt(1, SECURITY_AUTHENTICATED_USER_RID); }
inline CSid RestrictedCode() { return _Nt(1, SECURITY_RESTRICTED_CODE_RID); }
inline CSid TerminalServer() { return _Nt(1, SECURITY_TERMINAL_SERVER_RID); }
inline CSid System() { return _Nt(1, SECURITY_LOCAL_SYSTEM_RID); }
inline CSid NetworkService() { return _Nt(1, 20); }
inline CSid LocalService() { return _Nt(1, 19); }
inline CSid Admins() { return _Nt(2, SECURITY_BUILTIN_DOMAIN_RID, DOMAIN_ALIAS_RID_ADMINS); }
inline CSid Users() { return _Nt(2, SECURITY_BUILTIN_DOMAIN_RID, DOMAIN_ALIAS_RID_USERS); }
inline CSid Guests() { return _Nt(2, SECURITY_BUILTIN_DOMAIN_RID, DOMAIN_ALIAS_RID_GUESTS); }
inline CSid PowerUsers() { return _Nt(2, SECURITY_BUILTIN_DOMAIN_RID, DOMAIN_ALIAS_RID_POWER_USERS); }
inline CSid AccountOps() { return _Nt(2, SECURITY_BUILTIN_DOMAIN_RID, DOMAIN_ALIAS_RID_ACCOUNT_OPS); }
inline CSid SystemOps() { return _Nt(2, SECURITY_BUILTIN_DOMAIN_RID, DOMAIN_ALIAS_RID_SYSTEM_OPS); }
inline CSid PrintOps() { return _Nt(2, SECURITY_BUILTIN_DOMAIN_RID, DOMAIN_ALIAS_RID_PRINT_OPS); }
inline CSid BackupOps() { return _Nt(2, SECURITY_BUILTIN_DOMAIN_RID, DOMAIN_ALIAS_RID_BACKUP_OPS); }
inline CSid Replicator() { return _Nt(2, SECURITY_BUILTIN_DOMAIN_RID, DOMAIN_ALIAS_RID_REPLICATOR); }
}  // namespace Sids

// ---------------------------------------------------------------------
// ACLs

typedef CAtlArray<ACCESS_MASK> CAccessMaskArray;

// An ACL kept as its entries, built when it is asked for. A null ACL
// (no ACL at all: everyone gets in) differs from an empty one (no one
// does).
class CAcl {
public:
  typedef CAtlArray<BYTE> CAceTypeArray;
  typedef CAtlArray<BYTE> CAceFlagArray;

  CAcl() : m_bNull(false) {}
  CAcl(const CAcl& rhs) : m_aces(rhs.m_aces), m_bNull(rhs.m_bNull) {}
  CAcl& operator=(const CAcl& rhs) {
    m_aces = rhs.m_aces;
    m_bNull = rhs.m_bNull;
    m_buf.clear();
    return *this;
  }
  virtual ~CAcl() {}

  // Every entry's SID, rights, type and flags.
  void GetAclEntries(CSid::CSidArray* pSids, CAccessMaskArray* pAccessMasks = NULL,
                     CAceTypeArray* pAceTypes = NULL, CAceFlagArray* pAceFlags = NULL) const {
    if (pSids) pSids->RemoveAll();
    if (pAccessMasks) pAccessMasks->RemoveAll();
    if (pAceTypes) pAceTypes->RemoveAll();
    if (pAceFlags) pAceFlags->RemoveAll();
    for (const _Ace& ace : m_aces) {
      if (pSids) pSids->Add(CSid(*ace.Sid()));
      if (pAccessMasks) pAccessMasks->Add(ace.Mask());
      if (pAceTypes) pAceTypes->Add(ace.Type());
      if (pAceFlags) pAceFlags->Add(ace.Flags());
    }
  }
  void GetAclEntry(UINT nIndex, CSid* pSid, ACCESS_MASK* pMask = NULL, BYTE* pType = NULL,
                   BYTE* pFlags = NULL) const {
    const _Ace& ace = m_aces.at(nIndex);
    if (pSid) *pSid = CSid(*ace.Sid());
    if (pMask) *pMask = ace.Mask();
    if (pType) *pType = ace.Type();
    if (pFlags) *pFlags = ace.Flags();
  }
  UINT GetAceCount() const { return (UINT)m_aces.size(); }
  UINT GetLength() const {
    const_cast<CAcl*>(this)->_Build();
    return m_bNull ? 0 : (UINT)m_buf.size();
  }
  const ACL* GetPACL() const {
    const_cast<CAcl*>(this)->_Build();
    return m_bNull ? NULL : (const ACL*)m_buf.data();
  }
  operator const ACL*() const { return GetPACL(); }

  bool RemoveAce(UINT nIndex) {
    if (nIndex >= m_aces.size()) return false;
    m_aces.erase(m_aces.begin() + nIndex);
    m_buf.clear();
    return true;
  }
  void RemoveAllAces() {
    m_aces.clear();
    m_buf.clear();
  }
  bool IsEmpty() const { return !m_bNull && m_aces.empty(); }
  bool IsNull() const { return m_bNull; }
  void SetEmpty() {
    RemoveAllAces();
    m_bNull = false;
  }
  void SetNull() {
    RemoveAllAces();
    m_bNull = true;
  }

protected:
  // One entry: its bytes, as an ACE.
  struct _Ace {
    std::vector<BYTE> bytes;
    const ACE_HEADER* Header() const { return (const ACE_HEADER*)bytes.data(); }
    BYTE Type() const { return Header()->AceType; }
    BYTE Flags() const { return Header()->AceFlags; }
    // The allowed, denied and audit entries share a layout.
    ACCESS_MASK Mask() const { return ((const ACCESS_ALLOWED_ACE*)bytes.data())->Mask; }
    const SID* Sid() const { return (const SID*)&((const ACCESS_ALLOWED_ACE*)bytes.data())->SidStart; }
  };
  std::vector<_Ace> m_aces;
  bool m_bNull;
  std::vector<BYTE> m_buf;

  void _AddAce(BYTE type, BYTE flags, ACCESS_MASK mask, const CSid& sid) {
    if (!sid.IsValid()) AtlThrow(E_INVALIDARG);
    _Ace ace;
    size_t cb = sizeof(ACCESS_ALLOWED_ACE) - sizeof(DWORD) + sid.GetLength();
    ace.bytes.resize(cb);
    ACCESS_ALLOWED_ACE* p = (ACCESS_ALLOWED_ACE*)ace.bytes.data();
    p->Header.AceType = type;
    p->Header.AceFlags = flags;
    p->Header.AceSize = (WORD)cb;
    p->Mask = mask;
    ::CopySid(sid.GetLength(), (PSID)&p->SidStart, (PSID)sid.GetPSID());
    m_aces.push_back(ace);
    m_bNull = false;
    m_buf.clear();
  }
  void _FromAcl(const ACL* pAcl) {
    RemoveAllAces();
    m_bNull = pAcl == NULL;
    if (pAcl == NULL) return;
    for (DWORD i = 0; i < pAcl->AceCount; i++) {
      void* pv = NULL;
      if (!::GetAce(const_cast<ACL*>(pAcl), i, &pv)) AtlThrowLastWin32();
      const ACE_HEADER* h = (const ACE_HEADER*)pv;
      _Ace ace;
      ace.bytes.assign((const BYTE*)pv, (const BYTE*)pv + h->AceSize);
      m_aces.push_back(ace);
    }
  }
  bool _RemoveAcesFor(const CSid& sid, bool (*pfnType)(BYTE)) {
    bool b = false;
    for (size_t i = m_aces.size(); i-- > 0;) {
      if (pfnType(m_aces[i].Type()) && CSid(*m_aces[i].Sid()) == sid) {
        m_aces.erase(m_aces.begin() + (ptrdiff_t)i);
        b = true;
      }
    }
    m_buf.clear();
    return b;
  }
  void _Build() {
    if (m_bNull || !m_buf.empty()) return;
    size_t cb = sizeof(ACL);
    for (const _Ace& ace : m_aces) cb += ace.bytes.size();
    cb = (cb + 3) & ~(size_t)3;
    m_buf.assign(cb, 0);
    ACL* pAcl = (ACL*)m_buf.data();
    if (!::InitializeAcl(pAcl, (DWORD)cb, ACL_REVISION)) AtlThrowLastWin32();
    for (const _Ace& ace : m_aces)
      if (!::AddAce(pAcl, ACL_REVISION, MAXDWORD, (void*)ace.bytes.data(), (DWORD)ace.bytes.size()))
        AtlThrowLastWin32();
  }
};

// Who may do what: allowed and denied entries, the denied ones first
// when it is built (the order Windows expects).
class CDacl : public CAcl {
public:
  CDacl() {}
  CDacl(const ACL& rhs) { _FromAcl(&rhs); }
  CDacl& operator=(const ACL& rhs) {
    _FromAcl(&rhs);
    return *this;
  }

  bool AddAllowedAce(const CSid& rSid, ACCESS_MASK AccessMask, BYTE AceFlags = 0) {
    _AddAce(ACCESS_ALLOWED_ACE_TYPE, AceFlags, AccessMask, rSid);
    return true;
  }
  bool AddDeniedAce(const CSid& rSid, ACCESS_MASK AccessMask, BYTE AceFlags = 0) {
    _AddAce(ACCESS_DENIED_ACE_TYPE, AceFlags, AccessMask, rSid);
    // Denied entries go before the allowed ones that are not inherited.
    _Ace ace = m_aces.back();
    m_aces.pop_back();
    size_t i = 0;
    while (i < m_aces.size() && m_aces[i].Type() == ACCESS_DENIED_ACE_TYPE) i++;
    m_aces.insert(m_aces.begin() + (ptrdiff_t)i, ace);
    return true;
  }
  void RemoveAces(const CSid& rSid) {
    _RemoveAcesFor(rSid, [](BYTE t) {
      return t == ACCESS_ALLOWED_ACE_TYPE || t == ACCESS_DENIED_ACE_TYPE;
    });
  }
};

// What is audited.
class CSacl : public CAcl {
public:
  CSacl() {}
  CSacl(const ACL& rhs) { _FromAcl(&rhs); }
  CSacl& operator=(const ACL& rhs) {
    _FromAcl(&rhs);
    return *this;
  }

  bool AddAuditAce(const CSid& rSid, ACCESS_MASK AccessMask, bool bSuccess, bool bFailure,
                   BYTE AceFlags = 0) {
    BYTE flags = AceFlags;
    if (bSuccess) flags |= SUCCESSFUL_ACCESS_ACE_FLAG;
    if (bFailure) flags |= FAILED_ACCESS_ACE_FLAG;
    _AddAce(SYSTEM_AUDIT_ACE_TYPE, flags, AccessMask, rSid);
    return true;
  }
  void RemoveAces(const CSid& rSid) {
    _RemoveAcesFor(rSid, [](BYTE t) { return t == SYSTEM_AUDIT_ACE_TYPE; });
  }
};

// ---------------------------------------------------------------------
// CSecurityDesc: owner, group, DACL and SACL, kept apart and made into a
// self-relative descriptor when one is asked for.

class CSecurityDesc {
public:
  CSecurityDesc()
      : m_bOwnerDefaulted(false), m_bGroupDefaulted(false), m_bDaclPresent(false),
        m_bDaclDefaulted(false), m_bSaclPresent(false), m_bSaclDefaulted(false), m_control(0) {}
  CSecurityDesc(const SECURITY_DESCRIPTOR& rhs) : CSecurityDesc() { _From(&rhs); }
  CSecurityDesc& operator=(const SECURITY_DESCRIPTOR& rhs) {
    _From(&rhs);
    return *this;
  }

  // SDDL ("O:BAD:(A;;GA;;;WD)") in and out.
  bool FromString(LPCTSTR pstr) {
    PSECURITY_DESCRIPTOR psd = NULL;
    if (!::ConvertStringSecurityDescriptorToSecurityDescriptor(pstr, SDDL_REVISION_1, &psd, NULL))
      return false;
    _From((const SECURITY_DESCRIPTOR*)psd);
    ::LocalFree(psd);
    return true;
  }
  bool ToString(CString* pstr,
                SECURITY_INFORMATION si = OWNER_SECURITY_INFORMATION | GROUP_SECURITY_INFORMATION |
                                          DACL_SECURITY_INFORMATION | SACL_SECURITY_INFORMATION) const {
    if (pstr == NULL) return false;
    LPTSTR psz = NULL;
    if (!::ConvertSecurityDescriptorToStringSecurityDescriptor(
            (PSECURITY_DESCRIPTOR)GetPSECURITY_DESCRIPTOR(), SDDL_REVISION_1, si, &psz, NULL))
      return false;
    *pstr = psz;
    ::LocalFree(psz);
    return true;
  }

  void SetOwner(const CSid& sid, bool bDefaulted = false) {
    m_owner = sid;
    m_bOwnerDefaulted = bDefaulted;
    m_buf.clear();
  }
  void SetGroup(const CSid& sid, bool bDefaulted = false) {
    m_group = sid;
    m_bGroupDefaulted = bDefaulted;
    m_buf.clear();
  }
  void SetDacl(const CDacl& Dacl, bool bDefaulted = false) {
    m_dacl = Dacl;
    m_bDaclPresent = true;
    m_bDaclDefaulted = bDefaulted;
    m_buf.clear();
  }
  // Present with a null DACL (everyone), or not present at all.
  void SetDacl(bool bNull, bool bDefaulted = false) {
    if (bNull) m_dacl.SetNull(); else m_dacl.SetEmpty();
    m_bDaclPresent = bNull;
    m_bDaclDefaulted = bDefaulted;
    m_buf.clear();
  }
  void SetSacl(const CSacl& Sacl, bool bDefaulted = false) {
    m_sacl = Sacl;
    m_bSaclPresent = true;
    m_bSaclDefaulted = bDefaulted;
    m_buf.clear();
  }
  bool SetControl(SECURITY_DESCRIPTOR_CONTROL ControlBitsOfInterest,
                  SECURITY_DESCRIPTOR_CONTROL ControlBitsToSet) {
    m_control = (SECURITY_DESCRIPTOR_CONTROL)((m_control & ~ControlBitsOfInterest) |
                                              (ControlBitsToSet & ControlBitsOfInterest));
    m_buf.clear();
    return true;
  }

  bool GetOwner(CSid* pSid, bool* pbDefaulted = NULL) const {
    if (pSid == NULL || !m_owner.IsValid()) return false;
    *pSid = m_owner;
    if (pbDefaulted) *pbDefaulted = m_bOwnerDefaulted;
    return true;
  }
  bool GetGroup(CSid* pSid, bool* pbDefaulted = NULL) const {
    if (pSid == NULL || !m_group.IsValid()) return false;
    *pSid = m_group;
    if (pbDefaulted) *pbDefaulted = m_bGroupDefaulted;
    return true;
  }
  bool GetDacl(CDacl* pDacl, bool* pbPresent = NULL, bool* pbDefaulted = NULL) const {
    if (pDacl == NULL) return false;
    *pDacl = m_dacl;
    if (pbPresent) *pbPresent = m_bDaclPresent;
    if (pbDefaulted) *pbDefaulted = m_bDaclDefaulted;
    return true;
  }
  bool GetSacl(CSacl* pSacl, bool* pbPresent = NULL, bool* pbDefaulted = NULL) const {
    if (pSacl == NULL) return false;
    *pSacl = m_sacl;
    if (pbPresent) *pbPresent = m_bSaclPresent;
    if (pbDefaulted) *pbDefaulted = m_bSaclDefaulted;
    return true;
  }
  bool GetControl(SECURITY_DESCRIPTOR_CONTROL* psdc) const {
    if (psdc == NULL) return false;
    *psdc = (SECURITY_DESCRIPTOR_CONTROL)(m_control | SE_SELF_RELATIVE |
                                          (m_bDaclPresent ? SE_DACL_PRESENT : 0) |
                                          (m_bSaclPresent ? SE_SACL_PRESENT : 0));
    return true;
  }
  bool IsDaclPresent() const { return m_bDaclPresent; }
  bool IsDaclDefaulted() const { return m_bDaclDefaulted; }
  bool IsDaclAutoInherited() const { return (m_control & SE_DACL_AUTO_INHERITED) != 0; }
  bool IsDaclProtected() const { return (m_control & SE_DACL_PROTECTED) != 0; }
  bool IsSaclPresent() const { return m_bSaclPresent; }
  bool IsOwnerDefaulted() const { return m_bOwnerDefaulted; }
  bool IsGroupDefaulted() const { return m_bGroupDefaulted; }
  bool IsSelfRelative() const { return true; }

  // The descriptor, self-relative, valid until this one changes.
  const SECURITY_DESCRIPTOR* GetPSECURITY_DESCRIPTOR() const {
    const_cast<CSecurityDesc*>(this)->_Build();
    return (const SECURITY_DESCRIPTOR*)m_buf.data();
  }
  operator const SECURITY_DESCRIPTOR*() const { return GetPSECURITY_DESCRIPTOR(); }
  UINT GetLength() const {
    const_cast<CSecurityDesc*>(this)->_Build();
    return (UINT)m_buf.size();
  }

private:
  CSid m_owner, m_group;
  CDacl m_dacl;
  CSacl m_sacl;
  bool m_bOwnerDefaulted, m_bGroupDefaulted, m_bDaclPresent, m_bDaclDefaulted, m_bSaclPresent,
      m_bSaclDefaulted;
  SECURITY_DESCRIPTOR_CONTROL m_control;
  std::vector<BYTE> m_buf;

  void _From(const SECURITY_DESCRIPTOR* psd) {
    *this = CSecurityDesc();
    PSID pSid = NULL;
    BOOL bDefaulted = FALSE, bPresent = FALSE;
    if (::GetSecurityDescriptorOwner((PSECURITY_DESCRIPTOR)psd, &pSid, &bDefaulted) && pSid != NULL) {
      m_owner = *(const SID*)pSid;
      m_bOwnerDefaulted = bDefaulted != 0;
    }
    pSid = NULL;
    if (::GetSecurityDescriptorGroup((PSECURITY_DESCRIPTOR)psd, &pSid, &bDefaulted) && pSid != NULL) {
      m_group = *(const SID*)pSid;
      m_bGroupDefaulted = bDefaulted != 0;
    }
    PACL pAcl = NULL;
    if (::GetSecurityDescriptorDacl((PSECURITY_DESCRIPTOR)psd, &bPresent, &pAcl, &bDefaulted) &&
        bPresent) {
      if (pAcl != NULL) m_dacl = *pAcl; else m_dacl.SetNull();
      m_bDaclPresent = true;
      m_bDaclDefaulted = bDefaulted != 0;
    }
    pAcl = NULL;
    if (::GetSecurityDescriptorSacl((PSECURITY_DESCRIPTOR)psd, &bPresent, &pAcl, &bDefaulted) &&
        bPresent) {
      if (pAcl != NULL) m_sacl = *pAcl; else m_sacl.SetNull();
      m_bSaclPresent = true;
      m_bSaclDefaulted = bDefaulted != 0;
    }
    SECURITY_DESCRIPTOR_CONTROL sdc = 0;
    DWORD dwRev;
    if (::GetSecurityDescriptorControl((PSECURITY_DESCRIPTOR)psd, &sdc, &dwRev))
      m_control = (SECURITY_DESCRIPTOR_CONTROL)(sdc & (SE_DACL_AUTO_INHERITED | SE_SACL_AUTO_INHERITED |
                                                      SE_DACL_PROTECTED | SE_SACL_PROTECTED));
  }
  void _Build() {
    if (!m_buf.empty()) return;
    SECURITY_DESCRIPTOR sd;
    if (!::InitializeSecurityDescriptor(&sd, SECURITY_DESCRIPTOR_REVISION)) AtlThrowLastWin32();
    if (m_owner.IsValid()) ::SetSecurityDescriptorOwner(&sd, (PSID)m_owner.GetPSID(), m_bOwnerDefaulted);
    if (m_group.IsValid()) ::SetSecurityDescriptorGroup(&sd, (PSID)m_group.GetPSID(), m_bGroupDefaulted);
    if (m_bDaclPresent)
      ::SetSecurityDescriptorDacl(&sd, TRUE, (PACL)m_dacl.GetPACL(), m_bDaclDefaulted);
    if (m_bSaclPresent)
      ::SetSecurityDescriptorSacl(&sd, TRUE, (PACL)m_sacl.GetPACL(), m_bSaclDefaulted);
    if (m_control != 0)
      ::SetSecurityDescriptorControl(
          &sd, (SECURITY_DESCRIPTOR_CONTROL)(SE_DACL_AUTO_INHERITED | SE_SACL_AUTO_INHERITED |
                                             SE_DACL_PROTECTED | SE_SACL_PROTECTED),
          m_control);
    DWORD cb = 0;
    ::MakeSelfRelativeSD(&sd, NULL, &cb);
    m_buf.assign(cb ? cb : sizeof(SECURITY_DESCRIPTOR_RELATIVE), 0);
    if (!::MakeSelfRelativeSD(&sd, (PSECURITY_DESCRIPTOR)m_buf.data(), &cb)) {
      m_buf.clear();
      AtlThrowLastWin32();
    }
  }
};

// SECURITY_ATTRIBUTES over a CSecurityDesc it keeps.
class CSecurityAttributes : public SECURITY_ATTRIBUTES {
public:
  CSecurityAttributes() {
    nLength = sizeof(SECURITY_ATTRIBUTES);
    lpSecurityDescriptor = NULL;
    bInheritHandle = FALSE;
  }
  explicit CSecurityAttributes(const CSecurityDesc& rSecurityDescriptor, bool bInheritsHandle = false) {
    Set(rSecurityDescriptor, bInheritsHandle);
  }
  CSecurityAttributes(const CSecurityAttributes&) = delete;
  CSecurityAttributes& operator=(const CSecurityAttributes&) = delete;

  void Set(const CSecurityDesc& rSecurityDescriptor, bool bInheritsHandle = false) {
    m_secDesc = rSecurityDescriptor;
    nLength = sizeof(SECURITY_ATTRIBUTES);
    lpSecurityDescriptor = (LPVOID)m_secDesc.GetPSECURITY_DESCRIPTOR();
    bInheritHandle = bInheritsHandle;
  }

private:
  CSecurityDesc m_secDesc;
};

// ---------------------------------------------------------------------
// Objects' security

inline bool AtlGetSecurityDescriptor(HANDLE hObject, SE_OBJECT_TYPE ObjectType,
                                     CSecurityDesc* pSecurityDescriptor,
                                     SECURITY_INFORMATION requestedInfo =
                                         OWNER_SECURITY_INFORMATION | GROUP_SECURITY_INFORMATION |
                                         DACL_SECURITY_INFORMATION) {
  PSECURITY_DESCRIPTOR psd = NULL;
  if (pSecurityDescriptor == NULL ||
      ::GetSecurityInfo(hObject, ObjectType, requestedInfo, NULL, NULL, NULL, NULL, &psd) !=
          ERROR_SUCCESS)
    return false;
  *pSecurityDescriptor = *(const SECURITY_DESCRIPTOR*)psd;
  ::LocalFree(psd);
  return true;
}
inline bool AtlGetSecurityDescriptor(LPCTSTR pszObjectName, SE_OBJECT_TYPE ObjectType,
                                     CSecurityDesc* pSecurityDescriptor,
                                     SECURITY_INFORMATION requestedInfo =
                                         OWNER_SECURITY_INFORMATION | GROUP_SECURITY_INFORMATION |
                                         DACL_SECURITY_INFORMATION) {
  PSECURITY_DESCRIPTOR psd = NULL;
  if (pSecurityDescriptor == NULL ||
      ::GetNamedSecurityInfo(const_cast<LPTSTR>(pszObjectName), ObjectType, requestedInfo, NULL, NULL,
                             NULL, NULL, &psd) != ERROR_SUCCESS)
    return false;
  *pSecurityDescriptor = *(const SECURITY_DESCRIPTOR*)psd;
  ::LocalFree(psd);
  return true;
}

inline bool AtlGetDacl(HANDLE hObject, SE_OBJECT_TYPE ObjectType, CDacl* pDacl) {
  PACL pAcl = NULL;
  PSECURITY_DESCRIPTOR psd = NULL;
  if (pDacl == NULL || ::GetSecurityInfo(hObject, ObjectType, DACL_SECURITY_INFORMATION, NULL, NULL,
                                         &pAcl, NULL, &psd) != ERROR_SUCCESS)
    return false;
  if (pAcl != NULL) *pDacl = *pAcl; else pDacl->SetNull();
  ::LocalFree(psd);
  return true;
}
inline bool AtlGetDacl(LPCTSTR pszObjectName, SE_OBJECT_TYPE ObjectType, CDacl* pDacl) {
  PACL pAcl = NULL;
  PSECURITY_DESCRIPTOR psd = NULL;
  if (pDacl == NULL ||
      ::GetNamedSecurityInfo(const_cast<LPTSTR>(pszObjectName), ObjectType, DACL_SECURITY_INFORMATION,
                             NULL, NULL, &pAcl, NULL, &psd) != ERROR_SUCCESS)
    return false;
  if (pAcl != NULL) *pDacl = *pAcl; else pDacl->SetNull();
  ::LocalFree(psd);
  return true;
}
inline bool AtlSetDacl(HANDLE hObject, SE_OBJECT_TYPE ObjectType, const CDacl& rDacl,
                       DWORD dwInheritanceFlowControl = 0) {
  return ::SetSecurityInfo(hObject, ObjectType, DACL_SECURITY_INFORMATION | dwInheritanceFlowControl,
                           NULL, NULL, (PACL)rDacl.GetPACL(), NULL) == ERROR_SUCCESS;
}
inline bool AtlSetDacl(LPCTSTR pszObjectName, SE_OBJECT_TYPE ObjectType, const CDacl& rDacl,
                       DWORD dwInheritanceFlowControl = 0) {
  return ::SetNamedSecurityInfo(const_cast<LPTSTR>(pszObjectName), ObjectType,
                                DACL_SECURITY_INFORMATION | dwInheritanceFlowControl, NULL, NULL,
                                (PACL)rDacl.GetPACL(), NULL) == ERROR_SUCCESS;
}
inline bool AtlGetOwnerSid(HANDLE hObject, SE_OBJECT_TYPE ObjectType, CSid* pSid) {
  PSID pOwner = NULL;
  PSECURITY_DESCRIPTOR psd = NULL;
  if (pSid == NULL || ::GetSecurityInfo(hObject, ObjectType, OWNER_SECURITY_INFORMATION, &pOwner, NULL,
                                        NULL, NULL, &psd) != ERROR_SUCCESS)
    return false;
  bool b = pOwner != NULL;
  if (b) *pSid = *(const SID*)pOwner;
  ::LocalFree(psd);
  return b;
}
inline bool AtlGetOwnerSid(LPCTSTR pszObjectName, SE_OBJECT_TYPE ObjectType, CSid* pSid) {
  PSID pOwner = NULL;
  PSECURITY_DESCRIPTOR psd = NULL;
  if (pSid == NULL ||
      ::GetNamedSecurityInfo(const_cast<LPTSTR>(pszObjectName), ObjectType, OWNER_SECURITY_INFORMATION,
                             &pOwner, NULL, NULL, NULL, &psd) != ERROR_SUCCESS)
    return false;
  bool b = pOwner != NULL;
  if (b) *pSid = *(const SID*)pOwner;
  ::LocalFree(psd);
  return b;
}
inline bool AtlSetOwnerSid(HANDLE hObject, SE_OBJECT_TYPE ObjectType, const CSid& rSid) {
  return ::SetSecurityInfo(hObject, ObjectType, OWNER_SECURITY_INFORMATION, (PSID)rSid.GetPSID(), NULL,
                           NULL, NULL) == ERROR_SUCCESS;
}
inline bool AtlSetOwnerSid(LPCTSTR pszObjectName, SE_OBJECT_TYPE ObjectType, const CSid& rSid) {
  return ::SetNamedSecurityInfo(const_cast<LPTSTR>(pszObjectName), ObjectType,
                                OWNER_SECURITY_INFORMATION, (PSID)rSid.GetPSID(), NULL, NULL,
                                NULL) == ERROR_SUCCESS;
}

// ---------------------------------------------------------------------
// CAccessToken: a process's or thread's token.

class CAccessToken {
public:
  CAccessToken() : m_hToken(NULL) {}
  CAccessToken(const CAccessToken&) = delete;
  CAccessToken& operator=(const CAccessToken&) = delete;
  ~CAccessToken() { Clear(); }

  void Attach(HANDLE hToken) {
    Clear();
    m_hToken = hToken;
  }
  HANDLE Detach() {
    HANDLE h = m_hToken;
    m_hToken = NULL;
    return h;
  }
  HANDLE GetHandle() const { return m_hToken; }
  void Clear() {
    if (m_hToken != NULL) ::CloseHandle(m_hToken);
    m_hToken = NULL;
  }

  bool GetProcessToken(DWORD dwDesiredAccess, HANDLE hProcess = NULL) {
    HANDLE h = NULL;
    if (!::OpenProcessToken(hProcess ? hProcess : ::GetCurrentProcess(), dwDesiredAccess, &h)) return false;
    Attach(h);
    return true;
  }
  bool GetThreadToken(DWORD dwDesiredAccess, HANDLE hThread = NULL, bool bOpenAsSelf = true) {
    HANDLE h = NULL;
    if (!::OpenThreadToken(hThread ? hThread : ::GetCurrentThread(), dwDesiredAccess, bOpenAsSelf, &h))
      return false;
    Attach(h);
    return true;
  }
  // The thread's token when it impersonates, the process's otherwise.
  bool GetEffectiveToken(DWORD dwDesiredAccess) {
    return GetThreadToken(dwDesiredAccess) || GetProcessToken(dwDesiredAccess);
  }

  bool GetUser(CSid* pSid) const {
    std::vector<BYTE> buf;
    if (pSid == NULL || !_GetInfo(TokenUser, buf)) return false;
    *pSid = *(const SID*)((TOKEN_USER*)buf.data())->User.Sid;
    return true;
  }
  bool GetOwner(CSid* pSid) const {
    std::vector<BYTE> buf;
    if (pSid == NULL || !_GetInfo(TokenOwner, buf)) return false;
    *pSid = *(const SID*)((TOKEN_OWNER*)buf.data())->Owner;
    return true;
  }
  bool GetPrimaryGroup(CSid* pSid) const {
    std::vector<BYTE> buf;
    if (pSid == NULL || !_GetInfo(TokenPrimaryGroup, buf)) return false;
    *pSid = *(const SID*)((TOKEN_PRIMARY_GROUP*)buf.data())->PrimaryGroup;
    return true;
  }
  bool GetDefaultDacl(CDacl* pDacl) const {
    std::vector<BYTE> buf;
    if (pDacl == NULL || !_GetInfo(TokenDefaultDacl, buf)) return false;
    PACL pAcl = ((TOKEN_DEFAULT_DACL*)buf.data())->DefaultDacl;
    if (pAcl != NULL) *pDacl = *pAcl; else pDacl->SetNull();
    return true;
  }
  bool GetGroups(CSid::CSidArray* pSids, CAtlArray<DWORD>* pAttributes = NULL) const {
    std::vector<BYTE> buf;
    if (pSids == NULL || !_GetInfo(TokenGroups, buf)) return false;
    TOKEN_GROUPS* pGroups = (TOKEN_GROUPS*)buf.data();
    pSids->RemoveAll();
    if (pAttributes) pAttributes->RemoveAll();
    for (DWORD i = 0; i < pGroups->GroupCount; i++) {
      pSids->Add(CSid(*(const SID*)pGroups->Groups[i].Sid));
      if (pAttributes) pAttributes->Add(pGroups->Groups[i].Attributes);
    }
    return true;
  }
  bool GetType(TOKEN_TYPE* pType) const {
    DWORD cb = 0;
    return pType != NULL &&
           ::GetTokenInformation(m_hToken, TokenType, pType, sizeof(TOKEN_TYPE), &cb);
  }

  bool EnablePrivilege(LPCTSTR pszPrivilege) { return _SetPrivilege(pszPrivilege, SE_PRIVILEGE_ENABLED); }
  bool DisablePrivilege(LPCTSTR pszPrivilege) { return _SetPrivilege(pszPrivilege, 0); }

  // Whether the token's user is a member of rSid's group.
  bool CheckTokenMembership(const CSid& rSid, bool* pbIsMember) const {
    if (pbIsMember == NULL) return false;
    CSid::CSidArray sids;
    CAtlArray<DWORD> attrs;
    CSid user;
    if (!GetUser(&user) || !GetGroups(&sids, &attrs)) return false;
    *pbIsMember = user == rSid;
    for (size_t i = 0; i < sids.GetCount() && !*pbIsMember; i++)
      if ((attrs[i] & SE_GROUP_ENABLED) && sids[i] == rSid) *pbIsMember = true;
    return true;
  }

private:
  HANDLE m_hToken;

  bool _GetInfo(TOKEN_INFORMATION_CLASS cls, std::vector<BYTE>& buf) const {
    DWORD cb = 0;
    ::GetTokenInformation(m_hToken, cls, NULL, 0, &cb);
    if (cb == 0) return false;
    buf.assign(cb, 0);
    return ::GetTokenInformation(m_hToken, cls, buf.data(), cb, &cb) != 0;
  }
  bool _SetPrivilege(LPCTSTR pszPrivilege, DWORD dwAttributes) {
    TOKEN_PRIVILEGES tp;
    tp.PrivilegeCount = 1;
    tp.Privileges[0].Attributes = dwAttributes;
    if (!::LookupPrivilegeValue(NULL, pszPrivilege, &tp.Privileges[0].Luid)) return false;
    if (!::AdjustTokenPrivileges(m_hToken, FALSE, &tp, sizeof(tp), NULL, NULL)) return false;
    return ::GetLastError() == ERROR_SUCCESS;
  }
};

}  // namespace ATL

#endif  // __ATLSECURITY_H__
