#!/bin/bash
# usage: run.sh RUBY_BUILD_DIR RUBY_SRC_DIR SCRIPTS_DIR   (miniruby objects of a built ruby 3.1.6)
# Also builds a mutation (trigger compared with == instead of !=) that must FAIL.
cd "$(dirname "$0")"; B=$1; R=$2; D=$3
mkdir -p out
OBJS="$(ls $B/*.o | grep -vE '/(main|dmyenc)\.o$') $(find $B/coroutine -name '*.o') $(ls $B/enc/{ascii,us_ascii,unicode,utf_8}.o $B/enc/trans/newline.o)"
LIBS="-lz -lpthread -lrt -ldl -lcrypt -lm"
sed 's/neq(rb_ivar_get(self, idTrigger), INT2FIX(3))/eq(rb_ivar_get(self, idTrigger), INT2FIX(3))/' ../../../src/vita-event-native.cpp > out/mut.cpp
g++ -std=gnu++17 -O1 -w -I$R/include -I$B/.ext/include/x86_64-linux host_main.cpp ../../../src/vita-event-native.cpp $OBJS $LIBS -o out/t || exit 1
g++ -std=gnu++17 -O1 -w -I$R/include -I$B/.ext/include/x86_64-linux host_main.cpp out/mut.cpp $OBJS $LIBS -o out/tm || exit 1
out/t ./t_event_native.rb "$D"; rc=$?
echo "mutation:"; out/tm ./t_event_native.rb "$D" | grep FAIL
exit $rc
