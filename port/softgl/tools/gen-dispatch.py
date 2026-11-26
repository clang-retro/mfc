#!/usr/bin/env python3
"""Write the dispatch code of the software OpenGL (softgl).

    python3 port/softgl/tools/gen-dispatch.py

The script reads the OpenGL 1.0 and 1.1 functions from
port/softgl/include/GL/gl.h, and the extension functions in EXTENSIONS
below. It writes two files:

  - port/softgl/src/dispatch.h: the dispatch table (struct sgl_dispatch),
    with one entry for each function.
  - port/softgl/src/dispatch.c: the exported gl functions, the table that
    is used when no context is current, the table for execution, the
    table for display list compilation, the functions that record a
    command in a display list, and the function that plays a command
    back.

The design is that of opengl32.dll on Windows NT. Each exported gl
function calls the entry of the current dispatch table. The table
changes when a context becomes current and when glNewList starts or
glEndList stops the compilation of a display list.

The functions are of these kinds:

  - A canonical function changes the state or draws. The renderer has it
    as sgl_NAME. In a display list it is recorded: by a generated
    function save_NAME, or by a hand-written save function (HAND_SAVE).
  - A converter (a function of a family, such as glVertex3fv) changes
    its arguments to the canonical form and calls the canonical function
    through the current table. Thus it is the same in both tables. The
    script writes the converters.
  - A shared function is the same in both tables, because the
    specification does not put it in display lists (IMMEDIATE), or
    because it calls the canonical functions through the current table
    (THROUGH). The renderer has it as sgl_NAME.

Copyright (C) clang-retro contributors.
SPDX-License-Identifier: AGPL-3.0-only
"""
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
GL_H = os.path.join(ROOT, "include", "GL", "gl.h")
OUT_H = os.path.join(ROOT, "src", "dispatch.h")
OUT_C = os.path.join(ROOT, "src", "dispatch.c")

# The extensions of the generic implementation of Windows, which
# wglGetProcAddress gives. Each is (return type, name, parameters).
EXTENSIONS = [
    ("void", "AddSwapHintRectWIN", "GLint x, GLint y, GLsizei width, GLsizei height"),
    ("void", "ColorTableEXT", "GLenum target, GLenum internalFormat, GLsizei width, GLenum format, GLenum type, const void *table"),
    ("void", "ColorSubTableEXT", "GLenum target, GLsizei start, GLsizei count, GLenum format, GLenum type, const void *data"),
    ("void", "GetColorTableEXT", "GLenum target, GLenum format, GLenum type, void *data"),
    ("void", "GetColorTableParameterivEXT", "GLenum target, GLenum pname, GLint *params"),
    ("void", "GetColorTableParameterfvEXT", "GLenum target, GLenum pname, GLfloat *params"),
    ("void", "ArrayElementEXT", "GLint i"),
    ("void", "ColorPointerEXT", "GLint size, GLenum type, GLsizei stride, GLsizei count, const void *pointer"),
    ("void", "DrawArraysEXT", "GLenum mode, GLint first, GLsizei count"),
    ("void", "EdgeFlagPointerEXT", "GLsizei stride, GLsizei count, const GLboolean *pointer"),
    ("void", "GetPointervEXT", "GLenum pname, void **params"),
    ("void", "IndexPointerEXT", "GLenum type, GLsizei stride, GLsizei count, const void *pointer"),
    ("void", "NormalPointerEXT", "GLenum type, GLsizei stride, GLsizei count, const void *pointer"),
    ("void", "TexCoordPointerEXT", "GLint size, GLenum type, GLsizei stride, GLsizei count, const void *pointer"),
    ("void", "VertexPointerEXT", "GLint size, GLenum type, GLsizei stride, GLsizei count, const void *pointer"),
]

# Functions that the specification does not put in display lists: they
# run at once, also while a list is compiled.
IMMEDIATE = set("""
IsList DeleteLists GenLists NewList EndList FeedbackBuffer SelectBuffer
RenderMode ReadPixels PixelStoref PixelStorei GenTextures DeleteTextures
AreTexturesResident IsTexture IsEnabled Finish Flush GetError GetString
EdgeFlagPointer ColorPointer IndexPointer NormalPointer TexCoordPointer
VertexPointer EnableClientState DisableClientState InterleavedArrays
PushClientAttrib PopClientAttrib AddSwapHintRectWIN
GetColorTableEXT GetColorTableParameterivEXT GetColorTableParameterfvEXT
ColorPointerEXT EdgeFlagPointerEXT GetPointervEXT IndexPointerEXT
NormalPointerEXT TexCoordPointerEXT VertexPointerEXT
""".split())

# Functions that call the canonical functions through the current table.
# They are hand-written and the same in both tables.
THROUGH = set("""
DrawArrays DrawElements ArrayElement ArrayElementEXT DrawArraysEXT
Map1d Map2d PixelMapuiv PixelMapusv
""".split())

# Canonical functions whose save function is hand-written (save_NAME in
# list.c), because their pointers need the pixel store or a count that
# the arguments do not tell simply.
HAND_SAVE = set("""
TexImage1D TexImage2D TexSubImage1D TexSubImage2D DrawPixels Bitmap
PolygonStipple CallLists Map1f Map2f PixelMapfv ColorTableEXT
ColorSubTableEXT
""".split())

# The number of elements of each pointer parameter of a canonical
# function that has a generated save function: (function, parameter) ->
# a C expression.
POINTER_COUNTS = {
    ("Lightfv", "params"): "sgl_pname_count(pname)",
    ("Materialfv", "params"): "sgl_pname_count(pname)",
    ("LightModelfv", "params"): "sgl_pname_count(pname)",
    ("TexParameterfv", "params"): "sgl_pname_count(pname)",
    ("TexEnvfv", "params"): "sgl_pname_count(pname)",
    ("TexGenfv", "params"): "sgl_pname_count(pname)",
    ("Fogfv", "params"): "sgl_pname_count(pname)",
    ("LoadMatrixf", "m"): "16",
    ("MultMatrixf", "m"): "16",
    ("ClipPlane", "equation"): "4",
    ("PrioritizeTextures", "textures"): "n",
    ("PrioritizeTextures", "priorities"): "n",
}

# The integer types of the families, with the value that maps to 1.0
# when a color or a normal is normalized.
INT_MAX = {"b": "127.0f", "s": "32767.0f", "i": "2147483647.0f",
           "ub": "255.0f", "us": "65535.0f", "ui": "4294967295.0f"}
SUFFIX_TYPE = {"b": "GLbyte", "s": "GLshort", "i": "GLint", "f": "GLfloat",
               "d": "GLdouble", "ub": "GLubyte", "us": "GLushort",
               "ui": "GLuint"}


def parse_header():
    """Read the OpenGL 1.0 and 1.1 functions from gl.h.

    Gives a list of (return type, name without "gl", parameters)."""
    text = open(GL_H).read()
    start = text.index("#ifndef GL_VERSION_1_0")
    end = text.index("#endif /* GL_VERSION_1_1 */")
    funcs = []
    for m in re.finditer(r"^GLAPI (.+?) APIENTRY gl(\w+)\((.*)\);$",
                         text[start:end], re.M):
        ret, name, params = m.group(1).strip(), m.group(2), m.group(3).strip()
        funcs.append((ret, name, "" if params == "void" else params))
    return funcs + EXTENSIONS


def split_params(params):
    """Split a parameter list into (type, name, is_pointer) tuples."""
    out = []
    if not params:
        return out
    for p in params.split(","):
        p = p.strip()
        m = re.match(r"(.*?)(\w+)$", p)
        typ, name = m.group(1).strip(), m.group(2)
        out.append((typ, name, "*" in typ))
    return out


def call_args(params):
    """Give the names of the parameters, joined for a call."""
    return ", ".join(n for _, n, _ in split_params(params))


def decl_params(params):
    """Give a parameter list for a declaration (void when empty)."""
    return params if params else "void"


def scalar(typ, value, normalize):
    """Give a C expression that converts value of family type typ to
    GLfloat, normalized to [-1, 1] or [0, 1] when normalize is true."""
    if typ in ("f",):
        return value
    if typ == "d":
        return "(GLfloat)%s" % value
    if normalize:
        return "(GLfloat)%s / %s" % (value, INT_MAX[typ])
    return "(GLfloat)%s" % value


def converter(name, params):
    """Give the body of the converter function for name, or None when
    name is not a member of a family."""
    ps = split_params(params)
    m = re.match(r"^(Vertex|RasterPos)([234])(s|i|f|d)(v?)$", name)
    if m:
        n, t, v = int(m.group(2)), m.group(3), m.group(4)
        target = m.group(1) + "4f"
        if name == target:
            return None
        if v:
            vals = ["%s" % scalar(t, "v[%d]" % i, False) for i in range(n)]
        else:
            vals = [scalar(t, ps[i][1], False) for i in range(n)]
        vals += ["0.0f", "0.0f", "0.0f", "1.0f"][n:]
        return "SGL_DISPATCH->%s(%s);" % (target, ", ".join(vals))
    m = re.match(r"^TexCoord([1234])(s|i|f|d)(v?)$", name)
    if m:
        n, t, v = int(m.group(1)), m.group(2), m.group(3)
        if name == "TexCoord4f":
            return None
        if v:
            vals = [scalar(t, "v[%d]" % i, False) for i in range(n)]
        else:
            vals = [scalar(t, ps[i][1], False) for i in range(n)]
        vals += ["0.0f", "0.0f", "0.0f", "1.0f"][n:]
        return "SGL_DISPATCH->TexCoord4f(%s);" % ", ".join(vals)
    m = re.match(r"^Color([34])(b|s|i|f|d|ub|us|ui)(v?)$", name)
    if m:
        n, t, v = int(m.group(1)), m.group(2), m.group(3)
        if name == "Color4f":
            return None
        if v:
            vals = [scalar(t, "v[%d]" % i, True) for i in range(n)]
        else:
            vals = [scalar(t, ps[i][1], True) for i in range(n)]
        if n == 3:
            vals.append("1.0f")
        return "SGL_DISPATCH->Color4f(%s);" % ", ".join(vals)
    m = re.match(r"^Normal3(b|s|i|f|d)(v?)$", name)
    if m:
        t, v = m.group(1), m.group(2)
        if name == "Normal3f":
            return None
        if v:
            vals = [scalar(t, "v[%d]" % i, True) for i in range(3)]
        else:
            vals = [scalar(t, ps[i][1], True) for i in range(3)]
        return "SGL_DISPATCH->Normal3f(%s);" % ", ".join(vals)
    m = re.match(r"^Rect(s|i|f|d)(v?)$", name)
    if m:
        t, v = m.group(1), m.group(2)
        if name == "Rectf":
            return None
        if v:
            vals = [scalar(t, "v1[0]", False), scalar(t, "v1[1]", False),
                    scalar(t, "v2[0]", False), scalar(t, "v2[1]", False)]
        else:
            vals = [scalar(t, ps[i][1], False) for i in range(4)]
        return "SGL_DISPATCH->Rectf(%s);" % ", ".join(vals)
    m = re.match(r"^EvalCoord([12])(f|d)(v?)$", name)
    if m:
        n, t, v = int(m.group(1)), m.group(2), m.group(3)
        target = "EvalCoord%df" % n
        if name == target:
            return None
        if v:
            vals = [scalar(t, "u[%d]" % i, False) for i in range(n)]
        else:
            vals = [scalar(t, ps[i][1], False) for i in range(n)]
        return "SGL_DISPATCH->%s(%s);" % (target, ", ".join(vals))
    m = re.match(r"^Index(s|i|f|d|ub)(v?)$", name)
    if m:
        t, v = m.group(1), m.group(2)
        if name == "Indexf":
            return None
        val = scalar(t, "c[0]" if v else "c", False)
        return "SGL_DISPATCH->Indexf(%s);" % val
    if name == "EdgeFlagv":
        return "SGL_DISPATCH->EdgeFlag(flag[0]);"
    # The parameter families: a scalar or a vector of float, int or
    # double, to the float vector form.
    m = re.match(r"^(Light|Material|LightModel|TexParameter|TexEnv|TexGen|Fog)(d|f|i)(v?)$", name)
    if m:
        base, t, v = m.group(1), m.group(2), m.group(3)
        target = base + "fv"
        if name == target:
            return None
        lead = [n for _, n, _ in ps[:-1]]
        last = ps[-1][1]
        if not v:
            body = "GLfloat p[4];\n  p[0] = (GLfloat)%s;\n  p[1] = p[2] = p[3] = 0.0f;\n" % last
        else:
            norm = "sgl_pname_is_color(pname)" if t == "i" else "0"
            if t == "i":
                conv = "(norm ? (GLfloat)%s[i] / 2147483647.0f : (GLfloat)%s[i])" % (last, last)
            else:
                conv = "(GLfloat)%s[i]" % last
            body = ("GLfloat p[16];\n  int i, n = sgl_pname_count(pname);\n"
                    "  int norm = %s;\n  (void)norm;\n"
                    "  for (i = 0; i < n && i < 16; i++) p[i] = %s;\n") % (norm, conv)
        return body + "  SGL_DISPATCH->%s(%s);" % (target, ", ".join(lead + ["p"]))
    simple = {
        "PixelTransferi": "SGL_DISPATCH->PixelTransferf(pname, (GLfloat)param);",
        "PixelStoref": None,
        "LoadMatrixd": "GLfloat f[16];\n  int i;\n  for (i = 0; i < 16; i++) f[i] = (GLfloat)m[i];\n  SGL_DISPATCH->LoadMatrixf(f);",
        "MultMatrixd": "GLfloat f[16];\n  int i;\n  for (i = 0; i < 16; i++) f[i] = (GLfloat)m[i];\n  SGL_DISPATCH->MultMatrixf(f);",
        "Rotated": "SGL_DISPATCH->Rotatef((GLfloat)angle, (GLfloat)x, (GLfloat)y, (GLfloat)z);",
        "Translated": "SGL_DISPATCH->Translatef((GLfloat)x, (GLfloat)y, (GLfloat)z);",
        "Scaled": "SGL_DISPATCH->Scalef((GLfloat)x, (GLfloat)y, (GLfloat)z);",
        "MapGrid1d": "SGL_DISPATCH->MapGrid1f(un, (GLfloat)u1, (GLfloat)u2);",
        "MapGrid2d": "SGL_DISPATCH->MapGrid2f(un, (GLfloat)u1, (GLfloat)u2, vn, (GLfloat)v1, (GLfloat)v2);",
    }
    if name in simple:
        return simple[name]
    return None


def kinds(funcs):
    """Sort each function into canonical, converter or shared.

    Gives a dict name -> (kind, converter body or None)."""
    out = {}
    for ret, name, params in funcs:
        body = converter(name, params)
        if body is not None:
            out[name] = ("conv", body)
        elif name in IMMEDIATE or name.startswith("Get") or name.startswith("Is") \
                or name in THROUGH or name == "AreTexturesResident":
            out[name] = ("shared", None)
        else:
            out[name] = ("canon", None)
    return out


CELL = {"GLfloat": "f", "GLclampf": "f", "GLdouble": "d", "GLclampd": "d",
        "GLenum": "u", "GLbitfield": "u", "GLuint": "u", "GLboolean": "u",
        "GLubyte": "u", "GLushort": "u", "GLint": "i", "GLsizei": "i",
        "GLshort": "i", "GLbyte": "i"}


def save_function(name, params):
    """Give the C code of the generated save function of a canonical
    function, and the C code of its case in the playback switch."""
    ps = split_params(params)
    lines = []
    lines.append("/* Records gl%s in the display list that is compiled. */" % name)
    lines.append("static void APIENTRY save_%s(%s) {" % (name, decl_params(params)))
    if ps:
        lines.append("  sgl_cell *a = sgl_list_add(SGL_OP_%s, %d);" % (name, len(ps)))
    else:
        lines.append("  (void)sgl_list_add(SGL_OP_%s, 0);" % name)
    play = []
    for i, (typ, pname, ptr) in enumerate(ps):
        if ptr:
            count = POINTER_COUNTS[(name, pname)]
            base = typ.replace("const", "").replace("*", "").strip()
            lines.append("  if (a) a[%d].p = sgl_list_copy(%s, (size_t)(%s) * sizeof(%s));"
                         % (i, pname, count, base))
            play.append("(%s)a[%d].p" % (typ, i))
        else:
            c = CELL[typ]
            lines.append("  if (a) a[%d].%s = %s;" % (i, c, pname))
            play.append("(%s)a[%d].%s" % (typ, i, c))
    lines.append("  if (sgl_list_executes()) sgl_%s(%s);" % (name, call_args(params)))
    lines.append("}")
    case = "    case SGL_OP_%s:\n      sgl_%s(%s);\n      break;" % (name, name, ", ".join(play))
    return "\n".join(lines), case


def main():
    """Write dispatch.h and dispatch.c."""
    funcs = parse_header()
    kind = kinds(funcs)
    canon = [f for f in funcs if kind[f[1]][0] == "canon"]
    gen_save = [f for f in canon if f[1] not in HAND_SAVE]

    h = []
    h.append("/* dispatch.h - the dispatch table of the software OpenGL.")
    h.append(" *")
    h.append(" * Generated by tools/gen-dispatch.py. Do not edit.")
    h.append(" *")
    h.append(" * Part of clang-retro MFC, an independent implementation of the MFC API.")
    h.append(" * SPDX-License-Identifier: AGPL-3.0-only")
    h.append(" */")
    h.append("#ifndef SGL_DISPATCH_H")
    h.append("#define SGL_DISPATCH_H")
    h.append("")
    h.append("/* One entry for each OpenGL function. */")
    h.append("struct sgl_dispatch {")
    for ret, name, params in funcs:
        h.append("  %s(APIENTRY *%s)(%s);" % (ret + " " if not ret.endswith("*") else ret, name, decl_params(params)))
    h.append("};")
    h.append("")
    h.append("/* The operation codes of the commands in a display list. */")
    h.append("enum sgl_op {")
    h.append("  SGL_OP_NONE,")
    h.append("  SGL_OP_HAND, /* a hand-written command: a[0] is its play function */")
    for _, name, _ in gen_save:
        h.append("  SGL_OP_%s," % name)
    h.append("  SGL_OP_COUNT")
    h.append("};")
    h.append("")
    h.append("/* The table when no context is current, for execution, and for")
    h.append(" * compilation of a display list. */")
    h.append("extern const struct sgl_dispatch sgl_nop_table;")
    h.append("extern const struct sgl_dispatch sgl_exec_table;")
    h.append("extern const struct sgl_dispatch sgl_save_table;")
    h.append("")
    h.append("/* The implementations of the canonical and shared functions. */")
    for ret, name, params in funcs:
        if kind[name][0] in ("canon", "shared"):
            h.append("%s APIENTRY sgl_%s(%s);" % (ret, name, decl_params(params)))
    h.append("")
    h.append("/* The hand-written save functions (list.c). */")
    for ret, name, params in canon:
        if name in HAND_SAVE:
            h.append("void APIENTRY save_%s(%s);" % (name, decl_params(params)))
    h.append("")
    h.append("/* Plays the generated command with code op and arguments a back. */")
    h.append("void sgl_play_generated(int op, const sgl_cell *a);")
    h.append("")
    h.append("#endif")

    c = []
    c.append("/* dispatch.c - the exported OpenGL functions and the dispatch tables")
    c.append(" * of the software OpenGL.")
    c.append(" *")
    c.append(" * Generated by tools/gen-dispatch.py. Do not edit.")
    c.append(" *")
    c.append(" * Part of clang-retro MFC, an independent implementation of the MFC API.")
    c.append(" * SPDX-License-Identifier: AGPL-3.0-only")
    c.append(" */")
    c.append('#include "sgl.h"')
    c.append("")
    c.append("/* The converters: each changes its arguments to the canonical form. */")
    for ret, name, params in funcs:
        k, body = kind[name]
        if k == "conv":
            c.append("/* Converts the arguments of gl%s for the canonical function. */" % name)
            c.append("static void APIENTRY cv_%s(%s) {" % (name, decl_params(params)))
            c.append("  " + body)
            c.append("}")
    c.append("")
    c.append("/* The functions of the table when no context is current: each does")
    c.append(" * nothing and gives zero. */")
    for ret, name, params in funcs:
        c.append("/* Does nothing: no context is current (gl%s). */" % name)
        args = ", ".join(n for _, n, _ in split_params(params))
        c.append("static %s APIENTRY nop_%s(%s) {" % (ret, name, decl_params(params)))
        for _, n, _ in split_params(params):
            c.append("  (void)%s;" % n)
        if ret != "void":
            c.append("  return (%s)0;" % ret)
        c.append("}")
    c.append("")
    for ret, name, params in gen_save:
        fn, _ = save_function(name, params)
        c.append(fn)
    c.append("")
    c.append("/* The table when no context is current. */")
    c.append("const struct sgl_dispatch sgl_nop_table = {")
    for _, name, _ in funcs:
        c.append("  nop_%s," % name)
    c.append("};")
    c.append("")
    c.append("/* The table for execution. */")
    c.append("const struct sgl_dispatch sgl_exec_table = {")
    for _, name, _ in funcs:
        c.append("  %s_%s," % ("cv" if kind[name][0] == "conv" else "sgl", name))
    c.append("};")
    c.append("")
    c.append("/* The table for the compilation of a display list. */")
    c.append("const struct sgl_dispatch sgl_save_table = {")
    for _, name, _ in funcs:
        k = kind[name][0]
        c.append("  %s_%s," % ({"conv": "cv", "shared": "sgl", "canon": "save"}[k], name))
    c.append("};")
    c.append("")
    c.append("/* Plays the generated command with code op and arguments a back. */")
    c.append("void sgl_play_generated(int op, const sgl_cell *a) {")
    c.append("  switch (op) {")
    for ret, name, params in gen_save:
        _, case = save_function(name, params)
        c.append(case)
    c.append("    default:")
    c.append("      (void)a;")
    c.append("      break;")
    c.append("  }")
    c.append("}")
    c.append("")
    c.append("/* The exported functions: each calls the entry of the current table. */")
    for ret, name, params in funcs:
        r = "return " if ret != "void" else ""
        # A window layer that copies the picture on glFlush and glFinish
        # (as GDI does for a single-buffered format) defines them itself.
        # The renderer draws at once, so these have nothing to wait for.
        layer = name in ("Flush", "Finish")
        if layer:
            c.append("#ifndef SGL_LAYER_FLUSH")
        if name in [e[1] for e in EXTENSIONS]:
            # gl.h does not declare the extension functions.
            c.append("%s APIENTRY gl%s(%s);" % (ret, name, decl_params(params)))
            c.append("/* gl%s: an extension, which wglGetProcAddress gives. */" % name)
        else:
            c.append("/* gl%s: calls the current table. */" % name)
        c.append("%s APIENTRY gl%s(%s) {" % (ret, name, decl_params(params)))
        c.append("  %sSGL_DISPATCH->%s(%s);" % (r, name, call_args(params)))
        c.append("}")
        if layer:
            c.append("#endif")
    c.append("")
    c.append("/* The extension functions by name, for wglGetProcAddress. */")
    c.append("const struct sgl_proc sgl_extension_procs[] = {")
    for _, name, _ in EXTENSIONS:
        c.append('  {"gl%s", (void (*)(void))gl%s},' % (name, name))
    c.append("  {0, 0}")
    c.append("};")

    open(OUT_H, "w").write("\n".join(h) + "\n")
    open(OUT_C, "w").write("\n".join(c) + "\n")
    print("%d functions: %d canonical (%d generated saves), %d converters" % (
        len(funcs), len(canon), len(gen_save),
        len([1 for f in funcs if kind[f[1]][0] == "conv"])))


if __name__ == "__main__":
    sys.exit(main())
