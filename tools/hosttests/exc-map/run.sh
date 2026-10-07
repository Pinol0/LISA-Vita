#!/bin/bash
# usage: run.sh RUBY_BUILD_DIR RUBY_SRC_DIR [MKXP_DIR]   (host Ruby 3.1.6 objects, see setup_env.sh)
# A mutation (missing-file and RGSSError cases removed) must FAIL.
cd "$(dirname "$0")"; B=$1; R=$2; M=${3:-../../../deps/mkxp-z}
mkdir -p out
OBJS="$(ls $B/*.o | grep -vE '/(main|dmyenc|dln|builtin|loadpath|localeinit)\.o$') $(find $B/coroutine -name '*.o') $(ls $B/enc/{ascii,us_ascii,unicode,utf_8}.o $B/enc/trans/newline.o)"
LIBS="-lz -lpthread -lrt -ldl -lcrypt -lm"
I="-I$R/include -I$B/.ext/include/x86_64-linux -I$R -I$M/binding -I$M/src/util -I$M/src"
build() { g++ -std=gnu++17 -O1 -w $I -DMKXP_VITA_AUDIT_FIXES -DMKXP_VITA_RGSS_ARGS_V2 host_main.cpp $1 $OBJS $LIBS -o $2 || exit 1; }
build ../../../src/binding-shim.cpp out/t
# mutant: the NoFileError / RGSSError cases gone (both back to RuntimeError, as before the fix)
sed -e 's/case Exception::NoFileError:/case (Exception::Type)998:/' -e 's/case Exception::RGSSError:/case (Exception::Type)999:/' \
    ../../../src/binding-shim.cpp > out/mut.cpp
build out/mut.cpp out/tm
out/t; rc=$?
echo "mutation:"; out/tm | grep -c FAIL
exit $rc
