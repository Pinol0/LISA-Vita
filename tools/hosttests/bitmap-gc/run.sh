#!/bin/bash
# usage: run.sh RUBY_BUILD_DIR RUBY_SRC_DIR SRC_DIR   (a configured+built ruby 3.1.6 tree: miniruby objects)
# MKXP_VITA_BITMAP_GC's effect on Ruby's real GC: vitaGcReport (src/bitmap-vita.cpp) with src/main.cpp's
# GC parameters. Mutants (must FAIL): bytes reported but no GC started by the port (Ruby alone);
# nothing reported.
set -e
cd "$(dirname "$0")"
B=$1; R=$2; SRC=$3; MAIN=$SRC/main.cpp
OUT=$(mktemp -d "${TMPDIR:-/tmp}/bitmap-gc.XXXXXX")
OBJS="$(ls $B/*.o | grep -vE '/(main|dmyenc|dln|builtin|loadpath|localeinit)\.o$') $(find $B/coroutine -name '*.o') $(ls $B/enc/{ascii,us_ascii,unicode,utf_8}.o $B/enc/trans/newline.o)"
LIBS="-lz -lpthread -lrt -ldl -lcrypt -lm"
python3 - "$MAIN" "$OUT" "$SRC/bitmap-vita.cpp" <<'PY'
import sys, re, os
s = open(sys.argv[1]).read()
bv = open(sys.argv[3]).read()
i = bv.index('/* host test bitmap-gc: vitaGcReport')
j = bv.index('/* end vitaGcReport */', i)
report = '#define VITA_GAME_ROOT "' + sys.argv[2] + '/"\n#include <cstdio>\n' + bv[i:j]
nokick = report.replace('    rb_gc_start();\n', '', 1)
assert nokick != report
noreport = report.replace('    rb_gc_adjust_memory_usage((ssize_t)bytes);\n', '    (void)bytes;\n', 1).replace('    rb_gc_start();\n', '', 1)
assert noreport != nokick
a = s.index('#ifdef MKXP_VITA_GC_MID\n    {') if '#ifdef MKXP_VITA_GC_MID\n    {' in s else s.index('Perf fix (MKXP_VITA_GC_MID)')
b = s.index('ruby_gc_set_params();', a)
blk = s[a:b]
# the setenv lines, with MKXP_VITA_BITMAP_GC on (and its value), macros as the release options set them
lines = []
for m in re.finditer(r'setenv\("(RUBY_GC_[A-Z_]+)",\s*([^,]+),\s*1\);', blk):
    name, val = m.group(1), m.group(2).strip()
    if name == 'RUBY_GC_HEAP_FREE_SLOTS_MAX_RATIO':
        continue   # MKXP_VITA_GC_MAX_RATIO: not in the release options
    val = {"VITA_GC_INIT_SLOTS": "\"600000\"", "VITA_GC_FREE_SLOTS": "\"60000\""}.get(val, val)   # release options: GC_INIT_SLOTS=600000, FREE_SLOTS default
    lines.append((name, val))
assert any(n == 'RUBY_GC_MALLOC_LIMIT' for n, _ in lines), 'GC_MID block not found in main.cpp'
def write(d, keep, rep):
    os.makedirs(d)
    body = ' '.join('setenv("%s", %s, 1);' % (n, v) for n, v in lines if keep(n))
    open(os.path.join(d, 'gcenv.inc'), 'w').write('#define VITA_GC_ENV() do { %s } while (0)\n' % body)
    open(os.path.join(d, 'report.inc'), 'w').write(rep)
write(sys.argv[2] + '/ok', lambda n: True, report)
write(sys.argv[2] + '/m1', lambda n: True, nokick)
write(sys.argv[2] + '/m2', lambda n: True, noreport)
PY
build() { g++ -std=gnu++17 -O1 -w -I"$OUT/$1" -I$R/include -I$B/.ext/include/x86_64-linux $2 t_bitmap_gc.cpp $OBJS $LIBS -o "$OUT/$1/t"; }
for v in ok m1 m2; do build $v ""; done
set +e
"$OUT/ok/t"
rc=$?
echo "mutation:"
for v in m1 m2; do
    "$OUT/$v/t" > "$OUT/$v/log" 2>&1
    mrc=$?
    if [ $mrc -eq 1 ] && grep -q '^FAIL' "$OUT/$v/log"; then
        echo "PASS mutant $v killed: $(head -1 "$OUT/$v/log")"
    else
        echo "FAIL mutant $v: exit $mrc"; cat "$OUT/$v/log"; rc=1
    fi
done
rm -rf "$OUT"
exit $rc
