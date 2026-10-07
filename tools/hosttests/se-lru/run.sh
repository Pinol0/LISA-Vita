#!/bin/bash
# SE cache LRU (MKXP_VITA_SE_LRU) on the real SoundEmitter::allocateBuffer of mkxp-z.
# usage: run.sh   (MKXP_DIR: patched mkxp-z, default deps/mkxp-z)
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
MKXP_DIR=${MKXP_DIR:-$HERE/../../../deps/mkxp-z}
OUT=$(mktemp -d "${TMPDIR:-/tmp}/se-lru.XXXXXX")
python3 - "$MKXP_DIR/src/audio/soundemitter.cpp" "$OUT/allocate_buffer.inc" <<'PY'
import sys
s = open(sys.argv[1]).read()
i = s.index('SoundBuffer *SoundEmitter::allocateBuffer(const std::string &filename)')
j = s.index('{', i); d = 0; k = j
while True:
    d += {'{': 1, '}': -1}.get(s[k], 0); k += 1
    if d == 0: break
open(sys.argv[2], 'w').write(s[i:k] + '\n')
PY
F="-std=gnu++17 -O1 -fsanitize=undefined -fsanitize-trap=all -D_GLIBCXX_ASSERTIONS -I$MKXP_DIR/src/util -I$OUT"
g++ $F -DMKXP_VITA_SE_LRU "$HERE/t_se_lru.cpp" -o "$OUT/t" && "$OUT/t"
echo "mutation: without MKXP_VITA_SE_LRU (upstream order) must FAIL"
g++ $F "$HERE/t_se_lru.cpp" -o "$OUT/t_old" && { "$OUT/t_old" && { echo "FAIL mutant survived"; exit 1; } || echo "PASS mutant killed"; }
rm -rf "$OUT"
