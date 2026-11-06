#!/bin/sh
# Dev helper: compile every library source for the XP sysroot, report errors.
TC=${TC:-/home/user/tc}
OUT=${OUT:-/tmp/claude-0/obj}
mkdir -p $OUT
for f in ${@:-src/*.cpp}; do
  $TC/llvm/bin/clang++ --target=i586-pc-windows5.1-gnu --sysroot=$TC/sysroots/win2k-xp -stdlib=libc++ -std=c++17 \
    -Wno-invalid-offsetof -Wno-overloaded-virtual $CXXFLAGS -Iinclude -Isrc -c $f -o $OUT/$(basename $f .cpp).o 2>&1 | grep -E "error|warning: " | head -${N:-15} | sed "s|^|$(basename $f): |"
done
