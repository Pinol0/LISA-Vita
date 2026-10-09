#!/bin/bash
# usage: run.sh RUBY_BUILD_DIR RUBY_SRC_DIR   (a configured+built ruby 3.1.6 tree: miniruby objects)
# MKXP_VITA_SCRIPT_UTF8 on the real block of src/main.cpp. Mutants (must FAIL): ASCII-8BIT source as
# rb_eval_string_protect makes it; default encodings not set.
set -e
cd "$(dirname "$0")"
B=$1
R=$2
OUT=$(mktemp -d "${TMPDIR:-/tmp}/script-utf8.XXXXXX")
OBJS="$(ls $B/*.o | grep -vE '/(main|dmyenc|dln|builtin|loadpath|localeinit)\.o$') $(find $B/coroutine -name '*.o') $(ls $B/enc/{ascii,us_ascii,unicode,utf_8}.o $B/enc/trans/newline.o)"
LIBS="-lz -lpthread -lrt -ldl -lcrypt -lm"
python3 - ../../../src/main.cpp "$OUT" <<'PY'
import sys, os
s = open(sys.argv[1]).read()
i = s.index('#ifdef MKXP_VITA_SCRIPT_UTF8\n/*\n * Game scripts as UTF-8 source')
i += len('#ifdef MKXP_VITA_SCRIPT_UTF8\n')
j = s.index('\n#endif\n', i)
b = '#include <ruby/encoding.h>\n' + s[i:j] + '\n'
for name, blk in (('ok', b),
                  ('m1', b.replace('rb_utf8_str_new(src, len)', 'rb_str_new(src, len)')),
                  ('m2', b.replace('    rb_enc_set_default_external(', '    if (0) rb_enc_set_default_external('))):
    assert blk == b or name == 'ok' or blk != b
    os.makedirs(os.path.join(sys.argv[2], name))
    open(os.path.join(sys.argv[2], name, 'block.inc'), 'w').write(blk)
PY
for v in ok m1 m2; do
    cmp -s "$OUT/ok/block.inc" "$OUT/$v/block.inc" && [ $v != ok ] && { echo "FAIL mutant $v not applied"; exit 1; }
    g++ -std=gnu++17 -O1 -w -I"$OUT/$v" -I$R/include -I$B/.ext/include/x86_64-linux t_script_utf8.cpp $OBJS $LIBS -o "$OUT/$v/t"
done
set +e
"$OUT/ok/t"
rc=$?
echo "mutation:"
for v in m1 m2; do
    "$OUT/$v/t" > "$OUT/$v/log" 2>&1
    mrc=$?
    # killed = a normal exit 1 with a FAIL line (a crash proves nothing)
    if [ $mrc -eq 1 ] && grep -q '^FAIL' "$OUT/$v/log"; then
        echo "PASS mutant $v killed: $(grep -m1 '^FAIL' "$OUT/$v/log")"
    else
        echo "FAIL mutant $v: exit $mrc"; tail -5 "$OUT/$v/log"; rc=1
    fi
done
rm -rf "$OUT"
exit $rc
