#!/usr/bin/env python3
"""List member functions declared in the MFC headers that have no inline body.

Used during development to see what still needs implementing in src/.
Usage: undefined-decls.py <clang++> <args...>  (args must include -fsyntax-only)
"""
import json, subprocess, sys

def main():
    cmd = sys.argv[1:] + ["-Xclang", "-ast-dump=json", "-fsyntax-only"]
    out = subprocess.run(cmd, capture_output=True, text=True).stdout
    # filter= with empty prints many top-level json objects; parse them in sequence
    dec = json.JSONDecoder()
    i = 0
    seen = set()
    cur = [None]
    def walk(n, cls, _unused):
        # clang only records the file name when it changes, in document order
        for part in (n.get("loc", {}), n.get("loc", {}).get("expansionLoc", {}),
                     n.get("range", {}).get("begin", {}), n.get("range", {}).get("begin", {}).get("expansionLoc", {})):
            if part.get("file"): cur[0] = part["file"]
        fname = cur[0]
        k = n.get("kind")
        if k in ("CXXRecordDecl", "ClassTemplateDecl") and n.get("name"):
            cls = n["name"]
        if k in ("CXXMethodDecl", "CXXConstructorDecl", "CXXDestructorDecl", "FunctionDecl"):
            has_body = any(c.get("kind") == "CompoundStmt" for c in n.get("inner", []))
            if not has_body and not n.get("isImplicit") and not n.get("pure") and not n.get("explicitlyDeleted") and not n.get("explicitlyDefaulted") \
               and fname and ("/mfc/include/" in fname or fname.startswith("include/")):
                key = f"{cls}::{n.get('name')} {n.get('type',{}).get('qualType')}"
                if key not in seen:
                    seen.add(key); print(fname.split('/')[-1], key)
            return
        for c in n.get("inner", []) or []:
            walk(c, cls if k != "TranslationUnitDecl" else None, fname)
    try:
        walk(json.loads(out), None, None); return
    except ValueError:
        pass
    while i < len(out):
        while i < len(out) and out[i] in " \n\r\t": i += 1
        if i >= len(out): break
        if out[i] != '{':
            j = out.find('\n', i); i = len(out) if j < 0 else j + 1; continue
        obj, i = dec.raw_decode(out, i)
        walk(obj, None, None)

main()
