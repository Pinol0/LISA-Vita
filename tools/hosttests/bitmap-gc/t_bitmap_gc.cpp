// Host test for MKXP_VITA_BITMAP_GC in a real Ruby 3.1.6 (miniruby objects): objects that report
// their memory through the port's own vitaGcReport (src/bitmap-vita.cpp, extracted by run.sh) when
// made and give it back when freed (rb_gc_adjust_memory_usage, as ~BitmapPrivate), with the GC
// parameters src/main.cpp sets (extracted too), in a session like the game's where it was seen:
// 640x480 bitmaps kept for a while (they become old: only a major GC frees them), then dropped
// without dispose, among the small objects a game makes every frame. The memory held by dropped
// bitmaps must stay near Ruby's oldmalloc limit, 16 MiB (there: 47 MiB of them, then NoMemoryError).
//   ./run.sh RUBY_BUILD_DIR RUBY_SRC_DIR SRC_DIR   (mutants in run.sh)
#include <ruby.h>
#include <cstdio>
#include <cstdlib>
#include <sys/types.h>
extern "C" void ruby_init_loadpath(void);
extern "C" void rb_call_builtin_inits(void);
extern "C" void ruby_gc_set_params(void);
#include "gcenv.inc"   /* VITA_GC_ENV(): the setenv lines of src/main.cpp's GC_MID block */
#include "report.inc"  /* vitaGcReport from src/bitmap-vita.cpp */

static long long gLive = 0;   /* bytes held by bitmaps not yet freed (referenced or garbage) */
struct FakeBmp { size_t bytes; };
static void fbFree(void *p)
{
    FakeBmp *b = (FakeBmp *)p;
    gLive -= (long long)b->bytes;
    rb_gc_adjust_memory_usage(-(ssize_t)b->bytes);
    delete b;
}
static const rb_data_type_t fbType = { "FakeBmp", { 0, fbFree, 0 }, 0, 0, RUBY_TYPED_FREE_IMMEDIATELY };
static VALUE fbNew(VALUE, VALUE w, VALUE h)
{
    FakeBmp *b = new FakeBmp{ (size_t)NUM2INT(w) * NUM2INT(h) * 4 };
    VALUE o = TypedData_Wrap_Struct(rb_cObject, &fbType, b);
    gLive += (long long)b->bytes;
    vitaGcReport(b->bytes);
    return o;
}
static VALUE fbLive(VALUE) { return LL2NUM(gLive); }

int main()
{
    VITA_GC_ENV();
    ruby_init();
    ruby_init_loadpath();
    rb_call_builtin_inits();
    ruby_gc_set_params();
    rb_define_global_function("fake_bitmap", RUBY_METHOD_FUNC(fbNew), 2);
    rb_define_global_function("fake_bitmap_live", RUBY_METHOD_FUNC(fbLive), 0);
    int state = 0;
    rb_eval_string_protect(R"RUBY(
      kept = []                  # what the game still references: the last 12 screens' layers
      peak = 0
      junk = nil
      majors0 = GC.stat(:major_gc_count)
      30_000.times do |frame|
        junk = Array.new(40) { |i| "s#{frame}-#{i}" }      # a game's per-frame garbage: minor GCs
        if frame % 10 == 0
          kept << fake_bitmap(640, 480)                       # made, used for a while, dropped
          kept.shift if kept.size > 12
          garbage = fake_bitmap_live - kept.size * 640 * 480 * 4
          peak = garbage if garbage > peak
        end
      end
      mb = peak / 1048576.0
      majors = GC.stat(:major_gc_count) - majors0
      lim = GC.stat(:oldmalloc_increase_bytes_limit) / 1048576.0
      puts format("3000 bitmaps of 640x480 (1.2 MiB), 12 referenced: dropped ones held at most %.1f MiB; %d major GCs, %d GCs; oldmalloc limit %.0f MiB",
                  mb, majors, GC.count, lim)
      ok = mb <= 24.0 && majors > 0
      puts "#{ok ? 'PASS' : 'FAIL'}  memory of dropped bitmaps stays near the oldmalloc limit (<= 24 MiB)"
      $stdout.flush
      exit!(ok ? 0 : 1)
    )RUBY", &state);
    if (state) {
        VALUE m = rb_funcall(rb_errinfo(), rb_intern("full_message"), 0);
        std::fprintf(stderr, "%s\n", StringValueCStr(m));
        return 2;
    }
    return 0;
}
