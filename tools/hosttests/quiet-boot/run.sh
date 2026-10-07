#!/bin/bash
# MKXP_VITA_QUIET_BOOT boot log on the real block of src/main.cpp. Mutant: no "screen still ours" check.
set -e
cd "$(dirname "$0")"
OUT=$(mktemp -d "${TMPDIR:-/tmp}/quiet-boot.XXXXXX")
python3 - ../../../src/main.cpp "$OUT/block.inc" <<'PY'
import sys
s = open(sys.argv[1]).read()
i = s.index('#ifdef MKXP_VITA_QUIET_BOOT\n/*\n * Public builds')
j = s.index('#define psvDebugScreenPrintf vitaBootPrintf\n#endif', i) + len('#define psvDebugScreenPrintf vitaBootPrintf\n#endif')
open(sys.argv[2], 'w').write(s[i:j] + '\n')
PY
F="-std=gnu++17 -O1 -fsanitize=undefined -fsanitize-trap=all -D_GLIBCXX_ASSERTIONS -DVITA_GAME_ROOT=\"$OUT/\" -I$OUT"
g++ $F t_quiet_boot.cpp -lpng -o "$OUT/t"
"$OUT/t"
"$OUT/t" late
echo "mutation: failure always takes the screen"
sed -i 's/if (vitaBootScreenShown()) {/if (true) {/' "$OUT/block.inc"
g++ $F t_quiet_boot.cpp -lpng -o "$OUT/m"
if "$OUT/m" late > /dev/null; then echo "FAIL mutant survived"; exit 1; else echo "PASS mutant killed"; fi
rm -rf "$OUT"
