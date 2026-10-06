#!/bin/bash
# usage: run.sh RUBY_BUILD_DIR RUBY_SRC_DIR   (miniruby objects of a built ruby 3.1.6; a full build dir works too)
# Also builds two mutations of vita-prof-sub.inc (no root-Fiber check; self = inclusive) that must FAIL.
cd "$(dirname "$0")"; B=$1; R=$2
mkdir -p out
OBJS="$(ls $B/*.o | grep -vE '/(main|dmyenc|dln|builtin|loadpath|localeinit)\.o$') $(find $B/coroutine -name '*.o') $(ls $B/enc/{ascii,us_ascii,unicode,utf_8}.o $B/enc/trans/newline.o)"
LIBS="-lz -lpthread -lrt -ldl -lcrypt -lm"
build() { g++ -std=gnu++17 -O1 -w -I$R/include -I$B/.ext/include/x86_64-linux "$1" $OBJS $LIBS -o "$2" || exit 1; }
build host_main.cpp out/t
mkdir -p out/m1/src out/m2/src out/m1/tools/hosttests/prof-sub out/m2/tools/hosttests/prof-sub
sed 's/ || rb_fiber_current() != gSubRoot//' ../../../src/vita-prof-sub.inc > out/m1/src/vita-prof-sub.inc
sed 's/dur > child ? dur - child : 0/dur/' ../../../src/vita-prof-sub.inc > out/m2/src/vita-prof-sub.inc
for m in m1 m2; do cmp -s out/$m/src/vita-prof-sub.inc ../../../src/vita-prof-sub.inc && { echo "NO MUTATION $m"; exit 1; }
  cp host_main.cpp out/$m/tools/hosttests/prof-sub/; build out/$m/tools/hosttests/prof-sub/host_main.cpp out/t$m; done
out/t ./t_prof_sub.rb ../../../src/main.cpp; rc=$?
for m in m1 m2; do echo "mutation $m:"; out/t$m ./t_prof_sub.rb ../../../src/main.cpp | grep -c FAIL; done
exit $rc
