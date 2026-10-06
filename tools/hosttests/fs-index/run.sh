#!/bin/bash
# usage: run.sh   (host g++; also builds a mutation without lower-casing of names that must FAIL)
cd "$(dirname "$0")"; mkdir -p out
g++ -std=gnu++17 -O1 -Wall t_fs_index.cpp ../../../src/vita-fs-index.cpp -lpthread -o out/t || exit 1
sed 's/name = lower(p.substr(slash + 1));/name = p.substr(slash + 1);/' ../../../src/vita-fs-index.cpp > out/mut.cpp
cmp -s out/mut.cpp ../../../src/vita-fs-index.cpp && { echo "NO MUTATION"; exit 1; }
g++ -std=gnu++17 -O1 -w -I../../../src t_fs_index.cpp out/mut.cpp -lpthread -o out/tm || exit 1
out/t; rc=$?
echo "mutation:"; out/tm | grep -c FAIL
exit $rc
