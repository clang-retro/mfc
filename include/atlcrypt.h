// atlcrypt.h - ATL's CryptoAPI classes: CCryptProv (a provider context),
// CCryptHash and its MD5, SHA and HMAC kinds, and CCryptKey with the
// derived, random, imported and user key kinds. Each wraps one handle
// and frees it when it goes; the functions return HRESULTs.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#ifndef __ATLCRYPT_H__
#define __ATLCRYPT_H__

#include <atlbase.h>
#include <wincrypt.h>

#if !defined(_ATL_NO_DEFAULT_LIBS) && defined(_WIN32)
__asm__(".section .drectve,\"yn\"\n"
        ".ascii \" -defaultlib:advapi32\"\n"
        ".text");
#endif

namespace ATL {

#define _ATL_CRYPT_HR(expr) ((expr) ? S_OK : AtlHresultFromLastError())

// ---------------------------------------------------------------------
// CCryptProv

class CCryptProv {
public:
  HCRYPTPROV m_hProv;

  CCryptProv() : m_hProv(0) {}
  CCryptProv(const CCryptProv& prov) : m_hProv(prov.m_hProv) {
    if (m_hProv != 0) ::CryptContextAddRef(m_hProv, NULL, 0);
  }
  explicit CCryptProv(HCRYPTPROV hProv, BOOL bTakeOwnership = FALSE) : m_hProv(hProv) {
    if (!bTakeOwnership && m_hProv != 0) ::CryptContextAddRef(m_hProv, NULL, 0);
  }
  ~CCryptProv() { Release(); }

  CCryptProv& operator=(const CCryptProv& prov) {
    if (this != &prov) {
      Release();
      m_hProv = prov.m_hProv;
      if (m_hProv != 0) ::CryptContextAddRef(m_hProv, NULL, 0);
    }
    return *this;
  }
  operator HCRYPTPROV() const { return m_hProv; }
  HCRYPTPROV GetHandle() const { return m_hProv; }

  HRESULT AddRef() { return _ATL_CRYPT_HR(::CryptContextAddRef(m_hProv, NULL, 0)); }
  void Attach(HCRYPTPROV hProv, BOOL bTakeOwnership = FALSE) {
    Release();
    m_hProv = hProv;
    if (!bTakeOwnership && m_hProv != 0) ::CryptContextAddRef(m_hProv, NULL, 0);
  }
  HRESULT Release() {
    if (m_hProv == 0) return S_OK;
    BOOL bOK = ::CryptReleaseContext(m_hProv, 0);
    m_hProv = 0;
    return _ATL_CRYPT_HR(bOK);
  }

  HRESULT Initialize(DWORD dwProviderType = PROV_RSA_FULL, LPCTSTR szContainer = NULL,
                     LPCTSTR szProvider = MS_DEF_PROV, DWORD dwFlags = 0) {
    Release();
    if (::CryptAcquireContext(&m_hProv, szContainer, szProvider, dwProviderType, dwFlags))
      return S_OK;
    HRESULT hr = AtlHresultFromLastError();
    // A key container that is not there yet is made.
    if (szContainer != NULL && !(dwFlags & (CRYPT_NEWKEYSET | CRYPT_VERIFYCONTEXT)) &&
        ::GetLastError() == (DWORD)NTE_BAD_KEYSET &&
        ::CryptAcquireContext(&m_hProv, szContainer, szProvider, dwProviderType,
                              dwFlags | CRYPT_NEWKEYSET))
      return S_OK;
    m_hProv = 0;
    return hr;
  }
  HRESULT InitVerifyContext(DWORD dwProviderType = PROV_RSA_FULL,
                            LPCTSTR szProvider = MS_DEF_PROV, DWORD dwFlags = 0) {
    return Initialize(dwProviderType, NULL, szProvider, dwFlags | CRYPT_VERIFYCONTEXT);
  }
  HRESULT InitCreateKeySet(DWORD dwProviderType = PROV_RSA_FULL, LPCTSTR szContainer = NULL,
                           LPCTSTR szProvider = MS_DEF_PROV, DWORD dwFlags = 0) {
    return Initialize(dwProviderType, szContainer, szProvider, dwFlags | CRYPT_NEWKEYSET);
  }
  HRESULT DeleteKeySet(LPCTSTR szContainer, LPCTSTR szProvider = MS_DEF_PROV,
                       DWORD dwProviderType = PROV_RSA_FULL) {
    HCRYPTPROV h = 0;
    return _ATL_CRYPT_HR(
        ::CryptAcquireContext(&h, szContainer, szProvider, dwProviderType, CRYPT_DELETEKEYSET));
  }
  HRESULT Uninitialize() { return Release(); }

  HRESULT GetParam(DWORD dwParam, BYTE* pbData, DWORD* pdwDataLen, DWORD dwFlags = 0) {
    return _ATL_CRYPT_HR(::CryptGetProvParam(m_hProv, dwParam, pbData, pdwDataLen, dwFlags));
  }
  HRESULT SetParam(DWORD dwParam, BYTE* pbData, DWORD dwFlags = 0) {
    return _ATL_CRYPT_HR(::CryptSetProvParam(m_hProv, dwParam, pbData, dwFlags));
  }
  HRESULT GetName(LPSTR szBuf, DWORD* pdwLength) {
    return GetParam(PP_NAME, (BYTE*)szBuf, pdwLength);
  }
  HRESULT GetContainer(LPSTR szBuf, DWORD* pdwLength) {
    return GetParam(PP_CONTAINER, (BYTE*)szBuf, pdwLength);
  }
  HRESULT GetImpType(DWORD* pdwImpType) {
    DWORD dwLen = sizeof(DWORD);
    return GetParam(PP_IMPTYPE, (BYTE*)pdwImpType, &dwLen);
  }
  HRESULT GetVersion(DWORD* pdwVersion) {
    DWORD dwLen = sizeof(DWORD);
    return GetParam(PP_VERSION, (BYTE*)pdwVersion, &dwLen);
  }
  HRESULT GetProvType(DWORD* pdwType) {
    DWORD dwLen = sizeof(DWORD);
    return GetParam(PP_PROVTYPE, (BYTE*)pdwType, &dwLen);
  }
  HRESULT GetSecurityDesc(SECURITY_INFORMATION* pSecInfo) {
    DWORD dwLen = sizeof(SECURITY_INFORMATION);
    return GetParam(PP_KEYSET_SEC_DESCR, (BYTE*)pSecInfo, &dwLen);
  }
  HRESULT SetSecurityDesc(SECURITY_INFORMATION SecInfo) {
    return SetParam(PP_KEYSET_SEC_DESCR, (BYTE*)&SecInfo);
  }
  HRESULT GenRandom(ULONG nLength, BYTE* pbBuffer) {
    return _ATL_CRYPT_HR(::CryptGenRandom(m_hProv, nLength, pbBuffer));
  }
};

class CCryptKey;

// ---------------------------------------------------------------------
// CCryptHash

class CCryptHash {
public:
  HCRYPTHASH m_hHash;

  CCryptHash() : m_hHash(0) {}
  CCryptHash(const CCryptHash& hash) : m_hHash(0) { Duplicate(hash); }
  explicit CCryptHash(HCRYPTHASH hHash, BOOL bTakeOwnership = FALSE) : m_hHash(0) {
    Attach(hHash, bTakeOwnership);
  }
  virtual ~CCryptHash() { Destroy(); }

  CCryptHash& operator=(const CCryptHash& hash) {
    if (this != &hash) Duplicate(hash);
    return *this;
  }
  operator HCRYPTHASH() const { return m_hHash; }
  HCRYPTHASH GetHandle() const { return m_hHash; }

  // Attach without ownership duplicates the handle.
  void Attach(HCRYPTHASH hHash, BOOL bTakeOwnership = FALSE) {
    Destroy();
    if (bTakeOwnership || hHash == 0)
      m_hHash = hHash;
    else
      ::CryptDuplicateHash(hHash, NULL, 0, &m_hHash);
  }
  void Destroy() {
    if (m_hHash != 0) ::CryptDestroyHash(m_hHash);
    m_hHash = 0;
  }
  HRESULT Duplicate(const CCryptHash& hash) {
    Destroy();
    if (hash.m_hHash == 0) return S_OK;
    return _ATL_CRYPT_HR(::CryptDuplicateHash(hash.m_hHash, NULL, 0, &m_hHash));
  }

  HRESULT Initialize(CCryptProv& Prov, ALG_ID Algid, CCryptKey& Key, DWORD dwFlags);
  HRESULT AddData(const BYTE* pbData, DWORD dwDataLen, DWORD dwFlags = 0) {
    return _ATL_CRYPT_HR(::CryptHashData(m_hHash, pbData, dwDataLen, dwFlags));
  }
  HRESULT AddString(LPCTSTR szData, DWORD dwFlags = 0) {
    return AddData((const BYTE*)szData, (DWORD)(lstrlen(szData) * sizeof(TCHAR)), dwFlags);
  }
  HRESULT GetParam(DWORD dwParam, BYTE* pbData, DWORD* pdwDataLen, DWORD dwFlags = 0) {
    return _ATL_CRYPT_HR(::CryptGetHashParam(m_hHash, dwParam, pbData, pdwDataLen, dwFlags));
  }
  HRESULT SetParam(DWORD dwParam, BYTE* pbData, DWORD dwFlags = 0) {
    return _ATL_CRYPT_HR(::CryptSetHashParam(m_hHash, dwParam, pbData, dwFlags));
  }
  HRESULT GetAlgId(ALG_ID* pAlgId) {
    DWORD dwLen = sizeof(ALG_ID);
    return GetParam(HP_ALGID, (BYTE*)pAlgId, &dwLen);
  }
  HRESULT GetSize(DWORD* pdwSize) {
    DWORD dwLen = sizeof(DWORD);
    return GetParam(HP_HASHSIZE, (BYTE*)pdwSize, &dwLen);
  }
  HRESULT GetValue(BYTE* pBuf, DWORD* pdwSize) { return GetParam(HP_HASHVAL, pBuf, pdwSize); }
  HRESULT SetValue(BYTE* pBuf) { return SetParam(HP_HASHVAL, pBuf); }
  HRESULT Sign(BYTE* pbSignature, DWORD* pdwSigLen, DWORD dwFlags = 0,
               DWORD dwKeySpec = AT_SIGNATURE) {
    return _ATL_CRYPT_HR(
        ::CryptSignHash(m_hHash, dwKeySpec, NULL, dwFlags, pbSignature, pdwSigLen));
  }
  HRESULT VerifySignature(const BYTE* pbSignature, DWORD pdwSignLen, CCryptKey& PubKey,
                          DWORD dwFlags = 0);

  static CCryptHash EmptyHash;
};

// ---------------------------------------------------------------------
// CCryptKey

class CCryptKey {
public:
  HCRYPTKEY m_hKey;

  CCryptKey() : m_hKey(0) {}
  CCryptKey(const CCryptKey& key) : m_hKey(0) { Duplicate(key); }
  explicit CCryptKey(HCRYPTKEY hKey, BOOL bTakeOwnership = FALSE) : m_hKey(0) {
    Attach(hKey, bTakeOwnership);
  }
  virtual ~CCryptKey() { Destroy(); }

  CCryptKey& operator=(const CCryptKey& key) {
    if (this != &key) Duplicate(key);
    return *this;
  }
  operator HCRYPTKEY() const { return m_hKey; }
  HCRYPTKEY GetHandle() const { return m_hKey; }

  void Attach(HCRYPTKEY hKey, BOOL bTakeOwnership = FALSE) {
    Destroy();
    if (bTakeOwnership || hKey == 0)
      m_hKey = hKey;
    else
      ::CryptDuplicateKey(hKey, NULL, 0, &m_hKey);
  }
  void Destroy() {
    if (m_hKey != 0) ::CryptDestroyKey(m_hKey);
    m_hKey = 0;
  }
  HRESULT Duplicate(const CCryptKey& key) {
    Destroy();
    if (key.m_hKey == 0) return S_OK;
    return _ATL_CRYPT_HR(::CryptDuplicateKey(key.m_hKey, NULL, 0, &m_hKey));
  }

  HRESULT Encrypt(BOOL final, BYTE* pbData, DWORD* pdwDataLen, DWORD dwBufLen,
                  CCryptHash& Hash = CCryptHash::EmptyHash) {
    return _ATL_CRYPT_HR(::CryptEncrypt(m_hKey, Hash.m_hHash, final, 0, pbData, pdwDataLen,
                                        dwBufLen));
  }
  HRESULT Encrypt(const BYTE* pbPlainText, DWORD dwPlainTextLen, BYTE* pbCipherText,
                  DWORD* pdwCipherTextLen, CCryptHash& Hash = CCryptHash::EmptyHash) {
    // The size first, then the work in the caller's buffer.
    DWORD dwLen = dwPlainTextLen;
    if (!::CryptEncrypt(m_hKey, Hash.m_hHash, TRUE, 0, NULL, &dwLen, 0))
      return AtlHresultFromLastError();
    if (pbCipherText == NULL || *pdwCipherTextLen < dwLen) {
      *pdwCipherTextLen = dwLen;
      return pbCipherText == NULL ? S_OK : HRESULT_FROM_WIN32(ERROR_MORE_DATA);
    }
    memcpy(pbCipherText, pbPlainText, dwPlainTextLen);
    DWORD dwData = dwPlainTextLen;
    if (!::CryptEncrypt(m_hKey, Hash.m_hHash, TRUE, 0, pbCipherText, &dwData,
                        *pdwCipherTextLen))
      return AtlHresultFromLastError();
    *pdwCipherTextLen = dwData;
    return S_OK;
  }
  HRESULT Decrypt(BOOL final, BYTE* pbData, DWORD* pdwDataLen,
                  CCryptHash& Hash = CCryptHash::EmptyHash) {
    return _ATL_CRYPT_HR(::CryptDecrypt(m_hKey, Hash.m_hHash, final, 0, pbData, pdwDataLen));
  }
  HRESULT Decrypt(const BYTE* pbCipherText, DWORD dwCipherTextLen, BYTE* pbPlainText,
                  DWORD* pdwPlainTextLen, CCryptHash& Hash = CCryptHash::EmptyHash) {
    if (pbPlainText == NULL || *pdwPlainTextLen < dwCipherTextLen) {
      *pdwPlainTextLen = dwCipherTextLen;
      return pbPlainText == NULL ? S_OK : HRESULT_FROM_WIN32(ERROR_MORE_DATA);
    }
    memcpy(pbPlainText, pbCipherText, dwCipherTextLen);
    DWORD dwData = dwCipherTextLen;
    if (!::CryptDecrypt(m_hKey, Hash.m_hHash, TRUE, 0, pbPlainText, &dwData))
      return AtlHresultFromLastError();
    *pdwPlainTextLen = dwData;
    return S_OK;
  }
  HRESULT EncryptString(LPCTSTR szPlainText, BYTE* pbCipherText, DWORD* pdwCipherTextLen,
                        CCryptHash& Hash = CCryptHash::EmptyHash) {
    return Encrypt((const BYTE*)szPlainText, (DWORD)((lstrlen(szPlainText) + 1) * sizeof(TCHAR)),
                   pbCipherText, pdwCipherTextLen, Hash);
  }

  HRESULT ExportSimpleBlob(CCryptKey& ExpKey, DWORD dwFlags, BYTE* pbData, DWORD* pdwDataLen) {
    return _ATL_CRYPT_HR(
        ::CryptExportKey(m_hKey, ExpKey.m_hKey, SIMPLEBLOB, dwFlags, pbData, pdwDataLen));
  }
  HRESULT ExportPublicKeyBlob(CCryptKey& ExpKey, DWORD dwFlags, BYTE* pbData,
                              DWORD* pdwDataLen) {
    return _ATL_CRYPT_HR(
        ::CryptExportKey(m_hKey, ExpKey.m_hKey, PUBLICKEYBLOB, dwFlags, pbData, pdwDataLen));
  }
  HRESULT ExportPrivateKeyBlob(CCryptKey& ExpKey, DWORD dwFlags, BYTE* pbData,
                               DWORD* pdwDataLen) {
    return _ATL_CRYPT_HR(
        ::CryptExportKey(m_hKey, ExpKey.m_hKey, PRIVATEKEYBLOB, dwFlags, pbData, pdwDataLen));
  }

  HRESULT GetParam(DWORD dwParam, BYTE* pbData, DWORD* pdwDataLen, DWORD dwFlags = 0) {
    return _ATL_CRYPT_HR(::CryptGetKeyParam(m_hKey, dwParam, pbData, pdwDataLen, dwFlags));
  }
  HRESULT SetParam(DWORD dwParam, BYTE* pbData, DWORD dwFlags = 0) {
    return _ATL_CRYPT_HR(::CryptSetKeyParam(m_hKey, dwParam, pbData, dwFlags));
  }
  HRESULT GetAlgId(ALG_ID* pAlgId) {
    DWORD dwLen = sizeof(ALG_ID);
    return GetParam(KP_ALGID, (BYTE*)pAlgId, &dwLen);
  }
  HRESULT SetAlgId(ALG_ID AlgId, DWORD dwFlags) { return SetParam(KP_ALGID, (BYTE*)&AlgId, dwFlags); }
  HRESULT GetBlockLength(DWORD* pdwBlockLen) {
    DWORD dwLen = sizeof(DWORD);
    return GetParam(KP_BLOCKLEN, (BYTE*)pdwBlockLen, &dwLen);
  }
  HRESULT GetKeyLength(DWORD* pdwKeyLen) {
    DWORD dwLen = sizeof(DWORD);
    return GetParam(KP_KEYLEN, (BYTE*)pdwKeyLen, &dwLen);
  }
  HRESULT GetSalt(BYTE* pbSalt, DWORD* pdwLength) { return GetParam(KP_SALT, pbSalt, pdwLength); }
  HRESULT SetSalt(BYTE* pbSalt) { return SetParam(KP_SALT, pbSalt); }
  HRESULT SetSaltEx(_CRYPTOAPI_BLOB* pBlobSalt) { return SetParam(KP_SALT_EX, (BYTE*)pBlobSalt); }
  HRESULT GetPermissions(DWORD* pdwPerms) {
    DWORD dwLen = sizeof(DWORD);
    return GetParam(KP_PERMISSIONS, (BYTE*)pdwPerms, &dwLen);
  }
  HRESULT SetPermissions(DWORD dwPerms) { return SetParam(KP_PERMISSIONS, (BYTE*)&dwPerms); }
  HRESULT GetIV(BYTE* pbIV, DWORD* pdwLength) { return GetParam(KP_IV, pbIV, pdwLength); }
  HRESULT SetIV(BYTE* pbIV) { return SetParam(KP_IV, pbIV); }
  HRESULT GetPadding(DWORD* pdwPadding) {
    DWORD dwLen = sizeof(DWORD);
    return GetParam(KP_PADDING, (BYTE*)pdwPadding, &dwLen);
  }
  HRESULT SetPadding(DWORD dwPadding) { return SetParam(KP_PADDING, (BYTE*)&dwPadding); }
  HRESULT GetMode(DWORD* pdwMode) {
    DWORD dwLen = sizeof(DWORD);
    return GetParam(KP_MODE, (BYTE*)pdwMode, &dwLen);
  }
  HRESULT SetMode(DWORD dwMode) { return SetParam(KP_MODE, (BYTE*)&dwMode); }
  HRESULT GetModeBits(DWORD* pdwModeBits) {
    DWORD dwLen = sizeof(DWORD);
    return GetParam(KP_MODE_BITS, (BYTE*)pdwModeBits, &dwLen);
  }
  HRESULT SetModeBits(DWORD dwModeBits) { return SetParam(KP_MODE_BITS, (BYTE*)&dwModeBits); }
  HRESULT GetEffKeyLen(DWORD* pdwEffKeyLen) {
    DWORD dwLen = sizeof(DWORD);
    return GetParam(KP_EFFECTIVE_KEYLEN, (BYTE*)pdwEffKeyLen, &dwLen);
  }
  HRESULT SetEffKeyLen(DWORD dwEffKeyLen) {
    return SetParam(KP_EFFECTIVE_KEYLEN, (BYTE*)&dwEffKeyLen);
  }

  static CCryptKey EmptyKey;
};

__declspec(selectany) CCryptHash CCryptHash::EmptyHash;
__declspec(selectany) CCryptKey CCryptKey::EmptyKey;

inline HRESULT CCryptHash::Initialize(CCryptProv& Prov, ALG_ID Algid, CCryptKey& Key,
                                      DWORD dwFlags) {
  Destroy();
  return _ATL_CRYPT_HR(::CryptCreateHash(Prov.m_hProv, Algid, Key.m_hKey, dwFlags, &m_hHash));
}

inline HRESULT CCryptHash::VerifySignature(const BYTE* pbSignature, DWORD pdwSignLen,
                                           CCryptKey& PubKey, DWORD dwFlags) {
  return _ATL_CRYPT_HR(
      ::CryptVerifySignature(m_hHash, pbSignature, pdwSignLen, PubKey.m_hKey, NULL, dwFlags));
}

// ---------------------------------------------------------------------
// The kinds of key

class CCryptDerivedKey : public CCryptKey {
public:
  HRESULT Initialize(CCryptProv& Prov, CCryptHash& Hash, ALG_ID algid = CALG_RC4,
                     DWORD dwFlags = CRYPT_EXPORTABLE) {
    Destroy();
    return _ATL_CRYPT_HR(::CryptDeriveKey(Prov.m_hProv, algid, Hash.m_hHash, dwFlags, &m_hKey));
  }
};

class CCryptRandomKey : public CCryptKey {
public:
  HRESULT Initialize(CCryptProv& Prov, ALG_ID algid = CALG_RC4,
                     DWORD dwFlags = CRYPT_EXPORTABLE) {
    Destroy();
    return _ATL_CRYPT_HR(::CryptGenKey(Prov.m_hProv, algid, dwFlags, &m_hKey));
  }
};

class CCryptKeyPair : public CCryptKey {
public:
  HRESULT Initialize(CCryptProv& Prov, ALG_ID algid = AT_KEYEXCHANGE,
                     DWORD dwFlags = CRYPT_EXPORTABLE) {
    Destroy();
    return _ATL_CRYPT_HR(::CryptGenKey(Prov.m_hProv, algid, dwFlags, &m_hKey));
  }
};

class CCryptUserExKey : public CCryptKey {
public:
  HRESULT Initialize(CCryptProv& Prov) {
    Destroy();
    return _ATL_CRYPT_HR(::CryptGetUserKey(Prov.m_hProv, AT_KEYEXCHANGE, &m_hKey));
  }
  HRESULT Create(CCryptProv& Prov) {
    Destroy();
    return _ATL_CRYPT_HR(::CryptGenKey(Prov.m_hProv, AT_KEYEXCHANGE, CRYPT_EXPORTABLE, &m_hKey));
  }
};

class CCryptUserSigKey : public CCryptKey {
public:
  HRESULT Initialize(CCryptProv& Prov) {
    Destroy();
    return _ATL_CRYPT_HR(::CryptGetUserKey(Prov.m_hProv, AT_SIGNATURE, &m_hKey));
  }
  HRESULT Create(CCryptProv& Prov) {
    Destroy();
    return _ATL_CRYPT_HR(::CryptGenKey(Prov.m_hProv, AT_SIGNATURE, CRYPT_EXPORTABLE, &m_hKey));
  }
};

class CCryptImportKey : public CCryptKey {
public:
  HRESULT Initialize(CCryptProv& Prov, BYTE* pbData, DWORD dwDataLen, CCryptKey& PubKey,
                     DWORD dwFlags) {
    Destroy();
    return _ATL_CRYPT_HR(
        ::CryptImportKey(Prov.m_hProv, pbData, dwDataLen, PubKey.m_hKey, dwFlags, &m_hKey));
  }
};

// ---------------------------------------------------------------------
// The kinds of hash; each can hash a first piece of text as it starts.

class CCryptKeyedHash : public CCryptHash {
public:
  HRESULT Initialize(CCryptProv& Prov, ALG_ID Algid, CCryptKey& Key, DWORD dwFlags) {
    return CCryptHash::Initialize(Prov, Algid, Key, dwFlags);
  }
};

#define _ATL_DECLARE_CRYPT_HASH(name, algid)                                  \
  class name : public CCryptHash {                                            \
  public:                                                                     \
    HRESULT Initialize(CCryptProv& Prov, LPCTSTR szText = NULL) {             \
      HRESULT hr = CCryptHash::Initialize(Prov, algid, CCryptKey::EmptyKey, 0); \
      if (SUCCEEDED(hr) && szText != NULL) hr = AddString(szText);            \
      return hr;                                                              \
    }                                                                         \
  };

_ATL_DECLARE_CRYPT_HASH(CCryptMD5Hash, CALG_MD5)
_ATL_DECLARE_CRYPT_HASH(CCryptMD4Hash, CALG_MD4)
_ATL_DECLARE_CRYPT_HASH(CCryptMD2Hash, CALG_MD2)
_ATL_DECLARE_CRYPT_HASH(CCryptSHAHash, CALG_SHA)
_ATL_DECLARE_CRYPT_HASH(CCryptSHA1Hash, CALG_SHA1)
#ifdef CALG_SHA_256
_ATL_DECLARE_CRYPT_HASH(CCryptSHA256Hash, CALG_SHA_256)
_ATL_DECLARE_CRYPT_HASH(CCryptSHA384Hash, CALG_SHA_384)
_ATL_DECLARE_CRYPT_HASH(CCryptSHA512Hash, CALG_SHA_512)
#endif
#undef _ATL_DECLARE_CRYPT_HASH

class CCryptSSL3SHAMD5Hash : public CCryptHash {
public:
  HRESULT Initialize(CCryptProv& Prov, CCryptKey& Key, LPCTSTR szText = NULL) {
    HRESULT hr = CCryptHash::Initialize(Prov, CALG_SSL3_SHAMD5, Key, 0);
    if (SUCCEEDED(hr) && szText != NULL) hr = AddString(szText);
    return hr;
  }
};

// HMAC over a key: the inner hash is MD5 or SHA-1 (CALG_SHA1 by default).
class CCryptHMACHash : public CCryptHash {
public:
  HRESULT Initialize(CCryptProv& Prov, CCryptKey& Key, ALG_ID algidHash = CALG_SHA1,
                     LPCTSTR szText = NULL) {
    HRESULT hr = CCryptHash::Initialize(Prov, CALG_HMAC, Key, 0);
    if (FAILED(hr)) return hr;
    HMAC_INFO info;
    memset(&info, 0, sizeof(info));
    info.HashAlgid = algidHash;
    hr = SetParam(HP_HMAC_INFO, (BYTE*)&info);
    if (SUCCEEDED(hr) && szText != NULL) hr = AddString(szText);
    return hr;
  }
};

#undef _ATL_CRYPT_HR

}  // namespace ATL

#endif  // __ATLCRYPT_H__
