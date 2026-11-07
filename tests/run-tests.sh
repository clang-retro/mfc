#!/bin/sh
# run-tests.sh - builds the tests against the libraries and runs them.
#
# Called by `make check`, which sets BIN, CFG or SYSROOT and TARGET, and OUT. The
# programs run under Wine; the GUI tests also need an X display, so the
# script starts itself under xvfb-run when there is none. Without Wine the
# tests are only built.
#
# Logs and screenshots go to $OUT/tests/a (ANSI) and $OUT/tests/u
# (Unicode).
set -u

TARGET=${TARGET:-i586-pc-windows5.1-gnu}
OUT=${OUT:-build/$TARGET}
SYSROOT=${SYSROOT:-}
CFG=${CFG:-}
BIN=${BIN:-}
CXX=${BIN:+$BIN/}clang++
RC=${RC:-${BIN:+$BIN/}llvm-windres}
LDFLAGS=${LDFLAGS:-}
TESTS=${TESTS:-"core disptest socktest dlgtest sditest mditest docktest splittest richtest dbtest rvtest autotest dragtest oletest"}

if [ -z "${DISPLAY:-}" ] && [ -z "${MFC_TEST_NO_XVFB:-}" ] && command -v xvfb-run >/dev/null 2>&1; then
  MFC_TEST_NO_XVFB=1 exec xvfb-run -a -s "-screen 0 1024x768x24" "$0" "$@"
fi

HAVE_WINE=1
command -v wine >/dev/null 2>&1 || HAVE_WINE=0
export WINEDEBUG=${WINEDEBUG:--all}

T=$OUT/tests
mkdir -p "$T/a" "$T/u"
python3 tests/gen-toolbar-bmp.py "$T/toolbar.bmp" 6 || exit 1

if [ -n "$CFG" ]; then
  CXXFLAGS="--config=$CFG"
  # The resources need the sysroot's headers; the config file names it.
  [ -z "$SYSROOT" ] && SYSROOT=$(sed -n 's/^--sysroot=//p' "$CFG" | sed "s|<CFGDIR>|$(dirname "$CFG")|")
else
  CXXFLAGS="--target=$TARGET ${SYSROOT:+--sysroot=$SYSROOT}"
fi
CXXFLAGS="$CXXFLAGS -fuse-ld=lld ${STDLIB--stdlib=libc++} -std=c++17"
CXXFLAGS="$CXXFLAGS -Wall -Wno-unused-function -Wno-overloaded-virtual -Wno-missing-exception-spec -Iinclude -Itests"
RCFLAGS="--target=i686-w64-mingw32 -I tests -I include -I $T ${SYSROOT:+-I $SYSROOT/include}"

nfail=0
npass=0

# The tests run in a Wine prefix of their own, with a printer for the
# printing and print preview tests (Wine has none without CUPS).
if [ $HAVE_WINE = 1 ]; then
  export WINEPREFIX=${MFC_TEST_WINEPREFIX:-$(pwd)/$T/wineprefix}
  export WINEDLLOVERRIDES="mscoree,mshtml=;winemenubuilder.exe=d"
  if [ ! -f "$WINEPREFIX/system.reg" ]; then
    wineboot -i >/dev/null 2>&1
    wineserver -w
  fi
  # shellcheck disable=SC2086
  if "$CXX" $CXXFLAGS tests/addprinter.cpp $LDFLAGS -lwinspool -lgdi32 -o "$T/addprinter.exe" \
       2>"$T/addprinter-build.log"; then
    timeout 120 wine "$T/addprinter.exe" > "$T/addprinter.log" 2>&1 ||
      { echo "note: no test printer (see $T/addprinter.log)"; }
  else
    echo "note: addprinter did not build (see $T/addprinter-build.log)"
  fi
fi

# Turns a Unix path into the Windows path Wine sees.
winpath() {
  echo "Z:$(cd "$1" && pwd)" | tr / '\\'
}

run_test() {  # name exe outdir
  if [ $HAVE_WINE = 0 ]; then
    echo "built  $1-$v (no wine; not run)"
    return
  fi
  MFC_TEST_OUT=$(winpath "$3") timeout 300 wine "$2" > "$3/$1.out" 2>&1
  status=$?
  if [ $status = 0 ]; then
    echo "PASS   $1-$v"
    npass=$((npass + 1))
  else
    echo "FAIL   $1-$v (exit $status)"
    [ -f "$3/$1.log" ] && sed 's/^/       /' "$3/$1.log"
    [ -s "$3/$1.out" ] && sed 's/^/       /' "$3/$1.out"
    nfail=$((nfail + 1))
  fi
  # Screenshots as PNG, when ImageMagick is around.
  if command -v convert >/dev/null 2>&1; then
    for bmp in "$3"/*.bmp; do
      [ -f "$bmp" ] && convert "$bmp" "${bmp%.bmp}.png" && rm -f "$bmp"
    done
  fi
}

for v in a u; do
  flags=
  [ $v = u ] && flags="-DUNICODE -D_UNICODE"
  for name in $TESTS; do
    exe=$T/$name-$v.exe
    srcs=tests/$name.cpp
    if [ -f tests/$name.rc ]; then
      "$RC" $RCFLAGS tests/$name.rc -o "$T/$name-res.o" 2>"$T/$name-rc.log" || {
        echo "FAIL   $name-$v (resources)"; cat "$T/$name-rc.log"; nfail=$((nfail + 1)); continue; }
      srcs="$srcs $T/$name-res.o -mwindows"
    fi
    # The ODBC stand-in uses std::string. With GCC's libstdc++ (STDLIB
    # empty), clang passes an empty tag argument differently from the
    # GCC-built library on i686, so unoptimized calls into its
    # std::string code break the stack; optimizing inlines those calls.
    opt=
    case $name in dbtest|rvtest) [ -n "${STDLIB-x}" ] || opt=-O1 ;; esac
    # shellcheck disable=SC2086
    "$CXX" $CXXFLAGS $opt $flags $srcs -L"$OUT" $LDFLAGS -o "$exe" 2>"$T/$name-$v-build.log" || {
      echo "FAIL   $name-$v (build)"; cat "$T/$name-$v-build.log"; nfail=$((nfail + 1)); continue; }
    if [ ! -f tests/$name.rc ]; then
      # The console tests (no resources) report on standard output.
      if [ $HAVE_WINE = 1 ]; then
        if timeout 120 wine "$exe" > "$T/$v/$name.log" 2>&1; then
          echo "PASS   $name-$v"; npass=$((npass + 1))
        else
          echo "FAIL   $name-$v"; sed 's/^/       /' "$T/$v/$name.log"; nfail=$((nfail + 1))
        fi
      else
        echo "built  $name-$v (no wine; not run)"
      fi
    else
      run_test "$name" "$exe" "$T/$v"
    fi
  done
done

echo "$npass passed, $nfail failed"
[ $nfail = 0 ]
