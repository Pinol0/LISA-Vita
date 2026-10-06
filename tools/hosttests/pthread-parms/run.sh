#!/bin/bash
# Original, then two mutations (must FAIL): pass the freed block instead of the copy; never free.
cd "$(dirname "$0")"; T=$(mktemp -d); S=../../../src/vita-pthread-parms.cpp
sed 's/std::free(/testFree(/' $S > $T/ok.cpp
sed -e 's/std::free(/testFree(/' -e 's/__real_pte_threadStart(&parms)/__real_pte_threadStart(vthreadParms)/' $S > $T/m1.cpp
sed -e 's/    std::free(vthreadParms);//' $S > $T/m2.cpp
for v in ok m1 m2; do g++ -std=gnu++17 -O1 -DSRC="\"$T/$v.cpp\"" t_pthread_parms.cpp -o $T/$v && echo -n "$v: " && $T/$v; done
rm -rf $T
