#!/usr/bin/env python3
"""Static check of the port's Ruby bindings (MKXP_VITA_AUDIT_FIXES): every binding method that reaches
a C++ object (getPrivateData) must be an RB_METHOD_GUARD, so a C++ exception (mkxp Exception: disposed
object, RGSSError; std::bad_alloc) becomes a Ruby exception instead of crossing Ruby's C frames
(std::terminate, abort). Upstream mkxp-z guards the same methods.
usage: t_guards.py [binding.cpp...]   (default: src/bitmap-binding-vita.cpp)
Exit 1 and FAIL lines for every unguarded method; MUTATE=1 checks the source without the fix branch."""
import os, re, sys

HERE = os.path.dirname(os.path.abspath(__file__))
files = sys.argv[1:] or [os.path.join(HERE, '../../../src/bitmap-binding-vita.cpp')]
define = os.environ.get('MUTATE') is None   # MUTATE=1: as compiled without MKXP_VITA_AUDIT_FIXES


def resolve(text):
    """Keep the MKXP_VITA_AUDIT_FIXES branch of #ifdef/#else/#endif blocks on that macro only."""
    out, stack = [], []
    for line in text.splitlines():
        s = line.strip()
        if s.startswith('#if'):
            mine = s == '#ifdef MKXP_VITA_AUDIT_FIXES'
            stack.append([mine, True])
            if mine:
                stack[-1][1] = define
                continue
        elif s.startswith('#else') and stack and stack[-1][0]:
            stack[-1][1] = not define
            continue
        elif s.startswith('#endif') and stack:
            if stack.pop()[0]:
                continue
        if all(keep for _, keep in stack):
            out.append(line)
    return '\n'.join(out)


fails = checked = 0
for f in files:
    src = resolve(open(f).read())
    for m in re.finditer(r'\b(RB_METHOD|RB_METHOD_GUARD)\((\w+)\)\s*\{', src):
        start = m.end(); depth = 1; i = start
        while depth and i < len(src):
            depth += {'{': 1, '}': -1}.get(src[i], 0)
            i += 1
        body = src[start:i]
        if 'getPrivateData' not in body:
            continue
        checked += 1
        if m.group(1) != 'RB_METHOD_GUARD':
            fails += 1
            print('FAIL  %s: %s reaches a C++ object without RB_METHOD_GUARD' % (os.path.basename(f), m.group(2)))
print('%s  %d binding methods reaching C++ objects, all guarded' % ('FAIL' if fails else 'PASS', checked))
sys.exit(1 if fails else 0)
