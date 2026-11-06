// addprinter.cpp - gives the Wine prefix the tests run in a printer, so
// printing and print preview have a printer DC. Wine's PostScript driver
// needs a PPD file; this one describes a plain US Letter printer.
#include <windows.h>
#include <stdio.h>

static const char szPPD[] =
    "*PPD-Adobe: \"4.3\"\n"
    "*FormatVersion: \"4.3\"\n"
    "*FileVersion: \"1.0\"\n"
    "*LanguageVersion: English\n"
    "*LanguageEncoding: ISOLatin1\n"
    "*PCFileName: \"MFCTEST.PPD\"\n"
    "*Manufacturer: \"MFC Test\"\n"
    "*Product: \"(MFC Test Printer)\"\n"
    "*PSVersion: \"(3010.000) 0\"\n"
    "*ModelName: \"MFC Test Printer\"\n"
    "*ShortNickName: \"MFC Test Printer\"\n"
    "*NickName: \"MFC Test Printer\"\n"
    "*ColorDevice: True\n"
    "*DefaultColorSpace: RGB\n"
    "*LanguageLevel: \"3\"\n"
    "*DefaultResolution: 300dpi\n"
    "*OpenUI *PageSize: PickOne\n"
    "*DefaultPageSize: Letter\n"
    "*PageSize Letter/US Letter: \"<</PageSize[612 792]>>setpagedevice\"\n"
    "*CloseUI: *PageSize\n"
    "*DefaultPageRegion: Letter\n"
    "*DefaultImageableArea: Letter\n"
    "*ImageableArea Letter/US Letter: \"18 36 594 756\"\n"
    "*DefaultPaperDimension: Letter\n"
    "*PaperDimension Letter/US Letter: \"612 792\"\n";

int main() {
  const char* pszName = "MFC Test Printer";
  char szDir[MAX_PATH], szPath[MAX_PATH];
  DWORD cb = sizeof(szDir);
  if (!GetPrinterDriverDirectoryA(NULL, NULL, 1, (BYTE*)szDir, cb, &cb)) {
    printf("no printer driver directory\n");
    return 1;
  }
  CreateDirectoryA(szDir, NULL);
  snprintf(szPath, sizeof(szPath), "%s\\mfctest.ppd", szDir);
  FILE* f = fopen(szPath, "wb");
  if (f == NULL) {
    printf("cannot write %s\n", szPath);
    return 1;
  }
  fputs(szPPD, f);
  fclose(f);

  DRIVER_INFO_3A di;
  ZeroMemory(&di, sizeof(di));
  di.cVersion = 3;
  di.pName = (LPSTR) "MFC Test PostScript";
  di.pDriverPath = (LPSTR) "wineps.drv";
  di.pDataFile = (LPSTR) "mfctest.ppd";
  di.pConfigFile = (LPSTR) "wineps.drv";
  if (!AddPrinterDriverA(NULL, 3, (BYTE*)&di) && GetLastError() != ERROR_PRINTER_DRIVER_ALREADY_INSTALLED) {
    printf("AddPrinterDriver failed: %lu\n", GetLastError());
    return 1;
  }

  PRINTER_INFO_2A pi;
  ZeroMemory(&pi, sizeof(pi));
  pi.pPrinterName = (LPSTR)pszName;
  pi.pPortName = (LPSTR) "FILE:";
  pi.pDriverName = di.pName;
  pi.pPrintProcessor = (LPSTR) "WinPrint";
  pi.pDatatype = (LPSTR) "RAW";
  HANDLE h = AddPrinterA(NULL, 2, (BYTE*)&pi);
  if (h != NULL)
    ClosePrinter(h);
  else if (GetLastError() != ERROR_PRINTER_ALREADY_EXISTS) {
    printf("AddPrinter failed: %lu\n", GetLastError());
    return 1;
  }
  if (!SetDefaultPrinterA(pszName)) {
    printf("SetDefaultPrinter failed: %lu\n", GetLastError());
    return 1;
  }
  HDC hDC = CreateDCA("WINSPOOL", pszName, NULL, NULL);
  if (hDC == NULL) {
    printf("CreateDC failed\n");
    return 1;
  }
  DeleteDC(hDC);
  return 0;
}
