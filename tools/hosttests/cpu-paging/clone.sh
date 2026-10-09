#!/bin/sh
# MKXP_VITA_CLONE_OOM_RETRY (and, with "bmpgc", MKXP_VITA_BITMAP_GC): run.sh with the option(s) on (the
# whole random session plus the directed cases), then mutants of src/bitmap-vita.cpp that must FAIL.
# usage: clone.sh ROOT_DIR [bmpgc]   (env as run.sh)
cd "$(dirname "$0")"
ROOT=$1
D=-DMKXP_VITA_CLONE_OOM_RETRY
[ "$2" = bmpgc ] && D="$D -DMKXP_VITA_BITMAP_GC"
OUT=out-clone$2 STEPS=3000 ./run.sh "$ROOT" "$D" || exit 1
M=$(mktemp -d "${TMPDIR:-/tmp}/clone-mut.XXXXXX")
for h in ../../../src/*.h; do cp "$h" "$M/"; done
echo "mutation:"
rc=0
mut() {   # name, old, new
    python3 - ../../../src/bitmap-vita.cpp "$M/$1.cpp" "$2" "$3" <<'PY'
import sys
s=open(sys.argv[1]).read(); a=sys.argv[3]; b=sys.argv[4]
assert s.count(a)==1, ("not unique", a, s.count(a))
open(sys.argv[2],'w').write(s.replace(a,b))
PY
    [ -f "$M/$1.cpp" ] || { echo "FAIL mutant $1 not applied (text not found)"; rc=1; return; }
    if OUT="$M/out-$1" STEPS=300 ./run.sh "$ROOT" "$D" "$M/$1.cpp" > "$M/$1.log" 2>&1; then
        echo "FAIL mutant $1 survived"; rc=1
    else
        echo "PASS mutant $1 killed: $(grep -m1 '^FAIL' "$M/$1.log" | cut -c1-100)"
    fi
}
mut no_gc_give_back "    tex = vitaPgEvictIdle();
    rb_gc_start();" "    tex = vitaPgEvictIdle();"
mut clone_tex_leak "            shState->texPool().release(p->gl);   /* the constructor does not finish: nothing else frees them */" ""
if [ "$2" = bmpgc ]; then
    mut no_charge "    p->gcCharged += bytes;
    vitaGcReport(bytes);" "    p->gcCharged += bytes;"
    mut no_uncharge "        if (gcCharged)
            rb_gc_adjust_memory_usage(-(ssize_t)gcCharged);" ""
    mut new_tex_leak "        if (!ok) {
            shState->texPool().release(p->gl);
            delete p;" "        if (!ok) {
            delete p;"
    mut no_report_gc "    if (inc <= lim)
        return;
    rb_gc_start();" "    if (inc <= lim)
        return;"
    mut clone_uncharged "#ifdef MKXP_VITA_BITMAP_GC
    vitaGcCharge(p, (size_t)w * h * 4);
#endif
}" "}"
fi
rm -rf "$M"
exit $rc
