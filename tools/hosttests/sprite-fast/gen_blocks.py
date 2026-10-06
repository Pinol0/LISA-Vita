#!/usr/bin/env python3
"""Extract the OFFSCREEN and SPRITE_FAST Ruby blocks from src/main.cpp into blocks.rb.
Nested #ifdef/#else/#endif inside a block are resolved with the defines in $DEFINES (space separated,
e.g. DEFINES=MKXP_VITA_SPRITE_SCROLL)."""
import re, os
DEF = set(os.environ.get('DEFINES', '').split())
s = open(os.path.join(os.path.dirname(__file__), '../../../src/main.cpp')).read()
out = []
for opt, tag in (('MKXP_VITA_OFFSCREEN_SPRITES', 'VITA_OFFSCREEN'), ('MKXP_VITA_SPRITE_FAST', 'VITA_SPRITE_FAST')):
    a = s.index('#ifdef %s\n"' % opt)
    lines = s[a:].split('\n')[1:]
    stack = []; body = []
    for l in lines:
        if l.startswith('#ifdef ') or l.startswith('#if '):
            stack.append(l.split()[1] in DEF); continue
        if l.startswith('#else'):
            stack[-1] = not stack[-1]; continue
        if l.startswith('#endif'):
            if not stack: break
            stack.pop(); continue
        if all(stack):
            m = re.match(r'^"(.*)\\n"$', l)
            if m: body.append(m.group(1))
    out += [l.replace('" VITA_GAME_ROOT "', '/dev/null-').encode().decode('unicode_escape') for l in body[1:-1]]
open(os.path.join(os.path.dirname(__file__), 'blocks.rb'), 'w').write('\n'.join(out).replace("'/dev/null-qa.log'", "'/dev/null'") + '\n')
