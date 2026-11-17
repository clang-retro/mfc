// print.cpp - printing. The printers are CUPS's queues, when the system
// has CUPS, and "Save as PDF". A printer DC draws each page on a bitmap
// at the printer's resolution; EndPage adds the page to a PDF file, and
// EndDoc gives the file to CUPS's lp, or leaves it where the job's
// output names (the user's documents folder by default). The Print,
// Print Setup and Page Setup dialogs choose the printer and its paper.
//
// WINPORT_PRINTERS=pdf leaves CUPS out, so that only "Save as PDF" is
// there.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#include "gdi.h"
#include "user.h"

#include <commdlg.h>
#include <dlgs.h>
#include <dlfcn.h>
#include <errno.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#include <wchar.h>

#include <mutex>
#include <string>
#include <vector>

using namespace winport;

namespace winport {

// ---------------------------------------------------------------------
// Paper

namespace {

struct Paper {
  short id;
  const wchar_t* name;
  short width, length;  // in tenths of a millimetre
};
const Paper kPapers[] = {
    {DMPAPER_LETTER, L"Letter", 2159, 2794}, {DMPAPER_LEGAL, L"Legal", 2159, 3556},
    {DMPAPER_EXECUTIVE, L"Executive", 1842, 2667}, {DMPAPER_TABLOID, L"Tabloid", 2794, 4318},
    {DMPAPER_A3, L"A3", 2970, 4200}, {DMPAPER_A4, L"A4", 2100, 2970},
    {DMPAPER_A5, L"A5", 1480, 2100}, {DMPAPER_B5, L"B5 (JIS)", 1820, 2570},
};
const int kPaperCount = sizeof kPapers / sizeof kPapers[0];

const Paper* FindPaper(short id) {
  for (const Paper& p : kPapers)
    if (p.id == id) return &p;
  return NULL;
}

// Letter where the locale's country uses it (and in the C locale), else A4.
bool UsesLetter() {
  const char* names[] = {"LC_ALL", "LC_PAPER", "LANG"};
  for (const char* n : names) {
    const char* v = getenv(n);
    if (!v || !*v) continue;
    const char* u = strchr(v, '_');
    if (!u) return true;
    static const char* letter[] = {"US", "CA", "MX", "PH", "CL", "CO", "VE", "CR", "GT", "PR", "SV", "NI", "PA"};
    for (const char* c : letter)
      if (strncmp(u + 1, c, 2) == 0) return true;
    return false;
  }
  return true;
}

bool Metric() { return !UsesLetter(); }

// ---------------------------------------------------------------------
// Printers

const wchar_t kPdfPrinter[] = L"Save as PDF";

// The edges a printer cannot print on, in thousandths of an inch.
const int kUnprintable = 250;

struct Printer {
  std::wstring name, port;
  bool cups = false;
};

// Runs a command through the shell and returns what it writes.
std::string Output(const char* cmd) {
  std::string out;
  FILE* f = popen(cmd, "r");
  if (!f) return out;
  char buf[512];
  size_t n;
  while ((n = fread(buf, 1, sizeof buf, f)) > 0) out.append(buf, n);
  pclose(f);
  return out;
}

struct PrinterList {
  std::vector<Printer> printers;
  size_t def = 0;
};

const PrinterList& Printers() {
  static PrinterList list;
  static std::once_flag once;
  std::call_once(once, [] {
    const char* only = getenv("WINPORT_PRINTERS");
    std::string def;
    if (!only || strcmp(only, "pdf") != 0) {
      // lpstat -a lists the queues that take jobs, one a line, the name first.
      std::string all = Output("command -v lpstat >/dev/null 2>&1 && lpstat -a 2>/dev/null");
      size_t pos = 0;
      while (pos < all.size()) {
        size_t end = all.find('\n', pos);
        if (end == std::string::npos) end = all.size();
        std::string line = all.substr(pos, end - pos);
        pos = end + 1;
        size_t sp = line.find(' ');
        std::string name = line.substr(0, sp);
        if (name.empty() || name.size() >= CCHDEVICENAME) continue;
        Printer p;
        p.name = Widen(name.c_str());
        p.port = L"CUPS:";
        p.cups = true;
        list.printers.push_back(p);
      }
      if (!list.printers.empty()) {
        std::string d = Output("lpstat -d 2>/dev/null");
        size_t colon = d.find(": ");
        if (colon != std::string::npos && d.compare(0, 3, "no ") != 0) {
          def = d.substr(colon + 2);
          while (!def.empty() && (def.back() == '\n' || def.back() == ' ')) def.pop_back();
        }
      }
    }
    Printer pdf;
    pdf.name = kPdfPrinter;
    pdf.port = L"PDF:";
    list.printers.push_back(pdf);
    list.def = list.printers.size() - 1;
    for (size_t i = 0; i < list.printers.size(); i++)
      if (list.printers[i].cups && Narrow(list.printers[i].name.c_str()) == def) list.def = i;
  });
  return list;
}

const Printer* FindPrinter(const std::wstring& name) {
  for (const Printer& p : Printers().printers)
    if (_wcsicmp(p.name.c_str(), name.c_str()) == 0) return &p;
  return NULL;
}

void CopyName(WCHAR* to, const std::wstring& name) {
  size_t n = name.size() < CCHDEVICENAME - 1 ? name.size() : CCHDEVICENAME - 1;
  memcpy(to, name.data(), n * sizeof(WCHAR));
  to[n] = 0;
}

DEVMODEW DefaultDevMode(const std::wstring& name) {
  DEVMODEW dm;
  memset(&dm, 0, sizeof dm);
  CopyName(dm.dmDeviceName, name);
  dm.dmSpecVersion = DM_SPECVERSION;
  dm.dmDriverVersion = 0x100;
  dm.dmSize = sizeof dm;
  dm.dmFields = DM_ORIENTATION | DM_PAPERSIZE | DM_PAPERLENGTH | DM_PAPERWIDTH | DM_SCALE | DM_COPIES |
                DM_PRINTQUALITY | DM_YRESOLUTION | DM_COLOR | DM_DUPLEX | DM_COLLATE;
  const Paper* p = FindPaper(Metric() ? DMPAPER_A4 : DMPAPER_LETTER);
  dm.dmOrientation = DMORIENT_PORTRAIT;
  dm.dmPaperSize = p->id;
  dm.dmPaperWidth = p->width;
  dm.dmPaperLength = p->length;
  dm.dmScale = 100;
  dm.dmCopies = 1;
  dm.dmPrintQuality = 300;
  dm.dmYResolution = 300;
  dm.dmColor = DMCOLOR_COLOR;
  dm.dmDuplex = DMDUP_SIMPLEX;
  dm.dmCollate = DMCOLLATE_TRUE;
  return dm;
}

// The printer's settings with the application's changes, which name
// their fields in dmFields.
DEVMODEW MergeDevMode(const std::wstring& name, const DEVMODEW* in) {
  DEVMODEW dm = DefaultDevMode(name);
  if (!in) return dm;
  DWORD f = in->dmFields;
  if ((f & DM_ORIENTATION) && (in->dmOrientation == DMORIENT_PORTRAIT || in->dmOrientation == DMORIENT_LANDSCAPE))
    dm.dmOrientation = in->dmOrientation;
  if (f & DM_PAPERSIZE) {
    if (const Paper* p = FindPaper(in->dmPaperSize)) {
      dm.dmPaperSize = p->id;
      dm.dmPaperWidth = p->width;
      dm.dmPaperLength = p->length;
    } else {
      dm.dmPaperSize = DMPAPER_USER;
    }
  }
  if ((f & DM_PAPERWIDTH) && in->dmPaperWidth > 0) dm.dmPaperWidth = in->dmPaperWidth;
  if ((f & DM_PAPERLENGTH) && in->dmPaperLength > 0) dm.dmPaperLength = in->dmPaperLength;
  if (((f & DM_PAPERWIDTH) && in->dmPaperWidth > 0) || ((f & DM_PAPERLENGTH) && in->dmPaperLength > 0)) {
    const Paper* p = FindPaper(dm.dmPaperSize);
    if (!p || p->width != dm.dmPaperWidth || p->length != dm.dmPaperLength) dm.dmPaperSize = DMPAPER_USER;
  }
  if ((f & DM_SCALE) && in->dmScale > 0) dm.dmScale = in->dmScale;
  if ((f & DM_COPIES) && in->dmCopies > 0) dm.dmCopies = in->dmCopies;
  if (f & DM_PRINTQUALITY) {
    short q = in->dmPrintQuality;
    if (q == DMRES_DRAFT || q == DMRES_LOW) q = 150;
    else if (q == DMRES_MEDIUM) q = 300;
    else if (q == DMRES_HIGH) q = 600;
    if (q >= 72 && q <= 1200) dm.dmPrintQuality = dm.dmYResolution = q;
  }
  if ((f & DM_YRESOLUTION) && in->dmYResolution >= 72 && in->dmYResolution <= 1200)
    dm.dmYResolution = in->dmYResolution;
  if ((f & DM_COLOR) && (in->dmColor == DMCOLOR_MONOCHROME || in->dmColor == DMCOLOR_COLOR)) dm.dmColor = in->dmColor;
  if ((f & DM_DUPLEX) && in->dmDuplex >= DMDUP_SIMPLEX && in->dmDuplex <= DMDUP_HORIZONTAL) dm.dmDuplex = in->dmDuplex;
  if (f & DM_COLLATE) dm.dmCollate = in->dmCollate ? DMCOLLATE_TRUE : DMCOLLATE_FALSE;
  return dm;
}

// The page's size in device pixels.
SIZE PageSize(const DEVMODEW& dm) {
  int w = dm.dmPaperWidth, h = dm.dmPaperLength;
  int dx = dm.dmPrintQuality, dy = dm.dmYResolution;
  if (dm.dmOrientation == DMORIENT_LANDSCAPE) {
    int t = w;
    w = h;
    h = t;
    t = dx;
    dx = dy;
    dy = t;
  }
  SIZE s = {(LONG)((w * dx + 127) / 254), (LONG)((h * dy + 127) / 254)};
  return s;
}

// ---------------------------------------------------------------------
// PDF

// zlib, when the system has it, compresses the pages.
typedef int (*CompressFn)(unsigned char*, unsigned long*, const unsigned char*, unsigned long, int);
typedef unsigned long (*BoundFn)(unsigned long);
struct Zlib {
  CompressFn compress2 = NULL;
  BoundFn compressBound = NULL;
};
const Zlib& GetZlib() {
  static Zlib z;
  static std::once_flag once;
  std::call_once(once, [] {
    const char* names[] = {"libz.so.1", "libz.so", "libz.1.dylib", "/usr/lib/libz.1.dylib", "libz.dylib"};
    for (const char* n : names) {
      void* lib = dlopen(n, RTLD_NOW | RTLD_LOCAL);
      if (!lib) continue;
      z.compress2 = (CompressFn)dlsym(lib, "compress2");
      z.compressBound = (BoundFn)dlsym(lib, "compressBound");
      if (z.compress2 && z.compressBound) break;
      z.compress2 = NULL;
      z.compressBound = NULL;
    }
  });
  return z;
}

// PDF's RunLengthDecode, without zlib.
std::string RunLength(const std::vector<unsigned char>& in) {
  std::string out;
  size_t i = 0, n = in.size();
  while (i < n) {
    size_t run = 1;
    while (i + run < n && run < 128 && in[i + run] == in[i]) run++;
    if (run >= 2) {
      out += (char)(257 - run);
      out += (char)in[i];
      i += run;
      continue;
    }
    size_t start = i, len = 0;
    while (i < n && len < 128) {
      if (i + 2 < n && in[i] == in[i + 1] && in[i] == in[i + 2]) break;
      i++;
      len++;
    }
    out += (char)(len - 1);
    out.append((const char*)&in[start], len);
  }
  out += (char)128;
  return out;
}

class PdfWriter {
 public:
  explicit PdfWriter(FILE* f) : f_(f) {
    offsets_.resize(3, 0);
    fputs("%PDF-1.4\n%\xe2\xe3\xcf\xd3\n", f_);
  }
  // A page of pixels, shown size (in points).
  bool AddPage(const Pixels& px, double x, double y, double w, double h, double pageW, double pageH) {
    bool gray = true;
    for (int y = 0; y < px.height && gray; y++) {
      const DWORD* row = px.Row(y);
      for (int x = 0; x < px.width; x++) {
        DWORD p = row[x];
        if (((p >> 16) & 0xFF) != ((p >> 8) & 0xFF) || ((p >> 8) & 0xFF) != (p & 0xFF)) {
          gray = false;
          break;
        }
      }
    }
    int comps = gray ? 1 : 3;
    std::vector<unsigned char> raw((size_t)px.width * px.height * comps);
    unsigned char* o = raw.data();
    for (int y = 0; y < px.height; y++) {
      const DWORD* row = px.Row(y);
      for (int x = 0; x < px.width; x++) {
        DWORD p = row[x];
        if (gray) {
          *o++ = (unsigned char)p;
        } else {
          *o++ = (unsigned char)(p >> 16);
          *o++ = (unsigned char)(p >> 8);
          *o++ = (unsigned char)p;
        }
      }
    }
    std::string data;
    const char* filter = "/FlateDecode";
    const Zlib& z = GetZlib();
    bool packed = false;
    if (z.compress2) {
      unsigned long len = z.compressBound(raw.size());
      data.resize(len);
      if (z.compress2((unsigned char*)&data[0], &len, raw.data(), raw.size(), 6) == 0) {
        data.resize(len);
        packed = true;
      }
    }
    if (!packed) {
      data = RunLength(raw);
      filter = "/RunLengthDecode";
    }
    int image = Begin();
    fprintf(f_,
            "<< /Type /XObject /Subtype /Image /Width %d /Height %d /ColorSpace /%s /BitsPerComponent 8 "
            "/Filter %s /Length %lu >>\nstream\n",
            px.width, px.height, gray ? "DeviceGray" : "DeviceRGB", filter, (unsigned long)data.size());
    fwrite(data.data(), 1, data.size(), f_);
    fputs("\nendstream\nendobj\n", f_);
    char content[128];
    int clen = snprintf(content, sizeof content, "q %.2f 0 0 %.2f %.2f %.2f cm /Im0 Do Q\n", w, h, x, pageH - y - h);
    int contents = Begin();
    fprintf(f_, "<< /Length %d >>\nstream\n%s\nendstream\nendobj\n", clen, content);
    int page = Begin();
    fprintf(f_,
            "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 %.2f %.2f] /Resources << /XObject << /Im0 %d 0 R >> >> "
            "/Contents %d 0 R >>\nendobj\n",
            pageW, pageH, image, contents);
    pages_.push_back(page);
    return !ferror(f_);
  }
  bool Finish(const std::wstring& title) {
    int info = Begin();
    fputs("<< /Producer (clang-retro MFC)", f_);
    if (!title.empty()) {
      // UTF-16BE, in hex.
      fputs(" /Title <FEFF", f_);
      for (wchar_t c : title) {
        unsigned long u = (unsigned long)c;
        if (u >= 0x10000) {
          u -= 0x10000;
          fprintf(f_, "%04lX%04lX", 0xD800 + (u >> 10), 0xDC00 + (u & 0x3FF));
        } else {
          fprintf(f_, "%04lX", u);
        }
      }
      fputs(">", f_);
    }
    fputs(" >>\nendobj\n", f_);
    offsets_[1] = ftell(f_);
    fputs("1 0 obj\n<< /Type /Catalog /Pages 2 0 R >>\nendobj\n", f_);
    offsets_[2] = ftell(f_);
    fputs("2 0 obj\n<< /Type /Pages /Kids [", f_);
    for (int p : pages_) fprintf(f_, " %d 0 R", p);
    fprintf(f_, " ] /Count %d >>\nendobj\n", (int)pages_.size());
    long xref = ftell(f_);
    fprintf(f_, "xref\n0 %d\n0000000000 65535 f \n", (int)offsets_.size());
    for (size_t i = 1; i < offsets_.size(); i++) fprintf(f_, "%010ld 00000 n \n", offsets_[i]);
    fprintf(f_, "trailer\n<< /Size %d /Root 1 0 R /Info %d 0 R >>\nstartxref\n%ld\n%%%%EOF\n", (int)offsets_.size(),
            info, xref);
    return !ferror(f_);
  }

 private:
  int Begin() {
    offsets_.push_back(ftell(f_));
    int n = (int)offsets_.size() - 1;
    fprintf(f_, "%d 0 obj\n", n);
    return n;
  }
  FILE* f_;
  std::vector<long> offsets_;  // by object number; 0 is the free list's head
  std::vector<int> pages_;
};

// Hands a file to CUPS, and waits for lp to take it.
bool SubmitToCups(const std::string& file, const std::wstring& printer, const std::wstring& title, int copies) {
  std::string name = Narrow(printer.c_str()), t = Narrow(title.c_str());
  std::string n = std::to_string(copies < 1 ? 1 : copies);
  std::vector<char*> argv;
  const char* args[] = {"lp", "-s", "-d", name.c_str(), "-n", n.c_str()};
  for (const char* a : args) argv.push_back((char*)a);
  if (!t.empty()) {
    argv.push_back((char*)"-t");
    argv.push_back((char*)t.c_str());
  }
  argv.push_back((char*)file.c_str());
  argv.push_back(NULL);
  // fork and exec rather than posix_spawn, which Mac OS X 10.4 lacks.
  pid_t pid = fork();
  if (pid < 0) return false;
  if (pid == 0) {
    execvp("lp", argv.data());
    _exit(127);
  }
  int status = 0;
  while (waitpid(pid, &status, 0) < 0 && errno == EINTR) {
  }
  return WIFEXITED(status) && WEXITSTATUS(status) == 0;
}

// Where "Save as PDF" puts a job that names no output: the documents
// folder, or home, under the document's name.
std::string DefaultPdfPath(const std::wstring& docName) {
  std::string dir;
  const char* home = getenv("HOME");
  if (home && *home) {
    dir = home;
    struct stat st;
    std::string docs = dir + "/Documents";
    if (stat(docs.c_str(), &st) == 0 && S_ISDIR(st.st_mode)) dir = docs;
  } else {
    dir = ".";
  }
  std::string base = Narrow(docName.c_str());
  for (char& c : base)
    if (c == '/' || c == '\\' || c == ':' || (unsigned char)c < 32) c = '_';
  if (base.empty()) base = "Document";
  std::string path = dir + "/" + base + ".pdf";
  for (int i = 2; access(path.c_str(), F_OK) == 0 && i < 1000; i++)
    path = dir + "/" + base + " (" + std::to_string(i) + ").pdf";
  return path;
}

}  // namespace

// ---------------------------------------------------------------------
// Printer DCs

struct PrinterState {
  std::wstring device;
  bool cups = false;
  DEVMODEW dm;
  // The job
  bool inDoc = false, inPage = false;
  std::wstring docName;
  std::string path;    // the PDF being written
  bool temp = false;   // a file for CUPS, removed after
  FILE* file = NULL;
  PdfWriter* pdf = NULL;
  std::vector<DWORD> page;
  int width = 0, height = 0;

  ~PrinterState() { Abort(); }
  void Abort() {
    delete pdf;
    pdf = NULL;
    if (file) {
      fclose(file);
      file = NULL;
      unlink(path.c_str());
    }
    page.clear();
    page.shrink_to_fit();
    inDoc = inPage = false;
  }
};

DEVMODEW DevModeFromA(const DEVMODEA* a) {
  DEVMODEA in;
  memset(&in, 0, sizeof in);
  size_t n = a->dmSize ? a->dmSize : sizeof in;
  memcpy(&in, a, n < sizeof in ? n : sizeof in);
  DEVMODEW w;
  memset(&w, 0, sizeof w);
  char name[CCHDEVICENAME + 1] = {};
  memcpy(name, in.dmDeviceName, CCHDEVICENAME);
  CopyName(w.dmDeviceName, Widen(name));
  memcpy(&w.dmSpecVersion, &in.dmSpecVersion, offsetof(DEVMODEA, dmFormName) - offsetof(DEVMODEA, dmSpecVersion));
  char form[CCHFORMNAME + 1] = {};
  memcpy(form, in.dmFormName, CCHFORMNAME);
  std::wstring wf = Widen(form);
  for (size_t i = 0; i < wf.size() && i < CCHFORMNAME - 1; i++) w.dmFormName[i] = wf[i];
  memcpy(&w.dmLogPixels, &in.dmLogPixels, sizeof(DEVMODEA) - offsetof(DEVMODEA, dmLogPixels));
  w.dmSize = sizeof w;
  w.dmDriverExtra = 0;
  return w;
}

static DEVMODEA DevModeToA(const DEVMODEW& w) {
  DEVMODEA a;
  memset(&a, 0, sizeof a);
  std::string name = Narrow(w.dmDeviceName);
  memcpy(a.dmDeviceName, name.data(), name.size() < CCHDEVICENAME - 1 ? name.size() : CCHDEVICENAME - 1);
  memcpy(&a.dmSpecVersion, &w.dmSpecVersion, offsetof(DEVMODEA, dmFormName) - offsetof(DEVMODEA, dmSpecVersion));
  std::string form = Narrow(w.dmFormName);
  memcpy(a.dmFormName, form.data(), form.size() < CCHFORMNAME - 1 ? form.size() : CCHFORMNAME - 1);
  memcpy(&a.dmLogPixels, &w.dmLogPixels, sizeof(DEVMODEA) - offsetof(DEVMODEA, dmLogPixels));
  a.dmSize = sizeof a;
  a.dmDriverExtra = 0;
  return a;
}

static void ApplyDevMode(DCObj* dc) {
  PrinterState* ps = dc->printer.get();
  dc->paper = PageSize(ps->dm);
  bool land = ps->dm.dmOrientation == DMORIENT_LANDSCAPE;
  dc->dpiX = land ? ps->dm.dmYResolution : ps->dm.dmPrintQuality;
  dc->dpiY = land ? ps->dm.dmPrintQuality : ps->dm.dmYResolution;
  // Like most printers', the printable area leaves a quarter inch.
  dc->paperOffset.x = dc->dpiX * kUnprintable / 1000;
  dc->paperOffset.y = dc->dpiY * kUnprintable / 1000;
  dc->size.cx = dc->paper.cx - 2 * dc->paperOffset.x;
  dc->size.cy = dc->paper.cy - 2 * dc->paperOffset.y;
}

void InitPrinterDC(DCObj* dc, const std::wstring& device, const DEVMODEW* dm) {
  dc->printer = std::make_shared<PrinterState>();
  PrinterState* ps = dc->printer.get();
  const Printer* p = FindPrinter(device);
  ps->device = p ? p->name : device;
  ps->cups = p && p->cups;
  ps->dm = MergeDevMode(ps->device, dm);
  ApplyDevMode(dc);
}

bool ResetPrinterDC(DCObj* dc, const DEVMODEW* dm) {
  PrinterState* ps = dc->printer.get();
  if (!ps || ps->inPage) return false;
  ps->dm = MergeDevMode(ps->device, dm);
  ApplyDevMode(dc);
  return true;
}

Pixels PrinterPage(DCObj* dc) {
  Pixels px;
  PrinterState* ps = dc->printer.get();
  if (!ps || !ps->inPage || ps->page.empty()) return px;
  px.row0 = ps->page.data();
  px.stride = ps->width;
  px.width = ps->width;
  px.height = ps->height;
  return px;
}

}  // namespace winport

// ---------------------------------------------------------------------
// Jobs

static int StartDocCore(HDC hdc, const std::wstring& docName, const std::wstring& output) {
  DCObj* dc = DCOf(hdc);
  if (!dc) return SP_ERROR;
  PrinterState* ps = dc->kind == kPrinterDC ? dc->printer.get() : NULL;
  if (!ps || ps->inDoc) {
    SetLastError(ERROR_INVALID_PARAMETER);
    return SP_ERROR;
  }
  ps->docName = docName;
  bool toFile = !output.empty() && _wcsicmp(output.c_str(), L"FILE:") != 0 && output.back() != L':';
  if (toFile) {
    ps->path = PosixPath(output.c_str());
    ps->temp = false;
    ps->file = fopen(ps->path.c_str(), "wb");
  } else if (ps->cups) {
    const char* tmp = getenv("TMPDIR");
    std::string t = std::string(tmp && *tmp ? tmp : "/tmp") + "/winport-print-XXXXXX";
    std::vector<char> buf(t.begin(), t.end());
    buf.push_back(0);
    int fd = mkstemp(buf.data());
    if (fd >= 0) {
      ps->path = buf.data();
      ps->temp = true;
      ps->file = fdopen(fd, "wb");
    }
  } else {
    ps->path = DefaultPdfPath(docName);
    ps->temp = false;
    ps->file = fopen(ps->path.c_str(), "wb");
  }
  if (!ps->file) {
    SetErrorFromErrno();
    return SP_ERROR;
  }
  ps->pdf = new PdfWriter(ps->file);
  ps->inDoc = true;
  static std::atomic<int> jobs{0};
  return ++jobs;
}

extern "C" {

int WINAPI StartDocW(HDC hdc, const DOCINFOW* di) {
  return StartDocCore(hdc, di && di->lpszDocName ? di->lpszDocName : L"",
                      di && di->lpszOutput ? di->lpszOutput : L"");
}

int WINAPI StartDocA(HDC hdc, const DOCINFOA* di) {
  return StartDocCore(hdc, di && di->lpszDocName ? Widen(di->lpszDocName) : std::wstring(),
                      di && di->lpszOutput ? Widen(di->lpszOutput) : std::wstring());
}

int WINAPI StartPage(HDC hdc) {
  DCObj* dc = DCOf(hdc);
  PrinterState* ps = dc && dc->kind == kPrinterDC ? dc->printer.get() : NULL;
  if (!ps || !ps->inDoc || ps->inPage) return SP_ERROR;
  ps->width = dc->size.cx;
  ps->height = dc->size.cy;
  ps->page.assign((size_t)ps->width * ps->height, 0xFFFFFF);
  ps->inPage = true;
  return 1;
}

int WINAPI EndPage(HDC hdc) {
  DCObj* dc = DCOf(hdc);
  PrinterState* ps = dc && dc->kind == kPrinterDC ? dc->printer.get() : NULL;
  if (!ps || !ps->inPage) return SP_ERROR;
  Pixels px = PrinterPage(dc);
  double sx = 72.0 / dc->dpiX, sy = 72.0 / dc->dpiY;
  bool ok = ps->pdf->AddPage(px, dc->paperOffset.x * sx, dc->paperOffset.y * sy, ps->width * sx, ps->height * sy,
                             dc->paper.cx * sx, dc->paper.cy * sy);
  ps->inPage = false;
  ps->page.clear();
  ps->page.shrink_to_fit();
  if (!ok) {
    ps->Abort();
    return SP_OUTOFDISK;
  }
  if (dc->abortProc && !dc->abortProc(hdc, 0)) {
    ps->Abort();
    return SP_APPABORT;
  }
  return 1;
}

int WINAPI EndDoc(HDC hdc) {
  DCObj* dc = DCOf(hdc);
  PrinterState* ps = dc && dc->kind == kPrinterDC ? dc->printer.get() : NULL;
  if (!ps || !ps->inDoc) return SP_ERROR;
  if (ps->inPage) EndPage(hdc);
  if (!ps->inDoc) return SP_ERROR;
  bool ok = ps->pdf->Finish(ps->docName);
  delete ps->pdf;
  ps->pdf = NULL;
  if (fclose(ps->file) != 0) ok = false;
  ps->file = NULL;
  ps->inDoc = false;
  if (ok && ps->temp) ok = SubmitToCups(ps->path, ps->device, ps->docName, ps->dm.dmCopies);
  if (ps->temp || !ok) unlink(ps->path.c_str());
  return ok ? 1 : SP_ERROR;
}

int WINAPI AbortDoc(HDC hdc) {
  DCObj* dc = DCOf(hdc);
  PrinterState* ps = dc && dc->kind == kPrinterDC ? dc->printer.get() : NULL;
  if (!ps || !ps->inDoc) return SP_ERROR;
  ps->Abort();
  return 1;
}

int WINAPI SetAbortProc(HDC hdc, ABORTPROC proc) {
  DCObj* dc = DCOf(hdc);
  if (!dc) return SP_ERROR;
  dc->abortProc = proc;
  return 1;
}

}  // extern "C"

// ---------------------------------------------------------------------
// The dialogs' common parts

namespace {

thread_local DWORD tlsDlgError = 0;

// A dialog template in memory.
class Template {
 public:
  Template(DWORD style, int cx, int cy, const wchar_t* title) {
    style |= DS_SETFONT | DS_MODALFRAME | DS_CENTER | WS_POPUP | WS_CAPTION | WS_SYSMENU;
    Dword(style);
    Dword(0);
    w_.push_back(0);  // the item count, set by Add
    Short(0);
    Short(0);
    Short(cx);
    Short(cy);
    w_.push_back(0);  // no menu
    w_.push_back(0);  // the dialog class
    String(title);
    w_.push_back(8);
    String(L"MS Shell Dlg");
  }
  enum { kButton = 0x80, kEdit = 0x81, kStatic = 0x82, kCombo = 0x85 };
  void Add(int cls, DWORD style, int x, int y, int cx, int cy, int id, const wchar_t* text) {
    if (w_.size() & 1) w_.push_back(0);
    Dword(style | WS_CHILD | WS_VISIBLE);
    Dword(0);
    Short(x);
    Short(y);
    Short(cx);
    Short(cy);
    w_.push_back((WORD)id);
    w_.push_back(0xFFFF);
    w_.push_back((WORD)cls);
    String(text);
    w_.push_back(0);
    w_[4]++;
  }
  LPCDLGTEMPLATEW Get() const { return (LPCDLGTEMPLATEW)w_.data(); }

 private:
  void Dword(DWORD d) {
    w_.push_back((WORD)d);
    w_.push_back((WORD)(d >> 16));
  }
  void Short(int v) { w_.push_back((WORD)(short)v); }
  void String(const wchar_t* s) {
    for (; *s; s++) w_.push_back((WORD)*s);
    w_.push_back(0);
  }
  std::vector<WORD> w_;
};

void Label(Template& t, int x, int y, int cx, int id, const wchar_t* text) {
  t.Add(Template::kStatic, SS_LEFT, x, y, cx, 8, id, text);
}
void Group(Template& t, int x, int y, int cx, int cy, int id, const wchar_t* text) {
  t.Add(Template::kButton, BS_GROUPBOX, x, y, cx, cy, id, text);
}
void OkCancel(Template& t, int x, int y) {
  t.Add(Template::kButton, BS_DEFPUSHBUTTON | WS_TABSTOP | WS_GROUP, x, y, 50, 14, IDOK, L"OK");
  t.Add(Template::kButton, BS_PUSHBUTTON | WS_TABSTOP, x + 56, y, 50, 14, IDCANCEL, L"Cancel");
}

// DEVNAMES and DEVMODE globals, in the A or W form.
bool ReadDevNames(HGLOBAL h, bool ansi, std::wstring* device) {
  if (!h) return false;
  const DEVNAMES* dn = (const DEVNAMES*)GlobalLock(h);
  if (!dn) return false;
  if (ansi)
    *device = Widen((const char*)dn + dn->wDeviceOffset);
  else
    *device = (const WCHAR*)dn + dn->wDeviceOffset;
  GlobalUnlock(h);
  return true;
}

bool ReadDevMode(HGLOBAL h, bool ansi, DEVMODEW* dm) {
  if (!h) return false;
  const void* p = GlobalLock(h);
  if (!p) return false;
  if (ansi) {
    *dm = DevModeFromA((const DEVMODEA*)p);
  } else {
    const DEVMODEW* in = (const DEVMODEW*)p;
    memset(dm, 0, sizeof *dm);
    size_t n = in->dmSize ? in->dmSize : sizeof *dm;
    memcpy(dm, in, n < sizeof *dm ? n : sizeof *dm);
  }
  GlobalUnlock(h);
  return true;
}

// Fills a global, reusing the caller's when there is one.
HGLOBAL Fill(HGLOBAL old, const void* data, size_t n) {
  HGLOBAL h = old ? GlobalReAlloc(old, n, GMEM_MOVEABLE) : NULL;
  if (!h) {
    if (old) GlobalFree(old);
    h = GlobalAlloc(GMEM_MOVEABLE, n);
  }
  if (!h) return NULL;
  memcpy(GlobalLock(h), data, n);
  GlobalUnlock(h);
  return h;
}

HGLOBAL WriteDevNames(HGLOBAL old, bool ansi, const Printer& p, bool isDefault) {
  const wchar_t* driver = L"winspool";
  std::vector<BYTE> buf;
  DEVNAMES dn;
  dn.wDefault = isDefault ? DN_DEFAULTPRN : 0;
  if (ansi) {
    std::string d = Narrow(driver), n = Narrow(p.name.c_str()), o = Narrow(p.port.c_str());
    dn.wDriverOffset = sizeof dn;
    dn.wDeviceOffset = (WORD)(dn.wDriverOffset + d.size() + 1);
    dn.wOutputOffset = (WORD)(dn.wDeviceOffset + n.size() + 1);
    buf.resize(dn.wOutputOffset + o.size() + 1);
    memcpy(buf.data(), &dn, sizeof dn);
    memcpy(&buf[dn.wDriverOffset], d.c_str(), d.size() + 1);
    memcpy(&buf[dn.wDeviceOffset], n.c_str(), n.size() + 1);
    memcpy(&buf[dn.wOutputOffset], o.c_str(), o.size() + 1);
  } else {
    std::wstring d = driver;
    size_t head = (sizeof dn + sizeof(WCHAR) - 1) / sizeof(WCHAR);
    dn.wDriverOffset = (WORD)head;
    dn.wDeviceOffset = (WORD)(head + d.size() + 1);
    dn.wOutputOffset = (WORD)(dn.wDeviceOffset + p.name.size() + 1);
    buf.resize((dn.wOutputOffset + p.port.size() + 1) * sizeof(WCHAR));
    memcpy(buf.data(), &dn, sizeof dn);
    WCHAR* w = (WCHAR*)buf.data();
    memcpy(w + dn.wDriverOffset, d.c_str(), (d.size() + 1) * sizeof(WCHAR));
    memcpy(w + dn.wDeviceOffset, p.name.c_str(), (p.name.size() + 1) * sizeof(WCHAR));
    memcpy(w + dn.wOutputOffset, p.port.c_str(), (p.port.size() + 1) * sizeof(WCHAR));
  }
  return Fill(old, buf.data(), buf.size());
}

HGLOBAL WriteDevMode(HGLOBAL old, bool ansi, const DEVMODEW& dm) {
  if (ansi) {
    DEVMODEA a = DevModeToA(dm);
    return Fill(old, &a, sizeof a);
  }
  return Fill(old, &dm, sizeof dm);
}

// The printer the caller's DEVNAMES or DEVMODE names, else the default.
size_t ChoosePrinter(HGLOBAL devNames, HGLOBAL devMode, bool ansi) {
  const PrinterList& list = Printers();
  std::wstring name;
  DEVMODEW dm;
  if (!ReadDevNames(devNames, ansi, &name) && ReadDevMode(devMode, ansi, &dm)) name = dm.dmDeviceName;
  for (size_t i = 0; i < list.printers.size(); i++)
    if (!name.empty() && _wcsicmp(list.printers[i].name.c_str(), name.c_str()) == 0) return i;
  return list.def;
}

// The settings for printer i: the caller's when they are for it.
DEVMODEW SettingsFor(size_t i, HGLOBAL devMode, bool ansi) {
  const Printer& p = Printers().printers[i];
  DEVMODEW dm;
  if (ReadDevMode(devMode, ansi, &dm)) return MergeDevMode(p.name, &dm);
  return DefaultDevMode(p.name);
}

void FillPrinters(HWND h, int id, size_t sel) {
  const PrinterList& list = Printers();
  for (const Printer& p : list.printers) SendDlgItemMessageW(h, id, CB_ADDSTRING, 0, (LPARAM)p.name.c_str());
  SendDlgItemMessageW(h, id, CB_SETCURSEL, sel, 0);
}

void FillPapers(HWND h, int id, const DEVMODEW& dm) {
  SendDlgItemMessageW(h, id, CB_RESETCONTENT, 0, 0);
  int sel = -1;
  for (int i = 0; i < kPaperCount; i++) {
    SendDlgItemMessageW(h, id, CB_ADDSTRING, 0, (LPARAM)kPapers[i].name);
    if (kPapers[i].id == dm.dmPaperSize) sel = i;
  }
  if (sel < 0) {
    SendDlgItemMessageW(h, id, CB_ADDSTRING, 0, (LPARAM)L"Custom");
    sel = kPaperCount;
  }
  SendDlgItemMessageW(h, id, CB_SETCURSEL, sel, 0);
}

void ReadPaper(HWND h, int id, DEVMODEW* dm) {
  int i = (int)SendDlgItemMessageW(h, id, CB_GETCURSEL, 0, 0);
  if (i < 0 || i >= kPaperCount) return;
  dm->dmPaperSize = kPapers[i].id;
  dm->dmPaperWidth = kPapers[i].width;
  dm->dmPaperLength = kPapers[i].length;
}

void ShowWhere(HWND h, size_t i) {
  const Printer& p = Printers().printers[i];
  SetDlgItemTextW(h, stc12, p.cups ? L"CUPS" : L"A PDF file");
}

// ---------------------------------------------------------------------
// Print and Print Setup

struct PrintState {
  PRINTDLGW* pd;  // or PRINTDLGA, the same but for its strings
  bool ansi;
  bool setup;
  size_t printer;
  DEVMODEW dm;
};

UINT_PTR CallHook(PrintState* st, HWND h, UINT msg, WPARAM wp, LPARAM lp) {
  PRINTDLGW* pd = st->pd;
  if (st->setup) {
    if ((pd->Flags & PD_ENABLESETUPHOOK) && pd->lpfnSetupHook) return pd->lpfnSetupHook(h, msg, wp, lp);
  } else if ((pd->Flags & PD_ENABLEPRINTHOOK) && pd->lpfnPrintHook) {
    return pd->lpfnPrintHook(h, msg, wp, lp);
  }
  return 0;
}

bool PrintOk(HWND h, PrintState* st) {
  PRINTDLGW* pd = st->pd;
  DEVMODEW& dm = st->dm;
  if (st->setup) {
    ReadPaper(h, cmb2, &dm);
    dm.dmOrientation = IsDlgButtonChecked(h, rad2) ? DMORIENT_LANDSCAPE : DMORIENT_PORTRAIT;
  } else {
    BOOL ok1 = TRUE, ok2 = TRUE;
    bool pages = IsDlgButtonChecked(h, rad3) == BST_CHECKED;
    UINT from = GetDlgItemInt(h, edt1, &ok1, FALSE), to = GetDlgItemInt(h, edt2, &ok2, FALSE);
    if (pages) {
      if (!ok2 && ok1) to = from, ok2 = TRUE;
      if (!ok1 || !ok2 || from > to || from < pd->nMinPage || to > pd->nMaxPage) {
        wchar_t msg[96], lo[16], hi[16];
        wcscpy(lo, Widen(std::to_string(pd->nMinPage).c_str()).c_str());
        wcscpy(hi, Widen(std::to_string(pd->nMaxPage).c_str()).c_str());
        wcscpy(msg, L"Enter pages from ");
        wcscat(msg, lo);
        wcscat(msg, L" to ");
        wcscat(msg, hi);
        wcscat(msg, L".");
        MessageBoxW(h, msg, L"Print", MB_OK | MB_ICONEXCLAMATION);
        SetFocus(GetDlgItem(h, edt1));
        SendDlgItemMessageW(h, edt1, EM_SETSEL, 0, -1);
        return false;
      }
    }
    BOOL okc = TRUE;
    UINT copies = GetDlgItemInt(h, edt3, &okc, FALSE);
    if (!okc || copies < 1 || copies > 9999) {
      MessageBoxW(h, L"Enter a number of copies from 1 to 9999.", L"Print", MB_OK | MB_ICONEXCLAMATION);
      SetFocus(GetDlgItem(h, edt3));
      SendDlgItemMessageW(h, edt3, EM_SETSEL, 0, -1);
      return false;
    }
    pd->Flags &= ~(PD_PAGENUMS | PD_SELECTION | PD_PRINTTOFILE | PD_COLLATE);
    if (pages) {
      pd->Flags |= PD_PAGENUMS;
      pd->nFromPage = (WORD)from;
      pd->nToPage = (WORD)to;
    } else if (IsDlgButtonChecked(h, rad2) == BST_CHECKED) {
      pd->Flags |= PD_SELECTION;
    }
    if (IsDlgButtonChecked(h, chx1) == BST_CHECKED) pd->Flags |= PD_PRINTTOFILE;
    bool collate = IsDlgButtonChecked(h, chx2) == BST_CHECKED;
    if (pd->Flags & PD_USEDEVMODECOPIES) {
      dm.dmCopies = (short)copies;
      dm.dmCollate = collate ? DMCOLLATE_TRUE : DMCOLLATE_FALSE;
      pd->nCopies = 1;
    } else {
      dm.dmCopies = 1;
      pd->nCopies = (WORD)copies;
      if (collate) pd->Flags |= PD_COLLATE;
    }
  }
  return true;
}

INT_PTR CALLBACK PrintProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
  PrintState* st = (PrintState*)GetWindowLongPtrW(h, DWLP_USER);
  if (msg == WM_INITDIALOG) {
    st = (PrintState*)lp;
    SetWindowLongPtrW(h, DWLP_USER, (LONG_PTR)st);
    PRINTDLGW* pd = st->pd;
    FillPrinters(h, st->setup ? cmb1 : cmb4, st->printer);
    ShowWhere(h, st->printer);
    if (st->setup) {
      FillPapers(h, cmb2, st->dm);
      CheckRadioButton(h, rad1, rad2, st->dm.dmOrientation == DMORIENT_LANDSCAPE ? rad2 : rad1);
    } else {
      int range = (pd->Flags & PD_PAGENUMS) ? rad3 : (pd->Flags & PD_SELECTION) ? rad2 : rad1;
      CheckRadioButton(h, rad1, rad3, range);
      if (pd->Flags & PD_NOSELECTION) EnableWindow(GetDlgItem(h, rad2), FALSE);
      if (pd->Flags & PD_NOPAGENUMS) {
        static const int ids[] = {rad3, edt1, edt2, stc2, stc3};
        for (int id : ids) EnableWindow(GetDlgItem(h, id), FALSE);
      }
      if (pd->nFromPage != 0xFFFF) SetDlgItemInt(h, edt1, pd->nFromPage, FALSE);
      if (pd->nToPage != 0xFFFF) SetDlgItemInt(h, edt2, pd->nToPage, FALSE);
      bool devCopies = (pd->Flags & PD_USEDEVMODECOPIES) != 0;
      SetDlgItemInt(h, edt3, devCopies ? st->dm.dmCopies : (pd->nCopies ? pd->nCopies : 1), FALSE);
      bool collate = devCopies ? st->dm.dmCollate == DMCOLLATE_TRUE : (pd->Flags & PD_COLLATE) != 0;
      CheckDlgButton(h, chx2, collate ? BST_CHECKED : BST_UNCHECKED);
      CheckDlgButton(h, chx1, (pd->Flags & PD_PRINTTOFILE) ? BST_CHECKED : BST_UNCHECKED);
      if (pd->Flags & PD_HIDEPRINTTOFILE) ShowWindow(GetDlgItem(h, chx1), SW_HIDE);
      if (pd->Flags & PD_DISABLEPRINTTOFILE) EnableWindow(GetDlgItem(h, chx1), FALSE);
    }
    if (pd->Flags & (st->setup ? PD_ENABLESETUPHOOK : PD_ENABLEPRINTHOOK))
      return (INT_PTR)CallHook(st, h, msg, wp, (LPARAM)pd);
    return TRUE;
  }
  if (!st) return FALSE;
  if (CallHook(st, h, msg, wp, lp)) return TRUE;
  if (msg != WM_COMMAND) return FALSE;
  int id = LOWORD(wp), code = HIWORD(wp);
  switch (id) {
    case cmb1:
    case cmb4:
      if (code == CBN_SELCHANGE) {
        LRESULT i = SendDlgItemMessageW(h, id, CB_GETCURSEL, 0, 0);
        if (i >= 0 && (size_t)i < Printers().printers.size() && (size_t)i != st->printer) {
          DEVMODEW old = st->dm;
          st->printer = (size_t)i;
          st->dm = MergeDevMode(Printers().printers[i].name, &old);
          ShowWhere(h, st->printer);
        }
      }
      return TRUE;
    case edt1:
    case edt2:
      if (code == EN_CHANGE && GetFocus() == GetDlgItem(h, id)) CheckRadioButton(h, rad1, rad3, rad3);
      return TRUE;
    case IDOK:
      if (PrintOk(h, st)) EndDialog(h, IDOK);
      return TRUE;
    case IDCANCEL:
      EndDialog(h, IDCANCEL);
      return TRUE;
  }
  return FALSE;
}

Template PrintTemplate() {
  Template t(0, 280, 168, L"Print");
  Group(t, 7, 4, 266, 44, grp4, L"Printer");
  Label(t, 14, 18, 34, stc6, L"&Name:");
  t.Add(Template::kCombo, CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP | WS_GROUP, 50, 16, 160, 100, cmb4, L"");
  Label(t, 14, 33, 34, stc7, L"Where:");
  Label(t, 50, 33, 160, stc12, L"");
  Group(t, 7, 52, 130, 72, grp1, L"Print range");
  t.Add(Template::kButton, BS_AUTORADIOBUTTON | WS_GROUP | WS_TABSTOP, 14, 64, 110, 10, rad1, L"&All");
  t.Add(Template::kButton, BS_AUTORADIOBUTTON, 14, 78, 110, 10, rad3, L"Pa&ges");
  Label(t, 24, 94, 22, stc2, L"&from:");
  t.Add(Template::kEdit, ES_NUMBER | WS_BORDER | WS_TABSTOP | WS_GROUP, 48, 92, 28, 12, edt1, L"");
  Label(t, 80, 94, 14, stc3, L"&to:");
  t.Add(Template::kEdit, ES_NUMBER | WS_BORDER | WS_TABSTOP, 96, 92, 28, 12, edt2, L"");
  t.Add(Template::kButton, BS_AUTORADIOBUTTON | WS_GROUP | WS_TABSTOP, 14, 108, 110, 10, rad2, L"&Selection");
  Group(t, 143, 52, 130, 72, grp2, L"Copies");
  Label(t, 150, 66, 76, stc5, L"Number of &copies:");
  t.Add(Template::kEdit, ES_NUMBER | WS_BORDER | WS_TABSTOP | WS_GROUP, 230, 64, 34, 12, edt3, L"");
  t.Add(Template::kButton, BS_AUTOCHECKBOX | WS_TABSTOP, 150, 84, 100, 10, chx2, L"C&ollate");
  t.Add(Template::kButton, BS_AUTOCHECKBOX | WS_TABSTOP, 7, 130, 120, 10, chx1, L"Print to fi&le");
  OkCancel(t, 167, 148);
  return t;
}

Template SetupTemplate() {
  Template t(0, 280, 124, L"Print Setup");
  Group(t, 7, 4, 266, 44, grp4, L"Printer");
  Label(t, 14, 18, 34, stc6, L"&Name:");
  t.Add(Template::kCombo, CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP | WS_GROUP, 50, 16, 160, 100, cmb1, L"");
  Label(t, 14, 33, 34, stc7, L"Where:");
  Label(t, 50, 33, 160, stc12, L"");
  Group(t, 7, 52, 170, 44, grp2, L"Paper");
  Label(t, 14, 68, 30, stc2, L"Si&ze:");
  t.Add(Template::kCombo, CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP | WS_GROUP, 46, 66, 124, 100, cmb2, L"");
  Group(t, 183, 52, 90, 44, grp1, L"Orientation");
  t.Add(Template::kButton, BS_AUTORADIOBUTTON | WS_GROUP | WS_TABSTOP, 190, 64, 76, 10, rad1, L"Po&rtrait");
  t.Add(Template::kButton, BS_AUTORADIOBUTTON, 190, 78, 76, 10, rad2, L"L&andscape");
  OkCancel(t, 167, 104);
  return t;
}

BOOL PrintDlgCore(PRINTDLGW* pd, bool ansi) {
  tlsDlgError = 0;
  if (!pd || pd->lStructSize != sizeof(PRINTDLGW)) {
    tlsDlgError = CDERR_STRUCTSIZE;
    return FALSE;
  }
  const PrinterList& list = Printers();
  if (pd->Flags & PD_RETURNDEFAULT) {
    if (pd->hDevMode || pd->hDevNames) {
      tlsDlgError = PDERR_RETDEFFAILURE;
      return FALSE;
    }
    const Printer& p = list.printers[list.def];
    pd->hDevNames = WriteDevNames(NULL, ansi, p, true);
    pd->hDevMode = WriteDevMode(NULL, ansi, DefaultDevMode(p.name));
  } else {
    PrintState st;
    st.pd = pd;
    st.ansi = ansi;
    st.setup = (pd->Flags & PD_PRINTSETUP) != 0;
    st.printer = ChoosePrinter(pd->hDevNames, pd->hDevMode, ansi);
    st.dm = SettingsFor(st.printer, pd->hDevMode, ansi);
    if (!(pd->Flags & PD_USEDEVMODECOPIES) && pd->hDevMode) st.dm.dmCopies = pd->nCopies ? pd->nCopies : 1;
    Template t = st.setup ? SetupTemplate() : PrintTemplate();
    INT_PTR r = DialogBoxIndirectParamW(NULL, t.Get(), pd->hwndOwner, PrintProc, (LPARAM)&st);
    if (r != IDOK) {
      if (r < 0) tlsDlgError = CDERR_DIALOGFAILURE;
      return FALSE;
    }
    const Printer& p = list.printers[st.printer];
    pd->hDevNames = WriteDevNames(pd->hDevNames, ansi, p, st.printer == list.def);
    pd->hDevMode = WriteDevMode(pd->hDevMode, ansi, st.dm);
  }
  if (pd->Flags & (PD_RETURNDC | PD_RETURNIC)) {
    std::wstring name;
    ReadDevNames(pd->hDevNames, ansi, &name);
    DEVMODEW dm;
    ReadDevMode(pd->hDevMode, ansi, &dm);
    pd->hDC = (pd->Flags & PD_RETURNDC) ? CreateDCW(L"winspool", name.c_str(), NULL, &dm)
                                        : CreateICW(L"winspool", name.c_str(), NULL, &dm);
    if (!pd->hDC) {
      tlsDlgError = PDERR_CREATEICFAILURE;
      return FALSE;
    }
  }
  return TRUE;
}

// ---------------------------------------------------------------------
// Page Setup

struct PageState {
  PAGESETUPDLGW* psd;
  bool ansi;
  bool mm;  // hundredths of millimetres, else thousandths of inches
  size_t printer;
  DEVMODEW dm;
  RECT margins;
};

POINT PaperIn(const DEVMODEW& dm, bool mm) {
  int w = dm.dmPaperWidth, h = dm.dmPaperLength;  // tenths of a millimetre
  if (dm.dmOrientation == DMORIENT_LANDSCAPE) {
    int t = w;
    w = h;
    h = t;
  }
  POINT p;
  if (mm) {
    p.x = w * 10;
    p.y = h * 10;
  } else {
    p.x = (w * 1000 + 127) / 254;
    p.y = (h * 1000 + 127) / 254;
  }
  return p;
}

void SetMargin(HWND h, int id, LONG v, bool mm) {
  char buf[32];
  snprintf(buf, sizeof buf, mm ? "%.1f" : "%.2f", mm ? v / 100.0 : v / 1000.0);
  SetDlgItemTextW(h, id, Widen(buf).c_str());
}

bool GetMargin(HWND h, int id, bool mm, LONG* v) {
  wchar_t buf[32];
  GetDlgItemTextW(h, id, buf, 32);
  std::string s = Narrow(buf);
  for (char& c : s)
    if (c == ',') c = '.';
  char* end;
  double d = strtod(s.c_str(), &end);
  while (*end == ' ') end++;
  if (end == s.c_str() || *end || d < 0) return false;
  *v = (LONG)(d * (mm ? 100 : 1000) + 0.5);
  return true;
}

UINT_PTR CallPageHook(PageState* st, HWND h, UINT msg, WPARAM wp, LPARAM lp) {
  if ((st->psd->Flags & PSD_ENABLEPAGESETUPHOOK) && st->psd->lpfnPageSetupHook)
    return st->psd->lpfnPageSetupHook(h, msg, wp, lp);
  return 0;
}

// The sample page, in the rct1 frame's place.
void PaintSample(HWND h, PageState* st, HDC dc) {
  HWND frame = GetDlgItem(h, rct1);
  if (!frame) return;
  RECT box;
  GetWindowRect(frame, &box);
  MapWindowPoints(NULL, h, (POINT*)&box, 2);
  POINT paper = PaperIn(st->dm, st->mm);
  int bw = box.right - box.left - 8, bh = box.bottom - box.top - 8;
  int pw = bw, ph = (int)((double)bw * paper.y / paper.x);
  if (ph > bh) {
    ph = bh;
    pw = (int)((double)bh * paper.x / paper.y);
  }
  RECT page = {box.left + (box.right - box.left - pw) / 2, box.top + (box.bottom - box.top - ph) / 2, 0, 0};
  page.right = page.left + pw;
  page.bottom = page.top + ph;
  RECT shadow = {page.left + 3, page.top + 3, page.right + 3, page.bottom + 3};
  FillRect(dc, &shadow, GetSysColorBrush(COLOR_3DSHADOW));
  FillRect(dc, &page, (HBRUSH)GetStockObject(WHITE_BRUSH));
  FrameRect(dc, &page, (HBRUSH)GetStockObject(BLACK_BRUSH));
  RECT m = st->margins;
  RECT text = {page.left + (int)((double)pw * m.left / paper.x), page.top + (int)((double)ph * m.top / paper.y),
               page.right - (int)((double)pw * m.right / paper.x), page.bottom - (int)((double)ph * m.bottom / paper.y)};
  HBRUSH gray = GetSysColorBrush(COLOR_3DSHADOW);
  for (int y = text.top + 1; y + 1 < text.bottom; y += 4) {
    RECT line = {text.left, y, text.right, y + 2};
    if (line.right > line.left) FillRect(dc, &line, gray);
  }
}

bool PageOk(HWND h, PageState* st) {
  ReadPaper(h, cmb2, &st->dm);
  st->dm.dmOrientation = IsDlgButtonChecked(h, rad2) ? DMORIENT_LANDSCAPE : DMORIENT_PORTRAIT;
  RECT m;
  static const int ids[] = {edt4, edt5, edt6, edt7};
  LONG* v[] = {&m.left, &m.top, &m.right, &m.bottom};
  for (int i = 0; i < 4; i++) {
    if (!GetMargin(h, ids[i], st->mm, v[i])) {
      MessageBoxW(h, L"Enter a number for the margin.", L"Page Setup", MB_OK | MB_ICONEXCLAMATION);
      SetFocus(GetDlgItem(h, ids[i]));
      SendDlgItemMessageW(h, ids[i], EM_SETSEL, 0, -1);
      return false;
    }
  }
  POINT paper = PaperIn(st->dm, st->mm);
  const RECT& min = st->psd->rtMinMargin;
  if (m.left + m.right >= paper.x || m.top + m.bottom >= paper.y || m.left < min.left || m.top < min.top ||
      m.right < min.right || m.bottom < min.bottom) {
    MessageBoxW(h, L"The margins do not fit the page.", L"Page Setup", MB_OK | MB_ICONEXCLAMATION);
    SetFocus(GetDlgItem(h, edt4));
    return false;
  }
  st->margins = m;
  return true;
}

void ShowMargins(HWND h, PageState* st) {
  SetMargin(h, edt4, st->margins.left, st->mm);
  SetMargin(h, edt5, st->margins.top, st->mm);
  SetMargin(h, edt6, st->margins.right, st->mm);
  SetMargin(h, edt7, st->margins.bottom, st->mm);
}

INT_PTR CALLBACK PageProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
  PageState* st = (PageState*)GetWindowLongPtrW(h, DWLP_USER);
  if (msg == WM_INITDIALOG) {
    st = (PageState*)lp;
    SetWindowLongPtrW(h, DWLP_USER, (LONG_PTR)st);
    PAGESETUPDLGW* psd = st->psd;
    FillPapers(h, cmb2, st->dm);
    CheckRadioButton(h, rad1, rad2, st->dm.dmOrientation == DMORIENT_LANDSCAPE ? rad2 : rad1);
    ShowMargins(h, st);
    if (psd->Flags & PSD_DISABLEPAPER) EnableWindow(GetDlgItem(h, cmb2), FALSE);
    if (psd->Flags & PSD_DISABLEORIENTATION) {
      EnableWindow(GetDlgItem(h, rad1), FALSE);
      EnableWindow(GetDlgItem(h, rad2), FALSE);
    }
    if (psd->Flags & PSD_DISABLEMARGINS) {
      static const int ids[] = {edt4, edt5, edt6, edt7};
      for (int id : ids) EnableWindow(GetDlgItem(h, id), FALSE);
    }
    if (psd->Flags & PSD_DISABLEPRINTER) EnableWindow(GetDlgItem(h, psh3), FALSE);
    if (psd->Flags & PSD_ENABLEPAGESETUPHOOK) return (INT_PTR)CallPageHook(st, h, msg, wp, (LPARAM)psd);
    return TRUE;
  }
  if (!st) return FALSE;
  if (CallPageHook(st, h, msg, wp, lp)) return TRUE;
  switch (msg) {
    case WM_PAINT:
      if (!(st->psd->Flags & PSD_DISABLEPAGEPAINTING)) {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(h, &ps);
        PaintSample(h, st, dc);
        EndPaint(h, &ps);
        return TRUE;
      }
      return FALSE;
    case WM_COMMAND: {
      int id = LOWORD(wp), code = HIWORD(wp);
      RECT box;
      HWND frame = GetDlgItem(h, rct1);
      if (frame) {
        GetWindowRect(frame, &box);
        MapWindowPoints(NULL, h, (POINT*)&box, 2);
      }
      switch (id) {
        case cmb2:
          if (code == CBN_SELCHANGE) {
            ReadPaper(h, cmb2, &st->dm);
            if (frame) InvalidateRect(h, &box, TRUE);
          }
          return TRUE;
        case rad1:
        case rad2:
          if (code == BN_CLICKED) {
            short o = id == rad2 ? DMORIENT_LANDSCAPE : DMORIENT_PORTRAIT;
            if (o != st->dm.dmOrientation) {
              // The margins turn with the page.
              RECT m = st->margins;
              st->margins = o == DMORIENT_LANDSCAPE ? RECT{m.top, m.right, m.bottom, m.left}
                                                     : RECT{m.bottom, m.left, m.top, m.right};
              st->dm.dmOrientation = o;
              ShowMargins(h, st);
            }
            if (frame) InvalidateRect(h, &box, TRUE);
          }
          return TRUE;
        case edt4:
        case edt5:
        case edt6:
        case edt7:
          if (code == EN_CHANGE) {
            LONG v;
            LONG* f[] = {&st->margins.left, &st->margins.top, &st->margins.right, &st->margins.bottom};
            if (GetMargin(h, id, st->mm, &v)) *f[id - edt4] = v;
            if (frame) InvalidateRect(h, &box, TRUE);
          }
          return TRUE;
        case psh3: {
          // Printer...: Print Setup for the page's printer.
          PRINTDLGW pd;
          memset(&pd, 0, sizeof pd);
          pd.lStructSize = sizeof pd;
          pd.hwndOwner = h;
          pd.Flags = PD_PRINTSETUP;
          ReadPaper(h, cmb2, &st->dm);
          pd.hDevNames = WriteDevNames(NULL, false, Printers().printers[st->printer], st->printer == Printers().def);
          pd.hDevMode = WriteDevMode(NULL, false, st->dm);
          if (PrintDlgCore(&pd, false)) {
            st->printer = ChoosePrinter(pd.hDevNames, NULL, false);
            st->dm = SettingsFor(st->printer, pd.hDevMode, false);
            FillPapers(h, cmb2, st->dm);
            CheckRadioButton(h, rad1, rad2, st->dm.dmOrientation == DMORIENT_LANDSCAPE ? rad2 : rad1);
            if (frame) InvalidateRect(h, &box, TRUE);
          }
          GlobalFree(pd.hDevNames);
          GlobalFree(pd.hDevMode);
          return TRUE;
        }
        case IDOK:
          if (PageOk(h, st)) EndDialog(h, IDOK);
          return TRUE;
        case IDCANCEL:
          EndDialog(h, IDCANCEL);
          return TRUE;
      }
      return FALSE;
    }
  }
  return FALSE;
}

Template PageTemplate(bool mm) {
  Template t(0, 292, 146, L"Page Setup");
  Group(t, 7, 4, 180, 40, grp2, L"Paper");
  Label(t, 14, 20, 30, stc2, L"Si&ze:");
  t.Add(Template::kCombo, CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP | WS_GROUP, 46, 18, 132, 100, cmb2, L"");
  Group(t, 7, 50, 70, 48, grp1, L"Orientation");
  t.Add(Template::kButton, BS_AUTORADIOBUTTON | WS_GROUP | WS_TABSTOP, 14, 64, 58, 10, rad1, L"P&ortrait");
  t.Add(Template::kButton, BS_AUTORADIOBUTTON, 14, 80, 58, 10, rad2, L"L&andscape");
  Group(t, 83, 50, 104, 48, grp4, mm ? L"Margins (millimeters)" : L"Margins (inches)");
  Label(t, 90, 64, 22, stc15, L"&Left:");
  t.Add(Template::kEdit, ES_AUTOHSCROLL | WS_BORDER | WS_TABSTOP | WS_GROUP, 112, 62, 24, 12, edt4, L"");
  Label(t, 140, 64, 22, stc16, L"&Right:");
  t.Add(Template::kEdit, ES_AUTOHSCROLL | WS_BORDER | WS_TABSTOP, 160, 62, 24, 12, edt6, L"");
  Label(t, 90, 80, 22, stc13, L"&Top:");
  t.Add(Template::kEdit, ES_AUTOHSCROLL | WS_BORDER | WS_TABSTOP, 112, 78, 24, 12, edt5, L"");
  Label(t, 140, 80, 22, stc14, L"&Bottom:");
  t.Add(Template::kEdit, ES_AUTOHSCROLL | WS_BORDER | WS_TABSTOP, 160, 78, 24, 12, edt7, L"");
  t.Add(Template::kStatic, SS_LEFT, 193, 4, 92, 94, rct1, L"");
  t.Add(Template::kButton, BS_PUSHBUTTON | WS_TABSTOP | WS_GROUP, 7, 124, 60, 14, psh3, L"&Printer...");
  OkCancel(t, 179, 124);
  return t;
}

BOOL PageSetupDlgCore(PAGESETUPDLGW* psd, bool ansi) {
  tlsDlgError = 0;
  if (!psd || psd->lStructSize != sizeof(PAGESETUPDLGW)) {
    tlsDlgError = CDERR_STRUCTSIZE;
    return FALSE;
  }
  const PrinterList& list = Printers();
  PageState st;
  st.psd = psd;
  st.ansi = ansi;
  if (psd->Flags & PSD_INHUNDREDTHSOFMILLIMETERS)
    st.mm = true;
  else if (psd->Flags & PSD_INTHOUSANDTHSOFINCHES)
    st.mm = false;
  else
    st.mm = Metric();
  bool ret = (psd->Flags & PSD_RETURNDEFAULT) != 0;
  if (ret && (psd->hDevMode || psd->hDevNames)) {
    tlsDlgError = PDERR_RETDEFFAILURE;
    return FALSE;
  }
  st.printer = ret ? list.def : ChoosePrinter(psd->hDevNames, psd->hDevMode, ansi);
  st.dm = ret ? DefaultDevMode(list.printers[st.printer].name) : SettingsFor(st.printer, psd->hDevMode, ansi);
  if (!(psd->Flags & PSD_MINMARGINS)) {
    LONG m = st.mm ? kUnprintable * 254 / 100 : kUnprintable;
    psd->rtMinMargin = RECT{m, m, m, m};
  }
  if (psd->Flags & PSD_MARGINS) {
    st.margins = psd->rtMargin;
  } else {
    LONG m = st.mm ? 2500 : 1000;
    st.margins = RECT{m, m, m, m};
  }
  if (!ret) {
    Template t = PageTemplate(st.mm);
    INT_PTR r = DialogBoxIndirectParamW(NULL, t.Get(), psd->hwndOwner, PageProc, (LPARAM)&st);
    if (r != IDOK) {
      if (r < 0) tlsDlgError = CDERR_DIALOGFAILURE;
      return FALSE;
    }
  }
  const Printer& p = list.printers[st.printer];
  psd->hDevNames = WriteDevNames(psd->hDevNames, ansi, p, st.printer == list.def);
  psd->hDevMode = WriteDevMode(psd->hDevMode, ansi, st.dm);
  psd->ptPaperSize = PaperIn(st.dm, st.mm);
  psd->rtMargin = st.margins;
  psd->Flags &= ~(PSD_INHUNDREDTHSOFMILLIMETERS | PSD_INTHOUSANDTHSOFINCHES);
  psd->Flags |= st.mm ? PSD_INHUNDREDTHSOFMILLIMETERS : PSD_INTHOUSANDTHSOFINCHES;
  return TRUE;
}

}  // namespace

static_assert(sizeof(PRINTDLGA) == sizeof(PRINTDLGW), "PRINTDLG layouts");
static_assert(sizeof(PAGESETUPDLGA) == sizeof(PAGESETUPDLGW), "PAGESETUPDLG layouts");

extern "C" {

BOOL WINAPI PrintDlgW(LPPRINTDLGW pd) { return PrintDlgCore(pd, false); }
BOOL WINAPI PrintDlgA(LPPRINTDLGA pd) {
  if (pd && pd->lStructSize != sizeof(PRINTDLGA)) {
    tlsDlgError = CDERR_STRUCTSIZE;
    return FALSE;
  }
  if (pd) pd->lStructSize = sizeof(PRINTDLGW);
  BOOL r = PrintDlgCore((PRINTDLGW*)pd, true);
  if (pd) pd->lStructSize = sizeof(PRINTDLGA);
  return r;
}

BOOL WINAPI PageSetupDlgW(LPPAGESETUPDLGW psd) { return PageSetupDlgCore(psd, false); }
BOOL WINAPI PageSetupDlgA(LPPAGESETUPDLGA psd) {
  if (psd && psd->lStructSize != sizeof(PAGESETUPDLGA)) {
    tlsDlgError = CDERR_STRUCTSIZE;
    return FALSE;
  }
  if (psd) psd->lStructSize = sizeof(PAGESETUPDLGW);
  BOOL r = PageSetupDlgCore((PAGESETUPDLGW*)psd, true);
  if (psd) psd->lStructSize = sizeof(PAGESETUPDLGA);
  return r;
}

DWORD WINAPI CommDlgExtendedError(void) { return tlsDlgError; }

}  // extern "C"
