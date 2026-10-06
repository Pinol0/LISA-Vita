#!/bin/bash
# usage: run.sh PNG...   (>= 2 PNGs of >= 1 MiB decoded). Mutations that must FAIL: no wait for a job
# being worked on; pixels accepted without the size check against the entry header.
cd "$(dirname "$0")"; mkdir -p out
B="g++ -std=gnu++17 -O1 -w -I../../../src"
SRC="../../../src/vita-image-cache.cpp ../../../src/vita-prefetch.cpp ../../../third_party/lz4/lz4.c -lpng -lpthread"
$B t_predecode.cpp ../../../src/vita-predecode.cpp $SRC -o out/t || exit 1
sed 's/while ((it = gEntries.find(key)) != gEntries.end() \&\& it->second.st == WORKING) pthread_cond_wait(\&gDone, \&gLock);/;/' ../../../src/vita-predecode.cpp > out/m1.cpp
sed 's/ok = VitaImageCache::loadQuiet(name, out.a, out.b, out.data) \&\& out.data.size() == raw;/ok = VitaImageCache::loadQuiet(name, out.a, out.b, out.data); if (ok) out.data.resize(out.data.size() \/ 2);/' ../../../src/vita-predecode.cpp > out/m2.cpp
for m in m1 m2; do cmp -s out/$m.cpp ../../../src/vita-predecode.cpp && { echo "NO MUTATION $m"; exit 1; }
  $B t_predecode.cpp out/$m.cpp $SRC -o out/t$m || exit 1; done
out/t "$@"; rc=$?
for m in m1 m2; do out/t$m "$@" > out/$m.log 2>&1; r=$?; echo "mutation $m: exit $r ($( [ $r -ne 0 ] && echo killed || echo SURVIVED ))"; done
exit $rc
