// core.cpp - console tests for the non-window classes.
#include <afx.h>
#include <afxcoll.h>
#include <afxtempl.h>
#include <stdio.h>

static int g_failures = 0;
#define CHECK(cond)                                                   \
  do {                                                                \
    if (!(cond)) {                                                    \
      printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);          \
      g_failures++;                                                   \
    }                                                                 \
  } while (0)

class CShape : public CObject {
  DECLARE_SERIAL(CShape)
public:
  int m_nSides;
  CString m_strName;
  CShape() : m_nSides(0) {}
  CShape(int n, LPCTSTR name) : m_nSides(n), m_strName(name) {}
  virtual void Serialize(CArchive& ar) {
    CObject::Serialize(ar);
    if (ar.IsStoring())
      ar << m_nSides << m_strName;
    else
      ar >> m_nSides >> m_strName;
  }
};
IMPLEMENT_SERIAL(CShape, CObject, 1)

class CSquare : public CShape {
  DECLARE_SERIAL(CSquare)
public:
  CSquare() : CShape(4, _T("square")) {}
};
IMPLEMENT_SERIAL(CSquare, CShape, 1)

static void TestString() {
  CString s = _T("Hello");
  s += _T(", world");
  CHECK(s == _T("Hello, world"));
  CHECK(s.GetLength() == 12);
  CHECK(s.Find(_T("world")) == 7);
  CHECK(s.Left(5) == _T("Hello"));
  CHECK(s.Mid(7) == _T("world"));
  CHECK(s.Right(5) == _T("world"));
  CString t;
  t.Format(_T("%d-%s-%04x"), 42, _T("x"), 0xab);
  CHECK(t == _T("42-x-00ab"));
  CString u = _T("  trim me  ");
  u.Trim();
  CHECK(u == _T("trim me"));
  CString v = _T("a,b,,c");
  int iStart = 0;
  CString tok1 = v.Tokenize(_T(","), iStart);
  CString tok2 = v.Tokenize(_T(","), iStart);
  CString tok3 = v.Tokenize(_T(","), iStart);
  CHECK(tok1 == _T("a") && tok2 == _T("b") && tok3 == _T("c"));
  v.Replace(_T(','), _T(';'));
  CHECK(v == _T("a;b;;c"));
  CString w = v;  // shares the buffer until written
  w.MakeUpper();
  CHECK(v == _T("a;b;;c") && w == _T("A;B;;C"));
  CStringA a("narrow");
  CStringW wide(a);
  CHECK(wide == L"narrow");
  CString sub;
  CHECK(AfxExtractSubString(sub, _T("one\ntwo\nthree"), 1) && sub == _T("two"));
}

static void TestCollections() {
  CStringArray arr;
  arr.Add(_T("b"));
  arr.Add(_T("a"));
  arr.InsertAt(0, _T("c"));
  CHECK(arr.GetSize() == 3 && arr[0] == _T("c") && arr[2] == _T("a"));
  arr.RemoveAt(1);
  CHECK(arr.GetSize() == 2 && arr[1] == _T("a"));

  CMapStringToPtr map;
  int one = 1, two = 2;
  map[_T("one")] = &one;
  map.SetAt(_T("two"), &two);
  void* p = NULL;
  CHECK(map.Lookup(_T("two"), p) && p == &two);
  CHECK(!map.Lookup(_T("three"), p));
  CHECK(map.GetCount() == 2);

  CList<int, int> list;
  for (int i = 0; i < 5; i++) list.AddTail(i);
  CHECK(list.GetCount() == 5 && list.GetHead() == 0 && list.GetTail() == 4);
  POSITION pos = list.Find(3);
  CHECK(pos != NULL);
  list.RemoveAt(pos);
  CHECK(list.GetCount() == 4 && list.Find(3) == NULL);

  CMap<int, int, CString, LPCTSTR> imap;
  imap[7] = _T("seven");
  CString val;
  CHECK(imap.Lookup(7, val) && val == _T("seven"));

  CArray<SIZE, const SIZE&> pts;
  pts.SetSize(3);
  SIZE sz = {5, 6};
  pts[2] = sz;
  CHECK(pts.GetCount() == 3 && pts[2].cx == 5);

  CObList objs;
  objs.AddTail(new CSquare);
  objs.AddTail(new CShape(3, _T("triangle")));
  CHECK(objs.GetCount() == 2);
  while (!objs.IsEmpty()) delete objs.RemoveHead();
}

static void TestRuntimeClass() {
  CObject* p = RUNTIME_CLASS(CSquare)->CreateObject();
  CHECK(p != NULL);
  CHECK(p->IsKindOf(RUNTIME_CLASS(CShape)));
  CHECK(p->IsKindOf(RUNTIME_CLASS(CObject)));
  CHECK(!RUNTIME_CLASS(CShape)->IsDerivedFrom(RUNTIME_CLASS(CSquare)));
  CHECK(strcmp(p->GetRuntimeClass()->m_lpszClassName, "CSquare") == 0);
  CHECK(CRuntimeClass::FromName("CShape") == RUNTIME_CLASS(CShape));
  delete p;
}

static void TestArchive() {
  CMemFile file;
  {
    CArchive ar(&file, CArchive::store);
    CShape tri(3, _T("triangle"));
    CSquare sq;
    CObject* pShape = &sq;
    ar << (DWORD)0x12345678 << (short)-2 << 3.5 << CString(_T("text"));
    tri.Serialize(ar);
    ar << pShape;
    ar << pShape;  // the second time only a reference is written
    ar.Close();
  }
  file.SeekToBegin();
  {
    CArchive ar(&file, CArchive::load);
    DWORD dw;
    short sh;
    double d;
    CString str;
    ar >> dw >> sh >> d >> str;
    CHECK(dw == 0x12345678 && sh == -2 && d == 3.5 && str == _T("text"));
    CShape tri;
    tri.Serialize(ar);
    CHECK(tri.m_nSides == 3 && tri.m_strName == _T("triangle"));
    CObject* p1 = NULL;
    CObject* p2 = NULL;
    ar >> p1 >> p2;
    CHECK(p1 != NULL && p1 == p2 && p1->IsKindOf(RUNTIME_CLASS(CSquare)));
    CHECK(((CShape*)p1)->m_nSides == 4);
    delete p1;
  }
}

static void TestFileAndExceptions() {
  TCHAR szTemp[MAX_PATH], szPath[MAX_PATH];
  ::GetTempPath(MAX_PATH, szTemp);
  ::GetTempFileName(szTemp, _T("mfc"), 0, szPath);
  {
    CFile f(szPath, CFile::modeCreate | CFile::modeWrite);
    f.Write("abc", 3);
  }
  {
    CFile f(szPath, CFile::modeRead);
    char buf[8] = {0};
    CHECK(f.Read(buf, 8) == 3 && strcmp(buf, "abc") == 0);
    CHECK(f.GetLength() == 3);
  }
  CFile::Remove(szPath);

  BOOL bCaught = FALSE;
  try {
    CFile f(_T("Z:\\no\\such\\dir\\file.txt"), CFile::modeRead);
  } catch (CFileException* e) {
    bCaught = e->m_cause == CFileException::fileNotFound || e->m_cause == CFileException::badPath;
    e->Delete();
  }
  CHECK(bCaught);

  bCaught = FALSE;
  TRY { AfxThrowUserException(); }
  CATCH(CUserException, e) { bCaught = TRUE; }
  END_CATCH
  CHECK(bCaught);
}

static void TestTime() {
  CTime t(2024, 2, 29, 13, 45, 30);
  CHECK(t.GetYear() == 2024 && t.GetMonth() == 2 && t.GetDay() == 29);
  CHECK(t.Format(_T("%Y-%m-%d %H:%M:%S")) == _T("2024-02-29 13:45:30"));
  CTimeSpan span(1, 2, 3, 4);
  CTime t2 = t + span;
  CHECK(t2.GetDay() == 1 && t2.GetMonth() == 3 && t2.GetHour() == 15);
  CHECK((t2 - t).GetTotalSeconds() == span.GetTotalSeconds());
}

int main() {
  TestString();
  TestCollections();
  TestRuntimeClass();
  TestArchive();
  TestFileAndExceptions();
  TestTime();
  printf("%s: %d failure(s)\n", g_failures ? "FAIL" : "PASS", g_failures);
  return g_failures ? 1 : 0;
}
