# Writes the error-screen Ruby embedded in src/main.cpp (vitaShowErrorScreen) to stdout; qa.log -> ./qa.log
import re, ast, sys
s = open(sys.argv[1]).read()
seg = s[s.index('VALUE shown = rb_eval_string_protect('):]
seg = seg[:seg.index('&state);')]
out = ''
for m in re.finditer(r'^\s*("(?:[^"\\]|\\.)*")(?:\s*VITA_GAME_ROOT\s*("(?:[^"\\]|\\.)*"))?', seg, re.M):
    out += ast.literal_eval(m.group(1))
    if m.group(2): out += './' + ast.literal_eval(m.group(2))
print(out)
