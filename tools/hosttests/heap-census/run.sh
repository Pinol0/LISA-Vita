#!/bin/bash
# usage: run.sh RUBY_BUILD_DIR RUBY_SRC_DIR   (a configured+built ruby 3.1.6 tree: miniruby objects)
# Builds into ./out (overwritten each run). Then a mutation check, which must print FAIL lines:
# T_DATA keyed as untyped -> no 'proc' / 'fiber' keys.
set -e
cd "$(dirname "$0")"
B=$1
R=$2
mkdir -p out/ok out/mut
OBJS="$(ls $B/*.o | grep -vE '/(main|dmyenc|dln|builtin|loadpath|localeinit)\.o$') $(find $B/coroutine -name '*.o') $(ls $B/enc/{ascii,us_ascii,unicode,utf_8}.o $B/enc/trans/newline.o)"
LIBS="-lz -lpthread -lrt -ldl -lcrypt -lm"
python3 extract.py ../../../src/main.cpp > out/ok/census.inc
python3 extract.py ../../../src/main.cpp | sed 's/const bool typed = RTYPEDDATA_P(v);/const bool typed = false;/' > out/mut/census.inc
for v in ok mut; do
    g++ -std=gnu++17 -O1 -w -Iout/$v -I$R/include -I$B/.ext/include/x86_64-linux t_census.cpp $OBJS $LIBS -o out/$v/t
done
set +e
out/ok/t
rc=$?
echo "mutation:"
out/mut/t | grep FAIL
exit $rc
