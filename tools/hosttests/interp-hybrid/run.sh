#!/bin/bash
# usage: run.sh SCRIPTS_DIR DUMP_DIR [RUBY...]   (DUMP_DIR: check_embedded_ruby.py --dump of a build)
# Runs the original (Fiber) and the hybrid interpreter on the same random lists; traces must match.
# Mutation: SAFE also allows 230 (wait) -> must differ or fail.
cd "$(dirname "$0")"; S=$1; D=$2; shift 2; R=${*:-ruby}; mkdir -p out
$R t_interp_hybrid.rb "$S" "$D" fiber out/fiber.bin || exit 1
$R t_interp_hybrid.rb "$S" "$D" hybrid out/hybrid.bin || exit 1
cmp -s out/fiber.bin out/hybrid.bin && echo "PASS interp-hybrid (traces identical)" || { echo "FAIL traces differ"; rc=1; }
rm -rf out/mut; cp -r "$D" out/mut
sed -i 's/SAFE_CODES = \[0, /SAFE_CODES = [0, 230, /' out/mut/VITA_INTERP_HYBRID.rb
$R t_interp_hybrid.rb "$S" out/mut hybrid out/mut.bin > out/mut.log 2>&1
if [ -f out/mut.bin ] && cmp -s out/fiber.bin out/mut.bin; then echo "mutation: SURVIVED"; rc=1; else echo "mutation: killed"; fi
exit ${rc:-0}
