#!/bin/sh
# run-macos-tests.sh - builds the tests for the classic Mac OS against the
# libraries and runs them in the simulators of clang-retro/mac-sdk.
#
# Called by `make check TARGET=m68k-apple-macos` (or
# powerpc-apple-macos9), which sets BIN, MACSDK, TARGET and OUT.
# tools/macapp.py of mac-sdk makes each test an application;
# tools/sim68k.py (as a 68000 and a 68020) or tools/simppc.py runs it.
# The simulators need the Python module unicorn: without it, the tests
# are only built. A test passes when its console output has its "PASS"
# line. Logs go to $OUT/tests.
set -u

TARGET=${TARGET:-m68k-apple-macos}
OUT=${OUT:-build/$TARGET}
MACSDK=${MACSDK:-../mac-sdk}
BIN=${BIN:-}
TESTS=${MACOS_TESTS:-"core memtest"}
case $TARGET in
  m68k*) CPU=68k ;;
  *) CPU=ppc ;;
esac
CLANG=${BIN:+$BIN/}clang
export CLANG
T=$OUT/tests
mkdir -p "$T"
here=$(pwd)
run=1
python3 -c "import unicorn" 2>/dev/null || run=0
nfail=0
npass=0
for t in $TESTS; do
  app=$T/$t-$CPU.bin
  # tools/macapp.py takes joined flags: it gives each word that starts
  # with "-" to clang and takes the others as sources.
  if ! python3 "$MACSDK/tools/macapp.py" --cpu $CPU --lib "$MACSDK/lib" --size 8192 \
      -o "$app" -- -Os -femulated-tls -D_GNU_SOURCE -fno-delete-null-pointer-checks \
      "-isystem$here/port/compat/macos" "-I$here/include" "-I$here/port/include" \
      "-I$here/tests" -Wno-everything "tests/$t.cpp" \
      "$OUT/libmfc.a" "$OUT/libwinport.a" >"$T/$t-build.log" 2>&1; then
    echo "FAIL $t (build; see $T/$t-build.log)"
    nfail=$((nfail + 1))
    continue
  fi
  if [ $run = 0 ]; then
    echo "built $t"
    continue
  fi
  ok=1
  if [ $CPU = 68k ]; then
    for c in 68000 68020; do
      python3 "$MACSDK/tools/sim68k.py" --cpu $c --os --files --exit-anywhere "$app" \
        >"$T/$t-$c.log" 2>&1
      grep -q "^PASS" "$T/$t-$c.log" || { ok=0; echo "FAIL $t on a $c (see $T/$t-$c.log)"; }
    done
  else
    python3 "$MACSDK/tools/simppc.py" --files "$app" >"$T/$t-ppc.log" 2>&1
    grep -q "^PASS" "$T/$t-ppc.log" || { ok=0; echo "FAIL $t on PowerPC (see $T/$t-ppc.log)"; }
  fi
  if [ $ok = 1 ]; then
    echo "pass $t"
    npass=$((npass + 1))
  else
    nfail=$((nfail + 1))
  fi
done
[ $run = 0 ] && echo "install the Python module unicorn to run the tests"
echo "$npass passed, $nfail failed"
[ $nfail = 0 ]
