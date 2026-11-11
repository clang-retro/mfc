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
# libraries to dir/lib and the DLLs to dir/bin. `make check` builds and runs the tests (needs Wine
# for the GUI tests; see tests/run-tests.sh).

BIN ?=
CFG ?=
TARGET ?= i586-pc-windows5.1-gnu
SYSROOT ?=
ifneq ($(CFG),)
OUT ?= build/$(basename $(notdir $(CFG)))
TARGET_FLAGS = --config=$(CFG)
else
OUT ?= build/$(TARGET)
TARGET_FLAGS = --target=$(TARGET) $(if $(SYSROOT),--sysroot=$(SYSROOT),)
endif
PREFIX ?= $(SYSROOT)

CXX = $(if $(BIN),$(BIN)/,)clang++
AR = $(if $(BIN),$(BIN)/,)llvm-ar
NM = $(if $(BIN),$(BIN)/,)llvm-nm

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
OBJS_A = $(patsubst src/%.cpp,$(OUT)/a/%.o,$(SRCS))
OBJS_U = $(patsubst src/%.cpp,$(OUT)/u/%.o,$(SRCS))
HDRS = $(wildcard include/*.h) $(wildcard src/*.h) src/wnd_handlers.inc

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

all: $(STATIC_LIBS) $(SHARED_LIBS)
static: $(STATIC_LIBS)
shared: $(SHARED_LIBS)

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

install: all
	mkdir -p $(PREFIX)/include/mfc $(PREFIX)/lib $(PREFIX)/bin
	cp include/*.h include/*.rc $(PREFIX)/include/mfc/
	cp $(OUT)/libmfc.a $(OUT)/libmfcu.a $(OUT)/libmfcs.a $(OUT)/libmfcus.a \
	  $(OUT)/libmfcdll.a $(OUT)/libmfcudll.a $(PREFIX)/lib/
	cp $(OUT)/mfc.dll $(OUT)/mfcu.dll $(PREFIX)/bin/

check: all
	BIN=$(BIN) STDLIB="$(STDLIB)" RC="$(TEST_RC)" LDFLAGS="$(LDFLAGS)" CFG=$(CFG) SYSROOT=$(SYSROOT) TARGET=$(TARGET) OUT=$(OUT) sh tests/run-tests.sh

clean:
	rm -rf $(OUT)

.PHONY: all static shared install check clean
