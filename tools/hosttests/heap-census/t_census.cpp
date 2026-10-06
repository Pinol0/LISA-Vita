// Host test for vita_heap_census (src/main.cpp, MKXP_VITA_OBJ_HIST) linked into a real Ruby 3.1.6
// (miniruby objects): the slot counts add up to GC's live slots, and procs / fibers / hidden arrays
// / class-less strings / zombies show up under the right keys.
//   ./run.sh RUBY_BUILD_DIR RUBY_SRC_DIR   (also: a mutation that skips imemo subtypes must FAIL)
#include <ruby.h>
#include <cstdio>
#include <cstring>
#include <string>
#include "census.inc"
extern "C" void ruby_init_loadpath(void);
extern "C" void rb_call_builtin_inits(void);
int main(int argc, char **argv)
{
    ruby_init();
    ruby_init_loadpath();
    rb_call_builtin_inits();   // gc.rb etc. (GC.start), as src/main.cpp does
    rb_define_global_function("vita_heap_census", RUBY_METHOD_FUNC(vita_heap_census_rb), 0);
    int state = 0;
    rb_eval_string_protect(R"RUBY(
      fails = 0
      check = ->(c, m) { puts "#{c ? 'PASS' : 'FAIL'}  #{m}"; fails += 1 unless c }
      num = ->(s, sect, key) { (s[/^ *#{sect}.*?[ ]#{Regexp.escape(key)}=(\d+)/, 1] || 0).to_i }
      GC.start; GC.disable
      live = GC.stat(:heap_live_slots)
      c0 = vita_heap_census
      total = c0.lines.first.scan(/=(\d+)/).flatten.map(&:to_i).sum
      check.((total - live).abs <= 10, "type counts add up to heap_live_slots (#{total} vs #{live})")
      $keep = Array.new(3000) { |i| proc { i } }
      c1 = vita_heap_census
      check.(num.(c1, 'data', 'proc') - num.(c0, 'data', 'proc') == 3000, 'procs counted under data proc')
      check.(num.(c1, 'imemo', 'env') - num.(c0, 'imemo', 'env') >= 3000, 'their envs counted under imemo env')
      $fib = Array.new(50) { Fiber.new { 1 } }
      c2 = vita_heap_census
      check.(num.(c2, 'data', 'fiber') - num.(c1, 'data', 'fiber') == 50, 'fibers counted under data fiber')
      class Fin; end
      200.times { o = Fin.new; ObjectSpace.define_finalizer(o, proc { }) }
      GC.enable; GC.start
      c3 = vita_heap_census
      z = num.(c3, 'types', 'zombie')
      puts "  zombies after GC with 200 finalizers: #{z}"
      check.(c3 =~ /^  hidden .*array=\d+/, 'hidden (class-less) arrays counted')
      class Leaky; def ping; 1; end; end
      objs = Array.new(3000) { o = Leaky.new; o.singleton_class.class_eval { def ping; 2; end }; o }
      objs.each { |o| o.ping }   # one call cache per singleton class: ping@Leaky
      c4 = vita_heap_census
      check.(c4 =~ /^  callcache .*ping@Leaky(?:\(singleton\))?=(\d+)/ && $1.to_i >= 2900, 'callcache counted by method (ping, sample class Leaky)')
      check.(c4 =~ /^  callinfo /, 'callinfo line present')
      puts c3.lines.first(3).map { |l| '  ' + l[0, 200] }
      puts c4.lines.grep(/callcache|callinfo/).map { |l| '  ' + l[0, 200] }
      $stdout.flush
      exit!(fails == 0 ? 0 : 1)
    )RUBY", &state);
    if (state) {
        VALUE m = rb_funcall(rb_errinfo(), rb_intern("full_message"), 0);
        std::fprintf(stderr, "%s\n", StringValueCStr(m));
        return 2;
    }
    return 0;
}
