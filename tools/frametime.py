#!/usr/bin/env python3
"""Frame-time report of a Vita session from its logs (the build dir the user copied them to).

usage: frametime.py BUILD_DIR [--min-ms 60]

perf.log (PERF lines, one per 120 frames):
  - session percentiles: the last sess_* fields (exact, MKXP_VITA_AUDIT_FIXES builds), otherwise
    estimated from the per-window fp50/fp90/fp99 and f_over* counts;
  - per map and battle: windows, mean fps, frames over 34 / 100 ms, worst frame;
  - which phase dominates the windows that had a frame over 100 ms (max of each *_ms field).
slow_frames.log (RUBY_PROF + FRAME_HIST builds, one line per frame over SLOW_FRAME_MS):
  - frames by dominant cause: GC, blocking I/O (blk=), image decode/upload, text, Ruby phases,
    and the Ruby methods (sf=) and profiler phases (p_*) that take the most time in them.
"""
import os
import re
import sys
from collections import Counter, defaultdict


def kv(line):
    return dict(re.findall(r'(\S+?)=(\S+)', line))


def num(v, idx=None):
    try:
        if idx is not None:
            return float(v.split('/')[idx])
        return float(v)
    except (ValueError, IndexError, AttributeError):
        return 0.0


def perf_report(path):
    rows = [kv(l) for l in open(path, errors='replace') if l.startswith('PERF win=')]
    if not rows:
        print('perf.log: no PERF lines')
        return
    frames = sum(int(num(r.get('frames', '0'))) for r in rows)
    print('== perf.log: %d windows, %d frames (~%.1f min at 60 fps)' % (len(rows), frames, frames / 3600.0))
    last = rows[-1]
    if 'sess_p50' in last:
        print('session frame times (exact, %s frames, boot included): p50 %s  p90 %s  p95 %s  p99 %s  p99.9 %s  worst %s ms' % (
            last['sess_frames'], last['sess_p50'], last['sess_p90'], last['sess_p95'], last['sess_p99'],
            last['sess_p999'], last['sess_max']))
    elif 'fp50' in last:
        fp = sorted(num(r['fp50']) for r in rows if 'fp50' in r)
        print('session (estimated from windows): median window p50 %.1f ms, worst frame %.1f ms after boot '
              '(window 0, boot and first load: %.1f ms)' % (
                  fp[len(fp) // 2], max([num(r.get('fmax', '0')) for r in rows[1:]] or [0.0]),
                  num(rows[0].get('fmax', '0'))))
    tot = Counter()
    for r in rows:
        for k in ('f_over17', 'f_over34', 'f_over50', 'f_over100'):
            tot[k] += int(num(r.get(k, '0')))
    if tot:
        print('frames over 17 ms %d (%.2f%%), 34 ms %d, 50 ms %d, 100 ms %d' % (
            tot['f_over17'], 100.0 * tot['f_over17'] / max(frames, 1), tot['f_over34'], tot['f_over50'], tot['f_over100']))
    # per map / battle
    groups = defaultdict(list)
    for r in rows[1:]:
        groups[(r.get('map', '?'), r.get('battle', '?'))].append(r)
    print('%-6s %-6s %5s %7s %8s %8s %9s' % ('map', 'battle', 'win', 'fps', '>34ms', '>100ms', 'worst ms'))
    for (m, b), rs in sorted(groups.items(), key=lambda kvp: -sum(int(num(x.get('f_over34', '0'))) for x in kvp[1]))[:15]:
        print('%-6s %-6s %5d %7.1f %8d %8d %9.1f' % (
            m, b, len(rs), sum(num(x['fps']) for x in rs) / len(rs), sum(int(num(x.get('f_over34', '0'))) for x in rs),
            sum(int(num(x.get('f_over100', '0'))) for x in rs), max(num(x.get('fmax', '0')) for x in rs)))
    comp = ['ruby_ms', 'prepare_ms', 'composite_ms', 'final_blit_ms', 'swap_ms', 'setup_ms']
    dom = Counter()
    for r in rows[1:]:
        if int(num(r.get('f_over100', '0'))):
            mx = {k: num(r[k], 2) for k in comp if k in r}
            if mx:
                dom[max(mx, key=mx.get)] += 1
    if dom:
        print('windows with a frame over 100 ms, dominant phase (max within the window): ' +
              ', '.join('%s %d' % kvp for kvp in dom.most_common()))


def slow_report(path, min_ms):
    lines = [kv(l) for l in open(path, errors='replace') if l.startswith('SLOW_FRAME')]
    lines = [r for r in lines if num(r.get('ms', '0')) >= min_ms]
    if not lines:
        print('slow_frames.log: no frames >= %.0f ms' % min_ms)
        return
    print('== slow_frames.log: %d frames >= %.0f ms' % (len(lines), min_ms))
    cause = Counter()
    cause_ms = Counter()
    sf_ms = Counter()
    p_ms = Counter()
    op_ms = Counter()
    for r in lines:
        ms = num(r['ms'])
        parts = {
            'gc': num(r.get('gc_ms', '0')),
            'blocking (I/O, waits)': num(r.get('blk', '0')),
            'image decode/upload': num(r.get('png_decode', '0/0'), 1) + num(r.get('bitmap_upload', '0/0'), 1) +
                                   num(r.get('glTexImage2D', '0/0'), 1),
            'text (draw_text/TTF)': num(r.get('draw_text', '0/0'), 1) + num(r.get('TTF_Render', '0/0'), 1),
            'render (prepare+composite)': num(r.get('prep', '0/0'), 1) + num(r.get('comp', '0/0'), 1),
        }
        parts['other Ruby'] = max(0.0, num(r.get('ruby_ms', '0')) - sum(v for k, v in parts.items() if k != 'render (prepare+composite)'))
        top = max(parts, key=parts.get)
        cause[top] += 1
        cause_ms[top] += ms
        for item in r.get('sf', '').split(','):
            if ':' in item:
                name, v = item.rsplit(':', 1)
                sf_ms[name] += num(v)
        for k, v in r.items():
            if k.startswith('p_'):
                p_ms[k] += num(v)
            elif '/' in v and k not in ('prep', 'comp') and not k.startswith('blk_'):
                op_ms[k] += num(v, 1)
    print('dominant cause:   ' + ',  '.join('%s %d frames (%.0f ms)' % (k, n, cause_ms[k]) for k, n in cause.most_common()))
    print('Ruby methods (sf=, ms summed over these frames): ' + ', '.join('%s %.0f' % kvp for kvp in sf_ms.most_common(12)))
    print('profiler phases (p_*): ' + ', '.join('%s %.0f' % kvp for kvp in p_ms.most_common(10)))
    print('native operations: ' + ', '.join('%s %.0f' % kvp for kvp in op_ms.most_common(10)))
    worst = sorted(lines, key=lambda r: -num(r['ms']))[:8]
    print('worst frames:')
    for r in worst:
        sf = ','.join(r.get('sf', '').split(',')[:3])
        print('  %6.1f ms  ruby %6.1f  gc %5.1f  blk %5.1f  map %s %s  %s' % (
            num(r['ms']), num(r.get('ruby_ms', '0')), num(r.get('gc_ms', '0')), num(r.get('blk', '0')),
            r.get('map', '?'), r.get('scene', ''), sf))


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        sys.exit(1)
    d = sys.argv[1]
    min_ms = float(sys.argv[sys.argv.index('--min-ms') + 1]) if '--min-ms' in sys.argv else 60.0
    if os.path.exists(os.path.join(d, 'perf.log')):
        perf_report(os.path.join(d, 'perf.log'))
    if os.path.exists(os.path.join(d, 'slow_frames.log')):
        slow_report(os.path.join(d, 'slow_frames.log'), min_ms)


if __name__ == '__main__':
    main()
