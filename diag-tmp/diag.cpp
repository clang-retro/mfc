// Font trace for the classic Mac OS build of the portable layer.
#include <windows.h>
#include <stdio.h>
#include <string>

extern std::string g_fdbg;

// Makes the layer scan the fonts, then shows the trace.
int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) {
  HDC dc = CreateCompatibleDC(NULL);
  HFONT f = CreateFontA(100, 0, 0, 0, FW_NORMAL, 0, 0, 0, 0, OUT_TT_ONLY_PRECIS, 0, 0, 0, "Times");
  SelectObject(dc, f);
  OUTLINETEXTMETRICA otm;
  UINT n = GetOutlineTextMetricsA(dc, sizeof otm, &otm);
  char buf[64];
  snprintf(buf, sizeof buf, "\nTimes otm %u", n);
  g_fdbg += buf;
  FILE* o = fopen("fonttrace.txt", "w");
  if (o) { fputs(g_fdbg.c_str(), o); fclose(o); }
  MessageBoxA(NULL, g_fdbg.c_str(), "Font trace", MB_OK);
  return 0;
}
