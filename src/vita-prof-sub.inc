/* Included by vita_diag.cpp (MKXP_VITA_PROF_SUB) and by tools/hosttests/prof-sub/host_main.cpp. */
/*
 * Per-method profiler (MKXP_VITA_PROF_SUB, d77): d76b battles on map 282 spent 15-25 ms/frame in
 * Spriteset_Battle#update, maps 282/283 ~20 ms in Game_Map/Game_Player/Spriteset_Map, with no detail.
 * main.cpp wraps the update methods (slot = vita_prof_sub_slot("Class#meth") at install, then
 * t0 = vita_prof_sub_enter ... vita_prof_sub(slot, t0) around super). Self time = inclusive time minus
 * the measured methods called inside it, from a stack of child totals. Only the root Fiber counts:
 * an interpreter Fiber can yield inside a measured method and break the nesting (sub_skip counts
 * those calls). PERF sub_top=Class#meth:self_ms/incl_ms/calls (top by self time), sub_calls.
 */
struct ProfSub { VALUE name; uint64_t selfUs, inclUs; unsigned n; };
static const int kSubMax = 512, kSubDepth = 64;
static ProfSub gSub[kSubMax];
static int gSubN = 0;
static uint64_t gSubChild[kSubDepth];
static int gSubDepth = 0;
static unsigned gSubCalls = 0, gSubSkip = 0, gSubLost = 0;
static uint64_t gSubFrameUs[kSubMax];   /* self time in the current frame (slow-frame log) */
static VALUE gSubRoot = Qnil;   /* root Fiber of the main thread, at the first slot */

static VALUE rbProfSubSlot(VALUE self, VALUE name)
{
    (void)self;
    if (NIL_P(gSubRoot)) gSubRoot = rb_fiber_current();
    if (gSubN == kSubMax) return Qnil;
    const VALUE n = rb_str_new_frozen(StringValue(name));
    rb_gc_register_mark_object(n);
    gSub[gSubN] = ProfSub{ n, 0, 0, 0 };
    return INT2FIX(gSubN++);
}
static VALUE rbProfSubEnter(VALUE self)
{
    (void)self;
    if (gSubDepth == kSubDepth || rb_fiber_current() != gSubRoot) {
        ++gSubSkip;
        return Qnil;
    }
    gSubChild[gSubDepth++] = 0;
    return LONG2FIX((long)(vitaDiagNow() & kProfMask));
}
static VALUE rbProfSub(VALUE self, VALUE slot, VALUE t0)
{
    (void)self;
    if (!FIXNUM_P(t0)) return Qnil;
    if (gSubDepth == 0) { ++gSubLost; return Qnil; }   /* cannot happen with ensure; counted, not trusted */
    const uint64_t dur = profSince(t0), child = gSubChild[--gSubDepth];
    if (gSubDepth) gSubChild[gSubDepth - 1] += dur;
    ++gSubCalls;
    const int i = FIXNUM_P(slot) ? (int)FIX2LONG(slot) : -1;
    if (i < 0 || i >= gSubN) return Qnil;
    gSub[i].selfUs += dur > child ? dur - child : 0;
    gSubFrameUs[i] += dur > child ? dur - child : 0;
    gSub[i].inclUs += dur;
    gSub[i].n++;
    return Qnil;
}
static int subAppend(char *buf, int cap)
{
    int len = 0;
    for (int k = 0; k < 14 && len < cap; ++k) {
        int best = -1;
        for (int i = 0; i < gSubN; ++i)
            if (gSub[i].selfUs && (best < 0 || gSub[i].selfUs > gSub[best].selfUs)) best = i;
        if (best < 0) break;
        len += std::snprintf(buf + len, cap - len, "%s%s:%.2f/%.2f/%u", k ? "," : " sub_top=", RSTRING_PTR(gSub[best].name),
                             gSub[best].selfUs / 1000.0, gSub[best].inclUs / 1000.0, gSub[best].n);
        gSub[best].selfUs = 0;
    }
    if (len < cap)
        len += std::snprintf(buf + len, cap - len, " sub_calls=%u sub_skip=%u sub_lost=%u sub_slots=%d", gSubCalls, gSubSkip,
                             gSubLost, gSubN);
    for (int i = 0; i < gSubN; ++i) gSub[i].selfUs = gSub[i].inclUs = 0, gSub[i].n = 0;
    gSubCalls = gSubSkip = gSubLost = 0;
    return len;
}

/* Slow-frame log (d85): the 3 methods with the most own time in this frame (>= 0.3 ms),
 * " sf=Class#meth:ms,..."; the per-frame totals restart either way. */
static int subFrameAppend(char *buf, int cap, bool slow)
{
    int len = 0;
    if (slow && cap > 0) {
        for (int k = 0; k < 3 && len < cap - 80; ++k) {
            int best = -1;
            for (int i = 0; i < gSubN; ++i)
                if (gSubFrameUs[i] >= 300 && (best < 0 || gSubFrameUs[i] > gSubFrameUs[best])) best = i;
            if (best < 0) break;
            len += std::snprintf(buf + len, cap - len, "%s%s:%.1f", k ? "," : " sf=", RSTRING_PTR(gSub[best].name), gSubFrameUs[best] / 1000.0);
            gSubFrameUs[best] = 0;
        }
    }
    for (int i = 0; i < gSubN; ++i) gSubFrameUs[i] = 0;
    return len;
}
