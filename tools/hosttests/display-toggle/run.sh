#!/bin/bash
# MKXP_VITA_DISPLAY_TOGGLE on the real block of src/shell/sharedstate-vita.cpp, over three "launches".
# Mutants (must FAIL): display.cfg not read back; pixel mode scaled like original; the mode not written.
set -e
cd "$(dirname "$0")"
OUT=$(mktemp -d "${TMPDIR:-/tmp}/display-toggle.XXXXXX")
python3 - ../../../src/shell/sharedstate-vita.cpp "$OUT" <<'PY'
import sys, os
s = open(sys.argv[1]).read()
i = s.index('/* host test display-toggle: from here to "end display toggle" */')
j = s.index('/* end display toggle */', i)
b = '#define VITA_GAME_ROOT "' + sys.argv[2] + '/root/"\n' + s[i:j]
muts = {'ok': b,
        'm1': b.replace('if (f) {\n            char b[16]', 'if (false && f) {\n            char b[16]', 1),
        'm2': b.replace('if (m == VITA_DISPLAY_PIXEL && scrW', 'if (false && scrW', 1),
        'm3': b.replace('fprintf(f, "%s\\n", vitaDisplayNames[vitaDisplayMode]);', '(void)f;', 1)}
for k, v in muts.items():
    assert k == 'ok' or v != b, k
    os.makedirs(os.path.join(sys.argv[2], k))
    open(os.path.join(sys.argv[2], k, 'block.inc'), 'w').write(v)
PY
seq() {   # variant -> runs the three launches and the junk file case; exit status = any failure
    local v=$1 rc=0
    rm -rf "$OUT/root"; mkdir -p "$OUT/root"
    g++ -std=gnu++17 -O1 -fsanitize=undefined -fsanitize-trap=all -I"$OUT/$v" t_display.cpp -o "$OUT/$v/t"
    "$OUT/$v/t" first || rc=1
    "$OUT/$v/t" second || rc=1
    "$OUT/$v/t" third || rc=1
    echo "1080p" > "$OUT/root/display.cfg"
    "$OUT/$v/t" junk || rc=1
    return $rc
}
set +e
seq ok
rc=$?
echo "mutation:"
for v in m1 m2 m3; do
    if seq $v > "$OUT/$v.log" 2>&1; then echo "FAIL mutant $v survived"; rc=1; else echo "PASS mutant $v killed: $(grep -m1 '^FAIL' "$OUT/$v.log")"; fi
done
rm -rf "$OUT"
exit $rc
