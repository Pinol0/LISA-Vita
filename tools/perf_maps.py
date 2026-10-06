#!/usr/bin/env python3
"""Per scene/map/delay summary of perf.log (d37+ profiler fields). usage: perf_maps.py perf.log [min_windows]"""
import sys, collections
P=['map','map_events','map_interp','player','spriteset_map','windows','spriteset_battle',
   'spr_character','spr_reflect','spr_mirror','spr_shadow','spr_icon']
rows=[dict(kv.split('=',1) for kv in l.split()[1:] if '=' in kv) for l in open(sys.argv[1]) if l.startswith('PERF win')]
mn=int(sys.argv[2]) if len(sys.argv)>2 else 3
g=collections.defaultdict(list)
for d in rows: g[(d['scene'],d.get('map','?'),d.get('delay_us','0'))].append(d)
print('%-18s %4s %5s %5s %5s %5s %4s | '%('scene/map/delay','n','fps','ruby','swap','gc','gcN')+' '.join('%7s'%p.replace('spr_','s_')[:7] for p in P)+' events')
def pv(x,k):
    v=x.get('p_'+k,'0').split('/'); return float(v[0]), (int(v[1]) if len(v)>1 else 0)
for k in sorted(g, key=lambda k:(k[0],int(k[1]) if k[1].isdigit() else -1,int(k[2]))):
    v=g[k]; n=len(v)
    if n<mn: continue
    fr=sum(int(x['frames']) for x in v)
    a=lambda key: sum(float(x[key].split('/')[0]) for x in v)/n
    ph=[sum(pv(x,p)[0] for x in v)/fr for p in P]
    gc=sum(float(x.get('gc_ms',0)) for x in v)/fr; gcn=sum(int(x.get('gc_starts',0)) for x in v)/n
    print('%-18s %4d %5.1f %5.1f %5.1f %5.2f %4.1f | '%('/'.join(k),n,a('fps'),a('ruby_ms'),a('swap_ms'),gc,gcn)+' '.join('%7.2f'%x for x in ph)+' %s'%v[0].get('events'))
# sprite counts per frame per class
cnt=collections.defaultdict(list)
for d in rows:
    if d['scene']!='MAP': continue
    fr=int(d['frames']); cnt[d.get('map')].append([pv(d,p)[1]/fr for p in P[7:]])
print('sprites per frame (character reflect mirror shadow icon):')
for m,v in sorted(cnt.items(), key=lambda kv:int(kv[0]) if kv[0] and kv[0].isdigit() else -1):
    if len(v)>=mn: print('  map',m,' '.join('%.0f'%(sum(x[i] for x in v)/len(v)) for i in range(5)))
