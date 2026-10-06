#!/usr/bin/env python3
"""Embed tools/soak/soak.rb into src/main.cpp (the #ifdef MKXP_VITA_DEBUG_SOAK block).

Usage (from the project root):  python3 tools/soak/embed_soak.py
Then rebuild. Check the embedded copy with the preprocessed main.cpp (see HANDOFF.md, "Soak").
"""
import os

root = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
rb = open(os.path.join(root, 'tools/soak/soak.rb')).read().rstrip('\n').split('\n')
lines = ['#ifdef MKXP_VITA_DEBUG_SOAK',
         '/* Debug only: resource-churn soak test while R is held at boot (MKXP_VITA_DEBUG_SOAK). Source: soak.rb. */',
         '"  TOPLEVEL_BINDING.eval(<<~\'VITA_SOAK\', \'vita_soak\', 1)\\n"',
         '"  VITA_SOAK_ROOT = \'" VITA_GAME_ROOT "\'\\n"']
for l in rb:
    lines.append('"  ' + l.replace('\\', '\\\\').replace('"', '\\"') + '\\n"')
lines += ['"  VITA_SOAK\\n"', '#endif']

p = os.path.join(root, 'src/main.cpp')
s = open(p).read()
a = s.index('#ifdef MKXP_VITA_DEBUG_SOAK\n/* Debug only: resource-churn')
end = '"  VITA_SOAK\\n"\n#endif\n'
b = s.index(end, a) + len(end)
s = s[:a] + '\n'.join(lines) + '\n' + s[b:]
open(p, 'w').write(s)
print('soak.rb embedded into src/main.cpp')
