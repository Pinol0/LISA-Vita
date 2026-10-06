import re, os
s = open(os.path.join(os.path.dirname(__file__), '../../../src/main.cpp')).read()
a = s.index('#ifdef MKXP_VITA_REGION_INCR\n"'); b = s.index('#endif', a)
lines = re.findall(r'^"(.*)\\n"$', s[a:b], re.M)[1:-1]
out = '\n'.join(l.replace('" VITA_GAME_ROOT "', '/dev/null-').encode().decode('unicode_escape') for l in lines)
open(os.path.join(os.path.dirname(__file__), 'block.rb'), 'w').write(out.replace("'/dev/null-qa.log'", "'/dev/null'") + '\n')
