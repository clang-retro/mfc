#!/bin/sh
# run-win16-tests.sh - builds the 16-bit Windows tests against
# build/i8086-pc-win16/libmfc.a at -O0, -Os and -O2 and runs them in
# Wine. Called by `make check TARGET=i8086-pc-win16`, which sets BIN,
# SYSROOT and OUT. Wine gives the exit code 0 whatever a 16-bit program
# returns, so each test prints PASS or ALL PASSED. The window tests need
# an X display; without one, the script starts itself under xvfb-run.
#
#   core      the core classes (a console program)
#   memtest   the memory diagnostics (a console program)
#   win16wnd  CWinApp, CFrameWnd, messages, controls, CDialog and DDX
#   win16doc  document/view with CSingleDocTemplate
#   win16mdi  document/view with CMultiDocTemplate (win16doc.cpp)
#   win16bar  CToolBar, CStatusBar, docking and floating
#   win16cmn  CImageList, CProgressCtrl, CSliderCtrl, CSpinButtonCtrl, CTabCtrl
#             CListCtrl, CTreeCtrl, CToolTipCtrl, CHotKeyCtrl, CIPAddressCtrl,
#             CPagerCtrl, CLinkCtrl and CComboBoxEx
#   win16auto dispatch maps, IDispatch and connection points
#   win16oledoc  OLE data transfer and the OLE dialogs; containers, servers
#             and drag and drop are linked, not run
set -u
: "${BIN:?set BIN}" "${SYSROOT:?set SYSROOT}"
OUT=${OUT:-build/i8086-pc-win16}
WINE=${WINE:-wine}
WINEDEBUG=${WINEDEBUG:--all}
export WINEDEBUG

if [ -z "${DISPLAY:-}" ] && [ -z "${MFC_TEST_NO_XVFB:-}" ] && command -v xvfb-run >/dev/null 2>&1; then
  MFC_TEST_NO_XVFB=1 exec xvfb-run -a "$0" "$@"
fi

T=$OUT/tests
mkdir -p "$T"
CXX="$BIN/clang++ --target=i8086-pc-win16 -mcmodel=large -fexceptions -frtti"
CXX="$CXX --sysroot=$SYSROOT -Iinclude -Iinclude/win16 -idirafter port/include -Itests"
LIBS="-L$OUT -lmfc"
npass=0
nfail=0

# check NAME: NAME.out has the line of a test that passed.
check() {
  if grep -qE "^(PASS|ALL PASSED)" "$T/$1.out"; then
    npass=$((npass + 1))
  else
    nfail=$((nfail + 1))
    echo "FAIL $1"
    sed 's/^/    /' "$T/$1.out" | head -20
  fi
}

# test NAME SOURCE [FLAGS...]: builds and runs one test at each level.
test1() {
  name=$1
  src=$2
  shift 2
  for o in O0 Os O2; do
    if ! $CXX -$o "$@" "$src" $LIBS -o "$T/$name-$o.exe" >"$T/$name-$o.out" 2>&1; then
      nfail=$((nfail + 1))
      echo "FAIL $name-$o (build)"
      sed 's/^/    /' "$T/$name-$o.out" | head -20
      continue
    fi
    (cd "$T" && timeout 60 "$WINE" "$name-$o.exe" >"$name-$o.out" 2>&1)
    check "$name-$o"
  done
}

"$BIN/llvm-rc" -win16 -I "$SYSROOT/include" -I include -I tests -FO "$T/win16wnd.res" \
  tests/win16wnd.rc || exit 1
"$BIN/llvm-rc" -win16 -I "$SYSROOT/include" -I include -I tests -FO "$T/win16doc.res" \
  tests/win16doc.rc || exit 1
"$BIN/llvm-rc" -win16 -I "$SYSROOT/include" -I include -I tests -FO "$T/win16bar.res" \
  tests/win16bar.rc || exit 1

test1 core tests/core.cpp
test1 memtest tests/memtest.cpp
test1 win16wnd tests/win16wnd.cpp "$T/win16wnd.res"
test1 win16doc tests/win16doc.cpp "$T/win16doc.res"
test1 win16mdi tests/win16doc.cpp "$T/win16doc.res" -DTEST_MDI
test1 win16bar tests/win16bar.cpp "$T/win16bar.res"
test1 win16cmn tests/win16cmn.cpp
test1 win16auto tests/win16auto.cpp
test1 win16oledoc tests/win16oledoc.cpp

echo "$npass passed, $nfail failed"
[ $nfail = 0 ]
