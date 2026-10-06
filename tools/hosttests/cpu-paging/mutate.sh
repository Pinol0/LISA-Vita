#!/bin/sh
# Mutation check for run.sh: every mutant of src/bitmap-vita-minimal.cpp must FAIL.
# usage: mutate.sh ROOT_DIR SCRATCH_DIR
cd "$(dirname "$0")"
ROOT=$1; M=$2; mkdir -p "$M"
SRC=../../../src/bitmap-vita-minimal.cpp
run() {   # name, python replace (old -> new)
    name=$1
    python3 - "$SRC" "$M/$name.cpp" "$2" "$3" <<'PY'
import sys
s=open(sys.argv[1]).read(); a=sys.argv[3]; b=sys.argv[4]
assert s.count(a)==1, ("not unique", a, s.count(a))
open(sys.argv[2],'w').write(s.replace(a,b))
PY
    cp "$SRC".h "$M/" 2>/dev/null
    for h in ../../../src/*.h; do cp "$h" "$M/"; done
    if OUT="$M/out-$name" STEPS=3000 ./run.sh "$ROOT" "" "$M/$name.cpp" > "$M/$name.log" 2>&1; then
        echo "SURVIVED $name"
    else
        echo "killed   $name ($(grep -m1 'FAIL step\|COVERAGE\|Segmentation\|Aborted\|terminate' "$M/$name.log" | cut -c1-60))"
    fi
}
run decode_no_hue    "    if (p->cleanHue)
        VitaBitmapCpu::hueChange" "    if (false)
        VitaBitmapCpu::hueChange"
run clone_drops_hue  "        p->cleanHue = other.p->cleanHue;" "        p->cleanHue = 0;"
run second_hue_clean "p->fileClean && !p->sourcePath.empty() && p->cleanHue == 0;" "p->fileClean && !p->sourcePath.empty();"
run restore_no_hue   "    } else if (p->cleanHue) {   /* hue clone" "    } else if (false) {   /* hue clone"
run no_recipe        "    if (recipe) {   /* still the file" "    if (false) {   /* still the file"
run drop_pending     "        if (!b->fileClean || !b->hasCpuPixels || gPgFrame - b->lastUse < idle || b->pixels.capacity() < kPgMinBytes)
            continue;
#ifdef MKXP_VITA_BITMAP_DEFERRED_UPLOAD
        if (b->uploadPending)
            continue;
#endif" "        if (!b->fileClean || !b->hasCpuPixels || gPgFrame - b->lastUse < idle || b->pixels.capacity() < kPgMinBytes)
            continue;"
run no_retry         "        vitaCpuRelease(0, (size_t)-1, 1);
        return VITA_DECODE_SOURCE(bp, w, h, px);" "        throw;"
run blt_raw_decode   "               VITA_DECODE_SOURCE(source.p, srcW, srcH, decoded)) {" "               vitaLoadPngPixels(source.p->sourcePath.c_str(), srcW, srcH, decoded)) {"
run clone_not_clean  "        p->fileClean = true;
        p->cleanHue = other.p->cleanHue;" "        p->cleanHue = other.p->cleanHue;"
run drop_dirty       "        if (!b->fileClean || !b->hasCpuPixels || gPgFrame - b->lastUse < idle" "        if (!b->hasCpuPixels || gPgFrame - b->lastUse < idle"
run dirty_keeps_hue  "inline void vitaPgDirty(BitmapPrivate *p) { p->fileClean = false; p->cleanHue = 0; }" "inline void vitaPgDirty(BitmapPrivate *p) { p->fileClean = false; }"
