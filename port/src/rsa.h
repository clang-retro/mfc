// rsa.h - unsigned big integers and the RSA operations CryptoAPI needs:
// key generation, the public and private (CRT) transforms.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#ifndef _WINPORT_RSA_H
#define _WINPORT_RSA_H

#include <stddef.h>
#include <stdint.h>

#include <vector>

namespace winport {
namespace rsa {

// An unsigned integer, 32-bit limbs from the least significant, with no
// zero limbs at the top (zero is no limbs).
struct Big {
  std::vector<uint32_t> w;

  Big() {}
  explicit Big(uint32_t v) {
    if (v) w.push_back(v);
  }
  bool Zero() const { return w.empty(); }
  bool Odd() const { return !w.empty() && (w[0] & 1); }
  size_t Bits() const;
  bool Bit(size_t i) const { return i / 32 < w.size() && (w[i / 32] >> (i % 32)) & 1; }
  void Trim() {
    while (!w.empty() && !w.back()) w.pop_back();
  }

  // Bytes least significant first, as CryptoAPI keeps them; ToLe pads
  // or cuts to n bytes.
  static Big FromLe(const uint8_t* p, size_t n);
  static Big FromBe(const uint8_t* p, size_t n);
  std::vector<uint8_t> ToLe(size_t n) const;
  std::vector<uint8_t> ToBe(size_t n) const;
};

int Cmp(const Big& a, const Big& b);
Big Add(const Big& a, const Big& b);
Big Sub(const Big& a, const Big& b);  // a >= b
Big Mul(const Big& a, const Big& b);
Big Mod(const Big& a, const Big& m);
uint32_t ModSmall(const Big& a, uint32_t m);
Big DivSmall(const Big& a, uint32_t d);  // exact quotient, remainder dropped
// a^e mod m, for an odd m.
Big ModExp(const Big& a, const Big& e, const Big& m);

typedef bool (*RandomFn)(uint8_t*, size_t);

struct Key {
  unsigned bits = 0;
  uint32_t e = 0;
  Big n;
  bool hasPrivate = false;
  Big d, p, q, dp, dq, qinv;  // with hasPrivate
};

// A new key of exactly the given number of bits (an even number, 384 or
// more).
bool Generate(unsigned bits, uint32_t e, RandomFn random, Key* out);
// Fills in d, dp, dq and qinv from n, e, p and q; false when they do not
// make a key.
bool Complete(Key* k);
// The transforms, on numbers below n.
Big Public(const Key& k, const Big& m);
Big Private(const Key& k, const Big& c);

}  // namespace rsa
}  // namespace winport

#endif
