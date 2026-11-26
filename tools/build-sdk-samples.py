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
#   SYSLIBS   system libraries (default: the Mac OS X frameworks on Mac
#             OS X, none on 32-bit DOS, else -ldl -lpthread -lm)
#   HOST_CXX  a C++ compiler for the host, for res2cpp.py (default: CXX)
#   MACSDK    clang-retro/mac-sdk, for the classic Mac OS: its
#             tools/macapp.py links the program (default: ../mac-sdk)
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
# The GL headers of the generic implementation, for the systems that have
# no OpenGL of their own (all but Mac OS X).
SOFTGL_INCLUDE = os.path.join(ROOT, "port", "softgl", "include")

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
# On Mac OS X, each screen saver is also a .saver bundle with these names,
# which System Preferences shows in its list of screen savers.
SAVER_NAMES = {
    "3dfo": "3D Flying Objects",
    "flwbox": "3D Flower Box",
    "maze": "3D Maze",
    "pipes": "3D Pipes",
    "text3d": "3D Text",
}
SAVER_BUNDLE_SRC = os.path.join(ROOT, "port", "saver", "saver_bundle.mm")
# On the classic Mac OS, the screen saver host (port/saver/macos): the
# control panel and the background engine, and one After Dark module for
# each screen saver.
MACOS_SAVER_DIR = os.path.join(ROOT, "port", "saver", "macos")
COCOA_BACKEND_SRC = os.path.join(ROOT, "port", "src", "backend_cocoa.mm")
INFO_PLIST = """<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple Computer//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
	<key>CFBundleDevelopmentRegion</key>
	<string>English</string>
	<key>CFBundleExecutable</key>
	<string>{exe}</string>
	<key>CFBundleIdentifier</key>
	<string>io.github.clang-retro.saver.{exe}</string>
	<key>CFBundleInfoDictionaryVersion</key>
	<string>6.0</string>
	<key>CFBundleName</key>
	<string>{name}</string>
	<key>CFBundlePackageType</key>
	<string>BNDL</string>
	<key>CFBundleShortVersionString</key>
	<string>1.0</string>
	<key>CFBundleSignature</key>
	<string>????</string>
	<key>CFBundleVersion</key>
	<string>1.0</string>
	<key>NSPrincipalClass</key>
	<string>{cls}</string>
</dict>
</plist>
"""

# Small fixes to single files, as (file, pattern, replacement): code
# that only the compilers of the time accepted.
PATCHES = [
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


# Gives LRESULT to window procedures that old samples declare as LONG.
# The script uses it only for a 64-bit target: there LRESULT is wider than
# LONG, and C++ does not convert the function pointer. On a 32-bit target
# the layer makes LRESULT the same type as LONG.
def fix_window_procs(text):
    return re.sub(r"\bLONG(\s+(?:APIENTRY\s+|WINAPI\s+|CALLBACK\s+|FAR\s+PASCAL\s+|PASCAL\s+)?)((?:\w+|\(\s*\*\s*\w+\s*\))\s*\(\s*(?:register\s+)?HWND\b)",
                  r"LRESULT\1\2", text)


# Copies the files of folder src into dst with lower-case names, and fixes
# the #include lines of the text files and the commas of resource scripts.
def copy_sources(src, dst, env):
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
            if name.lower().endswith(SOURCE_EXTS + (".h", ".hxx")) and env["POINTER_SIZE"] == 8:
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
        if env["SOFTGL"]:
            cmd += ["-I", SOFTGL_INCLUDE]
        if not cxx:
            # C written for the first Win32 compilers: handles that are all
            # HANDLE, window procedures declared with UINT and LONG, and
            # functions used before they are declared. Globals defined in
            # more than one file without a value share one variable
            # (-fcommon), as with the linkers of that time. index and
            # rindex are not functions of the C library of Windows: the
            # samples use the names for their own variables.
            cmd += ["-DNO_STRICT", "-std=gnu89", "-fcommon", "-fno-builtin-index", "-fno-builtin-rindex", "-Wno-incompatible-function-pointer-types",
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


# The screen savers keep their textures as resources of their own types:
# 100 for a .bmp file and 101 for a .a8 file. The layer does not change
# the byte order of such resources, and the code of the savers reads their
# numbers in the order of the processor. For a big-endian target, the
# script therefore writes the numbers of these files in that order.
TEXTURE_TYPES = {"100": "bmp", "101": "a8"}
RC_TEXTURE = re.compile(r'^\s*\w+\s+(100|101)\s+[^"\n]*"([^"]+)"', re.M)


# Turns the bytes of each 16-bit number in data[start:end] around.
def swap16(data, start, end):
    for i in range(start, end - 1, 2):
        data[i], data[i + 1] = data[i + 1], data[i]


# Turns the bytes of each 32-bit number in data[start:end] around.
def swap32(data, start, end):
    for i in range(start, end - 3, 4):
        data[i:i + 4] = data[i:i + 4][::-1]


# Gives a .bmp texture for a big-endian target. The loader (ssdib.c) reads
# a 32-bit field as two 16-bit numbers, the low one first. Thus the script
# turns around each 16-bit number of the headers and keeps their order.
# The color table and the pixels are bytes.
def bmp_big_endian(data):
    start = 14 if data[:2] == b"BM" else 0
    if len(data) < start + 4:
        return data
    size = data[start] | (data[start + 1] << 8)
    swap16(data, 0, min(len(data), start + size))
    return data


# Gives a .a8 texture for a big-endian target. The loader (ssa8.c) reads
# the six 32-bit numbers of the header whole. The palette holds the bytes
# of the colors. Compressed data is a sequence of 16-bit codes. After some
# codes, there is a 16-bit count or a number of bytes.
def a8_big_endian(data):
    if len(data) < 24 + 1024:
        return data
    compress = int.from_bytes(data[16:20], "little")
    swap32(data, 0, 24)
    if compress == 0:
        return data
    pos = 24 + 1024
    while pos + 2 <= len(data):
        run, esc = data[pos], data[pos + 1]
        swap16(data, pos, pos + 2)
        pos += 2
        if run != 0:
            continue
        if esc == 1:  # The end of the image.
            break
        if esc == 2:  # A count of zero pixels.
            swap16(data, pos, pos + 2)
            pos += 2
        elif esc >= 3:  # Pixels as bytes.
            pos += esc - 2
    return data


# Writes the texture files that the resource script of folder names as
# types 100 and 101 in big-endian order.
def textures_big_endian(folder):
    for rc in [f for f in os.listdir(folder) if f.endswith(".rc")]:
        with open(os.path.join(folder, rc), "rb") as f:
            text = f.read().decode("latin-1")
        for rtype, name in RC_TEXTURE.findall(text):
            path = os.path.join(folder, fix_include_path(name))
            if not os.path.isfile(path):
                continue
            with open(path, "rb") as f:
                data = bytearray(f.read())
            convert = bmp_big_endian if TEXTURE_TYPES[rtype] == "bmp" else a8_big_endian
            with open(path, "wb") as f:
                f.write(convert(data))


# Gives the predefined macros of the C compiler for the target, as a
# dictionary of names and values.
def target_macros(env):
    r = subprocess.run([env["CC"]] + env["CFLAGS"] + ["-dM", "-E", "-x", "c", os.devnull],
                       stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, universal_newlines=True)
    macros = {}
    for line in r.stdout.splitlines():
        parts = line.split(None, 2)
        if len(parts) >= 2 and parts[0] == "#define":
            macros[parts[1]] = parts[2] if len(parts) == 3 else ""
    return macros


# Builds one sample. Returns the path of the program.
def build_sample(sample, sdk, out, env, log):
    name, tail, marker, saver, program = sample
    src = find_dir(sdk, tail, marker)
    if not src:
        raise RuntimeError("%s: no folder %s in the SDK" % (name, tail))
    work = os.path.join(out, "src", name)
    copy_sources(src, work, env)
    if env["BIG_ENDIAN"]:
        textures_big_endian(work)
    folders = [work]
    objs, defines = read_makefile(work)
    sources = sources_for(work, objs)
    if saver:
        csrc = find_dir(sdk, *COMMON)
        if not csrc:
            raise RuntimeError("%s: no folder %s in the SDK" % (name, COMMON[0]))
        common = os.path.join(out, "src", "common")
        copy_sources(csrc, common, env)
        folders.append(common)
        cobjs, cdefs = read_makefile(common)
        sources += sources_for(common, cobjs)
    includes = folders
    objdir = os.path.join(out, "obj", name)
    # The common code of the savers has a main for old NT screen savers,
    # which returns void. The layer has the main of the program.
    cdefs = defines + (["-Dmain=ss_old_main"] if saver else [])
    objects = compile_all(sources, objdir, includes, cdefs, env, log)
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
    if env["CLASSIC"]:
        # The classic Mac OS: an application in MacBinary, linked by
        # mac-sdk's tools/macapp.py with mac-sdk's libraries.
        exe += ".bin"
        cpu = "68k" if "__m68k__" in env["MACROS"] else "ppc"
        # macapp.py adds the C++ library only for C++ sources, and these
        # are objects already.
        cxxlibs = [os.path.join(env["MACSDK"], "lib", "m68k" if cpu == "68k" else "ppc", n) for n in ("libstdc++.a", "libunwind.a")]
        link_env = dict(os.environ, CLANG=env["CC"])
        # A screen saver has the name that the control panel of the host
        # shows (for example "3D Pipes").
        title = SAVER_NAMES.get(name, os.path.splitext(program)[0]) if saver else os.path.splitext(program)[0]
        run([sys.executable, os.path.join(env["MACSDK"], "tools", "macapp.py"), "--cpu", cpu,
             "--lib", os.path.join(env["MACSDK"], "lib"), "--size", "8192", "--large",
             "--name", title, "-o", exe, "--"] + objects + libs + cxxlibs, log, link_env)
        if saver:
            build_ad_module(title, os.path.dirname(exe), env, log)
        return exe
    if env["DOS"]:
        exe = os.path.splitext(exe)[0] + ".exe"
    run([env["CXX"]] + env["LDFLAGS"] + objects + libs + env["SYSLIBS"] + ["-o", exe], log)
    if saver and env["MACHO"]:
        build_saver_bundle(name, program, objects, libs, objdir, env, log)
    return exe


# Builds the .saver bundle of a screen saver for the screen saver engine of
# Mac OS X (out/bin/NAME.saver), from the objects of the program. The
# bundle has its own principal class (port/saver/saver_bundle.mm) and its
# own copy of the AppKit half of the Cocoa backend, with class names that
# no other screen saver has. Only the principal class is exported.
# Returns the path of the bundle.
def build_saver_bundle(name, program, objects, libs, objdir, env, log):
    stem = os.path.splitext(program)[0]
    cls = "WinportSaver_" + stem
    flags = env["CFLAGS"] + env["CXXFLAGS"] + ["-x", "objective-c++", "-fno-objc-arc", "-std=c++17", "-w",
                                               "-I", os.path.join(ROOT, "port", "src")]
    shim = os.path.join(objdir, "saver_bundle.o")
    run([env["CXX"]] + flags + ["-DWINPORT_SAVER_CLASS=" + cls, "-c", SAVER_BUNDLE_SRC, "-o", shim], log)
    cocoa = os.path.join(objdir, "backend_cocoa.o")
    run([env["CXX"]] + flags + ["-DWINPORT_OBJC_PREFIX=WinportSS_" + stem, "-c", COCOA_BACKEND_SRC, "-o", cocoa], log)
    bundle = os.path.join(os.path.dirname(os.path.dirname(objdir)), "bin", SAVER_NAMES.get(name, stem) + ".saver")
    if os.path.isdir(bundle):
        shutil.rmtree(bundle)
    macos = os.path.join(bundle, "Contents", "MacOS")
    os.makedirs(macos)
    os.makedirs(os.path.join(bundle, "Contents", "Resources"))
    # The Objective-C 2 runtime (64-bit) names classes by symbol; the
    # older runtime (PowerPC and 32-bit Intel) by a marker symbol.
    if env["POINTER_SIZE"] == 8:
        exported = ["_OBJC_CLASS_$_" + cls, "_OBJC_METACLASS_$_" + cls]
    else:
        exported = [".objc_class_name_" + cls]
    exports = os.path.join(objdir, "exports.txt")
    with open(exports, "w") as f:
        f.write("\n".join(exported) + "\n")
    run([env["CXX"]] + env["LDFLAGS"] + ["-bundle", "-Wl,-exported_symbols_list," + exports] + objects +
        [shim, cocoa] + libs + env["SYSLIBS"] + ["-framework", "ScreenSaver", "-o", os.path.join(macos, stem)], log)
    with open(os.path.join(bundle, "Contents", "Info.plist"), "w") as f:
        f.write(INFO_PLIST.format(exe=stem, name=SAVER_NAMES.get(name, stem), cls=cls))
    return bundle


# Reads the compilers and flags from the environment.
# Runs mac-sdk's tools/macapp.py with the arguments args, with the
# compiler of env, without the C library.
def macapp(args, env, log):
    link_env = dict(os.environ, CLANG=env["CC"])
    run([sys.executable, os.path.join(env["MACSDK"], "tools", "macapp.py"), "--no-libc",
         "--lib", os.path.join(env["MACSDK"], "lib")] + args, log, link_env)


# Adds the resources res, as (type, ID, bytes), to the MacBinary file path.
def add_resources(path, res, env):
    sys.path.insert(0, os.path.join(env["MACSDK"], "tools", "link"))
    import macres
    name, ftype, creator, data, rsrc = macres.read_macbinary(open(path, "rb").read())
    resources = macres.read_resource_fork(rsrc) + res
    with open(path, "wb") as f:
        f.write(macres.macbinary(name, ftype, creator, data, macres.resource_fork(resources)))


# Builds the After Dark module of the screen saver title (out/TITLE.ADgm.bin):
# a 68k code resource that starts the program from the "Screen Savers"
# folder (port/saver/macos/ad_module.c).
def build_ad_module(title, out, env, log):
    module = os.path.join(out, title + ".ADgm.bin")
    macapp(["--cpu", "68k", "--code-resource", "ADgm,0", "--type", "ADgm", "--creator", "ADrk",
            "--name", title, "-o", module, "--", '-DSAVER_NAME="%s"' % title,
            os.path.join(MACOS_SAVER_DIR, "ad_entry.S"), os.path.join(MACOS_SAVER_DIR, "ad_module.c"),
            os.path.join(MACOS_SAVER_DIR, "saver_common.c")], env, log)
    credit = ("Starts %s from the Screen Savers folder." % title).encode("mac_roman")
    # 'Cals': the messages that the module wants (Initialize, Blank,
    # DrawFrame and Close). 'STR ' 128: the credit line.
    add_resources(module, [(b"Cals", 1, b"\x0f"), (b"STR ", 128, bytes([len(credit)]) + credit)], env)
    return module


# Builds the screen saver host of the classic Mac OS, for 68k and PowerPC:
# the control panel (out/Screen Saver.bin) and the background engine
# (out/Screen Saver Engine.bin).
def build_saver_host(out, env, log):
    common = os.path.join(MACOS_SAVER_DIR, "saver_common.c")
    macapp(["--type", "APPC", "--creator", "WPsc", "--size", "256", "--name", "Screen Saver",
            "-o", os.path.join(out, "Screen Saver.bin"), "--",
            os.path.join(MACOS_SAVER_DIR, "saver_panel.c"), common], env, log)
    macapp(["--type", "appe", "--creator", "WPse", "--background-only", "--size", "128",
            "--name", "Screen Saver Engine", "-o", os.path.join(out, "Screen Saver Engine.bin"), "--",
            os.path.join(MACOS_SAVER_DIR, "saver_engine.c"), common], env, log)


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
    }
    macros = target_macros(env)
    env["MACROS"] = macros
    macho = "__APPLE__" in macros and "__MACH__" in macros
    env["MACHO"] = macho
    # The classic Mac OS: mac-sdk's tools/macapp.py links the program.
    env["CLASSIC"] = "macintosh" in macros
    env["MACSDK"] = os.environ.get("MACSDK", os.path.join(ROOT, "..", "mac-sdk"))
    # Mac OS X and the classic Mac OS for PowerPC have OpenGL; the other
    # systems have the generic implementation in libwinport.a.
    powerpc = "__POWERPC__" in macros or "__powerpc__" in macros
    env["SOFTGL"] = not (macho or (env["CLASSIC"] and powerpc))
    # 32-bit DOS: the C library has everything, and a program must end in
    # .exe for DOS to start it.
    env["DOS"] = "__MSDOS__" in macros and "__i386__" in macros
    default_libs = "-framework Cocoa -framework OpenGL -framework GLUT -lobjc" if macho else "" if env["DOS"] else "-ldl -lpthread -lm"
    env["SYSLIBS"] = shlex.split(os.environ.get("SYSLIBS", default_libs))
    env["BIG_ENDIAN"] = macros.get("__BYTE_ORDER__") == "__ORDER_BIG_ENDIAN__"
    env["POINTER_SIZE"] = int(macros.get("__SIZEOF_POINTER__", "4"))
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
        if env["CLASSIC"] and any(s[3] and (not only or s[0] in only) for s in SAMPLES):
            try:
                build_saver_host(os.path.join(args.out, "bin"), env, log)
                print("ok     saver host: %s" % os.path.join(args.out, "bin"))
            except RuntimeError as e:
                failed += 1
                print("FAIL   saver host (%s)" % e)
    sys.exit(1 if failed else 0)


if __name__ == "__main__":
    main()
