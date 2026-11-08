# Builds the MFC libraries with clang-retro.
#
#   make BIN=/path/to/llvm/bin SYSROOT=/path/to/sysroot TARGET=<triple>
#   make BIN=/path/to/llvm/bin CFG=/path/to/win32-sdk/cfg/win32-picolibc.cfg
#
# With CFG, the clang config file gives the target, sysroot and libraries
# (as win32-sdk's config files do), and the output goes to build/<name>.
#
# Produces $(OUT)/libmfc.a (ANSI) and $(OUT)/libmfcu.a (Unicode).
# `make install PREFIX=dir` copies the headers to dir/include/mfc and the
# libraries to dir/lib. `make check` builds and runs the tests (needs Wine
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

CXXFLAGS ?= -O2
STDLIB ?= -stdlib=libc++
TEST_RC ?=
# MFC code calls GetSafeHwnd and friends on pointers that may be NULL;
# -fno-delete-null-pointer-checks keeps the compiler from assuming that
# such a pointer, having been used as `this`, cannot be NULL afterwards.
ALL_CXXFLAGS = $(TARGET_FLAGS) $(STDLIB) -fno-delete-null-pointer-checks \
  -std=c++17 -Wall -Wno-invalid-offsetof -Wno-missing-exception-spec -Wno-overloaded-virtual -Wno-unused-private-field \
  -Iinclude -Isrc $(CXXFLAGS)

SRCS = $(wildcard src/*.cpp)
ifeq ($(PORTABLE),1)
PORT_SRCS = $(wildcard port/src/*.cpp)
PORT_OBJS = $(patsubst port/src/%.cpp,$(OUT)/port/%.o,$(PORT_SRCS))
ALL_CXXFLAGS += -Iport/include -fPIC
PORT_LIB = $(OUT)/libwinport.a
endif
OBJS_A = $(patsubst src/%.cpp,$(OUT)/a/%.o,$(SRCS))
OBJS_U = $(patsubst src/%.cpp,$(OUT)/u/%.o,$(SRCS))
HDRS = $(wildcard include/*.h) $(wildcard src/*.h) src/wnd_handlers.inc
ifeq ($(PORTABLE),1)
HDRS += $(wildcard port/include/*.h) $(wildcard port/src/*.h)
endif

all: $(OUT)/libmfc.a $(OUT)/libmfcu.a $(PORT_LIB)

$(PORT_LIB): $(PORT_OBJS)
	rm -f $@
	$(AR) rcs $@ $^

$(OUT)/port/%.o: port/src/%.cpp $(HDRS)
	@mkdir -p $(dir $@)
	$(CXX) $(ALL_CXXFLAGS) -Iport/src -c $< -o $@

$(OUT)/libmfc.a: $(OBJS_A)
	rm -f $@
	$(AR) rcs $@ $^

$(OUT)/libmfcu.a: $(OBJS_U)
	rm -f $@
	$(AR) rcs $@ $^

$(OUT)/a/%.o: src/%.cpp $(HDRS)
	@mkdir -p $(dir $@)
	$(CXX) $(ALL_CXXFLAGS) -c $< -o $@

$(OUT)/u/%.o: src/%.cpp $(HDRS)
	@mkdir -p $(dir $@)
	$(CXX) $(ALL_CXXFLAGS) -DUNICODE -D_UNICODE -c $< -o $@

install: all
	mkdir -p $(PREFIX)/include/mfc $(PREFIX)/lib
	cp include/*.h include/*.rc $(PREFIX)/include/mfc/
	cp $(OUT)/libmfc.a $(OUT)/libmfcu.a $(PREFIX)/lib/
ifeq ($(PORTABLE),1)
	mkdir -p $(PREFIX)/include/winport
	cp port/include/*.h $(PREFIX)/include/winport/
	cp $(PORT_LIB) $(PREFIX)/lib/
endif

check: all
	BIN=$(BIN) STDLIB="$(STDLIB)" RC="$(TEST_RC)" LDFLAGS="$(LDFLAGS)" CFG=$(CFG) SYSROOT=$(SYSROOT) TARGET=$(TARGET) OUT=$(OUT) sh tests/run-tests.sh

clean:
	rm -rf $(OUT)

.PHONY: all install check clean
