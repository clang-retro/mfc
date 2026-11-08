# mkdef.awk - makes the .def file of mfc.dll from `llvm-nm --defined-only
# --extern-only` of its objects: every name they define, apart from the
# C++ runtime's (the standard library's templates and inline functions,
# the global operator new and delete, which each module keeps for itself)
# and the DLL's entry point. Data is marked DATA, so that the import
# library has no code thunk for it. With strip=1 (32-bit x86) the leading
# underscore of the names comes off, as a .def file writes them.
BEGIN { print "EXPORTS" }
NF == 3 {
  type = $2
  name = $3
  if (strip == 1) sub(/^_/, "", name)
  if (name ~ /^\./ || name ~ /^__imp_/ || name ~ /\.refptr\./) next
  if (name ~ /^DllMain(@12)?$/ || name ~ /^_?_pei386_/) next
  if (name ~ /^_Z(nw|na)[jmy](RKSt9nothrow_t|St11align_val_t)*$/) next
  if (name ~ /^_Z(dl|da)Pv[jmy]?(RKSt9nothrow_t|St11align_val_t)*$/) next
  if (name ~ /St3__1/ || name ~ /^_Z(N|NK|T[ISVT]N?|GVZN|GVN)?S[tais]/) next
  if (name ~ /^_ZN?K?9__gnu_cxx/ || name ~ /^_ZN?K?10__cxxabiv1/) next
  if (seen[name]++) next
  if (type ~ /^[TtWwIi]$/)
    print name
  else
    print name " DATA"
}
