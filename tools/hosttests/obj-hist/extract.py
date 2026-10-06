# Writes the MKXP_VITA_OBJ_HIST heredoc body from src/main.cpp to stdout (qa.log -> ./qa.log)
import ast, sys
s = open(sys.argv[1]).read()
seg = s[s.index("<<~'VITA_OBJ_HIST'"):]
seg = seg[seg.index('\n') + 1:seg.index('"  VITA_OBJ_HIST\\n"')]
out = ''
for line in seg.split('\n'):
    line = line.strip()
    if line: out += ast.literal_eval(line.replace('" VITA_GAME_ROOT "', './'))
print(out)
