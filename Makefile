# Builds the MFC libraries with clang-retro.
#
#   make BIN=/path/to/llvm/bin SYSROOT=/path/to/sysroot TARGET=<triple>
#
# Produces $(OUT)/libmfc.a (ANSI) and $(OUT)/libmfcu.a (Unicode).
# `make install PREFIX=dir` copies the headers to dir/include/mfc and the
# libraries to dir/lib. `make check` builds and runs the tests (needs Wine
# for the GUI tests; see tests/run-tests.sh).

BIN ?=
TARGET ?= i586-pc-windows5.1-gnu
SYSROOT ?=
OUT ?= build/$(TARGET)
PREFIX ?= $(SYSROOT)

CXX = $(if $(BIN),$(BIN)/,)clang++
AR = $(if $(BIN),$(BIN)/,)llvm-ar

CXXFLAGS ?= -O2
ALL_CXXFLAGS = --target=$(TARGET) $(if $(SYSROOT),--sysroot=$(SYSROOT),) -stdlib=libc++ \
  -std=c++17 -Wall -Wno-invalid-offsetof -Wno-overloaded-virtual -Wno-unused-private-field \
  -Iinclude -Isrc $(CXXFLAGS)

SRCS = $(wildcard src/*.cpp)
OBJS_A = $(patsubst src/%.cpp,$(OUT)/a/%.o,$(SRCS))
OBJS_U = $(patsubst src/%.cpp,$(OUT)/u/%.o,$(SRCS))
HDRS = $(wildcard include/*.h) $(wildcard src/*.h) src/wnd_handlers.inc

all: $(OUT)/libmfc.a $(OUT)/libmfcu.a

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
	cp include/*.h $(PREFIX)/include/mfc/
	cp $(OUT)/libmfc.a $(OUT)/libmfcu.a $(PREFIX)/lib/

check: all
	BIN=$(BIN) SYSROOT=$(SYSROOT) TARGET=$(TARGET) OUT=$(OUT) sh tests/run-tests.sh

clean:
	rm -rf $(OUT)

.PHONY: all install check clean
