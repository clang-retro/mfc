#!/bin/sh
# run-haiku-tests.sh - builds the tests for Haiku against the libraries
# and runs them in Haiku, in a QEMU virtual machine.
#
# Called by `make check TARGET=x86_64-unknown-haiku SYSROOT=...` (or
# i586-pc-haiku), which sets BIN, SYSROOT, TARGET and OUT.
#
# HAIKU_IMAGE names a Haiku anyboot image (the nightly images of
# download.haiku-os.org). clang-retro/unix-sdk's haiku/tools/haiku-vm.py
# (HAIKU_VM, by default ../unix-sdk/haiku/tools/haiku-vm.py) starts it,
# and runs each test in it. Without HAIKU_IMAGE the tests are only built.
# A test passes when it ends with status 0. The tests run with the
# headless backend (WINPORT_BACKEND=headless), as on Linux. With
# HAIKU_VM_RUNNING=1 the tests use a "haiku-vm.py serve" that already
# runs (started with --files $OUT/tests), and leave it running. showcase,
# the sample, runs with the Haiku backend and its screenshots go to
# $OUT/tests/shots.
set -u

TARGET=${TARGET:-x86_64-unknown-haiku}
OUT=${OUT:-build/$TARGET}
SYSROOT=${SYSROOT:?set SYSROOT to the Haiku sysroot}
BIN=${BIN:-}
CXX=${BIN:+$BIN/}clang++
HAIKU_IMAGE=${HAIKU_IMAGE:-}
HAIKU_VM=${HAIKU_VM:-../unix-sdk/haiku/tools/haiku-vm.py}
PORT=${HAIKU_VM_PORT:-8765}
TESTS=${HAIKU_TESTS:-"core memtest wintest disptest isapitest dlgtest sditest mditest cmntest splittest docktest rvtest socktest occtest dlltest oletest iptest docobtest linktest bindtest richtest richsrvtest autotest srvtest atlwintest atltest conntest ctltest wltest dragtest daotest dvtest htmltest atlctltest atlutiltest comdeftest fptest fpctrltest cdlgtest vistatest iotest dpitest imetest sstest hlptest gltest"}

T=$OUT/tests
mkdir -p "$T/a" "$T/u" "$T/winhelp"
# hlptest reads the help files of tests/winhelp.
cp tests/winhelp/*.hlp tests/winhelp/*.cnt "$T/winhelp/"
python3 tests/gen-toolbar-bmp.py "$T/toolbar.bmp" 6 || exit 1
# llvm-rc compiles the resources: the one in BIN, on the PATH, or one
# that came with an LLVM installed beside the system compiler.
RC_NATIVE=${BIN:+$BIN/}llvm-rc
if [ -z "$BIN" ] && ! command -v llvm-rc >/dev/null 2>&1; then
  for rc in /usr/lib/llvm-*/bin/llvm-rc; do
    [ -x "$rc" ] && RC_NATIVE=$rc
  done
fi
CXXFLAGS="--target=$TARGET --sysroot=$SYSROOT -fuse-ld=lld -std=c++17 -D_DEFAULT_SOURCE"
CXXFLAGS="$CXXFLAGS -Wall -Wno-unused-function -Wno-overloaded-virtual -Wno-missing-exception-spec"
CXXFLAGS="$CXXFLAGS -Iport/include -Iinclude -Itests -Iport/softgl/include"
SYSLIBS="-lbe -lnetwork -lbsd"

nfail=0
npass=0
built=

# Builds one test (name, a or u). The program goes to $T/NAME-V, and a
# DLL of its own to $T/NAME-dll-V.dll, as run-tests.sh makes them.
build_test() {  # name v
  name=$1 v=$2
  flags= lib=-lmfc
  [ $v = u ] && flags="-DUNICODE -D_UNICODE" && lib=-lmfcu
  srcs=tests/$name.cpp
  if [ -f tests/$name.rc ]; then
    CXX="$CXX" RC="$RC_NATIVE" python3 port/tools/res2cpp.py -I include -I tests -I "$T" \
      tests/$name.rc -o "$T/$name-res.cpp" 2>"$T/$name-rc.log" || return 1
    srcs="$srcs $T/$name-res.cpp"
  fi
  exeflags=
  if [ -f tests/$name-dll.cpp ]; then
    [ -f tests/$name-dll.def ] || return 2
    dllsrcs=tests/$name-dll.cpp
    if [ -f tests/$name-dll.rc ]; then
      CXX="$CXX" RC="$RC_NATIVE" python3 port/tools/res2cpp.py -I include -I tests -I "$T" \
        tests/$name-dll.rc -o "$T/$name-dll-res.cpp" 2>"$T/$name-dll-rc.log" || return 1
      dllsrcs="$dllsrcs $T/$name-dll-res.cpp"
    fi
    dll=$T/$name-dll-$v.dll
    exports=$(sed -n '/^EXPORTS/,$p' tests/$name-dll.def | sed '1d' | awk '{split($1, a, "="); print a[1]}')
    { echo "{ global:"; for e in DllMain $exports; do echo "  $e;"; done; echo "local: *; };"; } > "$T/$name-dll.map"
    # The DLL takes the layer from the program, which exports it.
    # shellcheck disable=SC2086
    "$CXX" $CXXFLAGS $flags -fPIC -shared -Wl,-soname,"$(basename "$dll")" -Wl,--version-script="$T/$name-dll.map" \
      -Wl,--allow-shlib-undefined -Wl,--unresolved-symbols=ignore-all -D_USRDLL $dllsrcs -L"$OUT" $lib \
      -o "$dll" 2>"$T/$name-dll-$v-build.log" || return 1
    exeflags=-rdynamic
  fi
  [ $name = sstest ] && { [ $v = u ] && lib="-lscrnsavw $lib" || lib="-lscrnsave $lib"; }
  # shellcheck disable=SC2086
  "$CXX" $CXXFLAGS $flags $exeflags $srcs -L"$OUT" $lib -lwinport $SYSLIBS -o "$T/$name-$v" \
    2>"$T/$name-$v-build.log"
}

for v in a u; do
  for name in $TESTS; do
    build_test "$name" $v
    case $? in
      0) built="$built $name-$v" ;;
      2) echo "skip   $name-$v (an extension DLL needs the shared library)" ;;
      *) echo "FAIL   $name-$v (build; see $T/$name-$v-build.log)"; nfail=$((nfail + 1)) ;;
    esac
  done
done

# The sample, which shows the Haiku backend.
S=samples/showcase
if CXX="$CXX" RC="$RC_NATIVE" python3 port/tools/res2cpp.py -I include -I $S -I "$T" $S/showcase.rc \
     -o "$T/showcase-res.cpp" 2>"$T/showcase-rc.log" &&
   "$CXX" $CXXFLAGS -I$S $S/showcase.cpp "$T/showcase-res.cpp" -L"$OUT" -lmfc -lwinport $SYSLIBS \
     -o "$T/showcase" 2>"$T/showcase-build.log"; then
  showcase=1
else
  echo "FAIL   showcase (build; see $T/showcase-build.log)"
  nfail=$((nfail + 1))
  showcase=0
fi

if [ -z "$HAIKU_IMAGE" ] && [ -z "${HAIKU_VM_RUNNING:-}" ]; then
  for t in $built; do echo "built  $t (no HAIKU_IMAGE; not run)"; done
  echo "$nfail failed to build"
  [ $nfail = 0 ]
  exit
fi

# The virtual machine: its agent fetches the programs from $T.
if [ -z "${HAIKU_VM_RUNNING:-}" ]; then
  python3 "$HAIKU_VM" serve "$HAIKU_IMAGE" --files "$T" --shots "$T/shots" --port "$PORT" > "$T/vm.log" 2>&1 &
  vm=$!
  # A "serve" that is still starting does not take "stop": kill it then.
  trap 'python3 "$HAIKU_VM" stop --port "$PORT" 2>/dev/null || kill $vm 2>/dev/null; wait $vm 2>/dev/null' EXIT
fi
# Runs a shell command in Haiku and writes its output; the status is the
# command's.
vmexec() {
  python3 "$HAIKU_VM" exec --port "$PORT" "$1"
}
G=http://10.0.2.2:$PORT/files
# The tests work in /boot/home/mfc: /tmp is a symbolic link in Haiku, and
# the tests compare the paths that they make with the paths that the
# system gives.
# The first command waits until Haiku has started.
if ! vmexec "mkdir -p /boot/home/mfc && echo Haiku is ready: \$(uname -a)"; then
  echo "FAIL   Haiku did not start (see $T/vm.log)"
  exit 1
fi

for t in $built; do
  name=${t%-*} v=${t##*-}
  outdir=$T/$v
  mkdir -p "$outdir"
  get="curl -sf -o $t $G/$t && chmod +x $t"
  [ -f "$T/$name-dll-$v.dll" ] && get="$get && curl -sf -o $name-dll-$v.dll $G/$name-dll-$v.dll"
  args=
  [ $name = sstest ] && args=/s
  if [ $name = hlptest ]; then
    for f in "$T"/winhelp/*; do
      get="$get && curl -sf -o winhelp/$(basename "$f") $G/winhelp/$(basename "$f")"
    done
    get="mkdir -p winhelp && $get"
  fi
  box=
  case $name in rvtest|dvtest) box=window ;; esac
  if vmexec "mkdir -p /boot/home/mfc/$v && cd /boot/home/mfc && $get && cd /boot/home/mfc/$v &&
      WINPORT_BACKEND=headless WINPORT_MESSAGEBOX=$box MFC_TEST_OUT=/boot/home/mfc/$v MFC_TEST_HLP_DIR=/boot/home/mfc/winhelp \
      WINPORT_REGISTRY=/boot/home/mfc/$v/registry.reg timeout 600 ../$t $args; s=\$?;
      cat /boot/home/mfc/$v/$name.log 2>/dev/null; rm -f ../$t; exit \$s" > "$outdir/$name.log" 2>&1; then
    echo "PASS   $t"; npass=$((npass + 1))
  else
    echo "FAIL   $t"; sed 's/^/       /' "$outdir/$name.log" | tail -20; nfail=$((nfail + 1))
  fi
done

if [ $showcase = 1 ]; then
  # The sample with the Haiku backend; shot.sh asks the host for a
  # screenshot of the screen at each step.
  if vmexec "cd /boot/home/mfc && curl -sf -o showcase $G/showcase && chmod +x showcase &&
      printf '#!/bin/sh\ncurl -s -o /dev/null http://10.0.2.2:$PORT/shot/showcase-\$1\n' > shot.sh &&
      chmod +x shot.sh && SHOWCASE_DEMO=1 SHOWCASE_SHOT=/boot/home/mfc/shot.sh timeout 120 ./showcase" \
      > "$T/showcase.log" 2>&1 && [ -f "$T/shots/showcase-about.png" ]; then
    echo "PASS   showcase (screenshots in $T/shots)"; npass=$((npass + 1))
  else
    echo "FAIL   showcase"; sed 's/^/       /' "$T/showcase.log"; nfail=$((nfail + 1))
  fi
fi

echo "$npass passed, $nfail failed"
[ $nfail = 0 ]
