#!/bin/bash
# usage: run_native.sh RUBY_BUILD_DIR RUBY_SRC_DIR SCRIPTS_DIR
# The same test with the C modules (MKXP_VITA_SPRITE_FAST_NATIVE): results must equal the Ruby
# version's line for line (mismatches 0, same fast-path counts). Mutation: one comparison dropped.
cd "$(dirname "$0")"; B=$1; R=$2; S=$3; mkdir -p out
python3 gen_blocks.py || exit 1
OBJS="$(ls $B/*.o | grep -vE '/(main|dmyenc|dln|builtin|loadpath|localeinit)\.o$') $(find $B/coroutine -name '*.o') $(ls $B/enc/{ascii,us_ascii,unicode,utf_8}.o $B/enc/trans/newline.o)"
LIBS="-lz -lpthread -lrt -ldl -lcrypt -lm"
build() { g++ -std=gnu++17 -O1 -w -I$R/include -I$B/.ext/include/x86_64-linux host_main.cpp "$1" $OBJS $LIBS -o "$2" || exit 1; }
build ../../../src/vita-sprite-fast-native.cpp out/tn
sed 's/eq(iv(self, idVsfOp), rd(c, plain, R_OPACITY)) \&\& //' ../../../src/vita-sprite-fast-native.cpp > out/mut.cpp
cmp -s out/mut.cpp ../../../src/vita-sprite-fast-native.cpp && { echo "NO MUTATION"; exit 1; }
build out/mut.cpp out/tm
ruby t_sprite_fast.rb "$S" > out/ruby.txt; out/tn ./t_sprite_fast.rb "$S" > out/native.txt; rc=$?
cat out/native.txt
grep -q "VitaSpriteFastNative" blocks.rb || { echo "FAIL blocks.rb has no native branch"; rc=1; }
diff out/ruby.txt out/native.txt > /dev/null && echo "native == ruby (same lines, same counts)" || { echo "FAIL native differs from ruby"; diff out/ruby.txt out/native.txt; rc=1; }
out/tm ./t_sprite_fast.rb "$S" > out/mut.txt; diff -q out/ruby.txt out/mut.txt > /dev/null && { echo "mutation: SURVIVED"; rc=1; } || echo "mutation: killed"
exit $rc
