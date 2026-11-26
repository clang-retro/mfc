#!/bin/sh
# run-dos-tests.sh - builds the tests for 32-bit DOS against the libraries
# and runs them in DOSBox.
#
# Called by `make check TARGET=i386-pc-msdos`, which sets BIN and OUT.
# Each test is one LE program with the DOS/32A extender as its stub. A
# test passes when its output has its "PASS" line. Without DOSBox, the
# tests are only built. Logs go to $OUT/tests.
#
#   BIN        the folder of clang for i386-pc-msdos
#   DOSBOX     the DOSBox program (default: dosbox)
#   DOS_TESTS  the tests to run (default: core memtest sstest gltest)
#   TIMEOUT    seconds before DOSBox is stopped (default: 120)
set -u

OUT=${OUT:-build/i386-pc-msdos}
BIN=${BIN:-}
DOSBOX=${DOSBOX:-dosbox}
TIMEOUT=${TIMEOUT:-120}
TESTS=${DOS_TESTS:-"core memtest sstest gltest"}
CXX="${BIN:+$BIN/}clang++ --target=i386-pc-msdos"
T=$OUT/tests
mkdir -p "$T"
here=$(pwd)
run=1
command -v "$DOSBOX" >/dev/null 2>&1 || run=0
# llvm-rc compiles the tests' resources: the one in BIN or on the PATH.
RC_NATIVE=${BIN:+$BIN/}llvm-rc
command -v "$RC_NATIVE" >/dev/null 2>&1 || RC_NATIVE=llvm-rc

# rundos EXE LOG: runs EXE in DOSBox without a window and writes its
# output to LOG. The program runs as C:\RUN.EXE in an empty folder,
# because DOS names have 8 + 3 characters.
rundos() {
  d=$(mktemp -d)
  cp "$1" "$d/RUN.EXE" || { rm -rf "$d"; return 1; }
  cat >"$d/run.conf" <<EOF
[sdl]
output=surface
[dosbox]
memsize=64
machine=svga_s3
[cpu]
cycles=max
core=dynamic
[mixer]
nosound=true
[speaker]
pcspeaker=false
[sblaster]
sbtype=none
[gus]
gus=false
[autoexec]
mount c "$d"
c:
RUN.EXE > OUT.TXT
exit
EOF
  SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy \
    timeout "$TIMEOUT" "$DOSBOX" -conf "$d/run.conf" -exit >/dev/null 2>&1
  tr -d '\r' <"$d/OUT.TXT" >"$2" 2>/dev/null || : >"$2"
  rm -rf "$d"
}

nfail=0
npass=0
for t in $TESTS; do
  exe=$T/$t.exe
  # Resources become C++ that registers them as the program starts.
  srcs=tests/$t.cpp
  if [ -f tests/$t.rc ]; then
    if ! RC="$RC_NATIVE" python3 port/tools/res2cpp.py -I include -I tests -I "$T" \
        tests/$t.rc -o "$T/$t-res.cpp" >"$T/$t-rc.log" 2>&1; then
      echo "FAIL $t (resources; see $T/$t-rc.log)"
      nfail=$((nfail + 1))
      continue
    fi
    srcs="$srcs $T/$t-res.cpp"
  fi
  # A screen saver gets WinMain from the screen saver library.
  libs="$OUT/libmfc.a $OUT/libwinport.a"
  [ $t = sstest ] && libs="$OUT/libscrnsave.a $libs"
  if ! $CXX -O2 -D_GNU_SOURCE -fno-delete-null-pointer-checks \
      -isystem "$here/port/compat/dos" -I"$here/include" -I"$here/port/include" \
      -I"$here/port/softgl/include" -I"$here/tests" -Wno-everything \
      $srcs $libs -o "$exe" >"$T/$t-build.log" 2>&1; then
    echo "FAIL $t (build; see $T/$t-build.log)"
    nfail=$((nfail + 1))
    continue
  fi
  if [ $run = 0 ]; then
    echo "built $t"
    continue
  fi
  rundos "$exe" "$T/$t.log"
  if grep -q "^PASS" "$T/$t.log"; then
    echo "pass $t"
    npass=$((npass + 1))
  else
    echo "FAIL $t (see $T/$t.log)"
    nfail=$((nfail + 1))
  fi
done
echo "$npass passed, $nfail failed"
[ $nfail = 0 ]
