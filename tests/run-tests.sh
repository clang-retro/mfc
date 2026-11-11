#!/bin/sh
# run-tests.sh - builds the tests against the libraries and runs them.
#
# Called by `make check`, which sets BIN, CFG or SYSROOT and TARGET, and OUT. The
# programs run under Wine; the GUI tests also need an X display, so the
# script starts itself under xvfb-run when there is none. Without Wine the
# tests are only built.
#
# Logs and screenshots go to $OUT/tests/a (ANSI) and $OUT/tests/u
# (Unicode), and those of the tests built with the shared library
# (SHARED_TESTS) to $OUT/tests/sa and $OUT/tests/su.
set -u

TARGET=${TARGET:-i586-pc-windows5.1-gnu}
OUT=${OUT:-build/$TARGET}
SYSROOT=${SYSROOT:-}
CFG=${CFG:-}
BIN=${BIN:-}
CXX=${BIN:+$BIN/}clang++
RC=${RC:-${BIN:+$BIN/}llvm-windres}
LDFLAGS=${LDFLAGS:-}
SHARED_TESTS=${SHARED_TESTS-"exttest core sditest dlgtest dlltest"}
TESTS=${TESTS:-"core memtest wintest disptest isapitest daotest socktest inettest dlgtest sditest mditest docktest splittest richtest dbtest rvtest dvtest autotest conntest dragtest oletest srvtest linktest richsrvtest iptest docobtest occtest htmltest ctltest cmntest wltest bindtest dlltest"}

if [ -z "${DISPLAY:-}" ] && [ -z "${MFC_TEST_NO_XVFB:-}" ] && command -v xvfb-run >/dev/null 2>&1; then
  MFC_TEST_NO_XVFB=1 exec xvfb-run -a -s "-screen 0 1024x768x24" "$0" "$@"
fi

# Linux and macOS: the programs build over the portable layer and run
# directly. Only the tests the layer can run so far are on the list.
case "$TARGET" in
  *linux*|*darwin*|*apple*) NATIVE=1 ;;
  *) NATIVE=0 ;;
esac
if [ $NATIVE = 1 ]; then
  TESTS=${NATIVE_TESTS:-"core memtest wintest disptest isapitest"}
  T=$OUT/tests
  mkdir -p "$T/a" "$T/u"
  python3 tests/gen-toolbar-bmp.py "$T/toolbar.bmp" 6 || exit 1
  RC_NATIVE=${BIN:+$BIN/}llvm-rc
  CXXFLAGS="--target=$TARGET ${SYSROOT:+--sysroot=$SYSROOT} ${STDLIB--stdlib=libc++} -std=c++17"
  CXXFLAGS="$CXXFLAGS -Wall -Wno-unused-function -Wno-overloaded-virtual -Wno-missing-exception-spec"
  CXXFLAGS="$CXXFLAGS -Iport/include -Iinclude -Itests"
  nfail=0
  npass=0
  # macOS has no timeout(1) unless coreutils is installed.
  TIMEOUT="timeout 120"
  command -v timeout >/dev/null 2>&1 || TIMEOUT=
  command -v gtimeout >/dev/null 2>&1 && [ -z "$TIMEOUT" ] && TIMEOUT="gtimeout 120"
  for v in a u; do
    flags=
    lib=-lmfc
    [ $v = u ] && flags="-DUNICODE -D_UNICODE" && lib=-lmfcu
    for name in $TESTS; do
      exe=$T/$name-$v
      srcs=tests/$name.cpp
      # Resources become C++ that registers them as the program starts.
      if [ -f tests/$name.rc ]; then
        CXX="$CXX" RC="$RC_NATIVE" python3 port/tools/res2cpp.py -I include -I tests -I "$T" \
          tests/$name.rc -o "$T/$name-res.cpp" 2>"$T/$name-rc.log" || {
          echo "FAIL   $name-$v (resources)"; cat "$T/$name-rc.log"; nfail=$((nfail + 1)); continue; }
        srcs="$srcs $T/$name-res.cpp"
      fi
      # shellcheck disable=SC2086
      "$CXX" $CXXFLAGS $flags $srcs -L"$OUT" $LDFLAGS $lib -lwinport -ldl -lpthread -o "$exe" \
        2>"$T/$name-$v-build.log" || {
        echo "FAIL   $name-$v (build)"; cat "$T/$name-$v-build.log"; nfail=$((nfail + 1)); continue; }
      if MFC_TEST_OUT="$T/$v" WINPORT_BACKEND=${WINPORT_BACKEND:-headless} WINPORT_REGISTRY="$T/$v/registry.reg" $TIMEOUT "$exe" > "$T/$v/$name.log" 2>&1; then
        echo "PASS   $name-$v"; npass=$((npass + 1))
      else
        echo "FAIL   $name-$v"; sed 's/^/       /' "$T/$v/$name.log"; nfail=$((nfail + 1))
      fi
    done
  done
  echo "$npass passed, $nfail failed"
  [ $nfail = 0 ]
  exit
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

run_test() {  # name exe outdir label
  label=${4:-$1}
  if [ $HAVE_WINE = 0 ]; then
    echo "built  $label-$v (no wine; not run)"
    return
  fi
  MFC_TEST_OUT=$(winpath "$3") timeout 300 wine "$2" > "$3/$1.out" 2>&1
  status=$?
  if [ $status = 0 ]; then
    echo "PASS   $label-$v"
    npass=$((npass + 1))
  else
    echo "FAIL   $label-$v (exit $status)"
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

# Builds and runs one test. With shared=1 the test uses the shared
# library (_AFXDLL), as does its DLL, if it has one.
build_and_run() {  # name label exe outdir shared ldflags
  name=$1 label=$2 exe=$3 outdir=$4 shared=$5 ldflags=$6
  srcs=tests/$name.cpp
  dllflags=
  [ $shared = 1 ] && dllflags=-D_AFXDLL
  # A test with a DLL of its own: tests/NAME-dll.cpp, with its exports
  # in tests/NAME-dll.def (or marked in the source, for an extension
  # DLL) and resources in tests/NAME-dll.rc.
  if [ -f tests/$name-dll.cpp ]; then
    dllsrcs=tests/$name-dll.cpp
    [ -f tests/$name-dll.def ] && dllsrcs="$dllsrcs tests/$name-dll.def"
    if [ -f tests/$name-dll.rc ]; then
      "$RC" $RCFLAGS tests/$name-dll.rc -o "$T/$name-dll-res.o" 2>"$T/$name-dll-rc.log" || {
        echo "FAIL   $label-$v (DLL resources)"; cat "$T/$name-dll-rc.log"; nfail=$((nfail + 1)); return; }
      dllsrcs="$dllsrcs $T/$name-dll-res.o"
    fi
    # A DLL with a .def is a regular DLL, which the program loads itself;
    # the others are extension DLLs, which it links with.
    implib= usrdll=
    if [ $shared = 1 ] && [ -f tests/$name-dll.def ]; then
      usrdll=-D_USRDLL
    elif [ $shared = 1 ]; then
      implib="-Wl,--out-implib,$T/lib$name-dll-$v.a"
    fi
    # shellcheck disable=SC2086
    "$CXX" $CXXFLAGS $flags $dllflags $usrdll -shared $dllsrcs -L"$OUT" $ldflags $implib -o "$T/$name-dll-$v.dll" \
      2>"$T/$name-dll-$v-build.log" || {
      echo "FAIL   $label-$v (DLL build)"; cat "$T/$name-dll-$v-build.log"; nfail=$((nfail + 1)); return; }
    [ -n "$implib" ] && srcs="$srcs $T/lib$name-dll-$v.a"
  fi
  if [ -f tests/$name.rc ]; then
    "$RC" $RCFLAGS tests/$name.rc -o "$T/$name-res.o" 2>"$T/$name-rc.log" || {
      echo "FAIL   $label-$v (resources)"; cat "$T/$name-rc.log"; nfail=$((nfail + 1)); return; }
    srcs="$srcs $T/$name-res.o -mwindows"
  fi
  # shellcheck disable=SC2086
  "$CXX" $CXXFLAGS $flags $dllflags $srcs -L"$OUT" $ldflags -o "$exe" 2>"$T/$label-$v-build.log" || {
    echo "FAIL   $label-$v (build)"; cat "$T/$label-$v-build.log"; nfail=$((nfail + 1)); return; }
  if [ ! -f tests/$name.rc ]; then
    # The console tests (no resources) report on standard output.
    if [ $HAVE_WINE = 1 ]; then
      if timeout 120 wine "$exe" > "$outdir/$name.log" 2>&1; then
        echo "PASS   $label-$v"; npass=$((npass + 1))
        # Notes a passing test leaves, such as checks it could not run.
        grep '^note:' "$outdir/$name.log" | sed 's/^/       /'
      else
        echo "FAIL   $label-$v"; sed 's/^/       /' "$outdir/$name.log"; nfail=$((nfail + 1))
      fi
    else
      echo "built  $label-$v (no wine; not run)"
    fi
  else
    run_test "$name" "$exe" "$outdir" "$label"
  fi
}

for v in a u; do
  flags=
  [ $v = u ] && flags="-DUNICODE -D_UNICODE"
  for name in $TESTS; do
    build_and_run "$name" "$name" "$T/$name-$v.exe" "$T/$v" 0 "$LDFLAGS"
  done
done

# The shared library: the tests in SHARED_TESTS again, built with _AFXDLL,
# their output in $T/sa and $T/su. Wine finds mfc.dll through WINEPATH,
# with the C++ runtime's DLLs where the compiler has them (MinGW: an
# exception crosses from one module to another only with libstdc++ and
# libgcc in DLLs, so -static is left out).
if [ -n "$SHARED_TESTS" ] && [ -f "$OUT/mfc.dll" ]; then
  shared_ldflags=$(echo " $LDFLAGS " | sed 's/ -static / /g')
  winepath=$(winpath "$OUT")
  for f in libstdc++-6.dll libgcc_s_dw2-1.dll libwinpthread-1.dll; do
    # shellcheck disable=SC2086
    p=$("$CXX" $CXXFLAGS -print-file-name=$f 2>/dev/null)
    [ -f "$p" ] && winepath="$winepath;$(winpath "$(dirname "$p")")"
  done
  export WINEPATH="$winepath"
  for v in a u; do
    flags=
    [ $v = u ] && flags="-DUNICODE -D_UNICODE"
    mkdir -p "$T/s$v"
    for name in $SHARED_TESTS; do
      build_and_run "$name" "$name-shared" "$T/$name-shared-$v.exe" "$T/s$v" 1 "$shared_ldflags"
    done
  done
fi

echo "$npass passed, $nfail failed"
[ $nfail = 0 ]
