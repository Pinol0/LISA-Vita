#!/usr/bin/env python3
"""Rebuild every member of pthread-coop-fiberexit-v2/libruby-static.a with its reconstructed
compile command, optionally inserting extra flags right after the compiler (CFLAGS position).
Usage: build_libruby.py OUTDIR [extra flags...]   e.g.  OUTDIR-O0   |   OUTDIR-O2 -O2"""
import subprocess, sys, os, shlex
O=os.path.dirname(os.path.abspath(__file__))
outdir=os.path.abspath(sys.argv[1]); extra=sys.argv[2:]
os.makedirs(outdir, exist_ok=True)
VC='-I'+os.environ.get('VITA_COMPAT_DIR', os.path.join(O, '..', 'vita-compat'))+'/include'
XC='-D_FORTIFY_SOURCE=2 -fstack-protector-strong -fno-strict-overflow -fvisibility=hidden -fexcess-precision=standard -DRUBY_EXPORT'
INC='-I. -I.ext/include/arm-eabi -I../ruby-3.1.6/include -I../ruby-3.1.6 -I../ruby-3.1.6/enc/unicode/13.0.0'
G2={'error.o','file.o','io.o','io_buffer.o','parse.o','thread.o','vm.o'}          # + -std=gnu17 + vita-compat
G3={'gc.o','process.o'}                                                            # old config.h + -std=gnu17 + vita-compat
G4={'encdb.o','transdb.o','single_byte.o','stringio.o','strscan.o','zlib.o','pathname.o'}  # no -fPIC, -std=gnu17
G5={'ascii.o':'enc/ascii.c','us_ascii.o':'enc/us_ascii.c','unicode.o':'enc/unicode.c','utf_8.o':'enc/utf_8.c',
    'windows_1252.o':'enc/windows_1252.c','newline.o':'enc/trans/newline.c'}       # builtin encodings, no ONIG_ENC_REGISTER
recipe=[]
for l in open(O+'/make-dryrun-commands.tsv'):
    m,d,t,c=l.rstrip('\n').split('\t'); a=shlex.split(c); tree='tree-cur'
    if m in G2: a=a[:1]+['-std=gnu17']+a[1:a.index('-o')]+[VC]+a[a.index('-o'):]
    elif m in G3: tree='tree-oldcfg'; a=a[:1]+['-std=gnu17']+a[1:a.index('-o')]+[VC]+a[a.index('-o'):]
    elif m in G4: a=[a[0],'-std=gnu17']+[x for x in a[1:] if x!='-fPIC']
    elif m in G5: a=['arm-vita-eabi-gcc','-std=gnu17']+XC.split()+INC.split()+['-o',m,'-c','../ruby-3.1.6/'+G5[m]]; d='.'
    elif m=='dmyenc.o': a=['arm-vita-eabi-gcc']+INC.split()[:4]+['-o',m,'-c','vita_encinit.c']; d='.'
    recipe.append((m,tree,d,a))
F=XC+' -I. -I.ext/include/arm-eabi -I../ruby-3.1.6/include -I../ruby-3.1.6 -I../ruby-3.1.6/enc/unicode/13.0.0 '+VC
recipe.append(('Context.o','tree-cur','.',['arm-vita-eabi-gcc']+F.split()+['-Wall','-o','Context.o','-c','../ruby-3.1.6/coroutine/pthread/Context.c']))
recipe.append(('cont.o','tree-cur','.',['arm-vita-eabi-gcc']+F.split()+['-o','cont.o','-c','../ruby-3.1.6/cont.c']))
log=open(outdir+'/compile_commands.log','w'); wl=open(outdir+'/warnings.log','w'); fails=0; warns=0
env=dict(os.environ, PATH=os.path.join(os.environ['VITASDK'], 'bin')+':'+os.environ['PATH'])
for m,tree,d,a in recipe:
    a=list(a); i=a.index('-o'); a[i+1]=os.path.join(outdir,m); a=a[:1]+extra+a[1:]
    cwd=os.path.join(O,tree,d); log.write(f'[{tree}/{d}] '+' '.join(shlex.quote(x) for x in a)+'\n')
    r=subprocess.run(a,cwd=cwd,capture_output=True,text=True,env=env)
    if r.stderr: warns+=r.stderr.count('warning:'); wl.write(f'===== {m}\n{r.stderr}')
    if r.returncode: fails+=1; print('FAIL',m,r.stderr[-400:])
print(f'members={len(recipe)} fails={fails} warnings={warns}')
