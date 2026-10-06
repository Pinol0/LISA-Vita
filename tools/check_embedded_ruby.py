#!/usr/bin/env python3
"""Syntax-check every Ruby source embedded in src/main.cpp exactly as a build compiles it.
usage: check_embedded_ruby.py BUILD_DIR [--dump DIR]
--dump DIR: also writes each heredoc body, dedented, to DIR/<TAG>.rb (host tests run the exact code a
build embeds, with its -D options).
Preprocesses main.cpp with the build's own compile command (same -D options), takes every
rb_eval_string_protect() argument made of string literals, and runs `ruby -c` on it and on every
heredoc evaluated from it (TOPLEVEL_BINDING.eval(<<~'TAG', ...)). Exit 1 on any syntax error.
Added after d42: a constant assigned outside its heredoc (inside def rgss_main) = SyntaxError at boot."""
import os, re, subprocess, sys, tempfile, textwrap
bd = os.path.abspath(sys.argv[1])
dump = sys.argv[sys.argv.index('--dump') + 1] if '--dump' in sys.argv else None
if dump: os.makedirs(dump, exist_ok=True)
cmd = subprocess.run(['make', '-n', '-B', '-f', 'CMakeFiles/mkxp_vita_minimal.dir/build.make',
                      'CMakeFiles/mkxp_vita_minimal.dir/src/main.cpp.obj'], cwd=bd, capture_output=True, text=True).stdout
line = next(l for l in cmd.splitlines() if 'main.cpp' in l and ' -c ' in l and 'g++' in l and 'cmake_echo' not in l)
line = line.split('&&')[-1].strip()
out = os.path.join(tempfile.mkdtemp(), 'main.i')
pp = re.sub(r'\s-o\s+\S+', ' -o ' + out, line).replace(' -c ', ' -E ')
r = subprocess.run(pp, shell=True, cwd=bd, capture_output=True, text=True)
if r.returncode: print(r.stderr[-2000:]); sys.exit(2)
src = open(out, errors='replace').read()
src = '\n'.join(l for l in src.splitlines() if not l.startswith('#'))
lit = r'"(?:[^"\\\n]|\\.)*"'
def cstr(s):
    s = s[1:-1]
    return s.encode('latin-1', 'backslashreplace').decode('unicode_escape').encode('latin-1').decode('utf-8', 'replace')
progs = []
for m in re.finditer(r'rb_eval_string_protect\s*\(\s*((?:' + lit + r'\s*)+)\s*,', src):
    progs.append(''.join(cstr(x) for x in re.findall(lit, m.group(1))))
fails = 0
def check(name, code):
    global fails
    with tempfile.NamedTemporaryFile('w', suffix='.rb', delete=False) as f: f.write(code)
    r = subprocess.run(['ruby', '-c', f.name], capture_output=True, text=True)
    ok = r.returncode == 0
    if not ok: fails += 1; print('FAIL', name); print(r.stderr[:1500])
    return ok
n = 0
for i, p in enumerate(progs):
    n += 1; check('eval#%d (%d bytes)' % (i, len(p)), p)
    for h in re.finditer(r"<<~'(\w+)'", p):
        tag = h.group(1); start = p.index('\n', h.end()) + 1
        end = re.search(r'^\s*' + tag + r'\s*$', p[start:], re.M)
        if not end: fails += 1; print('FAIL heredoc', tag, 'not terminated'); continue
        body = textwrap.dedent(p[start:start + end.start()])
        n += 1; check('heredoc ' + tag, body)
        if dump:
            with open(os.path.join(dump, tag + '.rb'), 'w') as f: f.write(body)
print('%s: %d Ruby sources checked, %d failed' % ('PASS' if not fails else 'FAIL', n, fails))
sys.exit(1 if fails else 0)
