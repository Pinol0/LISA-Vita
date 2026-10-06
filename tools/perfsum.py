import re,sys
from collections import defaultdict
rows=[dict(re.findall(r'(\S+?)=(\S+)',l)) for l in open(sys.argv[1]) if l.startswith('PERF win=')]
scene=sys.argv[2] if len(sys.argv)>2 else 'BATTLE'
b=[r for r in rows if r.get('scene')==scene]
if not b: print('none'); sys.exit()
fps=[float(r['fps']) for r in b]
keys=['ruby_ms','prepare_ms','setup_ms','composite_ms','delay_ms','final_blit_ms','swap_ms','frame_ms']
print('%s win=%d fps media %.1f min %.1f max %.1f'%(scene,len(b),sum(fps)/len(fps),min(fps),max(fps)))
print('  medie: '+' '.join('%s=%.1f'%(k[:-3],sum(float(r[k].split('/')[0]) for r in b)/len(b)) for k in keys if k in b[0]))
print('  max frame per finestra: '+' '.join('%.0f'%float(r['frame_ms'].split('/')[2]) for r in b))
sub=[k for k in b[0] if k.endswith('_ms') and k not in keys and '/' not in b[0][k]]
print('  sub: '+' '.join('%s=%.2f'%(k[:-3],sum(float(r[k]) for r in b)/len(b)) for k in sub))
cnt=['draw_calls','vbo_uploads','tex_image','tex_subimage','window_draws','sprite_draws','fbo_live','fbo_rt_max']
print('  cnt/fin: '+' '.join('%s=%.0f'%(k,sum(float(r.get(k,0)) for r in b)/len(b)) for k in cnt))
tot={}
for r in b:
    for k,v in r.items():
        if k.startswith('bmp_') or k.startswith('refresh['):
            p=v.split('/'); tot.setdefault(k,[0,0.0]); tot[k][0]+=int(p[0]); tot[k][1]+=float(p[1])
for k,(c,ms) in sorted(tot.items(), key=lambda x:-x[1][1])[:14]: print('  %-34s n/fin=%6.1f ms/fin=%7.1f ms/call=%.3f'%(k,c/len(b),ms/len(b), ms/c if c else 0))
