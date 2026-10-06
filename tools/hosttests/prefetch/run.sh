#!/bin/bash
# usage: run.sh   (host g++; plus two mutations that must FAIL: no wait for a file being read,
# clear forgetting the held bytes)
cd "$(dirname "$0")"; mkdir -p out
B="g++ -std=gnu++17 -O1 -Wall -DVITA_PREFETCH_TEST -I../../../src"
$B t_prefetch.cpp ../../../src/vita-prefetch.cpp -lpthread -o out/t || exit 1
sed 's/while ((it = gEntries.find(path)) != gEntries.end() \&\& it->second.st == READING) pthread_cond_wait(\&gDone, \&gLock);/;/' ../../../src/vita-prefetch.cpp > out/m1.cpp
sed 's/if (it->second.st == DONE) gHeld -= it->second.data.size();/;/' ../../../src/vita-prefetch.cpp > out/m2.cpp
for m in m1 m2; do cmp -s out/$m.cpp ../../../src/vita-prefetch.cpp && { echo "NO MUTATION $m"; exit 1; }
  $B -w t_prefetch.cpp out/$m.cpp -lpthread -o out/t$m || exit 1; done
out/t; rc=$?
for m in m1 m2; do out/t$m > out/$m.log 2>&1; r=$?; echo "mutation $m: exit $r ($( [ $r -ne 0 ] && echo killed || echo SURVIVED ))"; done
exit $rc
