# Builds the MFC libraries with clang-retro.
#
#   make BIN=/path/to/llvm/bin SYSROOT=/path/to/sysroot TARGET=<triple>
#   make BIN=/path/to/llvm/bin CFG=/path/to/win32-sdk/cfg/win32-picolibc.cfg
#
# With CFG, the clang config file gives the target, sysroot and libraries
# (as win32-sdk's config files do), and the output goes to build/<name>.
#
# Produces the static libraries $(OUT)/libmfc.a (ANSI) and
# $(OUT)/libmfcu.a (Unicode), and the shared library for programs built
# with _AFXDLL: $(OUT)/mfc.dll and mfcu.dll, their import libraries
# libmfcdll.a and libmfcudll.a, and libmfcs.a and libmfcus.a, the part
# each module links itself. `make static` builds only the static ones.
# `make install PREFIX=dir` copies the headers to dir/include/mfc, the
# libraries to dir/lib and the DLLs to dir/bin; for Linux and macOS, the
# headers to dir/include/libmfc, the static libraries to dir/lib, the
# shared ones to dir/lib/libmfc-shared, res2cpp.py to dir/share/libmfc,
# and the pkg-config (libmfc, libmfcu, libmfc-shared, libmfcu-shared)
# and CMake (libmfc) packages to dir/lib. `make check` builds and runs the tests (needs Wine
# for the GUI tests; see tests/run-tests.sh).

BIN ?=
CFG ?=
TARGET ?= i586-pc-windows5.1-gnu
# Linux and macOS targets (make TARGET=x86_64-linux-gnu, or
# TARGET=arm64-apple-macos11) build over the portable Win32 layer in
# port/, which goes into libwinport.a beside the MFC libraries.
ifneq ($(findstring linux,$(TARGET))$(findstring darwin,$(TARGET))$(findstring apple,$(TARGET)),)
PORTABLE = 1
endif
# The classic Mac OS (make TARGET=m68k-apple-macos or
# TARGET=powerpc-apple-macos9 MACSDK=/path/to/mac-sdk) also builds over
# the portable layer, with the C and C++ libraries of clang-retro/mac-sdk.
# There are no shared libraries, no network and no X11, Wayland or Cocoa
# backend. See docs/macos-classic.md.
# (powerpc-apple-macosx is Mac OS X, below.)
ifneq ($(filter m68k-apple-macos% powerpc-apple-macos%,$(if $(findstring macosx,$(TARGET)),,$(TARGET))),)
MACOS_CLASSIC = 1
MACSDK ?= ../mac-sdk
MACSDK_LIB = $(MACSDK)/lib
CLASSIC_TRIPLE = $(if $(findstring m68k,$(TARGET)),m68k-apple-macos,powerpc-apple-macos9)
CXXFLAGS ?= -Os
STDLIB = -nostdinc++ -isystem $(MACSDK_LIB)/include/c++/15.2.0/$(CLASSIC_TRIPLE) \
  -isystem $(MACSDK_LIB)/include/c++/15.2.0 -isystem port/compat/macos \
  -isystem $(MACSDK_LIB)/include -I$(MACSDK)/include
CLASSIC_FLAGS = $(if $(findstring m68k,$(TARGET)),-mcmodel=large) -femulated-tls \
  -fexceptions -frtti -ffunction-sections -fdata-sections -D_GNU_SOURCE
endif
# 32-bit DOS (make TARGET=i386-pc-msdos BIN=/path/to/clang-dos/bin) also
# builds over the portable layer, with the i386-msdos sysroot of
# clang-retro/dos-sdk (picolibc with cooperative threads, and libc++).
# port/compat/dos has the POSIX headers and port/src/compat_dos.cpp the
# functions that DOS does not have. The DOS backend shows the windows on
# a VESA screen. There are no shared libraries, no network and no other
# programs to talk to. See docs/dos.md.
ifneq ($(filter i386-pc-msdos i486-pc-msdos i586-pc-msdos i686-pc-msdos,$(TARGET)),)
PORTABLE = 1
DOS32 = 1
CXXFLAGS ?= -O2
STDLIB = -isystem port/compat/dos
DOS32_FLAGS = -ffunction-sections -fdata-sections -D_GNU_SOURCE
endif
# Mac OS X 10.0 to 10.5 (make TARGET=powerpc-apple-macosx10.4
# SDK=/path/to/MacOSX10.4u.sdk LIBCXX=/path/to/libcxx-macosx) build over
# the portable layer with the Cocoa backend, against Apple's SDK and a
# libc++ built with threads (dos-sdk's scripts/build-libcxx-macosx.sh, which
# has them for 10.4). port/compat/macosx adds the C library functions that those
# versions do not have, and thread_local is emulated. The archives are
# made with Apple's ar (CCTOOLS, default powerpc-apple-darwin8-), because
# ld64 cannot read the symbol table that llvm-ar writes for PowerPC. See
# docs/macosx-10.4.md. For 10.0 to 10.3 (TARGET=powerpc-apple-macosx10.0),
# SDK is a 10.0 sysroot made with dos-sdk's scripts (fix-macosx-10.0-
# headers.sh, then build-c99-macosx.sh), and programs link with -lc99.
MACOSX_OLD_TARGETS = $(foreach v,0 1 2 3,%-apple-macosx10.$(v))
ifneq ($(filter $(MACOSX_OLD_TARGETS) %-apple-macosx10.4 %-apple-macosx10.5 %-apple-darwin8 %-apple-darwin9,$(TARGET)),)
MACOSX_LEGACY = 1
SDK ?= /Developer/SDKs/MacOSX10.4u.sdk
LIBCXX ?= ../libcxx-macosx
CCTOOLS ?= powerpc-apple-darwin8-
STDLIB = -nostdinc++ -isystem $(LIBCXX)/include/c++/v1 -isystem port/compat/macosx \
  -isysroot $(SDK) -D__STDC_FORMAT_MACROS -DWINPORT_MACOSX_COMPAT -femulated-tls
endif
SYSROOT ?=
ifneq ($(CFG),)
OUT ?= build/$(basename $(notdir $(CFG)))
TARGET_FLAGS = --config=$(CFG)
else
OUT ?= build/$(TARGET)
TARGET_FLAGS = --target=$(TARGET) $(if $(SYSROOT),--sysroot=$(SYSROOT),)
endif
ifeq ($(PORTABLE)$(SYSROOT),1)
PREFIX ?= /usr/local
endif
PREFIX ?= $(SYSROOT)
DESTDIR ?=

CXX = $(if $(BIN),$(BIN)/,)clang++
# Without BIN, llvm-ar and llvm-nm from the path, or else the system's.
ifneq ($(BIN),)
AR = $(BIN)/llvm-ar
NM = $(BIN)/llvm-nm
else
AR = $(if $(shell command -v llvm-ar 2>/dev/null),llvm-ar,ar)
NM = $(if $(shell command -v llvm-nm 2>/dev/null),llvm-nm,nm)
endif

ifeq ($(MACOSX_LEGACY),1)
AR = $(CCTOOLS)ar
endif

CXXFLAGS ?= -O2
STDLIB ?= -stdlib=libc++
TEST_RC ?=
# The linker flags of mfc.dll. (LDFLAGS are the tests'.)
DLL_LDFLAGS ?=
# MFC code calls GetSafeHwnd and friends on pointers that may be NULL;
# -fno-delete-null-pointer-checks keeps the compiler from assuming that
# such a pointer, having been used as `this`, cannot be NULL afterwards.
ALL_CXXFLAGS = $(TARGET_FLAGS) $(STDLIB) -fno-delete-null-pointer-checks \
  -std=c++17 -Wall -Wno-invalid-offsetof -Wno-missing-exception-spec -Wno-overloaded-virtual -Wno-unused-private-field \
  -Iinclude -Isrc $(CXXFLAGS)

SRCS = $(wildcard src/*.cpp)
ifeq ($(PORTABLE),1)
PORT_SRCS = $(filter-out port/src/scrnsave.cpp port/src/glaux.cpp,$(wildcard port/src/*.cpp))
PORT_OBJS = $(patsubst port/src/%.cpp,$(OUT)/port/%.o,$(PORT_SRCS))
# macOS adds the AppKit half of the Cocoa backend, in Objective-C++.
ifneq ($(findstring darwin,$(TARGET))$(findstring apple,$(TARGET)),)
ifneq ($(MACOS_CLASSIC),1)
PORT_OBJS += $(patsubst port/src/%.mm,$(OUT)/port/%.mm.o,$(wildcard port/src/*.mm))
endif
endif
ifneq ($(MACOS_CLASSIC)$(DOS32),)
# The classic Mac OS and DOS have no services and no window system that
# these files use. mac-sdk's C library has the sockets (over MacTCP) that
# Winsock, WinInet, CAsyncSocket and the internet classes use; on DOS
# they fail (compat_dos.cpp).
CLASSIC_SKIP = service com_remote com_proxy com_ole \
  backend_x11 backend_wayland backend_cocoa
PORT_SRCS := $(filter-out $(patsubst %,port/src/%.cpp,$(CLASSIC_SKIP)),$(PORT_SRCS))
PORT_OBJS = $(patsubst port/src/%.cpp,$(OUT)/port/%.o,$(PORT_SRCS))
ALL_CXXFLAGS += -Iport/include $(CLASSIC_FLAGS) $(DOS32_FLAGS)
else
ALL_CXXFLAGS += -Iport/include -fPIC
endif
PORT_LIB = $(OUT)/libwinport.a
SCRNSAVE_LIBS = $(OUT)/libscrnsave.a $(OUT)/libscrnsavw.a
# The OpenGL auxiliary library (GL/glaux.h): over GLUT on Mac OS X, and
# over glaux_solids.h on the classic Mac OS and with the generic
# implementation.
ifneq ($(findstring darwin,$(TARGET))$(findstring apple,$(TARGET)),)
ifneq ($(MACOS_CLASSIC),1)
SCRNSAVE_LIBS += $(OUT)/libglaux.a
else ifneq ($(findstring powerpc,$(TARGET)),)
SCRNSAVE_LIBS += $(OUT)/libglaux.a
else
SOFTGL = 1
endif
else
SOFTGL = 1
endif
# The systems with no OpenGL of their own (Linux, the classic Mac OS on
# 68k) have the generic implementation: port/softgl, a software renderer
# in C, and the SGI reference GLU. Their objects go into libwinport.a, so
# that a program links with libwinport.a only. port/softgl is a submodule
# (clang-retro/softgl). See docs/software-opengl.md.
ifeq ($(SOFTGL),1)
ifeq ($(wildcard port/softgl/src/sgl.h),)
$(error port/softgl is empty. Run: git submodule update --init)
endif
SCRNSAVE_LIBS += $(OUT)/libglaux.a
CC = $(if $(BIN),$(BIN)/,)clang
SOFTGL_SRCS = $(wildcard port/softgl/src/*.c)
SOFTGL_GLU_SRCS = $(filter-out %/priorityq-heap.c,$(wildcard port/softgl/glu/libtess/*.c port/softgl/glu/libutil/*.c))
SOFTGL_NURBS_SRCS = $(wildcard port/softgl/glu/libnurbs/*/*.cc)
SOFTGL_OBJS = $(patsubst port/softgl/%.c,$(OUT)/softgl/%.o,$(SOFTGL_SRCS) $(SOFTGL_GLU_SRCS)) \
  $(patsubst port/softgl/%.cc,$(OUT)/softgl/%.o,$(SOFTGL_NURBS_SRCS))
PORT_OBJS += $(SOFTGL_OBJS)
SOFTGL_CFLAGS = $(TARGET_FLAGS) $(CXXFLAGS) -DSGL_LAYER_FLUSH -Iport/softgl/include \
  $(if $(MACOS_CLASSIC),$(if $(findstring m68k,$(TARGET)),-mcmodel=large) -ffunction-sections -fdata-sections \
  -isystem $(MACSDK_LIB)/include -I$(MACSDK)/include,$(if $(DOS32),$(DOS32_FLAGS) -isystem port/compat/dos,-fPIC))
ALL_CXXFLAGS += -Iport/softgl/include
# The names of the entries of softgl's dispatch table, in order, for the
# table that points at the system's OpenGL on Linux (opengl_egl.cpp).
$(OUT)/port/opengl_egl.o: $(OUT)/gl_names.inc
$(OUT)/port/opengl_egl.o: ALL_CXXFLAGS += -I$(OUT)
$(OUT)/gl_names.inc: port/softgl/src/dispatch.h
	@mkdir -p $(dir $@)
	sed -n 's/^.*(APIENTRY \*\([A-Za-z0-9_]*\))(.*$$/  "gl\1",/p' $< > $@
endif
endif
# 16-bit Windows (make TARGET=i8086-pc-win16): the core classes in the
# large model (CObject, CString, the collections, CFile, CArchive, CTime
# and the exceptions) and the window classes (CWinApp, CWnd, CFrameWnd,
# CMDIFrameWnd, CDialog, the controls, CDC, CMenu, the documents and the
# views), ANSI only: libmfc.a. The flags give the large model and C++
# exceptions. include/win16 replaces the OLE headers of the portable
# layer; port/include is last, for the Win32 types that the headers of
# the 16-bit SDK do not have.
ifneq ($(findstring win16,$(TARGET)),)
WIN16 = 1
SRCS = $(addprefix src/,object.cpp strcore.cpp coll.cpp file.cpp archive.cpp except.cpp \
  time.cpp memdebug.cpp state.cpp win16.cpp win16u.cpp win16ole.cpp win16bar.cpp \
  cmdcom.cpp cmdevent.cpp oleconn.cpp oledisp.cpp olevar.cpp \
  olecli.cpp oledata.cpp oledlgs.cpp oledoc.cpp oledrop.cpp olefact.cpp oleipcli.cpp \
  oleipsvr.cpp olemon.cpp olemsgf.cpp olereg.cpp olestrm.cpp olesvr.cpp oledocob.cpp \
  win16oleui.cpp win16olepro.cpp occmgr.cpp occsite.cpp \
  ctlcore.cpp ctlintf.cpp ctlprop.cpp ctlstock.cpp ctlpict.cpp ctlppg.cpp ctlreg.cpp ctldata.cpp \
  win16ctl.cpp win16imgl.cpp win16lv.cpp win16tv.cpp win16tt.cpp win16misc.cpp win16cbex.cpp win16date.cpp win16rebar.cpp win16anim.cpp rebar.cpp cmnctls.cpp fileshrd.cpp tracker.cpp \
  wincore.cpp winocc.cpp winutil.cpp app.cpp appmain.cpp cmdtarg.cpp thread.cpp mtcore.cpp \
  frame.cpp winmdi.cpp winmini.cpp winsplit.cpp bars.cpp dockbar.cpp dockcont.cpp dockstat.cpp \
  dlgcore.cpp dlgdata.cpp dlgtmpl.cpp dlgcomm.cpp dlgvista.cpp \
  ctrls.cpp listbox.cpp gdi.cpp dcprev.cpp doccore.cpp doctempl.cpp view.cpp viewform.cpp \
  viewedit.cpp viewprnt.cpp \
  viewprev.cpp)
WIN16_FLAGS ?= -mcmodel=large -fexceptions
ALL_CXXFLAGS += $(WIN16_FLAGS) -frtti -ffunction-sections \
  -Iinclude/win16 -idirafter port/include
STDLIB =
endif
OBJS_A = $(patsubst src/%.cpp,$(OUT)/a/%.o,$(SRCS))
OBJS_U = $(patsubst src/%.cpp,$(OUT)/u/%.o,$(SRCS))
HDRS = $(wildcard include/*.h) $(wildcard src/*.h) src/wnd_handlers.inc
ifeq ($(PORTABLE),1)
HDRS += $(wildcard port/include/*.h) $(wildcard port/include/GL/*.h) $(wildcard port/include/sys/*.h) $(wildcard port/src/*.h) $(wildcard port/src/*.inc)
endif

# The shared library: every source, built with _AFXDLL; the files that
# each module keeps for itself are empty there and make up libmfcs.a.
STUB_SRCS = src/appmain.cpp src/appmainc.cpp src/dllmodul.cpp src/dllstub.cpp src/pseudoreloc.cpp
OBJS_DA = $(patsubst src/%.cpp,$(OUT)/da/%.o,$(SRCS))
OBJS_DU = $(patsubst src/%.cpp,$(OUT)/du/%.o,$(SRCS))
OBJS_SA = $(patsubst src/%.cpp,$(OUT)/sa/%.o,$(STUB_SRCS))
OBJS_SU = $(patsubst src/%.cpp,$(OUT)/su/%.o,$(STUB_SRCS))
# 32-bit x86 C names start with an underscore, which a .def file leaves out.
MACHINE := $(shell $(CXX) $(TARGET_FLAGS) -dumpmachine 2>/dev/null)
DEF_STRIP = $(if $(filter i%86,$(firstword $(subst -, ,$(MACHINE)))),1,0)

STATIC_LIBS = $(OUT)/libmfc.a $(OUT)/libmfcu.a
SHARED_LIBS = $(OUT)/mfc.dll $(OUT)/mfcu.dll $(OUT)/libmfcs.a $(OUT)/libmfcus.a
# Linux and macOS: the shared libraries go in $(OUT)/shared, under the
# static ones' names (libmfc.so, libmfcu.so and libwinport.so, or .dylib),
# so that -L$(OUT)/shared picks them; libmfcs.a and libmfcus.a are as on
# Windows. The program's main stays in the program (libmfcs.a for MFC
# programs), so the layer's crt0 is left out of libwinport.so. So is the
# screen saver host (saver_host.cpp), which calls the program's WinMain.
ifeq ($(PORTABLE),1)
STATIC_LIBS += $(PORT_LIB) $(SCRNSAVE_LIBS)
ifneq ($(findstring darwin,$(TARGET))$(findstring apple,$(TARGET)),)
SHLIB_EXT = dylib
SHLIB_SYSLIBS = -framework Cocoa -framework OpenGL -lpthread
SHLIB_FLAGS = -dynamiclib -Wl,-install_name,@rpath/$(notdir $@) -Wl,-rpath,@loader_path
else
SHLIB_EXT = so
SHLIB_SYSLIBS = -ldl -lpthread
SHLIB_FLAGS = -shared -Wl,-soname,$(notdir $@) -Wl,-rpath,'$$ORIGIN'
endif
SHARED_DIR = $(OUT)/shared
PORT_SHLIB = $(SHARED_DIR)/libwinport.$(SHLIB_EXT)
SHARED_LIBS = $(SHARED_DIR)/libmfc.$(SHLIB_EXT) $(SHARED_DIR)/libmfcu.$(SHLIB_EXT) $(PORT_SHLIB) \
  $(OUT)/libmfcs.a $(OUT)/libmfcus.a
endif

ifeq ($(WIN16),1)
STATIC_LIBS = $(OUT)/libmfc.a
SHARED_LIBS =
endif
ifneq ($(MACOS_CLASSIC)$(DOS32),)
STATIC_LIBS = $(OUT)/libmfc.a $(OUT)/libmfcu.a $(PORT_LIB) $(SCRNSAVE_LIBS)
SHARED_LIBS =
endif

all: $(STATIC_LIBS) $(SHARED_LIBS)
static: $(STATIC_LIBS)
shared: $(SHARED_LIBS)

$(PORT_LIB): $(PORT_OBJS)
	rm -f $@
	$(AR) rcs $@ $^

# The screen saver library: WinMain for screen savers (scrnsave.h), ANSI
# in libscrnsave.a and Unicode in libscrnsavw.a, as on Windows.
$(OUT)/libscrnsave.a: $(OUT)/port/scrnsave-a.o
	rm -f $@
	$(AR) rcs $@ $^
$(OUT)/libscrnsavw.a: $(OUT)/port/scrnsave-u.o
	rm -f $@
	$(AR) rcs $@ $^
$(OUT)/libglaux.a: $(OUT)/port/glaux.o
	rm -f $@
	$(AR) rcs $@ $^
$(OUT)/port/scrnsave-a.o: port/src/scrnsave.cpp $(HDRS)
	@mkdir -p $(dir $@)
	$(CXX) $(ALL_CXXFLAGS) -Iport/src -c $< -o $@
$(OUT)/port/scrnsave-u.o: port/src/scrnsave.cpp $(HDRS)
	@mkdir -p $(dir $@)
	$(CXX) $(ALL_CXXFLAGS) -DUNICODE -D_UNICODE -Iport/src -c $< -o $@

$(OUT)/softgl/src/%.o: port/softgl/src/%.c port/softgl/src/sgl.h port/softgl/src/dispatch.h
	@mkdir -p $(dir $@)
	$(CC) $(SOFTGL_CFLAGS) -std=c99 -Wall -Iport/softgl/src -c $< -o $@
$(OUT)/softgl/glu/%.o: port/softgl/glu/%.c
	@mkdir -p $(dir $@)
	$(CC) $(SOFTGL_CFLAGS) -std=gnu99 -w -Iport/softgl/glu/include -c $< -o $@
# The NURBS of GLU are C++ (the SGI reference code).
$(OUT)/softgl/glu/%.o: port/softgl/glu/%.cc
	@mkdir -p $(dir $@)
	$(CXX) $(SOFTGL_CFLAGS) -w -fno-exceptions -fno-rtti -DNDEBUG -DLIBRARYBUILD \
	  -Iport/softgl/glu/include -Iport/softgl/glu/libnurbs/internals \
	  -Iport/softgl/glu/libnurbs/interface -Iport/softgl/glu/libnurbs/nurbtess -c $< -o $@

$(OUT)/port/%.o: port/src/%.cpp $(HDRS)
	@mkdir -p $(dir $@)
	$(CXX) $(ALL_CXXFLAGS) -Iport/src -c $< -o $@

$(OUT)/port/%.mm.o: port/src/%.mm port/src/cocoa.h
	@mkdir -p $(dir $@)
	$(CXX) $(TARGET_FLAGS) $(STDLIB) -x objective-c++ -fno-objc-arc -std=c++17 -Wall $(CXXFLAGS) -fPIC -c $< -o $@

$(OUT)/libmfc.a: $(OBJS_A)
	rm -f $@
	$(AR) rcs $@ $^

$(OUT)/libmfcu.a: $(OBJS_U)
	rm -f $@
	$(AR) rcs $@ $^

$(OUT)/libmfcs.a: $(OBJS_SA)
	rm -f $@
	$(AR) rcs $@ $^

$(OUT)/libmfcus.a: $(OBJS_SU)
	rm -f $@
	$(AR) rcs $@ $^

ifeq ($(PORTABLE),1)
$(PORT_SHLIB): $(filter-out $(OUT)/port/crt0.o $(OUT)/port/saver_host.o,$(PORT_OBJS))
	@mkdir -p $(dir $@)
	$(CXX) $(ALL_CXXFLAGS) $(SHLIB_FLAGS) -o $@ $^ $(SHLIB_SYSLIBS) $(DLL_LDFLAGS)

$(SHARED_DIR)/libmfc.$(SHLIB_EXT): $(OBJS_DA) $(PORT_SHLIB)
	$(CXX) $(ALL_CXXFLAGS) $(SHLIB_FLAGS) -o $@ $(OBJS_DA) -L$(SHARED_DIR) -lwinport $(SHLIB_SYSLIBS) $(DLL_LDFLAGS)

$(SHARED_DIR)/libmfcu.$(SHLIB_EXT): $(OBJS_DU) $(PORT_SHLIB)
	$(CXX) $(ALL_CXXFLAGS) $(SHLIB_FLAGS) -o $@ $(OBJS_DU) -L$(SHARED_DIR) -lwinport $(SHLIB_SYSLIBS) $(DLL_LDFLAGS)
endif

# The DLL exports what its objects define, apart from the C++ runtime's
# (src/mkdef.awk); the import library comes with it.
$(OUT)/mfc.dll: $(OBJS_DA) src/mkdef.awk
	$(NM) --defined-only --extern-only $(OBJS_DA) | awk -v strip=$(DEF_STRIP) -f src/mkdef.awk > $(OUT)/mfc.def
	$(CXX) $(ALL_CXXFLAGS) -fuse-ld=lld -shared -o $@ $(OBJS_DA) $(OUT)/mfc.def \
	  -Wl,--out-implib,$(OUT)/libmfcdll.a $(DLL_LDFLAGS)

$(OUT)/mfcu.dll: $(OBJS_DU) src/mkdef.awk
	$(NM) --defined-only --extern-only $(OBJS_DU) | awk -v strip=$(DEF_STRIP) -f src/mkdef.awk > $(OUT)/mfcu.def
	$(CXX) $(ALL_CXXFLAGS) -fuse-ld=lld -shared -o $@ $(OBJS_DU) $(OUT)/mfcu.def \
	  -Wl,--out-implib,$(OUT)/libmfcudll.a $(DLL_LDFLAGS)

$(OUT)/a/%.o: src/%.cpp $(HDRS)
	@mkdir -p $(dir $@)
	$(CXX) $(ALL_CXXFLAGS) -c $< -o $@

$(OUT)/u/%.o: src/%.cpp $(HDRS)
	@mkdir -p $(dir $@)
	$(CXX) $(ALL_CXXFLAGS) -DUNICODE -D_UNICODE -c $< -o $@

$(OUT)/da/%.o: src/%.cpp $(HDRS)
	@mkdir -p $(dir $@)
	$(CXX) $(ALL_CXXFLAGS) -D_AFXDLL -D_AFX_BUILDING_DLL -c $< -o $@

$(OUT)/du/%.o: src/%.cpp $(HDRS)
	@mkdir -p $(dir $@)
	$(CXX) $(ALL_CXXFLAGS) -D_AFXDLL -D_AFX_BUILDING_DLL -DUNICODE -D_UNICODE -c $< -o $@

$(OUT)/sa/%.o: src/%.cpp $(HDRS)
	@mkdir -p $(dir $@)
	$(CXX) $(ALL_CXXFLAGS) -D_AFXDLL -c $< -o $@

$(OUT)/su/%.o: src/%.cpp $(HDRS)
	@mkdir -p $(dir $@)
	$(CXX) $(ALL_CXXFLAGS) -D_AFXDLL -DUNICODE -D_UNICODE -c $< -o $@

DEST = $(DESTDIR)$(PREFIX)
PKG_VERSION = 6.0
# $(call pcfile,name,u or nothing,static or shared,description)
pcfile = sed -e 's|@NAME@|$(1)|g' -e 's|@DESC@|$(4)|g' -e 's|@VERSION@|$(PKG_VERSION)|' \
  -e 's|@DEFS@|$(if $(2), -DUNICODE -D_UNICODE)$(if $(filter shared,$(3)), -D_AFXDLL)|' \
  -e 's|@LIBS@|$(PC_LIBS_$(3))|' -e 's|@LIB@|mfc$(2)|g' -e 's|@SYSLIBS@|$(SHLIB_SYSLIBS)|' \
  port/pkg/libmfc.pc.in > $(DEST)/lib/pkgconfig/$(1).pc
PC_LIBS_static = -L$${libdir} -l@LIB@ -lwinport
# The program's own part (main among it) from libmfcs.a, then the shared libraries.
PC_LIBS_shared = -L$${shareddir} -L$${libdir} -l@LIB@s -l@LIB@ -lwinport -Wl,-rpath,$${shareddir}

install: all
ifeq ($(WIN16),1)
	mkdir -p $(DEST)/include/mfc/win16 $(DEST)/include/mfc/winport $(DEST)/lib
	cp include/*.h include/*.rc $(DEST)/include/mfc/
	cp include/win16/*.h $(DEST)/include/mfc/win16/
	cp port/include/*.h $(DEST)/include/mfc/winport/
	cp $(OUT)/libmfc.a $(DEST)/lib/
else ifneq ($(MACOS_CLASSIC)$(DOS32),)
	mkdir -p $(DEST)/include/libmfc/winport $(DEST)/include/libmfc/compat $(DEST)/lib
	cp include/*.h include/*.rc include/*.cpp $(DEST)/include/libmfc/
	cp port/include/*.h $(DEST)/include/libmfc/winport/
	cp -R port/compat/$(if $(DOS32),dos,macos)/. $(DEST)/include/libmfc/compat/
	cp $(STATIC_LIBS) $(DEST)/lib/
else ifeq ($(PORTABLE),1)
	mkdir -p $(DEST)/include/libmfc/winport $(DEST)/lib/libmfc-shared $(DEST)/lib/pkgconfig \
	  $(DEST)/lib/cmake/libmfc $(DEST)/share/libmfc
	cp include/*.h include/*.rc include/*.cpp $(DEST)/include/libmfc/
	cp port/include/*.h $(DEST)/include/libmfc/winport/
	cp $(OUT)/libmfc.a $(OUT)/libmfcu.a $(PORT_LIB) $(SCRNSAVE_LIBS) $(OUT)/libmfcs.a $(OUT)/libmfcus.a $(DEST)/lib/
	cp $(filter-out %.a,$(SHARED_LIBS)) $(DEST)/lib/libmfc-shared/
	cp port/tools/res2cpp.py $(DEST)/share/libmfc/
	$(call pcfile,libmfc,,static,the ANSI static library)
	$(call pcfile,libmfcu,u,static,the Unicode static library)
	$(call pcfile,libmfc-shared,,shared,the ANSI shared library)
	$(call pcfile,libmfcu-shared,u,shared,the Unicode shared library)
	cp port/pkg/libmfcConfig.cmake $(DEST)/lib/cmake/libmfc/
	sed -e 's|@VERSION@|$(PKG_VERSION)|' port/pkg/libmfcConfigVersion.cmake.in \
	  > $(DEST)/lib/cmake/libmfc/libmfcConfigVersion.cmake
else
	mkdir -p $(DEST)/include/mfc $(DEST)/lib $(DEST)/bin
	cp include/*.h include/*.rc $(DEST)/include/mfc/
	cp $(OUT)/libmfc.a $(OUT)/libmfcu.a $(OUT)/libmfcs.a $(OUT)/libmfcus.a \
	  $(OUT)/libmfcdll.a $(OUT)/libmfcudll.a $(DEST)/lib/
	cp $(OUT)/mfc.dll $(OUT)/mfcu.dll $(DEST)/bin/
endif

check: all
ifeq ($(DOS32),1)
	BIN=$(BIN) OUT=$(OUT) sh tests/run-dos-tests.sh
else ifeq ($(MACOS_CLASSIC),1)
	BIN=$(BIN) MACSDK=$(MACSDK) TARGET=$(TARGET) OUT=$(OUT) sh tests/run-macos-tests.sh
else ifeq ($(WIN16),1)
	BIN=$(BIN) SYSROOT=$(SYSROOT) OUT=$(OUT) sh tests/run-win16-tests.sh
else
	BIN=$(BIN) STDLIB="$(STDLIB)" RC="$(TEST_RC)" LDFLAGS="$(LDFLAGS)" CFG=$(CFG) SYSROOT=$(SYSROOT) TARGET=$(TARGET) OUT=$(OUT) sh tests/run-tests.sh
endif

clean:
	rm -rf $(OUT)

.PHONY: all static shared install check clean
