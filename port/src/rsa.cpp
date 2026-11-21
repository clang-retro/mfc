// rsa.cpp - unsigned big integers and RSA: Montgomery exponentiation,
// primes by sieve and Miller-Rabin, and the transforms with the private
// key through the Chinese remainder theorem.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#include "rsa.h"

#include <string.h>

#include <algorithm>
#include <utility>

namespace winport {
namespace rsa {

size_t Big::Bits() const {
  if (w.empty()) return 0;
  size_t b = (w.size() - 1) * 32;
  for (uint32_t top = w.back(); top; top >>= 1) b++;
  return b;
}

Big Big::FromLe(const uint8_t* p, size_t n) {
  Big r;
  r.w.assign((n + 3) / 4, 0);
  for (size_t i = 0; i < n; i++) r.w[i / 4] |= (uint32_t)p[i] << (8 * (i % 4));
  r.Trim();
  return r;
}

Big Big::FromBe(const uint8_t* p, size_t n) {
  std::vector<uint8_t> le(p, p + n);
  std::reverse(le.begin(), le.end());
  return FromLe(le.data(), n);
}

std::vector<uint8_t> Big::ToLe(size_t n) const {
  std::vector<uint8_t> out(n, 0);
  for (size_t i = 0; i < n && i / 4 < w.size(); i++) out[i] = (uint8_t)(w[i / 4] >> (8 * (i % 4)));
  return out;
}

std::vector<uint8_t> Big::ToBe(size_t n) const {
  std::vector<uint8_t> out = ToLe(n);
  std::reverse(out.begin(), out.end());
  return out;
}

int Cmp(const Big& a, const Big& b) {
  if (a.w.size() != b.w.size()) return a.w.size() < b.w.size() ? -1 : 1;
  for (size_t i = a.w.size(); i-- > 0;)
    if (a.w[i] != b.w[i]) return a.w[i] < b.w[i] ? -1 : 1;
  return 0;
}

Big Add(const Big& a, const Big& b) {
  const Big& x = a.w.size() >= b.w.size() ? a : b;
  const Big& y = a.w.size() >= b.w.size() ? b : a;
  Big r;
  r.w.resize(x.w.size() + 1);
  uint64_t c = 0;
  for (size_t i = 0; i < x.w.size(); i++) {
    c += (uint64_t)x.w[i] + (i < y.w.size() ? y.w[i] : 0);
    r.w[i] = (uint32_t)c;
    c >>= 32;
  }
  r.w[x.w.size()] = (uint32_t)c;
  r.Trim();
  return r;
}

Big Sub(const Big& a, const Big& b) {
  Big r;
  r.w.resize(a.w.size());
  int64_t c = 0;
  for (size_t i = 0; i < a.w.size(); i++) {
    c += (int64_t)a.w[i] - (i < b.w.size() ? b.w[i] : 0);
    r.w[i] = (uint32_t)c;
    c >>= 32;
  }
  r.Trim();
  return r;
}

Big Mul(const Big& a, const Big& b) {
  Big r;
  if (a.Zero() || b.Zero()) return r;
  r.w.assign(a.w.size() + b.w.size(), 0);
  for (size_t i = 0; i < a.w.size(); i++) {
    uint64_t c = 0;
    for (size_t j = 0; j < b.w.size(); j++) {
      c += (uint64_t)a.w[i] * b.w[j] + r.w[i + j];
      r.w[i + j] = (uint32_t)c;
      c >>= 32;
    }
    r.w[i + b.w.size()] = (uint32_t)c;
  }
  r.Trim();
  return r;
}

// Bit by bit: slow, but only for setting up, never in a loop of
// exponentiation.
Big Mod(const Big& a, const Big& m) {
  if (Cmp(a, m) < 0) return a;
  size_t n = m.w.size();
  std::vector<uint32_t> r(n + 1, 0);
  for (size_t i = a.Bits(); i-- > 0;) {
    for (size_t j = n + 1; j-- > 1;) r[j] = (r[j] << 1) | (r[j - 1] >> 31);
    r[0] = (r[0] << 1) | (a.Bit(i) ? 1 : 0);
    // r >= m?
    bool ge = r[n] != 0;
    if (!ge) {
      ge = true;
      for (size_t j = n; j-- > 0;)
        if (r[j] != m.w[j]) {
          ge = r[j] > m.w[j];
          break;
        }
    }
    if (ge) {
      int64_t c = 0;
      for (size_t j = 0; j <= n; j++) {
        c += (int64_t)r[j] - (j < n ? m.w[j] : 0);
        r[j] = (uint32_t)c;
        c >>= 32;
      }
    }
  }
  Big out;
  out.w = r;
  out.Trim();
  return out;
}

uint32_t ModSmall(const Big& a, uint32_t m) {
  uint64_t r = 0;
  for (size_t i = a.w.size(); i-- > 0;) r = ((r << 32) | a.w[i]) % m;
  return (uint32_t)r;
}

Big DivSmall(const Big& a, uint32_t d) {
  Big q;
  q.w.resize(a.w.size());
  uint64_t r = 0;
  for (size_t i = a.w.size(); i-- > 0;) {
    uint64_t cur = (r << 32) | a.w[i];
    q.w[i] = (uint32_t)(cur / d);
    r = cur % d;
  }
  q.Trim();
  return q;
}

namespace {

// Montgomery arithmetic modulo an odd m of n limbs, R = 2^(32n).
struct Mont {
  std::vector<uint32_t> m;
  size_t n;
  uint32_t minv;  // -m^-1 mod 2^32
  std::vector<uint32_t> t;

  explicit Mont(const Big& mod) : m(mod.w), n(mod.w.size()), t(mod.w.size() + 2) {
    uint32_t inv = m[0];  // right to 3 bits; each step doubles that
    for (int i = 0; i < 5; i++) inv *= 2 - m[0] * inv;
    minv = 0u - inv;
  }

  std::vector<uint32_t> Limbs(const Big& a) const {
    std::vector<uint32_t> v(a.w);
    v.resize(n, 0);
    return v;
  }

  // out = a * b / R mod m, for a and b below m.
  void Mul(const uint32_t* a, const uint32_t* b, uint32_t* out) {
    std::fill(t.begin(), t.end(), 0);
    for (size_t i = 0; i < n; i++) {
      uint64_t c = 0;
      for (size_t j = 0; j < n; j++) {
        c += (uint64_t)a[j] * b[i] + t[j];
        t[j] = (uint32_t)c;
        c >>= 32;
      }
      c += t[n];
      t[n] = (uint32_t)c;
      t[n + 1] = (uint32_t)(c >> 32);
      uint32_t q = t[0] * minv;
      c = ((uint64_t)q * m[0] + t[0]) >> 32;
      for (size_t j = 1; j < n; j++) {
        c += (uint64_t)q * m[j] + t[j];
        t[j - 1] = (uint32_t)c;
        c >>= 32;
      }
      c += t[n];
      t[n - 1] = (uint32_t)c;
      t[n] = t[n + 1] + (uint32_t)(c >> 32);
    }
    bool ge = t[n] != 0;
    if (!ge) {
      ge = true;
      for (size_t j = n; j-- > 0;)
        if (t[j] != m[j]) {
          ge = t[j] > m[j];
          break;
        }
    }
    if (ge) {
      int64_t c = 0;
      for (size_t j = 0; j < n; j++) {
        c += (int64_t)t[j] - m[j];
        out[j] = (uint32_t)c;
        c >>= 32;
      }
    } else {
      memcpy(out, t.data(), n * sizeof(uint32_t));
    }
  }
};

}  // namespace

Big ModExp(const Big& a, const Big& e, const Big& m) {
  if (m.Zero()) return Big();
  if (m.w.size() == 1 && m.w[0] == 1) return Big();
  Mont mt(m);
  size_t n = mt.n;
  Big rrBig;
  rrBig.w.assign(2 * n + 1, 0);
  rrBig.w[2 * n] = 1;
  std::vector<uint32_t> rr = mt.Limbs(Mod(rrBig, m));
  std::vector<uint32_t> base = mt.Limbs(Mod(a, m));
  // A table of base^0 .. base^15, in Montgomery form.
  std::vector<std::vector<uint32_t>> table(16, std::vector<uint32_t>(n));
  std::vector<uint32_t> one(n, 0);
  one[0] = 1;
  mt.Mul(one.data(), rr.data(), table[0].data());
  mt.Mul(base.data(), rr.data(), table[1].data());
  for (int i = 2; i < 16; i++) mt.Mul(table[i - 1].data(), table[1].data(), table[i].data());
  std::vector<uint32_t> x = table[0];
  size_t bits = e.Bits();
  size_t windows = (bits + 3) / 4;
  for (size_t wi = windows; wi-- > 0;) {
    if (wi + 1 != windows)
      for (int s = 0; s < 4; s++) mt.Mul(x.data(), x.data(), x.data());
    unsigned d = 0;
    for (int b = 3; b >= 0; b--) d = (d << 1) | (e.Bit(wi * 4 + b) ? 1 : 0);
    if (d) mt.Mul(x.data(), table[d].data(), x.data());
  }
  std::vector<uint32_t> out(n);
  mt.Mul(x.data(), one.data(), out.data());
  Big r;
  r.w = out;
  r.Trim();
  return r;
}

namespace {

const std::vector<uint32_t>& SmallPrimes() {
  static const std::vector<uint32_t>* primes = [] {
    auto* v = new std::vector<uint32_t>;
    for (uint32_t c = 3; c < 4000; c += 2) {
      bool prime = true;
      for (uint32_t p : *v) {
        if (p * p > c) break;
        if (c % p == 0) {
          prime = false;
          break;
        }
      }
      if (prime) v->push_back(c);
    }
    return v;
  }();
  return *primes;
}

Big RandomBelow(const Big& bound, RandomFn random) {
  size_t bytes = (bound.Bits() + 7) / 8 + 8;
  std::vector<uint8_t> buf(bytes);
  random(buf.data(), buf.size());
  return Mod(Big::FromLe(buf.data(), buf.size()), bound);
}

bool MillerRabin(const Big& c, int rounds, RandomFn random) {
  Big one(1), cm1 = Sub(c, one);
  size_t s = 0;
  while (!cm1.Bit(s)) s++;
  Big d;
  d.w.assign(cm1.w.size(), 0);
  for (size_t i = 0; i + s < cm1.Bits(); i++)
    if (cm1.Bit(i + s)) d.w[i / 32] |= 1u << (i % 32);
  d.Trim();
  Big three(3), two(2);
  for (int r = 0; r < rounds; r++) {
    Big a = r == 0 ? two : Add(RandomBelow(Sub(c, three), random), two);
    Big x = ModExp(a, d, c);
    if (Cmp(x, one) == 0 || Cmp(x, cm1) == 0) continue;
    bool witness = true;
    for (size_t i = 1; i < s && witness; i++) {
      x = ModExp(x, two, c);
      if (Cmp(x, cm1) == 0) witness = false;
      else if (Cmp(x, one) == 0) break;
    }
    if (witness) return false;
  }
  return true;
}

// A prime of exactly the given bits, its top two bits set so that two of
// them make a product of the sum of their lengths, and p - 1 prime to e.
Big Prime(unsigned bits, uint32_t e, RandomFn random) {
  const std::vector<uint32_t>& small = SmallPrimes();
  std::vector<uint32_t> res(small.size());
  int rounds = bits >= 512 ? 8 : 16;
  for (;;) {
    std::vector<uint8_t> buf((bits + 7) / 8);
    if (!random(buf.data(), buf.size())) return Big();
    Big c = Big::FromLe(buf.data(), buf.size());
    c.w.resize((bits + 31) / 32, 0);
    size_t top = bits % 32 ? bits % 32 : 32;
    if (top < 32) c.w.back() &= (1u << top) - 1;
    c.w.back() |= 1u << (top - 1);
    if (top >= 2) c.w.back() |= 1u << (top - 2);
    else c.w[c.w.size() - 2] |= 0x80000000u;
    c.w[0] |= 1;
    for (size_t i = 0; i < small.size(); i++) res[i] = ModSmall(c, small[i]);
    uint32_t re = ModSmall(c, e);
    for (uint32_t delta = 0; delta < (1u << 20); delta += 2) {
      bool ok = (uint32_t)(((uint64_t)re + delta) % e) != 1;
      for (size_t i = 0; ok && i < small.size(); i++)
        if ((res[i] + delta) % small[i] == 0) ok = false;
      if (!ok) continue;
      Big cand = Add(c, Big(delta));
      if (cand.Bits() != bits) break;
      if (MillerRabin(cand, rounds, random)) return cand;
    }
  }
}

// x with x * a = 1 mod m, or 0 when a and m share a factor.
uint32_t InverseSmall(uint32_t a, uint32_t m) {
  int64_t t = 0, nt = 1, r = m, nr = a % m;
  while (nr) {
    int64_t q = r / nr;
    std::swap(t, nt);
    nt -= q * t;
    std::swap(r, nr);
    nr -= q * r;
  }
  if (r != 1) return 0;
  return (uint32_t)(t < 0 ? t + m : t);
}

}  // namespace

bool Complete(Key* k) {
  if (k->e < 3 || !(k->e & 1) || k->p.Zero() || k->q.Zero()) return false;
  Big one(1);
  Big p1 = Sub(k->p, one), q1 = Sub(k->q, one);
  Big phi = Mul(p1, q1);
  uint32_t inv = InverseSmall(ModSmall(phi, k->e), k->e);
  if (!inv) return false;
  // d * e = 1 + f * phi, with f * phi = -1 mod e.
  uint32_t f = k->e - inv;
  k->d = DivSmall(Add(Mul(phi, Big(f)), one), k->e);
  k->n = Mul(k->p, k->q);
  k->bits = (unsigned)k->n.Bits();
  k->dp = Mod(k->d, p1);
  k->dq = Mod(k->d, q1);
  k->qinv = ModExp(k->q, Sub(k->p, Big(2)), k->p);
  k->hasPrivate = true;
  return true;
}

bool Generate(unsigned bits, uint32_t e, RandomFn random, Key* out) {
  if (bits < 64 || bits % 2) return false;
  for (;;) {
    Key k;
    k.e = e;
    k.p = Prime(bits / 2, e, random);
    k.q = Prime(bits / 2, e, random);
    if (k.p.Zero() || k.q.Zero()) return false;
    int c = Cmp(k.p, k.q);
    if (c == 0) continue;
    if (c < 0) std::swap(k.p, k.q);
    if (!Complete(&k) || k.bits != bits) continue;
    *out = k;
    return true;
  }
}

Big Public(const Key& k, const Big& m) { return ModExp(m, Big(k.e), k.n); }

Big Private(const Key& k, const Big& c) {
  Big m1 = ModExp(c, k.dp, k.p);
  Big m2 = ModExp(c, k.dq, k.q);
  Big m2p = Mod(m2, k.p);
  Big diff = Cmp(m1, m2p) >= 0 ? Sub(m1, m2p) : Sub(Add(m1, k.p), m2p);
  Big h = Mod(Mul(k.qinv, diff), k.p);
  return Add(m2, Mul(h, k.q));
}

}  // namespace rsa
}  // namespace winport
