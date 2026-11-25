#!/usr/bin/env python3
# build-sdk-samples.py - builds sample programs from a Win32 SDK of your
# own (the Windows NT 3.1, NT 3.51 or Windows 95 SDK) over the portable
# Win32 layer: MULTIPAD (the Notepad-style editor) and the OpenGL screen
# savers (3D Flying Objects, Flower Box, Maze, Pipes and 3D Text).
#
# The SDK's files are not part of this project. The script copies the
# sources it needs from the SDK into the output directory, makes the file
# names lower case and the #include paths use '/', then compiles them.
# See docs/sdk-samples.md.
#
#   build-sdk-samples.py --sdk DIR [--out DIR] [--only NAME,...]
#
# The compiler and the libraries come from the environment:
#   CC, CXX   C and C++ compilers (default: clang, clang++ in $BIN or PATH)
#   CFLAGS    flags for C and C++ (target, sysroot and so on)
#   CXXFLAGS  more flags for C++ (the C++ library's headers)
#   LDFLAGS   flags for the link (target, sysroot, linker, C++ library)
#   MFC_OUT   the directory with libwinport.a and libglaux.a
#   SYSLIBS   system libraries (default: the Mac OS X frameworks)
#   HOST_CXX  a C++ compiler for the host, for res2cpp.py (default: CXX)
#
# Part of clang-retro MFC, an independent implementation of the MFC API.
# SPDX-License-Identifier: AGPL-3.0-only
import argparse
import os
import re
import shlex
import shutil
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
PORT_INCLUDE = os.path.join(ROOT, "port", "include")
RES2CPP = os.path.join(ROOT, "port", "tools", "res2cpp.py")

# The samples: a name, the path that ends the sample's folder in the SDK
# (any case), a file that must be in it, whether it is an OpenGL screen
# saver (which adds the savers' COMMON folder), and the program's file name.
SAMPLES = [
    ("multipad", "multipad", "mpfind.c", False, "multipad"),
    ("3dfo", "scrsave/3dfo", "ss3dfo.c", True, "ss3dfo.scr"),
    ("flwbox", "scrsave/flwbox", "ssflwbox.c", True, "ssflwbox.scr"),
    ("maze", "scrsave/maze", "ssmaze.c", True, "ssmaze.scr"),
    ("pipes", "scrsave/pipes", "sspipes.cxx", True, "sspipes.scr"),
    ("text3d", "scrsave/text3d", "sstext3d.c", True, "sstext3d.scr"),
]
COMMON = ("scrsave/common", "glscrnsv.cxx")

# Small fixes to single files, as (file, pattern, replacement): code
# that only the compilers of the time accepted.
PATCHES = [
    # A BOOL function's result stored in an input context (a DWORD then).
    ("scrnsave.cxx", r"hPrevImc = ImmFnc\(", "hPrevImc = (HIMC)(UINT_PTR)ImmFnc("),
    # A main that returns void, for old NT screen savers; the layer has main.
    ("scrnsave.cxx", r"void _cdecl main\(", "static void _cdecl ss_old_main("),
    # A global named index, which is also a function of the C library.
    ("geom.c", r"\bindex\b", "vertex_index"),
    # A structure named first in a prototype, which C scopes to it.
    ("maze.h", r"(BOOL InitMaze\()", r"struct _MazeGoal;\n\1"),
]

# Structured exception handling: the guarded block runs and the handler
# does not (no exception reaches it here).
SEH_DEFINES = ["-D_try=if(1)", "-D__try=if(1)", "-D_except(x)=else if(0)", "-D__except(x)=else if(0)",
               "-D_finally=", "-D__finally="]

SOURCE_EXTS = (".c", ".cxx", ".cpp")
TEXT_EXTS = (".c", ".cxx", ".cpp", ".h", ".hxx", ".rc", ".dlg", ".rc2")
HEADER_EXTS = (".h", ".hxx")


# Finds the folder under sdk whose path ends with tail (compared without
# case) and that holds the file marker. Returns None when there is none.
def find_dir(sdk, tail, marker):
    parts = tail.lower().split("/")
    for dirpath, dirnames, filenames in os.walk(sdk):
        dirnames.sort()
        rel = os.path.relpath(dirpath, sdk).replace(os.sep, "/").lower().split("/")
        if rel[-len(parts):] != parts:
            continue
        if marker in (f.lower() for f in filenames):
            return dirpath
    return None


# Rewrites one #include path: '/' for '\', lower case (the copies have
# lower-case names), and GL/ for the OpenGL headers.
def fix_include_path(path):
    path = path.replace("\\", "/").lower()
    if path.startswith("gl/"):
        path = "GL/" + path[3:]
    return path


# Rewrites the #include lines of a source file's text.
def fix_includes(text):
    def repl(m):
        return m.group(1) + m.group(2) + fix_include_path(m.group(3)) + m.group(4)
    return re.sub(r'^(\s*#\s*include\s*)([<"])([^>"]+)([>"])', repl, text, flags=re.M)


# Statements of a resource script whose string can be followed by the
# next field without a comma: the resource compiler of Windows accepts that.
RC_ITEMS = ("MENUITEM", "CONTROL", "LTEXT", "RTEXT", "CTEXT", "PUSHBUTTON", "DEFPUSHBUTTON",
            "CHECKBOX", "AUTOCHECKBOX", "RADIOBUTTON", "AUTORADIOBUTTON", "GROUPBOX", "STATE3",
            "AUTO3STATE", "PUSHBOX")


# Puts in the commas that old resource scripts leave out after a string,
# which llvm-rc needs.
def fix_rc(text):
    out = []
    for line in text.split("\n"):
        words = line.split(None, 1)
        if words and words[0].upper() in RC_ITEMS:
            line = re.sub(r'("(?:[^"]|"")*")[ \t]+(?=[A-Za-z0-9_\-(])', r"\1, ", line)
        out.append(line)
    return "\n".join(out)


# Rewrites the Microsoft C cast-as-lvalue idiom *((TYPE *)p)++, which
# reads a TYPE and steps p past it, into standard C.
def fix_lvalue_casts(text):
    def repl(m):
        t, p = m.group(1).strip(), m.group(2)
        return "(*(%s*)(%s = (void*)((char*)%s + sizeof(%s)), (char*)%s - sizeof(%s)))" % (t, p, p, t, p, t)
    return re.sub(r"\*\s*\(\s*\(\s*([\w\s]+?)\s*\*\s*\)\s*(\w+)\s*\)\s*\+\+", repl, text)


# Gives a type to the old implicit-int declarations "static name;" and
# "static name = value;", which C++ does not accept.
def fix_implicit_int(text):
    return re.sub(r"\bstatic(\s+)(?!int\b|const\b|volatile\b)([A-Za-z_]\w*)(\s*[;=])", r"static\1int \2\3", text)


# Gives LRESULT to window procedures that old samples declare as LONG:
# LRESULT is wider than LONG on a 64-bit system, and C++ does not convert
# the function pointer.
def fix_window_procs(text):
    return re.sub(r"\bLONG(\s+(?:APIENTRY\s+|WINAPI\s+|CALLBACK\s+|FAR\s+PASCAL\s+|PASCAL\s+)?)((?:\w+|\(\s*\*\s*\w+\s*\))\s*\(\s*(?:register\s+)?HWND\b)",
                  r"LRESULT\1\2", text)


# Copies the files of folder src into dst with lower-case names, and fixes
# the #include lines of the text files and the commas of resource scripts.
def copy_sources(src, dst):
    os.makedirs(dst, exist_ok=True)
    for name in sorted(os.listdir(src)):
        path = os.path.join(src, name)
        if not os.path.isfile(path):
            continue
        out = os.path.join(dst, name.lower())
        if name.lower().endswith(TEXT_EXTS):
            with open(path, "rb") as f:
                text = f.read().decode("latin-1")
            text = fix_includes(text)
            if name.lower().endswith(SOURCE_EXTS):
                text = fix_lvalue_casts(text)
                text = fix_implicit_int(text)
            if name.lower().endswith(SOURCE_EXTS + (".h", ".hxx")):
                text = fix_window_procs(text)
            for file, pattern, replacement in PATCHES:
                if name.lower() == file:
                    text = re.sub(pattern, replacement, text)
            if name.lower().endswith((".rc", ".dlg", ".rc2")):
                text = fix_rc(text)
            with open(out, "wb") as f:
                f.write(text.encode("latin-1"))
        else:
            shutil.copyfile(path, out)


# Reads a sample's MAKEFILE: the object files of the program (or library)
# and the extra -D flags of its cflags lines.
def read_makefile(folder):
    path = os.path.join(folder, "makefile")
    if not os.path.exists(path):
        return [], []
    text = open(path, "rb").read().decode("latin-1")
    objs = []
    for m in re.finditer(r"\b(\w+)\.obj\b", text):
        name = m.group(1).lower()
        if name not in objs:
            objs.append(name)
    defines = []
    for line in text.splitlines():
        line = line.strip()
        if line.startswith("#") or not line.lower().startswith("cflags"):
            continue
        defines += [d for d in line.split() if d.startswith("-D")]
    return objs, defines


# The sources of the objects that a makefile names, found in folder.
def sources_for(folder, objs):
    files = {f[: f.rfind(".")]: f for f in os.listdir(folder) if f.endswith(SOURCE_EXTS)}
    return [os.path.join(folder, files[o]) for o in objs if o in files]


# The headers of the C and C++ libraries that sample headers include.
STD_HEADERS = ("assert.h", "ctype.h", "errno.h", "float.h", "limits.h", "math.h", "memory.h",
               "stdarg.h", "stddef.h", "stdio.h", "stdlib.h", "string.h", "time.h", "tchar.h",
               "io.h", "malloc.h", "process.h", "sys/types.h", "sys/stat.h", "sys/timeb.h")


# Writes, for each header of the folders, a stand-in that keeps only the
# preprocessor lines. The resource compiler of Windows reads .h files in
# this way, so headers with C declarations can be included from a .rc file.
def rc_stand_ins(folders, out):
    os.makedirs(os.path.join(out, "GL"), exist_ok=True)
    for name in ("gl.h", "glu.h", "glaux.h"):
        open(os.path.join(out, "GL", name), "w").close()
    # The C library's headers hold nothing a resource script uses.
    for name in STD_HEADERS:
        os.makedirs(os.path.dirname(os.path.join(out, name)), exist_ok=True)
        open(os.path.join(out, name), "w").close()
    for folder in folders:
        for name in os.listdir(folder):
            if not name.endswith(HEADER_EXTS):
                continue
            keep = []
            cont = False
            for line in open(os.path.join(folder, name), "rb").read().decode("latin-1").splitlines():
                if cont or line.lstrip().startswith("#"):
                    keep.append(line)
                    cont = line.rstrip().endswith("\\")
            with open(os.path.join(out, name), "w", encoding="latin-1") as f:
                f.write("\n".join(keep) + "\n")


# Runs a command (with the environment env, or this one) and stops the
# script with its output if it fails.
def run(cmd, log, env=None):
    r = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, env=env)
    log.write(" ".join(shlex.quote(c) for c in cmd) + "\n")
    log.write(r.stdout.decode(errors="replace"))
    if r.returncode != 0:
        sys.stderr.write(r.stdout.decode(errors="replace")[-4000:])
        raise RuntimeError("failed: %s" % cmd[0])


# Old C++ compilers kept a variable declared in a for statement in scope
# after the loop. For each name that the compiler reports as undeclared,
# moves the declaration of the nearest such loop above it out of the for
# statement. Returns True when it changed the file.
def fix_for_scope(src, output):
    lines = open(src, "rb").read().decode("latin-1").split("\n")
    changed = False
    for m in re.finditer(r":(\d+):\d+: error: use of undeclared identifier '(\w+)'", output):
        line, name = int(m.group(1)) - 1, m.group(2)
        decl = re.compile(r"\bfor\s*\(\s*((?:unsigned\s+|const\s+)?\w+)\s+(%s)\b" % name)
        for k in range(min(line, len(lines) - 1), -1, -1):
            d = decl.search(lines[k])
            if d:
                lines[k] = lines[k][: d.start()] + "%s %s; for( %s" % (d.group(1), name, name) + lines[k][d.end():]
                changed = True
                break
    if changed:
        with open(src, "wb") as f:
            f.write("\n".join(lines).encode("latin-1"))
    return changed


# The C library of Visual C++, where it differs from the system's in a way
# the samples see: rand gives numbers up to 0x7FFF, and the samples work
# out RAND_MAX + 1 in an int.
VCRT_HEADER = """// Generated by build-sdk-samples.py.
#pragma once
#include <stdlib.h>
#undef RAND_MAX
#define RAND_MAX 0x7FFF
#define rand() (rand() & RAND_MAX)
"""


# Compiles the sources into objects in objdir and returns the objects.
# The release build of the SDK's makefiles: NDEBUG, so assert does nothing.
def compile_all(sources, objdir, includes, defines, env, log):
    os.makedirs(objdir, exist_ok=True)
    vcrt = os.path.join(objdir, "vcrt.h")
    with open(vcrt, "w") as f:
        f.write(VCRT_HEADER)
    objs = []
    for src in sources:
        cxx = not src.endswith(".c")
        # The folder in the name: the sample and the common code can each
        # have a file of the same name (such as dialog.c).
        folder = os.path.basename(os.path.dirname(src))
        obj = os.path.join(objdir, folder + "-" + os.path.basename(src) + ".o")
        cmd = [env["CXX" if cxx else "CC"]] + env["CFLAGS"] + (env["CXXFLAGS"] if cxx else [])
        cmd += ["-w", "-DWIN32", "-D_WIN32", "-DWINVER=0x0400", "-DNDEBUG", "-include", vcrt,
                "-I", PORT_INCLUDE] + SEH_DEFINES
        if not cxx:
            # C written for the first Win32 compilers: handles that are all
            # HANDLE, window procedures declared with UINT and LONG, and
            # functions used before they are declared. Globals defined in
            # more than one file without a value share one variable
            # (-fcommon), as with the linkers of that time.
            cmd += ["-DNO_STRICT", "-std=gnu89", "-fcommon", "-Wno-incompatible-function-pointer-types",
                    "-Wno-incompatible-pointer-types", "-Wno-int-conversion",
                    "-Wno-implicit-function-declaration", "-Wno-implicit-int"]
        cmd += [a for i in includes for a in ("-I", i)] + defines + ["-c", src, "-o", obj]
        for attempt in range(20):
            r = subprocess.run(cmd + ["-ferror-limit=0"], stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
            out = r.stdout.decode(errors="replace")
            if r.returncode == 0 or not cxx or not fix_for_scope(src, out):
                break
            log.write("%s: moved for-loop variables out of their loops\n" % src)
        if r.returncode != 0:
            run(cmd, log)
        objs.append(obj)
    return objs


# Builds one sample. Returns the path of the program.
def build_sample(sample, sdk, out, env, log):
    name, tail, marker, saver, program = sample
    src = find_dir(sdk, tail, marker)
    if not src:
        raise RuntimeError("%s: no folder %s in the SDK" % (name, tail))
    work = os.path.join(out, "src", name)
    copy_sources(src, work)
    folders = [work]
    objs, defines = read_makefile(work)
    sources = sources_for(work, objs)
    if saver:
        csrc = find_dir(sdk, *COMMON)
        if not csrc:
            raise RuntimeError("%s: no folder %s in the SDK" % (name, COMMON[0]))
        common = os.path.join(out, "src", "common")
        copy_sources(csrc, common)
        folders.append(common)
        cobjs, cdefs = read_makefile(common)
        sources += sources_for(common, cobjs)
    includes = folders
    objdir = os.path.join(out, "obj", name)
    objects = compile_all(sources, objdir, includes, defines, env, log)
    # The resources, as C++ that registers them when the program starts.
    rcs = [f for f in os.listdir(work) if f.endswith(".rc")]
    if rcs:
        stand = os.path.join(out, "rc", name)
        rc_stand_ins(folders, stand)
        res = os.path.join(objdir, name + "-res.cpp")
        # res2cpp.py reads the layer's macros with $CXX for the host. A
        # compiler that has only the target can be replaced with HOST_CXX.
        res_env = dict(os.environ)
        if os.environ.get("HOST_CXX"):
            res_env["CXX"] = os.environ["HOST_CXX"]
        # The scripts are in the Western Windows code page (the copyright
        # sign of an About box, for example).
        cmd = [sys.executable, RES2CPP, "--codepage", "1252", "-I", stand] + [a for f in folders for a in ("-I", f)]
        cmd += [a for d in defines for a in ("-D", d[2:])] + [os.path.join(work, rcs[0]), "-o", res]
        run(cmd, log, res_env)
        objects += compile_all([res], objdir, includes, [], env, log)
    exe = os.path.join(out, "bin", program)
    os.makedirs(os.path.dirname(exe), exist_ok=True)
    libs = [os.path.join(env["MFC_OUT"], "libglaux.a")] if saver else []
    libs.append(os.path.join(env["MFC_OUT"], "libwinport.a"))
    run([env["CXX"]] + env["LDFLAGS"] + objects + libs + env["SYSLIBS"] + ["-o", exe], log)
    return exe


# Reads the compilers and flags from the environment.
def environment():
    bindir = os.environ.get("BIN", "")
    tool = lambda n: os.path.join(bindir, n) if bindir else n
    env = {
        "CC": os.environ.get("CC", tool("clang")),
        "CXX": os.environ.get("CXX", tool("clang++")),
        "CFLAGS": shlex.split(os.environ.get("CFLAGS", "")),
        "CXXFLAGS": shlex.split(os.environ.get("CXXFLAGS", "")),
        "LDFLAGS": shlex.split(os.environ.get("LDFLAGS", "")),
        "MFC_OUT": os.environ.get("MFC_OUT", os.path.join(ROOT, "build")),
        "SYSLIBS": shlex.split(os.environ.get(
            "SYSLIBS", "-framework Cocoa -framework OpenGL -framework GLUT")),
    }
    return env


# Builds the samples that are asked for and prints the result of each.
def main():
    p = argparse.ArgumentParser(description="Build Win32 SDK samples over the portable layer.")
    p.add_argument("--sdk", required=True, help="the SDK, or its SAMPLES folder")
    p.add_argument("--out", default="build-sdk-samples", help="the output directory")
    p.add_argument("--only", default="", help="the samples to build, separated by commas")
    args = p.parse_args()
    env = environment()
    only = [n for n in args.only.split(",") if n]
    os.makedirs(args.out, exist_ok=True)
    failed = 0
    with open(os.path.join(args.out, "build.log"), "w") as log:
        for sample in SAMPLES:
            if only and sample[0] not in only:
                continue
            try:
                exe = build_sample(sample, args.sdk, args.out, env, log)
                print("ok     %s: %s" % (sample[0], exe))
            except RuntimeError as e:
                failed += 1
                print("FAIL   %s (%s)" % (sample[0], e))
    sys.exit(1 if failed else 0)


if __name__ == "__main__":
    main()
