// atlrx.h - ATL's regular expressions: CAtlRegExp parses an expression,
// Match finds its first match in a string and fills a CAtlREMatchContext
// with the whole match and its groups.
//
// The syntax is ATL's: . [] [^] ^ $ | ? * + and the lazy *? +? ??,
// () to group without capturing, {} to capture a match group, ! for
// "what follows must not match here", \ to escape, and the abbreviations
// \a (alphanumeric) \b (blank) \c (letter) \d (digit) \h (hex digit)
// \n (newline) \q (quoted string) \w (word) \z (integer).
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#ifndef __ATLRX_H__
#define __ATLRX_H__

#include <atlbase.h>
#include <vector>

namespace ATL {

enum REParseError {
  REPARSE_ERROR_OK = 0,
  REPARSE_ERROR_OUTOFMEMORY,
  REPARSE_ERROR_BRACE_EXPECTED,
  REPARSE_ERROR_PAREN_EXPECTED,
  REPARSE_ERROR_BRACKET_EXPECTED,
  REPARSE_ERROR_UNEXPECTED,
  REPARSE_ERROR_EMPTY_RANGE,
  REPARSE_ERROR_INVALID_GROUP,
  REPARSE_ERROR_INVALID_RANGE,
  REPARSE_ERROR_EMPTY_REPEATOP,
  REPARSE_ERROR_INVALID_INPUT
};

// The character types. Case folding is ASCII's and the C runtime's.
class CAtlRECharTraitsA {
public:
  typedef char RECHAR;
  static RECHAR ToLower(RECHAR ch) { return (RECHAR)(::tolower)((unsigned char)ch); }
  static RECHAR ToUpper(RECHAR ch) { return (RECHAR)(::toupper)((unsigned char)ch); }
  static size_t Length(const RECHAR* psz) { return ::strlen(psz); }
};

class CAtlRECharTraitsW {
public:
  typedef wchar_t RECHAR;
  static RECHAR ToLower(RECHAR ch) { return (RECHAR)::towlower(ch); }
  static RECHAR ToUpper(RECHAR ch) { return (RECHAR)::towupper(ch); }
  static size_t Length(const RECHAR* psz) { return ::wcslen(psz); }
};

#ifdef _UNICODE
typedef CAtlRECharTraitsW CAtlRECharTraits;
#else
typedef CAtlRECharTraitsA CAtlRECharTraits;
#endif

template <class CharTraits = CAtlRECharTraits>
class CAtlREMatchContext {
public:
  typedef typename CharTraits::RECHAR RECHAR;
  struct MatchGroup {
    const RECHAR* szStart;
    const RECHAR* szEnd;
  };

  UINT m_uNumGroups;
  MatchGroup m_Match;

  CAtlREMatchContext() : m_uNumGroups(0) {
    m_Match.szStart = m_Match.szEnd = NULL;
  }

  void GetMatch(UINT nIndex, const RECHAR** szStart, const RECHAR** szEnd) {
    if (nIndex >= m_uNumGroups) {
      *szStart = *szEnd = NULL;
      return;
    }
    *szStart = m_groups[nIndex].szStart;
    *szEnd = m_groups[nIndex].szEnd;
  }
  void GetMatch(UINT nIndex, MatchGroup* pGroup) { GetMatch(nIndex, &pGroup->szStart, &pGroup->szEnd); }

  std::vector<MatchGroup> m_groups;
};

template <class CharTraits = CAtlRECharTraits>
class CAtlRegExp {
public:
  typedef typename CharTraits::RECHAR RECHAR;

  CAtlRegExp() : m_nGroups(0), m_bCaseSensitive(TRUE) {}

  REParseError Parse(const RECHAR* szRE, BOOL bCaseSensitive = TRUE) {
    m_prog.clear();
    m_classes.clear();
    m_nGroups = 0;
    m_nLoops = 0;
    m_bCaseSensitive = bCaseSensitive;
    if (szRE == NULL) return REPARSE_ERROR_INVALID_INPUT;
    const RECHAR* p = szRE;
    REParseError err = ParseAlt(p, 0);
    if (err == REPARSE_ERROR_OK && *p != 0) err = REPARSE_ERROR_UNEXPECTED;
    if (err != REPARSE_ERROR_OK) {
      m_prog.clear();
      return err;
    }
    Emit(opMatch);
    return REPARSE_ERROR_OK;
  }

  BOOL Match(const RECHAR* szIn, CAtlREMatchContext<CharTraits>* pContext,
             const RECHAR** ppszEnd = NULL) {
    if (szIn == NULL || pContext == NULL || m_prog.empty()) return FALSE;
    m_szInput = szIn;
    for (const RECHAR* s = szIn;; s++) {
      m_saves.assign(m_nGroups * 2, (const RECHAR*)NULL);
      m_loops.assign(m_nLoops, (const RECHAR*)NULL);
      const RECHAR* pEnd = NULL;
      if (Run(0, s, &pEnd)) {
        pContext->m_Match.szStart = s;
        pContext->m_Match.szEnd = pEnd;
        pContext->m_uNumGroups = (UINT)m_nGroups;
        pContext->m_groups.resize(m_nGroups);
        for (int i = 0; i < m_nGroups; i++) {
          pContext->m_groups[i].szStart = m_saves[i * 2];
          pContext->m_groups[i].szEnd = m_saves[i * 2 + 1];
        }
        if (ppszEnd != NULL) *ppszEnd = pEnd;
        return TRUE;
      }
      if (*s == 0) break;
    }
    return FALSE;
  }

  int GetGroupCount() const { return m_nGroups; }

private:
  enum Op { opChar, opAny, opClass, opSplit, opJmp, opSave, opBol, opEol, opNotAhead, opLoop,
            opMatch };
  struct Inst {
    Op op;
    int x, y;  // targets, group slot, class index, loop slot
    RECHAR ch;
  };
  struct Range {
    RECHAR lo, hi;
  };
  struct CharClass {
    bool bNegate;
    std::vector<Range> ranges;
  };

  std::vector<Inst> m_prog;
  std::vector<CharClass> m_classes;
  int m_nGroups, m_nLoops;
  BOOL m_bCaseSensitive;
  const RECHAR* m_szInput;
  std::vector<const RECHAR*> m_saves, m_loops;

  int Emit(Op op, int x = 0, int y = 0, RECHAR ch = 0) {
    Inst i = {op, x, y, ch};
    m_prog.push_back(i);
    return (int)m_prog.size() - 1;
  }

  // ---- Parsing, straight to the program ----

  // alt := seq ('|' seq)*. nDepth tells what closes it.
  REParseError ParseAlt(const RECHAR*& p, int nDepth) {
    int nStart = (int)m_prog.size();
    REParseError err = ParseSeq(p);
    if (err != REPARSE_ERROR_OK) return err;
    while (*p == '|') {
      p++;
      // Split(first, second) in front of what is there; first jumps past.
      Inst split = {opSplit, nStart + 1, 0, 0};
      m_prog.insert(m_prog.begin() + nStart, split);
      Relocate(nStart, 1);
      int nJmp = Emit(opJmp);
      m_prog[nStart].y = (int)m_prog.size();
      err = ParseSeq(p);
      if (err != REPARSE_ERROR_OK) return err;
      m_prog[nJmp].x = (int)m_prog.size();
    }
    (void)nDepth;
    return REPARSE_ERROR_OK;
  }

  // After n instructions are inserted at nAt: the targets that moved.
  // A target at nAt from before the insert is the start of the new
  // construct and stays; from inside the shifted part it moves with it.
  void Relocate(int nAt, int n) {
    for (size_t i = 0; i < m_prog.size(); i++) {
      int ii = (int)i;
      if (ii >= nAt && ii < nAt + n) continue;  // the inserted ones
      int nFirst = ii < nAt ? nAt + 1 : nAt;
      Inst& in = m_prog[i];
      if (in.op == opSplit || in.op == opJmp || in.op == opNotAhead) {
        if (in.x >= nFirst) in.x += n;
        if (in.op == opSplit && in.y >= nFirst) in.y += n;
      }
    }
  }

  REParseError ParseSeq(const RECHAR*& p) {
    while (*p != 0 && *p != '|' && *p != ')' && *p != '}') {
      REParseError err = ParseRepeat(p);
      if (err != REPARSE_ERROR_OK) return err;
    }
    return REPARSE_ERROR_OK;
  }

  REParseError ParseRepeat(const RECHAR*& p) {
    if (*p == '*' || *p == '+' || *p == '?') return REPARSE_ERROR_EMPTY_REPEATOP;
    if (*p == '!') {
      // NotAhead(end): the following item must not match here.
      p++;
      int nAt = Emit(opNotAhead);
      REParseError err = ParseRepeat(p);
      if (err != REPARSE_ERROR_OK) return err;
      Emit(opMatch);
      m_prog[nAt].x = (int)m_prog.size();
      return REPARSE_ERROR_OK;
    }
    int nStart = (int)m_prog.size();
    REParseError err = ParseAtom(p);
    if (err != REPARSE_ERROR_OK) return err;
    if (*p != '*' && *p != '+' && *p != '?') return REPARSE_ERROR_OK;
    RECHAR op = *p++;
    bool bGreedy = true;
    if (*p == '?') {
      bGreedy = false;
      p++;
    }
    int nLoop = m_nLoops++;
    if (op == '+') {
      // L1: loop-check; atom; Split(L1, out)
      Inst chk = {opLoop, nLoop, 0, 0};
      m_prog.insert(m_prog.begin() + nStart, chk);
      Relocate(nStart, 1);
      int nSplit = Emit(opSplit);
      int nOut = (int)m_prog.size();
      m_prog[nSplit].x = bGreedy ? nStart : nOut;
      m_prog[nSplit].y = bGreedy ? nOut : nStart;
      return REPARSE_ERROR_OK;
    }
    if (op == '?') {
      // Split(atom, out); atom; out
      Inst split = {opSplit, 0, 0, 0};
      m_prog.insert(m_prog.begin() + nStart, split);
      Relocate(nStart, 1);
      int nOut = (int)m_prog.size();
      m_prog[nStart].x = bGreedy ? nStart + 1 : nOut;
      m_prog[nStart].y = bGreedy ? nOut : nStart + 1;
      return REPARSE_ERROR_OK;
    }
    // '*': L1: Split(L2, out); L2: loop-check; atom; Jmp L1; out
    Inst split = {opSplit, 0, 0, 0};
    Inst chk = {opLoop, nLoop, 0, 0};
    m_prog.insert(m_prog.begin() + nStart, chk);
    m_prog.insert(m_prog.begin() + nStart, split);
    Relocate(nStart, 2);
    Emit(opJmp, nStart);
    int nOut = (int)m_prog.size();
    m_prog[nStart].x = bGreedy ? nStart + 1 : nOut;
    m_prog[nStart].y = bGreedy ? nOut : nStart + 1;
    return REPARSE_ERROR_OK;
  }

  REParseError ParseAtom(const RECHAR*& p) {
    RECHAR ch = *p;
    switch (ch) {
      case '(': {
        p++;
        REParseError err = ParseAlt(p, 1);
        if (err != REPARSE_ERROR_OK) return err;
        if (*p != ')') return REPARSE_ERROR_PAREN_EXPECTED;
        p++;
        return REPARSE_ERROR_OK;
      }
      case '{': {
        p++;
        int nGroup = m_nGroups++;
        Emit(opSave, nGroup * 2);
        REParseError err = ParseAlt(p, 2);
        if (err != REPARSE_ERROR_OK) return err;
        if (*p != '}') return REPARSE_ERROR_BRACE_EXPECTED;
        p++;
        Emit(opSave, nGroup * 2 + 1);
        return REPARSE_ERROR_OK;
      }
      case '[': p++; return ParseClass(p);
      case '.': p++; Emit(opAny); return REPARSE_ERROR_OK;
      case '^': p++; Emit(opBol); return REPARSE_ERROR_OK;
      case '$': p++; Emit(opEol); return REPARSE_ERROR_OK;
      case ')': return REPARSE_ERROR_UNEXPECTED;
      case '}': return REPARSE_ERROR_UNEXPECTED;
      case ']': return REPARSE_ERROR_UNEXPECTED;
      case '\\': {
        p++;
        if (*p == 0) return REPARSE_ERROR_UNEXPECTED;
        const char* pszAbbrev = Abbreviation(*p);
        if (pszAbbrev != NULL) {
          p++;
          RECHAR sz[64];
          size_t n = 0;
          for (; pszAbbrev[n] != 0; n++) sz[n] = (RECHAR)pszAbbrev[n];
          sz[n] = 0;
          const RECHAR* q = sz;
          // As a group of its own, so a repeat after it covers all of it.
          REParseError err = ParseAlt(q, 1);
          return err;
        }
        Emit(opChar, 0, 0, *p++);
        return REPARSE_ERROR_OK;
      }
      default: p++; Emit(opChar, 0, 0, ch); return REPARSE_ERROR_OK;
    }
  }

  static const char* Abbreviation(RECHAR ch) {
    switch (ch) {
      case 'a': return "([a-zA-Z0-9])";
      case 'b': return "([ \\t])";
      case 'c': return "([a-zA-Z])";
      case 'd': return "([0-9])";
      case 'h': return "([0-9a-fA-F])";
      case 'n': return "(\r|(\r?\n))";
      case 'q': return "((\"[^\"]*\")|('[^']*'))";
      case 'w': return "([a-zA-Z]+)";
      case 'z': return "([0-9]+)";
      default: return NULL;
    }
  }

  REParseError ParseClass(const RECHAR*& p) {
    CharClass cls;
    cls.bNegate = false;
    if (*p == '^') {
      cls.bNegate = true;
      p++;
    }
    bool bFirst = true;
    while (*p != ']' || bFirst) {
      if (*p == 0) return REPARSE_ERROR_BRACKET_EXPECTED;
      bFirst = false;
      RECHAR lo = *p++;
      if (lo == '\\') {
        if (*p == 0) return REPARSE_ERROR_BRACKET_EXPECTED;
        lo = *p++;
        if (lo == 't') lo = '\t';
        else if (lo == 'n') lo = '\n';
        else if (lo == 'r') lo = '\r';
      }
      RECHAR hi = lo;
      if (*p == '-' && p[1] != ']' && p[1] != 0) {
        p++;
        hi = *p++;
        if (hi == '\\') {
          if (*p == 0) return REPARSE_ERROR_BRACKET_EXPECTED;
          hi = *p++;
        }
        if (hi < lo) return REPARSE_ERROR_INVALID_RANGE;
      }
      Range r = {lo, hi};
      cls.ranges.push_back(r);
    }
    p++;  // ']'
    if (cls.ranges.empty()) return REPARSE_ERROR_EMPTY_RANGE;
    m_classes.push_back(cls);
    Emit(opClass, (int)m_classes.size() - 1);
    return REPARSE_ERROR_OK;
  }

  // ---- Matching: a backtracking run of the program ----

  bool SameChar(RECHAR a, RECHAR b) const {
    return a == b || (!m_bCaseSensitive && CharTraits::ToLower(a) == CharTraits::ToLower(b));
  }

  bool InClass(const CharClass& cls, RECHAR ch) const {
    bool bIn = false;
    for (size_t i = 0; i < cls.ranges.size() && !bIn; i++) {
      const Range& r = cls.ranges[i];
      if (ch >= r.lo && ch <= r.hi) bIn = true;
      if (!m_bCaseSensitive && !bIn) {
        RECHAR l = CharTraits::ToLower(ch), u = CharTraits::ToUpper(ch);
        if ((l >= r.lo && l <= r.hi) || (u >= r.lo && u <= r.hi)) bIn = true;
      }
    }
    return bIn != cls.bNegate;
  }

  bool Run(int pc, const RECHAR* sp, const RECHAR** ppEnd) {
    for (;;) {
      const Inst& in = m_prog[pc];
      switch (in.op) {
        case opChar:
          if (*sp == 0 || !SameChar(*sp, in.ch)) return false;
          sp++;
          pc++;
          break;
        case opAny:
          if (*sp == 0) return false;
          sp++;
          pc++;
          break;
        case opClass:
          if (*sp == 0 || !InClass(m_classes[in.x], *sp)) return false;
          sp++;
          pc++;
          break;
        case opBol:
          if (sp != m_szInput) return false;
          pc++;
          break;
        case opEol:
          if (*sp != 0) return false;
          pc++;
          break;
        case opJmp: pc = in.x; break;
        case opSplit:
          if (Run(in.x, sp, ppEnd)) return true;
          pc = in.y;
          break;
        case opSave: {
          const RECHAR* pOld = m_saves[in.x];
          m_saves[in.x] = sp;
          if (Run(pc + 1, sp, ppEnd)) return true;
          m_saves[in.x] = pOld;
          return false;
        }
        case opLoop: {
          // A repeat that matched nothing goes round no more.
          const RECHAR* pOld = m_loops[in.x];
          if (pOld == sp) return false;
          m_loops[in.x] = sp;
          if (Run(pc + 1, sp, ppEnd)) return true;
          m_loops[in.x] = pOld;
          return false;
        }
        case opNotAhead: {
          std::vector<const RECHAR*> saves = m_saves;
          const RECHAR* pDummy = NULL;
          bool bMatched = Run(pc + 1, sp, &pDummy);
          m_saves = saves;
          if (bMatched) return false;
          pc = in.x;
          break;
        }
        case opMatch:
          *ppEnd = sp;
          return true;
      }
    }
  }
};

}  // namespace ATL

#endif  // __ATLRX_H__
