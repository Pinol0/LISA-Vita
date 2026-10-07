#!/bin/bash
# Analog sticks (MKXP_VITA_ANALOG_INPUT) on the real vita-analog.h / rgss-input.h / main.cpp bindings.
# Mutants: no hysteresis; right stick down bound to C.
set -e
cd "$(dirname "$0")"
OUT=$(mktemp -d "${TMPDIR:-/tmp}/analog.XXXXXX")
python3 - ../../../src/main.cpp "$OUT/bindings.inc" <<'PY'
import sys
s = open(sys.argv[1]).read()
i = s.index('static void vitaInputInitBindings()')
j = s.index('\n}\n', i) + 3
open(sys.argv[2], 'w').write(s[i:j])
PY
F="-std=gnu++17 -O1 -fsanitize=undefined -fsanitize-trap=all -D_GLIBCXX_ASSERTIONS -I$OUT"
g++ $F t_analog.cpp -o "$OUT/t"
"$OUT/t"
echo "mutation: no hysteresis, right stick down = C"
mkdir -p "$OUT/m/src"
sed 's/const int kLeftPress = 56, kLeftRelease = 44;/const int kLeftPress = 56, kLeftRelease = 56;/' ../../../src/vita-analog.h > "$OUT/m/src/vita-analog.h"
sed 's#"../../../src/vita-analog.h"#"vita-analog.h"#' t_analog.cpp > "$OUT/m/t1.cpp"
cp ../../../src/rgss-input.h "$OUT/m/src/"; sed -i 's#"../../../src/rgss-input.h"#"src/rgss-input.h"#' "$OUT/m/t1.cpp"
g++ $F -I"$OUT/m/src" "$OUT/m/t1.cpp" -o "$OUT/m1"
if "$OUT/m1" > /dev/null; then echo "FAIL hysteresis mutant survived"; exit 1; else echo "PASS hysteresis mutant killed"; fi
sed -i 's/{ VitaAnalog::kRightDown, Y }/{ VitaAnalog::kRightDown, C }/' "$OUT/bindings.inc"
g++ $F t_analog.cpp -o "$OUT/m2"
if "$OUT/m2" > /dev/null; then echo "FAIL binding mutant survived"; exit 1; else echo "PASS binding mutant killed"; fi
rm -rf "$OUT"
