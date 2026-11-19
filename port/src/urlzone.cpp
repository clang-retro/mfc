// urlzone.cpp - the internet security manager: which zone a URL is in,
// and what the zone lets a page do.
//
// The zones, without the user's settings that Windows keeps: a local file
// or the program's own resources (res:) are the local machine; a file on
// another host (file://host/, \\host\) and a host without a dot (a name
// on the local network) are the intranet; everything else is the
// internet. The mappings SetZoneMapping makes come first, for this
// process: a pattern is a host, or *. and a domain for the domain and
// every host in it, with a scheme in front (http://...) or for every
// scheme.
//
// A security manager made with a service provider asks the provider's own
// (SID_SInternetSecurityManager) first, and goes on with its own answer
// only when that one says INET_E_DEFAULT_ACTION.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#include "ole.h"

#include <urlmon.h>
#include <wctype.h>

#include <mutex>
#include <string>
#include <vector>

using namespace winport::ole;

namespace {

std::wstring Lower(std::wstring s) {
  for (wchar_t& c : s) c = (wchar_t)towlower(c);
  return s;
}

// A URL's parts, as far as zones go.
struct Parts {
  std::wstring scheme;  // lower case, without the colon; empty for a path
  std::wstring host;    // lower case, without a port or user
  bool path = false;    // a path, not a URL
};

bool ParseUrl(const wchar_t* url, Parts* p) {
  std::wstring s = url;
  size_t colon = s.find(L':');
  bool scheme = colon != std::wstring::npos && colon > 1;
  for (size_t i = 0; scheme && i < colon; i++) {
    wchar_t c = s[i];
    if (!(iswalnum(c) || c == L'+' || c == L'-' || c == L'.')) scheme = false;
  }
  if (!scheme) {
    // A path: a drive, a root, or a share's (\\host\share).
    p->path = true;
    if (s.size() > 2 && (s[0] == L'\\' || s[0] == L'/') && s[1] == s[0]) {
      size_t end = s.find_first_of(L"\\/", 2);
      p->host = Lower(s.substr(2, end == std::wstring::npos ? std::wstring::npos : end - 2));
      return !p->host.empty();
    }
    return (s.size() >= 2 && s[1] == L':') || (!s.empty() && (s[0] == L'/' || s[0] == L'\\'));
  }
  p->scheme = Lower(s.substr(0, colon));
  std::wstring rest = s.substr(colon + 1);
  if (rest.size() >= 2 && (rest[0] == L'/' || rest[0] == L'\\') && rest[1] == rest[0]) {
    size_t end = rest.find_first_of(L"/\\?#", 2);
    std::wstring auth = rest.substr(2, end == std::wstring::npos ? std::wstring::npos : end - 2);
    size_t at = auth.rfind(L'@');
    if (at != std::wstring::npos) auth = auth.substr(at + 1);
    if (!auth.empty() && auth[0] == L'[') {
      size_t close = auth.find(L']');
      auth = auth.substr(1, close == std::wstring::npos ? std::wstring::npos : close - 1);
    } else {
      size_t port = auth.find(L':');
      if (port != std::wstring::npos) auth = auth.substr(0, port);
    }
    p->host = Lower(auth);
  }
  return true;
}

struct Mapping {
  DWORD zone;
  std::wstring pattern;  // as given
  std::wstring scheme;   // lower case; empty or * for every scheme
  std::wstring host;     // lower case; *.domain for the domain and its hosts
};

std::mutex g_lock;
std::vector<Mapping>& Mappings() {
  static std::vector<Mapping> m;
  return m;
}

bool ParsePattern(const wchar_t* pattern, Mapping* m) {
  std::wstring s = pattern;
  size_t sep = s.find(L"://");
  if (sep != std::wstring::npos) {
    m->scheme = Lower(s.substr(0, sep));
    s = s.substr(sep + 3);
  }
  while (!s.empty() && s.back() == L'/') s.pop_back();
  if (s.empty() || s.find_first_of(L"/\\") != std::wstring::npos) return false;
  // A wildcard only as the first label.
  if (s.find(L'*', s.compare(0, 2, L"*.") == 0 ? 1 : 0) != std::wstring::npos) return false;
  m->host = Lower(s);
  return true;
}

bool Matches(const Mapping& m, const Parts& p) {
  if (!m.scheme.empty() && m.scheme != L"*" && m.scheme != p.scheme) return false;
  if (m.host.compare(0, 2, L"*.") == 0) {
    std::wstring domain = m.host.substr(2);
    if (p.host == domain) return true;
    return p.host.size() > domain.size() &&
           p.host.compare(p.host.size() - domain.size(), domain.size(), domain) == 0 &&
           p.host[p.host.size() - domain.size() - 1] == L'.';
  }
  return m.host == p.host;
}

DWORD ZoneOf(const Parts& p) {
  {
    std::lock_guard<std::mutex> g(g_lock);
    for (const Mapping& m : Mappings())
      if (!p.host.empty() && Matches(m, p)) return m.zone;
  }
  if (p.path) return p.host.empty() ? URLZONE_LOCAL_MACHINE : URLZONE_INTRANET;
  if (p.scheme == L"file") {
    return p.host.empty() || p.host == L"localhost" ? URLZONE_LOCAL_MACHINE : URLZONE_INTRANET;
  }
  if (p.scheme == L"res") return URLZONE_LOCAL_MACHINE;
  if (p.host.empty()) return URLZONE_INTERNET;
  if (p.host.find(L'.') == std::wstring::npos && p.host.find(L':') == std::wstring::npos) return URLZONE_INTRANET;
  return URLZONE_INTERNET;
}

// What a zone lets a page do.
DWORD PolicyOf(DWORD zone, DWORD action) {
  bool override = action >= URLACTION_ACTIVEX_OVERRIDE_OBJECT_SAFETY &&
                  action <= URLACTION_ACTIVEX_OVERRIDE_SCRIPT_SAFETY;
  override = override || action == URLACTION_SCRIPT_OVERRIDE_SAFETY ||
             action == URLACTION_ACTIVEX_CONFIRM_NOOBJECTSAFETY;
  switch (zone) {
    case URLZONE_LOCAL_MACHINE:
    case URLZONE_TRUSTED:
      return URLPOLICY_ALLOW;
    case URLZONE_INTRANET:
      if (override || action == URLACTION_DOWNLOAD_UNSIGNED_ACTIVEX) return URLPOLICY_DISALLOW;
      if (action == URLACTION_DOWNLOAD_SIGNED_ACTIVEX) return URLPOLICY_QUERY;
      return URLPOLICY_ALLOW;
    case URLZONE_INTERNET:
      if (override || action == URLACTION_DOWNLOAD_UNSIGNED_ACTIVEX) return URLPOLICY_DISALLOW;
      if (action == URLACTION_DOWNLOAD_SIGNED_ACTIVEX) return URLPOLICY_QUERY;
      if (action >= URLACTION_DOWNLOAD_MIN && action <= URLACTION_DOWNLOAD_MAX) return URLPOLICY_DISALLOW;
      return URLPOLICY_ALLOW;
    default:
      return URLPOLICY_DISALLOW;
  }
}

class SecurityManager final : public IInternetSecurityManager {
 public:
  explicit SecurityManager(IInternetSecurityManager* custom) : custom_(custom) {}
  ~SecurityManager() {
    SafeRelease(custom_);
    SafeRelease(site_);
  }
  STDMETHOD(QueryInterface)(REFIID riid, void** out) override {
    if (!out) return E_POINTER;
    if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, IID_IInternetSecurityManager)) {
      *out = static_cast<IInternetSecurityManager*>(this);
      AddRef();
      return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
  }
  REFCOUNTED(IInternetSecurityManager)

  STDMETHOD(SetSecuritySite)(IInternetSecurityMgrSite* site) override {
    Assign(site_, site);
    if (custom_) custom_->SetSecuritySite(site);
    return S_OK;
  }
  STDMETHOD(GetSecuritySite)(IInternetSecurityMgrSite** out) override {
    if (!out) return E_INVALIDARG;
    *out = site_;
    if (site_) site_->AddRef();
    return S_OK;
  }
  STDMETHOD(MapUrlToZone)(LPCWSTR url, DWORD* zone, DWORD flags) override {
    if (custom_) {
      HRESULT hr = custom_->MapUrlToZone(url, zone, flags);
      if (hr != INET_E_DEFAULT_ACTION) return hr;
    }
    if (!zone) return E_INVALIDARG;
    *zone = (DWORD)URLZONE_INVALID;
    if (!url || !*url) return E_INVALIDARG;
    Parts p;
    if (!ParseUrl(url, &p)) return E_INVALIDARG;
    *zone = ZoneOf(p);
    return S_OK;
  }
  // "scheme:host" and the zone, as four bytes; for a file, "file:" and the
  // zone.
  STDMETHOD(GetSecurityId)(LPCWSTR url, BYTE* id, DWORD* size, DWORD_PTR reserved) override {
    if (custom_) {
      HRESULT hr = custom_->GetSecurityId(url, id, size, reserved);
      if (hr != INET_E_DEFAULT_ACTION) return hr;
    }
    if (!url || !id || !size) return E_INVALIDARG;
    Parts p;
    if (!ParseUrl(url, &p)) return E_INVALIDARG;
    DWORD zone = ZoneOf(p);
    std::string s;
    std::wstring scheme = p.path ? L"file" : p.scheme;
    for (wchar_t c : scheme) s += (char)c;
    s += ':';
    if (scheme != L"file") s += winport::Narrow(p.host.c_str(), (int)p.host.size());
    DWORD need = (DWORD)s.size() + 4;
    if (*size < need) {
      *size = need;
      return HRESULT_FROM_WIN32(ERROR_INSUFFICIENT_BUFFER);
    }
    memcpy(id, s.data(), s.size());
    for (int i = 0; i < 4; i++) id[s.size() + i] = (BYTE)(zone >> (8 * i));
    *size = need;
    return S_OK;
  }
  STDMETHOD(ProcessUrlAction)(LPCWSTR url, DWORD action, BYTE* policy, DWORD cbPolicy, BYTE* context,
                              DWORD cbContext, DWORD flags, DWORD reserved) override {
    if (custom_) {
      HRESULT hr = custom_->ProcessUrlAction(url, action, policy, cbPolicy, context, cbContext, flags, reserved);
      if (hr != INET_E_DEFAULT_ACTION) return hr;
    }
    if (!url) return E_INVALIDARG;
    Parts p;
    if (!ParseUrl(url, &p)) return E_INVALIDARG;
    DWORD value = PolicyOf(ZoneOf(p), action);
    if (policy && cbPolicy >= sizeof(DWORD)) memcpy(policy, &value, sizeof(DWORD));
    else if (policy && cbPolicy) *policy = (BYTE)value;
    switch (value) {
      case URLPOLICY_ALLOW: return S_OK;
      case URLPOLICY_QUERY: return S_FALSE;  // with nobody to ask, not allowed
      default: return E_ACCESSDENIED;
    }
  }
  STDMETHOD(QueryCustomPolicy)(LPCWSTR url, REFGUID key, BYTE** policy, DWORD* cbPolicy, BYTE* context,
                               DWORD cbContext, DWORD reserved) override {
    if (custom_) {
      HRESULT hr = custom_->QueryCustomPolicy(url, key, policy, cbPolicy, context, cbContext, reserved);
      if (hr != INET_E_DEFAULT_ACTION) return hr;
    }
    if (policy) *policy = NULL;
    if (cbPolicy) *cbPolicy = 0;
    return HRESULT_FROM_WIN32(ERROR_NOT_FOUND);
  }
  STDMETHOD(SetZoneMapping)(DWORD zone, LPCWSTR pattern, DWORD flags) override {
    if (custom_) {
      HRESULT hr = custom_->SetZoneMapping(zone, pattern, flags);
      if (hr != INET_E_DEFAULT_ACTION) return hr;
    }
    if (!pattern) return E_INVALIDARG;
    Mapping m;
    m.zone = zone;
    m.pattern = pattern;
    if (!ParsePattern(pattern, &m)) return E_INVALIDARG;
    std::lock_guard<std::mutex> g(g_lock);
    std::vector<Mapping>& all = Mappings();
    for (size_t i = 0; i < all.size(); i++) {
      if (all[i].host != m.host || all[i].scheme != m.scheme) continue;
      if (flags & SZM_DELETE) {
        if (all[i].zone != zone) return HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND);
        all.erase(all.begin() + (ptrdiff_t)i);
        return S_OK;
      }
      return HRESULT_FROM_WIN32(ERROR_FILE_EXISTS);
    }
    if (flags & SZM_DELETE) return HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND);
    all.push_back(m);
    return S_OK;
  }
  STDMETHOD(GetZoneMappings)(DWORD zone, IEnumString** out, DWORD flags) override {
    if (custom_) {
      HRESULT hr = custom_->GetZoneMappings(zone, out, flags);
      if (hr != INET_E_DEFAULT_ACTION) return hr;
    }
    if (!out) return E_INVALIDARG;
    std::vector<LPOLESTR> items;
    std::lock_guard<std::mutex> g(g_lock);
    for (Mapping& m : Mappings())
      if (m.zone == zone) items.push_back(const_cast<LPOLESTR>(m.pattern.c_str()));
    *out = new StringEnum(items);  // (copies them)
    return S_OK;
  }

 private:
  IInternetSecurityManager* custom_ = NULL;
  IInternetSecurityMgrSite* site_ = NULL;
};

}  // namespace

extern "C" HRESULT WINAPI CoInternetCreateSecurityManager(IServiceProvider* sp, IInternetSecurityManager** out,
                                                          DWORD) {
  if (!out) return E_INVALIDARG;
  *out = NULL;
  IInternetSecurityManager* custom = NULL;
  if (sp &&
      FAILED(sp->QueryService(SID_SInternetSecurityManager, IID_IInternetSecurityManager, (void**)&custom)))
    custom = NULL;
  *out = new SecurityManager(custom);
  return S_OK;
}
