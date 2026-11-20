// crypt.cpp - CryptoAPI: provider contexts, hashes (MD4, MD5, SHA-1,
// SHA-256, SHA-384, SHA-512, the SSL3 MD5 and SHA-1 pair, and HMAC over
// any of them), RC4 and AES keys, and random numbers from the system.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#include "winport.h"

#include <wincrypt.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>

#include <algorithm>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace {

// ---------------------------------------------------------------------
// Digests

const uint32_t kSha256K[64] = {
    0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u, 0x3956c25bu, 0x59f111f1u, 0x923f82a4u, 0xab1c5ed5u,
    0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u, 0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u, 0xc19bf174u,
    0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu, 0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau,
    0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u, 0xc6e00bf3u, 0xd5a79147u, 0x06ca6351u, 0x14292967u,
    0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu, 0x53380d13u, 0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u,
    0xa2bfe8a1u, 0xa81a664bu, 0xc24b8b70u, 0xc76c51a3u, 0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u,
    0x19a4c116u, 0x1e376c08u, 0x2748774cu, 0x34b0bcb5u, 0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu, 0x682e6ff3u,
    0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u, 0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u,
};
const uint32_t kSha256H[8] = {
    0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u, 0xa54ff53au, 0x510e527fu, 0x9b05688cu, 0x1f83d9abu, 0x5be0cd19u,
};
const uint64_t kSha512K[80] = {
    0x428a2f98d728ae22ull, 0x7137449123ef65cdull, 0xb5c0fbcfec4d3b2full, 0xe9b5dba58189dbbcull,
    0x3956c25bf348b538ull, 0x59f111f1b605d019ull, 0x923f82a4af194f9bull, 0xab1c5ed5da6d8118ull,
    0xd807aa98a3030242ull, 0x12835b0145706fbeull, 0x243185be4ee4b28cull, 0x550c7dc3d5ffb4e2ull,
    0x72be5d74f27b896full, 0x80deb1fe3b1696b1ull, 0x9bdc06a725c71235ull, 0xc19bf174cf692694ull,
    0xe49b69c19ef14ad2ull, 0xefbe4786384f25e3ull, 0x0fc19dc68b8cd5b5ull, 0x240ca1cc77ac9c65ull,
    0x2de92c6f592b0275ull, 0x4a7484aa6ea6e483ull, 0x5cb0a9dcbd41fbd4ull, 0x76f988da831153b5ull,
    0x983e5152ee66dfabull, 0xa831c66d2db43210ull, 0xb00327c898fb213full, 0xbf597fc7beef0ee4ull,
    0xc6e00bf33da88fc2ull, 0xd5a79147930aa725ull, 0x06ca6351e003826full, 0x142929670a0e6e70ull,
    0x27b70a8546d22ffcull, 0x2e1b21385c26c926ull, 0x4d2c6dfc5ac42aedull, 0x53380d139d95b3dfull,
    0x650a73548baf63deull, 0x766a0abb3c77b2a8ull, 0x81c2c92e47edaee6ull, 0x92722c851482353bull,
    0xa2bfe8a14cf10364ull, 0xa81a664bbc423001ull, 0xc24b8b70d0f89791ull, 0xc76c51a30654be30ull,
    0xd192e819d6ef5218ull, 0xd69906245565a910ull, 0xf40e35855771202aull, 0x106aa07032bbd1b8ull,
    0x19a4c116b8d2d0c8ull, 0x1e376c085141ab53ull, 0x2748774cdf8eeb99ull, 0x34b0bcb5e19b48a8ull,
    0x391c0cb3c5c95a63ull, 0x4ed8aa4ae3418acbull, 0x5b9cca4f7763e373ull, 0x682e6ff3d6b2b8a3ull,
    0x748f82ee5defb2fcull, 0x78a5636f43172f60ull, 0x84c87814a1f0ab72ull, 0x8cc702081a6439ecull,
    0x90befffa23631e28ull, 0xa4506cebde82bde9ull, 0xbef9a3f7b2c67915ull, 0xc67178f2e372532bull,
    0xca273eceea26619cull, 0xd186b8c721c0c207ull, 0xeada7dd6cde0eb1eull, 0xf57d4f7fee6ed178ull,
    0x06f067aa72176fbaull, 0x0a637dc5a2c898a6ull, 0x113f9804bef90daeull, 0x1b710b35131c471bull,
    0x28db77f523047d84ull, 0x32caab7b40c72493ull, 0x3c9ebe0a15c9bebcull, 0x431d67c49c100d4cull,
    0x4cc5d4becb3e42b6ull, 0x597f299cfc657e2aull, 0x5fcb6fab3ad6faecull, 0x6c44198c4a475817ull,
};
const uint64_t kSha512H[8] = {
    0x6a09e667f3bcc908ull, 0xbb67ae8584caa73bull, 0x3c6ef372fe94f82bull, 0xa54ff53a5f1d36f1ull,
    0x510e527fade682d1ull, 0x9b05688c2b3e6c1full, 0x1f83d9abfb41bd6bull, 0x5be0cd19137e2179ull,
};
const uint64_t kSha384H[8] = {
    0xcbbb9d5dc1059ed8ull, 0x629a292a367cd507ull, 0x9159015a3070dd17ull, 0x152fecd8f70e5939ull,
    0x67332667ffc00b31ull, 0x8eb44a8768581511ull, 0xdb0c2e0d64f98fa7ull, 0x47b5481dbefa4fa4ull,
};
const uint32_t kMd5K[64] = {
    0xd76aa478u, 0xe8c7b756u, 0x242070dbu, 0xc1bdceeeu, 0xf57c0fafu, 0x4787c62au, 0xa8304613u, 0xfd469501u,
    0x698098d8u, 0x8b44f7afu, 0xffff5bb1u, 0x895cd7beu, 0x6b901122u, 0xfd987193u, 0xa679438eu, 0x49b40821u,
    0xf61e2562u, 0xc040b340u, 0x265e5a51u, 0xe9b6c7aau, 0xd62f105du, 0x02441453u, 0xd8a1e681u, 0xe7d3fbc8u,
    0x21e1cde6u, 0xc33707d6u, 0xf4d50d87u, 0x455a14edu, 0xa9e3e905u, 0xfcefa3f8u, 0x676f02d9u, 0x8d2a4c8au,
    0xfffa3942u, 0x8771f681u, 0x6d9d6122u, 0xfde5380cu, 0xa4beea44u, 0x4bdecfa9u, 0xf6bb4b60u, 0xbebfbc70u,
    0x289b7ec6u, 0xeaa127fau, 0xd4ef3085u, 0x04881d05u, 0xd9d4d039u, 0xe6db99e5u, 0x1fa27cf8u, 0xc4ac5665u,
    0xf4292244u, 0x432aff97u, 0xab9423a7u, 0xfc93a039u, 0x655b59c3u, 0x8f0ccc92u, 0xffeff47du, 0x85845dd1u,
    0x6fa87e4fu, 0xfe2ce6e0u, 0xa3014314u, 0x4e0811a1u, 0xf7537e82u, 0xbd3af235u, 0x2ad7d2bbu, 0xeb86d391u,
};

uint32_t Rol(uint32_t x, int n) { return (x << n) | (x >> (32 - n)); }
uint32_t Ror(uint32_t x, int n) { return (x >> n) | (x << (32 - n)); }
uint64_t Ror64(uint64_t x, int n) { return (x >> n) | (x << (64 - n)); }

struct Digest {
  virtual ~Digest() {}
  virtual void Update(const BYTE* p, size_t n) = 0;
  virtual void Final(BYTE* out) = 0;  // Size() bytes; the digest is then spent
  virtual size_t Size() const = 0;
  virtual size_t Block() const = 0;
  virtual Digest* Clone() const = 0;
};

// The Merkle-Damgard digests with 64-byte blocks.
template <typename Self>
struct Digest64 : Digest {
  BYTE buf[64];
  size_t used = 0;
  uint64_t total = 0;
  bool bigEndian = false;
  void Update(const BYTE* p, size_t n) override {
    total += n;
    while (n) {
      size_t k = std::min(n, 64 - used);
      memcpy(buf + used, p, k);
      used += k, p += k, n -= k;
      if (used == 64) {
        static_cast<Self*>(this)->Compress(buf);
        used = 0;
      }
    }
  }
  void Pad() {
    uint64_t bits = total * 8;
    BYTE pad[72] = {0x80};
    size_t padLen = (used < 56 ? 56 - used : 120 - used);
    BYTE len[8];
    for (int i = 0; i < 8; i++) len[i] = bigEndian ? (BYTE)(bits >> (56 - 8 * i)) : (BYTE)(bits >> (8 * i));
    uint64_t keep = total;
    Update(pad, padLen);
    Update(len, 8);
    total = keep;
  }
  size_t Block() const override { return 64; }
  Digest* Clone() const override { return new Self(*static_cast<const Self*>(this)); }
};

uint32_t Le32(const BYTE* p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }
uint32_t Be32(const BYTE* p) { return (uint32_t)p[0] << 24 | p[1] << 16 | p[2] << 8 | p[3]; }
void PutLe32(BYTE* p, uint32_t v) {
  for (int i = 0; i < 4; i++) p[i] = (BYTE)(v >> (8 * i));
}
void PutBe32(BYTE* p, uint32_t v) {
  for (int i = 0; i < 4; i++) p[i] = (BYTE)(v >> (24 - 8 * i));
}

struct Md5 : Digest64<Md5> {
  uint32_t h[4] = {0x67452301u, 0xefcdab89u, 0x98badcfeu, 0x10325476u};
  void Compress(const BYTE* b) {
    static const int s[64] = {7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22,
                              5, 9,  14, 20, 5, 9,  14, 20, 5, 9,  14, 20, 5, 9,  14, 20,
                              4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23,
                              6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21};
    uint32_t m[16];
    for (int i = 0; i < 16; i++) m[i] = Le32(b + 4 * i);
    uint32_t a = h[0], bb = h[1], c = h[2], d = h[3];
    for (int i = 0; i < 64; i++) {
      uint32_t f;
      int g;
      if (i < 16) f = (bb & c) | (~bb & d), g = i;
      else if (i < 32) f = (d & bb) | (~d & c), g = (5 * i + 1) % 16;
      else if (i < 48) f = bb ^ c ^ d, g = (3 * i + 5) % 16;
      else f = c ^ (bb | ~d), g = (7 * i) % 16;
      uint32_t t = d;
      d = c;
      c = bb;
      bb = bb + Rol(a + f + kMd5K[i] + m[g], s[i]);
      a = t;
    }
    h[0] += a, h[1] += bb, h[2] += c, h[3] += d;
  }
  void Final(BYTE* out) override {
    Pad();
    for (int i = 0; i < 4; i++) PutLe32(out + 4 * i, h[i]);
  }
  size_t Size() const override { return 16; }
};

struct Md4 : Digest64<Md4> {
  uint32_t h[4] = {0x67452301u, 0xefcdab89u, 0x98badcfeu, 0x10325476u};
  void Compress(const BYTE* b) {
    uint32_t x[16];
    for (int i = 0; i < 16; i++) x[i] = Le32(b + 4 * i);
    uint32_t v[4] = {h[0], h[1], h[2], h[3]};
    static const int o2[16] = {0, 4, 8, 12, 1, 5, 9, 13, 2, 6, 10, 14, 3, 7, 11, 15};
    static const int o3[16] = {0, 8, 4, 12, 2, 10, 6, 14, 1, 9, 5, 13, 3, 11, 7, 15};
    static const int s1[4] = {3, 7, 11, 19}, s2[4] = {3, 5, 9, 13}, s3[4] = {3, 9, 11, 15};
    for (int r = 0; r < 3; r++)
      for (int i = 0; i < 16; i++) {
        uint32_t& a = v[(16 - i) % 4];
        uint32_t bb = v[(17 - i) % 4], c = v[(18 - i) % 4], d = v[(19 - i) % 4];
        uint32_t f;
        int k, s;
        if (r == 0) f = (bb & c) | (~bb & d), k = i, s = s1[i % 4];
        else if (r == 1) f = ((bb & c) | (bb & d) | (c & d)) + 0x5a827999u, k = o2[i], s = s2[i % 4];
        else f = (bb ^ c ^ d) + 0x6ed9eba1u, k = o3[i], s = s3[i % 4];
        a = Rol(a + f + x[k], s);
      }
    for (int i = 0; i < 4; i++) h[i] += v[i];
  }
  void Final(BYTE* out) override {
    Pad();
    for (int i = 0; i < 4; i++) PutLe32(out + 4 * i, h[i]);
  }
  size_t Size() const override { return 16; }
};

struct Sha1 : Digest64<Sha1> {
  uint32_t h[5] = {0x67452301u, 0xefcdab89u, 0x98badcfeu, 0x10325476u, 0xc3d2e1f0u};
  Sha1() { bigEndian = true; }
  void Compress(const BYTE* b) {
    uint32_t w[80];
    for (int i = 0; i < 16; i++) w[i] = Be32(b + 4 * i);
    for (int i = 16; i < 80; i++) w[i] = Rol(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
    uint32_t a = h[0], bb = h[1], c = h[2], d = h[3], e = h[4];
    for (int i = 0; i < 80; i++) {
      uint32_t f, k;
      if (i < 20) f = (bb & c) | (~bb & d), k = 0x5a827999u;
      else if (i < 40) f = bb ^ c ^ d, k = 0x6ed9eba1u;
      else if (i < 60) f = (bb & c) | (bb & d) | (c & d), k = 0x8f1bbcdcu;
      else f = bb ^ c ^ d, k = 0xca62c1d6u;
      uint32_t t = Rol(a, 5) + f + e + k + w[i];
      e = d, d = c, c = Rol(bb, 30), bb = a, a = t;
    }
    h[0] += a, h[1] += bb, h[2] += c, h[3] += d, h[4] += e;
  }
  void Final(BYTE* out) override {
    Pad();
    for (int i = 0; i < 5; i++) PutBe32(out + 4 * i, h[i]);
  }
  size_t Size() const override { return 20; }
};

struct Sha256 : Digest64<Sha256> {
  uint32_t h[8];
  Sha256() {
    bigEndian = true;
    memcpy(h, kSha256H, sizeof h);
  }
  void Compress(const BYTE* b) {
    uint32_t w[64];
    for (int i = 0; i < 16; i++) w[i] = Be32(b + 4 * i);
    for (int i = 16; i < 64; i++) {
      uint32_t s0 = Ror(w[i - 15], 7) ^ Ror(w[i - 15], 18) ^ (w[i - 15] >> 3);
      uint32_t s1 = Ror(w[i - 2], 17) ^ Ror(w[i - 2], 19) ^ (w[i - 2] >> 10);
      w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }
    uint32_t v[8];
    memcpy(v, h, sizeof v);
    for (int i = 0; i < 64; i++) {
      uint32_t S1 = Ror(v[4], 6) ^ Ror(v[4], 11) ^ Ror(v[4], 25);
      uint32_t ch = (v[4] & v[5]) ^ (~v[4] & v[6]);
      uint32_t t1 = v[7] + S1 + ch + kSha256K[i] + w[i];
      uint32_t S0 = Ror(v[0], 2) ^ Ror(v[0], 13) ^ Ror(v[0], 22);
      uint32_t maj = (v[0] & v[1]) ^ (v[0] & v[2]) ^ (v[1] & v[2]);
      memmove(v + 1, v, 7 * sizeof v[0]);
      v[4] += t1;
      v[0] = t1 + S0 + maj;
    }
    for (int i = 0; i < 8; i++) h[i] += v[i];
  }
  void Final(BYTE* out) override {
    Pad();
    for (int i = 0; i < 8; i++) PutBe32(out + 4 * i, h[i]);
  }
  size_t Size() const override { return 32; }
};

// SHA-512, and SHA-384 with other starting values and a shorter result.
struct Sha512 : Digest {
  uint64_t h[8];
  BYTE buf[128];
  size_t used = 0;
  uint64_t total = 0;
  size_t size;
  explicit Sha512(bool is384) : size(is384 ? 48 : 64) { memcpy(h, is384 ? kSha384H : kSha512H, sizeof h); }
  void Compress(const BYTE* b) {
    uint64_t w[80];
    for (int i = 0; i < 16; i++) {
      w[i] = 0;
      for (int k = 0; k < 8; k++) w[i] = w[i] << 8 | b[8 * i + k];
    }
    for (int i = 16; i < 80; i++) {
      uint64_t s0 = Ror64(w[i - 15], 1) ^ Ror64(w[i - 15], 8) ^ (w[i - 15] >> 7);
      uint64_t s1 = Ror64(w[i - 2], 19) ^ Ror64(w[i - 2], 61) ^ (w[i - 2] >> 6);
      w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }
    uint64_t v[8];
    memcpy(v, h, sizeof v);
    for (int i = 0; i < 80; i++) {
      uint64_t S1 = Ror64(v[4], 14) ^ Ror64(v[4], 18) ^ Ror64(v[4], 41);
      uint64_t ch = (v[4] & v[5]) ^ (~v[4] & v[6]);
      uint64_t t1 = v[7] + S1 + ch + kSha512K[i] + w[i];
      uint64_t S0 = Ror64(v[0], 28) ^ Ror64(v[0], 34) ^ Ror64(v[0], 39);
      uint64_t maj = (v[0] & v[1]) ^ (v[0] & v[2]) ^ (v[1] & v[2]);
      memmove(v + 1, v, 7 * sizeof v[0]);
      v[4] += t1;
      v[0] = t1 + S0 + maj;
    }
    for (int i = 0; i < 8; i++) h[i] += v[i];
  }
  void Update(const BYTE* p, size_t n) override {
    total += n;
    while (n) {
      size_t k = std::min(n, 128 - used);
      memcpy(buf + used, p, k);
      used += k, p += k, n -= k;
      if (used == 128) {
        Compress(buf);
        used = 0;
      }
    }
  }
  void Final(BYTE* out) override {
    uint64_t bits = total * 8;
    BYTE pad[144] = {0x80};
    size_t padLen = used < 112 ? 112 - used : 240 - used;
    BYTE len[16] = {0};
    for (int i = 0; i < 8; i++) len[8 + i] = (BYTE)(bits >> (56 - 8 * i));
    Update(pad, padLen);
    Update(len, 16);
    for (size_t i = 0; i < size; i++) out[i] = (BYTE)(h[i / 8] >> (56 - 8 * (i % 8)));
  }
  size_t Size() const override { return size; }
  size_t Block() const override { return 128; }
  Digest* Clone() const override { return new Sha512(*this); }
};

// SSL3's pair: MD5 then SHA-1 of the same data, 36 bytes.
struct Ssl3ShaMd5 : Digest {
  Md5 md5;
  Sha1 sha;
  void Update(const BYTE* p, size_t n) override {
    md5.Update(p, n);
    sha.Update(p, n);
  }
  void Final(BYTE* out) override {
    md5.Final(out);
    sha.Final(out + 16);
  }
  size_t Size() const override { return 36; }
  size_t Block() const override { return 64; }
  Digest* Clone() const override { return new Ssl3ShaMd5(*this); }
};

Digest* NewDigest(ALG_ID alg) {
  switch (alg) {
    case CALG_MD4: return new Md4;
    case CALG_MD5: return new Md5;
    case CALG_SHA1: return new Sha1;
    case CALG_SHA_256: return new Sha256;
    case CALG_SHA_384: return new Sha512(true);
    case CALG_SHA_512: return new Sha512(false);
    case CALG_SSL3_SHAMD5: return new Ssl3ShaMd5;
    default: return NULL;
  }
}

// ---------------------------------------------------------------------
// Ciphers

struct Rc4 {
  BYTE s[256];
  int i = 0, j = 0;
  void Init(const BYTE* key, size_t n) {
    for (int k = 0; k < 256; k++) s[k] = (BYTE)k;
    for (int k = 0, jj = 0; k < 256; k++) {
      jj = (jj + s[k] + key[k % n]) & 255;
      std::swap(s[k], s[jj]);
    }
    i = j = 0;
  }
  void Run(BYTE* p, size_t n) {
    for (size_t k = 0; k < n; k++) {
      i = (i + 1) & 255;
      j = (j + s[i]) & 255;
      std::swap(s[i], s[j]);
      p[k] ^= s[(s[i] + s[j]) & 255];
    }
  }
};

BYTE Xtime(BYTE x) { return (BYTE)((x << 1) ^ ((x & 0x80) ? 0x1b : 0)); }
BYTE Mul(BYTE a, BYTE b) {
  BYTE r = 0;
  while (b) {
    if (b & 1) r ^= a;
    a = Xtime(a);
    b >>= 1;
  }
  return r;
}

struct AesTables {
  BYTE sbox[256], inv[256];
  AesTables() {
    // The S-box from the multiplicative inverse and the affine map.
    BYTE p = 1, q = 1;
    do {
      p = (BYTE)(p ^ (p << 1) ^ ((p & 0x80) ? 0x1b : 0));
      q ^= q << 1;
      q ^= q << 2;
      q ^= q << 4;
      if (q & 0x80) q ^= 0x09;
      BYTE x = (BYTE)(q ^ (q << 1 | q >> 7) ^ (q << 2 | q >> 6) ^ (q << 3 | q >> 5) ^ (q << 4 | q >> 4));
      sbox[p] = (BYTE)(x ^ 0x63);
    } while (p != 1);
    sbox[0] = 0x63;
    for (int i = 0; i < 256; i++) inv[sbox[i]] = (BYTE)i;
  }
};
const AesTables& Aes() {
  static AesTables* t = new AesTables;
  return *t;
}

struct AesKey {
  int rounds = 0;
  BYTE rk[240];
  void Init(const BYTE* key, size_t n) {
    const BYTE* s = Aes().sbox;
    int nk = (int)n / 4;
    rounds = nk + 6;
    memcpy(rk, key, n);
    BYTE rcon = 1;
    for (int i = nk; i < 4 * (rounds + 1); i++) {
      BYTE t[4];
      memcpy(t, rk + 4 * (i - 1), 4);
      if (i % nk == 0) {
        BYTE u = t[0];
        t[0] = (BYTE)(s[t[1]] ^ rcon), t[1] = s[t[2]], t[2] = s[t[3]], t[3] = s[u];
        rcon = Xtime(rcon);
      } else if (nk > 6 && i % nk == 4) {
        for (int k = 0; k < 4; k++) t[k] = s[t[k]];
      }
      for (int k = 0; k < 4; k++) rk[4 * i + k] = (BYTE)(rk[4 * (i - nk) + k] ^ t[k]);
    }
  }
  void Encrypt(BYTE* b) const {
    const BYTE* s = Aes().sbox;
    for (int k = 0; k < 16; k++) b[k] ^= rk[k];
    for (int r = 1; r <= rounds; r++) {
      BYTE t[16];
      for (int k = 0; k < 16; k++) t[k] = s[b[(k + 4 * (k % 4)) % 16]];  // SubBytes, ShiftRows
      if (r != rounds)
        for (int c = 0; c < 4; c++) {
          BYTE* col = t + 4 * c;
          BYTE a0 = col[0], a1 = col[1], a2 = col[2], a3 = col[3];
          col[0] = (BYTE)(Xtime(a0) ^ Xtime(a1) ^ a1 ^ a2 ^ a3);
          col[1] = (BYTE)(a0 ^ Xtime(a1) ^ Xtime(a2) ^ a2 ^ a3);
          col[2] = (BYTE)(a0 ^ a1 ^ Xtime(a2) ^ Xtime(a3) ^ a3);
          col[3] = (BYTE)(Xtime(a0) ^ a0 ^ a1 ^ a2 ^ Xtime(a3));
        }
      for (int k = 0; k < 16; k++) b[k] = (BYTE)(t[k] ^ rk[16 * r + k]);
    }
  }
  void Decrypt(BYTE* b) const {
    const BYTE* si = Aes().inv;
    for (int k = 0; k < 16; k++) b[k] ^= rk[16 * rounds + k];
    for (int r = rounds - 1; r >= 0; r--) {
      BYTE t[16];
      for (int k = 0; k < 16; k++) t[(k + 4 * (k % 4)) % 16] = si[b[k]];  // inverse ShiftRows, SubBytes
      for (int k = 0; k < 16; k++) t[k] ^= rk[16 * r + k];
      if (r != 0)
        for (int c = 0; c < 4; c++) {
          BYTE* col = t + 4 * c;
          BYTE a0 = col[0], a1 = col[1], a2 = col[2], a3 = col[3];
          col[0] = (BYTE)(Mul(a0, 14) ^ Mul(a1, 11) ^ Mul(a2, 13) ^ Mul(a3, 9));
          col[1] = (BYTE)(Mul(a0, 9) ^ Mul(a1, 14) ^ Mul(a2, 11) ^ Mul(a3, 13));
          col[2] = (BYTE)(Mul(a0, 13) ^ Mul(a1, 9) ^ Mul(a2, 14) ^ Mul(a3, 11));
          col[3] = (BYTE)(Mul(a0, 11) ^ Mul(a1, 13) ^ Mul(a2, 9) ^ Mul(a3, 14));
        }
      memcpy(b, t, 16);
    }
  }
};

// ---------------------------------------------------------------------
// Handles

struct Provider {
  DWORD magic = 0x76727043;  // "Cprv"
  int refs = 1;
  DWORD type = PROV_RSA_FULL;
  std::string name, container;
  bool enhanced = false;  // longer default keys than the base provider's
};

struct Key {
  DWORD magic = 0x79654b43;  // "CKey"
  Provider* prov = NULL;
  ALG_ID alg = 0;
  std::vector<BYTE> material;  // the key proper
  std::vector<BYTE> salt;
  DWORD flags = 0, permissions = 0xFFFFFFFF;
  DWORD mode = CRYPT_MODE_CBC, padding = PKCS5_PADDING;
  BYTE iv[16] = {0};
  // The running state, from the last Final on.
  Rc4 rc4;
  AesKey aes;
  BYTE chain[16] = {0};
  bool fresh = true;
  // Decryption keeps the last block back until it knows it is the last.
  std::vector<BYTE> pending;
};

struct Hash {
  DWORD magic = 0x73614843;  // "CHas"
  Provider* prov = NULL;
  ALG_ID alg = 0;
  std::unique_ptr<Digest> d;
  // HMAC: the key, and the inner digest once HP_HMAC_INFO is set.
  bool hmac = false;
  std::vector<BYTE> hmacKey;
  ALG_ID hmacAlg = 0;
  bool finished = false;
  std::vector<BYTE> value;
};

std::mutex& Lock() {
  static std::mutex* m = new std::mutex;
  return *m;
}

Provider* ProvOf(HCRYPTPROV h) {
  Provider* p = (Provider*)h;
  return p && p->magic == 0x76727043 ? p : NULL;
}
Key* KeyOf(HCRYPTKEY h) {
  Key* k = (Key*)h;
  return k && k->magic == 0x79654b43 ? k : NULL;
}
Hash* HashOf(HCRYPTHASH h) {
  Hash* x = (Hash*)h;
  return x && x->magic == 0x73614843 ? x : NULL;
}

BOOL Fail(HRESULT e) {
  SetLastError((DWORD)e);
  return FALSE;
}
BOOL FailW(DWORD e) {
  SetLastError(e);
  return FALSE;
}

// Copies a result out, with CryptoAPI's rules for the size.
BOOL Out(const void* data, DWORD n, BYTE* out, DWORD* len) {
  if (!len) return FailW(ERROR_INVALID_PARAMETER);
  if (!out) {
    *len = n;
    return TRUE;
  }
  if (*len < n) {
    *len = n;
    return FailW(ERROR_MORE_DATA);
  }
  memcpy(out, data, n);
  *len = n;
  return TRUE;
}

bool RandomBytes(BYTE* p, size_t n) {
  int fd = open("/dev/urandom", O_RDONLY | O_CLOEXEC);
  if (fd < 0) return false;
  size_t got = 0;
  while (got < n) {
    ssize_t r = read(fd, p + got, n - got);
    if (r <= 0) break;
    got += (size_t)r;
  }
  close(fd);
  return got == n;
}

bool IsCipher(ALG_ID alg) {
  return alg == CALG_RC4 || alg == CALG_AES_128 || alg == CALG_AES_192 || alg == CALG_AES_256;
}
bool IsBlock(ALG_ID alg) { return GET_ALG_TYPE(alg) == ALG_TYPE_BLOCK; }

// The length in bits a key gets: the flags' upper 16 bits, or the
// provider's default.
bool KeyBits(Provider* p, ALG_ID alg, DWORD flags, DWORD* bits) {
  DWORD want = flags >> 16;
  switch (alg) {
    case CALG_RC4:
      if (!want) want = p->enhanced ? 128 : 40;
      if (want < 40 || want > 128 || want % 8) return false;
      break;
    case CALG_AES_128: want = want ? want : 128; return (*bits = want) == 128;
    case CALG_AES_192: want = want ? want : 192; return (*bits = want) == 192;
    case CALG_AES_256: want = want ? want : 256; return (*bits = want) == 256;
    default: return false;
  }
  *bits = want;
  return true;
}

Key* NewKey(Provider* p, ALG_ID alg, DWORD flags, const BYTE* material, DWORD bytes) {
  Key* k = new Key;
  k->prov = p;
  k->alg = alg;
  k->flags = flags;
  k->material.assign(material, material + bytes);
  // The base provider pads a 40-bit RC4 key with 11 bytes of zero salt.
  if (alg == CALG_RC4 && bytes < 16 && !p->enhanced && !(flags & CRYPT_NO_SALT))
    k->salt.assign(16 - bytes, 0);
  if (!(flags & CRYPT_EXPORTABLE)) k->permissions &= ~(DWORD)CRYPT_EXPORT;
  p->refs++;
  return k;
}

// Puts the key's running state back to the start.
void Restart(Key* k) {
  std::vector<BYTE> full = k->material;
  full.insert(full.end(), k->salt.begin(), k->salt.end());
  if (k->alg == CALG_RC4) k->rc4.Init(full.data(), full.size());
  else k->aes.Init(k->material.data(), k->material.size());
  memcpy(k->chain, k->iv, 16);
  k->pending.clear();
  k->fresh = false;
}

void ReleaseProv(Provider* p) {
  if (--p->refs == 0) {
    p->magic = 0;
    delete p;
  }
}

// The hash's value, finishing it the first time.
const std::vector<BYTE>& HashValue(Hash* h) {
  if (!h->finished) {
    h->value.resize(h->d->Size());
    h->d->Final(h->value.data());
    if (h->hmac) {
      std::unique_ptr<Digest> outer(NewDigest(h->hmacAlg));
      std::vector<BYTE> k = h->hmacKey;
      k.resize(outer->Block(), 0);
      for (BYTE& b : k) b ^= 0x5c;
      outer->Update(k.data(), k.size());
      outer->Update(h->value.data(), h->value.size());
      h->value.resize(outer->Size());
      outer->Final(h->value.data());
    }
    h->finished = true;
  }
  return h->value;
}

std::string Narrow(LPCWSTR s) {
  std::string out;
  if (!s) return out;
  int n = WideCharToMultiByte(CP_UTF8, 0, s, -1, NULL, 0, NULL, NULL);
  if (n > 0) {
    out.resize((size_t)n);
    WideCharToMultiByte(CP_UTF8, 0, s, -1, &out[0], n, NULL, NULL);
    out.resize((size_t)n - 1);
  }
  return out;
}

}  // namespace

extern "C" {

BOOL WINAPI CryptAcquireContextA(HCRYPTPROV* phProv, LPCSTR container, LPCSTR provider, DWORD type, DWORD flags) {
  if (!phProv) return FailW(ERROR_INVALID_PARAMETER);
  *phProv = 0;
  if (type != PROV_RSA_FULL && type != PROV_RSA_AES && type != PROV_RSA_SCHANNEL && type != PROV_RSA_SIG)
    return Fail(NTE_BAD_PROV_TYPE);
  std::string name = provider ? provider : (type == PROV_RSA_AES ? MS_ENH_RSA_AES_PROV_A : MS_DEF_PROV_A);
  bool enhanced;
  if (name == MS_DEF_PROV_A) enhanced = false;
  else if (name == MS_ENHANCED_PROV_A || name == MS_STRONG_PROV_A || name == MS_ENH_RSA_AES_PROV_A) enhanced = true;
  else return Fail(NTE_KEYSET_NOT_DEF);
  // Key containers hold no keys here, so every one is there to open,
  // make or delete.
  if (flags & CRYPT_DELETEKEYSET) return TRUE;
  Provider* p = new Provider;
  p->type = type;
  p->name = name;
  p->enhanced = enhanced;
  if (container) p->container = container;
  else if (!(flags & CRYPT_VERIFYCONTEXT)) {
    char user[256];
    DWORD n = sizeof user;
    if (GetUserNameA(user, &n)) p->container = user;
  }
  *phProv = (HCRYPTPROV)p;
  return TRUE;
}

BOOL WINAPI CryptAcquireContextW(HCRYPTPROV* phProv, LPCWSTR container, LPCWSTR provider, DWORD type, DWORD flags) {
  std::string c = Narrow(container), p = Narrow(provider);
  return CryptAcquireContextA(phProv, container ? c.c_str() : NULL, provider ? p.c_str() : NULL, type, flags);
}

BOOL WINAPI CryptReleaseContext(HCRYPTPROV hProv, DWORD flags) {
  std::lock_guard<std::mutex> g(Lock());
  Provider* p = ProvOf(hProv);
  if (!p) return Fail(NTE_BAD_UID);
  if (flags) return Fail(NTE_BAD_FLAGS);
  ReleaseProv(p);
  return TRUE;
}

BOOL WINAPI CryptContextAddRef(HCRYPTPROV hProv, DWORD* reserved, DWORD flags) {
  std::lock_guard<std::mutex> g(Lock());
  Provider* p = ProvOf(hProv);
  if (!p) return Fail(NTE_BAD_UID);
  if (reserved || flags) return FailW(ERROR_INVALID_PARAMETER);
  p->refs++;
  return TRUE;
}

BOOL WINAPI CryptGenRandom(HCRYPTPROV hProv, DWORD n, BYTE* buf) {
  if (!ProvOf(hProv)) return Fail(NTE_BAD_UID);
  if (n && !buf) return FailW(ERROR_INVALID_PARAMETER);
  return RandomBytes(buf, n) ? TRUE : Fail(NTE_FAIL);
}

BOOL WINAPI CryptGetProvParam(HCRYPTPROV hProv, DWORD param, BYTE* data, DWORD* len, DWORD flags) {
  Provider* p = ProvOf(hProv);
  if (!p) return Fail(NTE_BAD_UID);
  switch (param) {
    case PP_NAME: return Out(p->name.c_str(), (DWORD)p->name.size() + 1, data, len);
    case PP_CONTAINER: return Out(p->container.c_str(), (DWORD)p->container.size() + 1, data, len);
    case PP_IMPTYPE: {
      DWORD v = CRYPT_IMPL_SOFTWARE;
      return Out(&v, sizeof v, data, len);
    }
    case PP_VERSION: {
      DWORD v = 0x200;
      return Out(&v, sizeof v, data, len);
    }
    case PP_PROVTYPE: return Out(&p->type, sizeof p->type, data, len);
    case PP_ENUMALGS: {
      static const PROV_ENUMALGS kAlgs[] = {
          {CALG_RC4, 128, 4, "RC4"},      {CALG_AES_128, 128, 8, "AES 128"}, {CALG_AES_192, 192, 8, "AES 192"},
          {CALG_AES_256, 256, 8, "AES 256"}, {CALG_MD4, 128, 4, "MD4"},  {CALG_MD5, 128, 4, "MD5"},
          {CALG_SHA1, 160, 6, "SHA-1"},   {CALG_SHA_256, 256, 8, "SHA-256"}, {CALG_SHA_384, 384, 8, "SHA-384"},
          {CALG_SHA_512, 512, 8, "SHA-512"}, {CALG_SSL3_SHAMD5, 288, 12, "SSL3 SHAMD5"}, {CALG_HMAC, 0, 5, "HMAC"}};
      static thread_local size_t next = 0;
      if (flags & CRYPT_FIRST) next = 0;
      if (next >= sizeof kAlgs / sizeof kAlgs[0]) return FailW(ERROR_NO_MORE_ITEMS);
      if (data && *len >= sizeof(PROV_ENUMALGS)) return Out(&kAlgs[next++], sizeof(PROV_ENUMALGS), data, len);
      return Out(&kAlgs[next], sizeof(PROV_ENUMALGS), data, len);
    }
    default:
      return Fail(NTE_BAD_TYPE);
  }
}

BOOL WINAPI CryptSetProvParam(HCRYPTPROV hProv, DWORD param, const BYTE*, DWORD) {
  if (!ProvOf(hProv)) return Fail(NTE_BAD_UID);
  if (param == PP_KEYSET_SEC_DESCR) return TRUE;
  return Fail(NTE_BAD_TYPE);
}

BOOL WINAPI CryptCreateHash(HCRYPTPROV hProv, ALG_ID alg, HCRYPTKEY hKey, DWORD flags, HCRYPTHASH* phHash) {
  std::lock_guard<std::mutex> g(Lock());
  Provider* p = ProvOf(hProv);
  if (!p) return Fail(NTE_BAD_UID);
  if (!phHash) return FailW(ERROR_INVALID_PARAMETER);
  if (flags) return Fail(NTE_BAD_FLAGS);
  std::unique_ptr<Hash> h(new Hash);
  h->prov = p;
  h->alg = alg;
  if (alg == CALG_HMAC) {
    Key* k = KeyOf(hKey);
    if (!k) return Fail(NTE_BAD_KEY);
    h->hmac = true;
    h->hmacKey = k->material;
  } else {
    h->d.reset(NewDigest(alg));
    if (!h->d) return Fail(NTE_BAD_ALGID);
  }
  p->refs++;
  *phHash = (HCRYPTHASH)h.release();
  return TRUE;
}

BOOL WINAPI CryptHashData(HCRYPTHASH hHash, const BYTE* data, DWORD n, DWORD) {
  Hash* h = HashOf(hHash);
  if (!h) return Fail(NTE_BAD_HASH);
  if (h->finished) return Fail(NTE_BAD_HASH_STATE);
  if (!h->d) return Fail(NTE_BAD_HASH_STATE);  // HMAC before HP_HMAC_INFO
  if (n && !data) return FailW(ERROR_INVALID_PARAMETER);
  h->d->Update(data, n);
  return TRUE;
}

BOOL WINAPI CryptHashSessionKey(HCRYPTHASH hHash, HCRYPTKEY hKey, DWORD) {
  Key* k = KeyOf(hKey);
  if (!k) return Fail(NTE_BAD_KEY);
  return CryptHashData(hHash, k->material.data(), (DWORD)k->material.size(), 0);
}

BOOL WINAPI CryptGetHashParam(HCRYPTHASH hHash, DWORD param, BYTE* data, DWORD* len, DWORD) {
  Hash* h = HashOf(hHash);
  if (!h) return Fail(NTE_BAD_HASH);
  switch (param) {
    case HP_ALGID: return Out(&h->alg, sizeof h->alg, data, len);
    case HP_HASHSIZE: {
      if (!h->d) return Fail(NTE_BAD_HASH_STATE);
      DWORD n;
      if (h->hmac) {
        std::unique_ptr<Digest> d(NewDigest(h->hmacAlg));
        n = (DWORD)d->Size();
      } else {
        n = (DWORD)h->d->Size();
      }
      return Out(&n, sizeof n, data, len);
    }
    case HP_HASHVAL: {
      if (!h->d) return Fail(NTE_BAD_HASH_STATE);
      // Asking for the size leaves the hash open.
      if (!data && !h->finished) {
        std::unique_ptr<Digest> d(NewDigest(h->hmac ? h->hmacAlg : h->alg));
        if (!len) return FailW(ERROR_INVALID_PARAMETER);
        *len = (DWORD)d->Size();
        return TRUE;
      }
      const std::vector<BYTE>& v = HashValue(h);
      return Out(v.data(), (DWORD)v.size(), data, len);
    }
    default:
      return Fail(NTE_BAD_TYPE);
  }
}

BOOL WINAPI CryptSetHashParam(HCRYPTHASH hHash, DWORD param, const BYTE* data, DWORD) {
  Hash* h = HashOf(hHash);
  if (!h) return Fail(NTE_BAD_HASH);
  if (!data) return FailW(ERROR_INVALID_PARAMETER);
  if (param == HP_HMAC_INFO) {
    if (!h->hmac || h->d) return Fail(NTE_BAD_HASH_STATE);
    const HMAC_INFO* info = (const HMAC_INFO*)data;
    std::unique_ptr<Digest> inner(NewDigest(info->HashAlgid));
    if (!inner || info->HashAlgid == CALG_SSL3_SHAMD5) return Fail(NTE_BAD_ALGID);
    if (h->hmacKey.size() > inner->Block()) {
      std::unique_ptr<Digest> kd(NewDigest(info->HashAlgid));
      kd->Update(h->hmacKey.data(), h->hmacKey.size());
      h->hmacKey.resize(kd->Size());
      kd->Final(h->hmacKey.data());
    }
    std::vector<BYTE> k = h->hmacKey;
    k.resize(inner->Block(), 0);
    for (BYTE& b : k) b ^= 0x36;
    inner->Update(k.data(), k.size());
    h->hmacAlg = info->HashAlgid;
    h->d = std::move(inner);
    return TRUE;
  }
  if (param == HP_HASHVAL) {
    if (!h->d) return Fail(NTE_BAD_HASH_STATE);
    std::unique_ptr<Digest> d(NewDigest(h->hmac ? h->hmacAlg : h->alg));
    h->value.assign(data, data + d->Size());
    h->finished = true;
    return TRUE;
  }
  return Fail(NTE_BAD_TYPE);
}

BOOL WINAPI CryptDuplicateHash(HCRYPTHASH hHash, DWORD* reserved, DWORD flags, HCRYPTHASH* phHash) {
  std::lock_guard<std::mutex> g(Lock());
  Hash* h = HashOf(hHash);
  if (!h) return Fail(NTE_BAD_HASH);
  if (reserved || flags || !phHash) return FailW(ERROR_INVALID_PARAMETER);
  Hash* c = new Hash;
  c->prov = h->prov;
  c->alg = h->alg;
  if (h->d) c->d.reset(h->d->Clone());
  c->hmac = h->hmac;
  c->hmacKey = h->hmacKey;
  c->hmacAlg = h->hmacAlg;
  c->finished = h->finished;
  c->value = h->value;
  h->prov->refs++;
  *phHash = (HCRYPTHASH)c;
  return TRUE;
}

BOOL WINAPI CryptDestroyHash(HCRYPTHASH hHash) {
  std::lock_guard<std::mutex> g(Lock());
  Hash* h = HashOf(hHash);
  if (!h) return Fail(NTE_BAD_HASH);
  h->magic = 0;
  ReleaseProv(h->prov);
  delete h;
  return TRUE;
}

BOOL WINAPI CryptDeriveKey(HCRYPTPROV hProv, ALG_ID alg, HCRYPTHASH hHash, DWORD flags, HCRYPTKEY* phKey) {
  Provider* p = ProvOf(hProv);
  if (!p) return Fail(NTE_BAD_UID);
  Hash* h = HashOf(hHash);
  if (!h || !h->d) return Fail(NTE_BAD_HASH);
  if (!phKey) return FailW(ERROR_INVALID_PARAMETER);
  if (!IsCipher(alg)) return Fail(NTE_BAD_ALGID);
  DWORD bits;
  if (!KeyBits(p, alg, flags, &bits)) return Fail(NTE_BAD_FLAGS);
  std::vector<BYTE> value = HashValue(h);
  std::vector<BYTE> material;
  if (bits / 8 <= value.size()) {
    material.assign(value.begin(), value.begin() + bits / 8);
  } else {
    // Longer than the hash: CryptoAPI's two hashes of the value padded
    // with 0x36 and 0x5c.
    ALG_ID halg = h->hmac ? h->hmacAlg : h->alg;
    for (BYTE fill : {(BYTE)0x36, (BYTE)0x5c}) {
      std::vector<BYTE> buf(64, fill);
      for (size_t i = 0; i < value.size() && i < 64; i++) buf[i] ^= value[i];
      std::unique_ptr<Digest> d(NewDigest(halg));
      d->Update(buf.data(), buf.size());
      std::vector<BYTE> out(d->Size());
      d->Final(out.data());
      material.insert(material.end(), out.begin(), out.end());
    }
    if (material.size() < bits / 8) return Fail(NTE_BAD_LEN);
    material.resize(bits / 8);
  }
  std::lock_guard<std::mutex> g(Lock());
  *phKey = (HCRYPTKEY)NewKey(p, alg, flags, material.data(), (DWORD)material.size());
  return TRUE;
}

BOOL WINAPI CryptGenKey(HCRYPTPROV hProv, ALG_ID alg, DWORD flags, HCRYPTKEY* phKey) {
  Provider* p = ProvOf(hProv);
  if (!p) return Fail(NTE_BAD_UID);
  if (!phKey) return FailW(ERROR_INVALID_PARAMETER);
  if (!IsCipher(alg)) return Fail(NTE_BAD_ALGID);
  DWORD bits;
  if (!KeyBits(p, alg, flags, &bits)) return Fail(NTE_BAD_FLAGS);
  std::vector<BYTE> material(bits / 8);
  if (!RandomBytes(material.data(), material.size())) return Fail(NTE_FAIL);
  std::lock_guard<std::mutex> g(Lock());
  Key* k = NewKey(p, alg, flags, material.data(), (DWORD)material.size());
  if ((flags & CRYPT_CREATE_SALT) && !k->salt.empty()) RandomBytes(k->salt.data(), k->salt.size());
  *phKey = (HCRYPTKEY)k;
  return TRUE;
}

BOOL WINAPI CryptDuplicateKey(HCRYPTKEY hKey, DWORD* reserved, DWORD flags, HCRYPTKEY* phKey) {
  std::lock_guard<std::mutex> g(Lock());
  Key* k = KeyOf(hKey);
  if (!k) return Fail(NTE_BAD_KEY);
  if (reserved || flags || !phKey) return FailW(ERROR_INVALID_PARAMETER);
  Key* c = new Key(*k);
  k->prov->refs++;
  *phKey = (HCRYPTKEY)c;
  return TRUE;
}

BOOL WINAPI CryptDestroyKey(HCRYPTKEY hKey) {
  std::lock_guard<std::mutex> g(Lock());
  Key* k = KeyOf(hKey);
  if (!k) return Fail(NTE_BAD_KEY);
  k->magic = 0;
  std::fill(k->material.begin(), k->material.end(), 0);
  ReleaseProv(k->prov);
  delete k;
  return TRUE;
}

BOOL WINAPI CryptGetKeyParam(HCRYPTKEY hKey, DWORD param, BYTE* data, DWORD* len, DWORD) {
  Key* k = KeyOf(hKey);
  if (!k) return Fail(NTE_BAD_KEY);
  DWORD v;
  switch (param) {
    case KP_ALGID: return Out(&k->alg, sizeof k->alg, data, len);
    case KP_KEYLEN:
    case KP_EFFECTIVE_KEYLEN: v = (DWORD)k->material.size() * 8; return Out(&v, sizeof v, data, len);
    case KP_BLOCKLEN: v = IsBlock(k->alg) ? 128 : 0; return Out(&v, sizeof v, data, len);
    case KP_IV:
      if (!IsBlock(k->alg)) return Fail(NTE_BAD_TYPE);
      return Out(k->iv, 16, data, len);
    case KP_SALT: return Out(k->salt.data(), (DWORD)k->salt.size(), data, len);
    case KP_PERMISSIONS: return Out(&k->permissions, sizeof k->permissions, data, len);
    case KP_MODE:
      if (!IsBlock(k->alg)) return Fail(NTE_BAD_TYPE);
      return Out(&k->mode, sizeof k->mode, data, len);
    case KP_MODE_BITS: v = 8; return Out(&v, sizeof v, data, len);
    case KP_PADDING: return Out(&k->padding, sizeof k->padding, data, len);
    default: return Fail(NTE_BAD_TYPE);
  }
}

BOOL WINAPI CryptSetKeyParam(HCRYPTKEY hKey, DWORD param, const BYTE* data, DWORD) {
  Key* k = KeyOf(hKey);
  if (!k) return Fail(NTE_BAD_KEY);
  if (!data) return FailW(ERROR_INVALID_PARAMETER);
  switch (param) {
    case KP_IV:
      if (!IsBlock(k->alg)) return Fail(NTE_BAD_TYPE);
      memcpy(k->iv, data, 16);
      k->fresh = true;
      return TRUE;
    case KP_SALT:
      if (k->salt.empty()) return Fail(NTE_BAD_KEY);
      memcpy(k->salt.data(), data, k->salt.size());
      k->fresh = true;
      return TRUE;
    case KP_SALT_EX: {
      const CRYPT_DATA_BLOB* b = (const CRYPT_DATA_BLOB*)data;
      k->salt.assign(b->pbData, b->pbData + b->cbData);
      k->fresh = true;
      return TRUE;
    }
    case KP_MODE: {
      DWORD m = *(const DWORD*)data;
      if (!IsBlock(k->alg) || (m != CRYPT_MODE_CBC && m != CRYPT_MODE_ECB)) return Fail(NTE_BAD_DATA);
      k->mode = m;
      k->fresh = true;
      return TRUE;
    }
    case KP_PADDING:
      if (*(const DWORD*)data != PKCS5_PADDING) return Fail(NTE_BAD_DATA);
      return TRUE;
    case KP_PERMISSIONS:
      k->permissions &= *(const DWORD*)data;
      return TRUE;
    default:
      return Fail(NTE_BAD_TYPE);
  }
}

BOOL WINAPI CryptEncrypt(HCRYPTKEY hKey, HCRYPTHASH hHash, BOOL final, DWORD, BYTE* data, DWORD* len, DWORD bufLen) {
  std::lock_guard<std::mutex> g(Lock());
  Key* k = KeyOf(hKey);
  if (!k) return Fail(NTE_BAD_KEY);
  if (!IsCipher(k->alg)) return Fail(NTE_BAD_ALGID);
  if (!len) return FailW(ERROR_INVALID_PARAMETER);
  Hash* h = hHash ? HashOf(hHash) : NULL;
  if (hHash && !h) return Fail(NTE_BAD_HASH);
  DWORD n = *len;
  DWORD need = n;
  if (IsBlock(k->alg)) {
    if (final) need = (n / 16 + 1) * 16;
    else if (n % 16) return Fail(NTE_BAD_DATA);
  }
  if (!data) {
    *len = need;
    return TRUE;
  }
  if (need > bufLen) {
    *len = need;
    return FailW(ERROR_MORE_DATA);
  }
  if (h && h->d) h->d->Update(data, n);
  if (k->fresh) Restart(k);
  if (k->alg == CALG_RC4) {
    k->rc4.Run(data, n);
  } else {
    if (final) {
      BYTE pad = (BYTE)(need - n);
      memset(data + n, pad, pad);
    }
    for (DWORD off = 0; off < need; off += 16) {
      BYTE* b = data + off;
      if (k->mode == CRYPT_MODE_CBC)
        for (int i = 0; i < 16; i++) b[i] ^= k->chain[i];
      k->aes.Encrypt(b);
      memcpy(k->chain, b, 16);
    }
  }
  *len = need;
  if (final) k->fresh = true;
  return TRUE;
}

BOOL WINAPI CryptDecrypt(HCRYPTKEY hKey, HCRYPTHASH hHash, BOOL final, DWORD, BYTE* data, DWORD* len) {
  std::lock_guard<std::mutex> g(Lock());
  Key* k = KeyOf(hKey);
  if (!k) return Fail(NTE_BAD_KEY);
  if (!IsCipher(k->alg)) return Fail(NTE_BAD_ALGID);
  if (!len || (*len && !data)) return FailW(ERROR_INVALID_PARAMETER);
  Hash* h = hHash ? HashOf(hHash) : NULL;
  if (hHash && !h) return Fail(NTE_BAD_HASH);
  DWORD n = *len;
  if (k->fresh) Restart(k);
  if (k->alg == CALG_RC4) {
    k->rc4.Run(data, n);
  } else {
    if (n % 16) return Fail(NTE_BAD_DATA);
    for (DWORD off = 0; off < n; off += 16) {
      BYTE* b = data + off;
      BYTE c[16];
      memcpy(c, b, 16);
      k->aes.Decrypt(b);
      if (k->mode == CRYPT_MODE_CBC)
        for (int i = 0; i < 16; i++) b[i] ^= k->chain[i];
      memcpy(k->chain, c, 16);
    }
    if (final) {
      BYTE pad = n ? data[n - 1] : 0;
      if (pad < 1 || pad > 16 || pad > n) {
        k->fresh = true;
        return Fail(NTE_BAD_DATA);
      }
      for (DWORD i = n - pad; i < n; i++)
        if (data[i] != pad) {
          k->fresh = true;
          return Fail(NTE_BAD_DATA);
        }
      n -= pad;
    }
  }
  if (h && h->d) h->d->Update(data, n);
  *len = n;
  if (final) k->fresh = true;
  return TRUE;
}

BOOL WINAPI CryptExportKey(HCRYPTKEY hKey, HCRYPTKEY hExpKey, DWORD type, DWORD, BYTE* data, DWORD* len) {
  Key* k = KeyOf(hKey);
  if (!k) return Fail(NTE_BAD_KEY);
  if (!len) return FailW(ERROR_INVALID_PARAMETER);
  if (type != PLAINTEXTKEYBLOB || hExpKey) return Fail(NTE_BAD_TYPE);
  if (!(k->permissions & CRYPT_EXPORT)) return Fail(NTE_BAD_KEY_STATE);
  std::vector<BYTE> blob(sizeof(BLOBHEADER) + sizeof(DWORD) + k->material.size());
  BLOBHEADER hdr = {(BYTE)PLAINTEXTKEYBLOB, CUR_BLOB_VERSION, 0, k->alg};
  memcpy(blob.data(), &hdr, sizeof hdr);
  DWORD n = (DWORD)k->material.size();
  memcpy(blob.data() + sizeof hdr, &n, sizeof n);
  memcpy(blob.data() + sizeof hdr + sizeof n, k->material.data(), n);
  return Out(blob.data(), (DWORD)blob.size(), data, len);
}

BOOL WINAPI CryptImportKey(HCRYPTPROV hProv, const BYTE* data, DWORD len, HCRYPTKEY hPubKey, DWORD flags,
                           HCRYPTKEY* phKey) {
  Provider* p = ProvOf(hProv);
  if (!p) return Fail(NTE_BAD_UID);
  if (!data || !phKey) return FailW(ERROR_INVALID_PARAMETER);
  if (len < sizeof(BLOBHEADER) + sizeof(DWORD)) return Fail(NTE_BAD_DATA);
  BLOBHEADER hdr;
  memcpy(&hdr, data, sizeof hdr);
  if (hdr.bType != PLAINTEXTKEYBLOB || hPubKey) return Fail(NTE_BAD_TYPE);
  DWORD n;
  memcpy(&n, data + sizeof hdr, sizeof n);
  if (len < sizeof hdr + sizeof n + n) return Fail(NTE_BAD_DATA);
  // An HMAC key may be any length, and of any algorithm (RC2, usually):
  // it is only ever hashed.
  if (!(flags & CRYPT_IPSEC_HMAC_KEY)) {
    if (!IsCipher(hdr.aiKeyAlg)) return Fail(NTE_BAD_ALGID);
    DWORD bits;
    if (!KeyBits(p, hdr.aiKeyAlg, n * 8 << 16, &bits)) return Fail(NTE_BAD_DATA);
  }
  std::lock_guard<std::mutex> g(Lock());
  *phKey = (HCRYPTKEY)NewKey(p, hdr.aiKeyAlg, flags | CRYPT_NO_SALT, data + sizeof hdr + sizeof n, n);
  return TRUE;
}

BOOL WINAPI CryptGetUserKey(HCRYPTPROV hProv, DWORD, HCRYPTKEY* phKey) {
  if (!ProvOf(hProv)) return Fail(NTE_BAD_UID);
  if (phKey) *phKey = 0;
  return Fail(NTE_NO_KEY);
}

BOOL WINAPI CryptSignHashA(HCRYPTHASH, DWORD, LPCSTR, DWORD, BYTE*, DWORD*) { return Fail(NTE_NO_KEY); }
BOOL WINAPI CryptSignHashW(HCRYPTHASH, DWORD, LPCWSTR, DWORD, BYTE*, DWORD*) { return Fail(NTE_NO_KEY); }
BOOL WINAPI CryptVerifySignatureA(HCRYPTHASH, const BYTE*, DWORD, HCRYPTKEY, LPCSTR, DWORD) {
  return Fail(NTE_BAD_KEY);
}
BOOL WINAPI CryptVerifySignatureW(HCRYPTHASH, const BYTE*, DWORD, HCRYPTKEY, LPCWSTR, DWORD) {
  return Fail(NTE_BAD_KEY);
}

}  // extern "C"
