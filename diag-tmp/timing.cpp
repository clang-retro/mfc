// Times the steps of the first text draw over the portable layer.
#include <windows.h>
#include <stdio.h>
#include <string>

static std::string g_out;
extern std::string g_fdbg;  // the font trace in font_dbg.cpp

// Adds a line with the milliseconds since t.
static void Step(const char* what, DWORD t) {
  char b[160];
  snprintf(b, sizeof b, "%s: %lu ms\n", what, (unsigned long)(GetTickCount() - t));
  g_out += b;
}

// Times font scan, font load and glyph drawing, then shows the times.
int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) {
  HDC dc = CreateCompatibleDC(NULL);
  HBITMAP bm = CreateCompatibleBitmap(dc, 600, 60);
  SelectObject(dc, bm);
  SIZE sz;
  char face[64];
  DWORD t = GetTickCount();
  SelectObject(dc, GetStockObject(DEFAULT_GUI_FONT));
  GetTextExtentPoint32A(dc, "x", 1, &sz);
  GetTextFaceA(dc, sizeof face, face);
  Step((std::string("1 GUI font first extent [") + face + "]").c_str(), t);
  t = GetTickCount();
  TextOutA(dc, 0, 0, "File Edit Search Window", 23);
  Step("2 GUI font first draw", t);
  t = GetTickCount();
  SelectObject(dc, GetStockObject(SYSTEM_FONT));
  GetTextExtentPoint32A(dc, "x", 1, &sz);
  GetTextFaceA(dc, sizeof face, face);
  Step((std::string("3 System font first extent [") + face + "]").c_str(), t);
  t = GetTickCount();
  TextOutA(dc, 0, 20, "a", 1);
  Step("4 System font first glyph 'a'", t);
  t = GetTickCount();
  TextOutA(dc, 0, 20, "Hello from fix9", 15);
  Step("5 System font 'Hello from fix9'", t);
  t = GetTickCount();
  TextOutA(dc, 0, 20, "Hello from fix9", 15);
  Step("6 same text again", t);
  t = GetTickCount();
  TextOutA(dc, 0, 20, "QWERTYZ", 7);
  Step("7 seven new glyphs", t);
  g_out += "\n(times are wall clock)\n" + g_fdbg;
  FILE* o = fopen("timing.txt", "w");
  if (o) { fputs(g_out.c_str(), o); fclose(o); }
  MessageBoxA(NULL, g_out.c_str(), "First text timing", MB_OK);
  return 0;
}
