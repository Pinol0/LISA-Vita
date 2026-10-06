# Host test for MKXP_VITA_PROF_SUB: the VitaProfSub installer from src/main.cpp + the C accounting
# (src/vita-prof-sub.inc) with a fake clock. Wrapped methods return the same values, keep visibility,
# blocks and keywords; self/inclusive times follow the nesting; exceptions and a Fiber yielding inside
# a measured method do not break the stack. usage: run.sh (builds host_main.cpp)
$stdout.sync = true
src = File.read($test_args[0])
ruby = src.lines.map { |l| l =~ /^\s*"(.*)\\n"\s*$/ ? $1.gsub('\\"', '"').gsub('\\\\', '\\') + "\n" : "#--\n" }.join
mod = ruby[/^  module VitaProfSub\n.*?^  end\n/m] or abort 'FAIL: module VitaProfSub not found'
abort 'FAIL: run line not found' unless ruby.include?('  VitaProfSub.run(VitaProfSub::DEEP, VitaProfSub::TOP)')
def vita_diag_mark(*a); $marks << a; end
$marks = []
def tick(ms) = vita_test_tick(ms * 1000)   # PERF prints ms with 2 decimals

class Sprite; def update; tick(1); :core; end; def flash; end; end
class Plane; end; class Window; end; class Viewport; end; class Bitmap; end
class Sprite_Battler < Sprite   # EXTRA pattern (d78)
  def update; tick(2); super; setup_new_effect; end
  def setup_new_effect; tick(3); :e; end
  def animation?; false; end
end
class Spr
  def update; tick(10); update_a; v = update_b(2) { |x| x * 3 }; [v, update_kw(k: 4)]; end
  def update_a; tick(5); :a; end
  def update_kw(k: 1); tick(1); k; end
  def updated?; true; end
  def boom_update; tick(2); raise ArgumentError, 'x'; end
  private
  def update_b(n); tick(7); yield n; end
end
class Spr   # alias chain, as LISA's scripts do
  alias old_update_a update_a
  def update_a; tick(1); old_update_a; end
end
class SprChild < Spr; end
class Many; def update; tick(3); :m; end; def update_x; tick(100); end; end
class Fib; def update_f; tick(4); Fiber.yield; tick(4); :f; end; end
eval(mod)
VitaProfSub.run(%i[Spr Fib Missing Sprite_Battler], %i[Many])

$fails = 0
def check(name, got, exp) = (got == exp ? nil : ($fails += 1; puts "FAIL #{name}: #{got.inspect} != #{exp.inspect}"))
def parse(dump)
  top = dump[/ sub_top=(\S+)/, 1].to_s.split(',').map { |e| n, v = e.split(':'); [n, v.split('/').map(&:to_f)] }.to_h
  [top, dump[/sub_calls=(\d+) sub_skip=(\d+) sub_lost=(\d+)/, 0]]
end
vita_prof_sub_dump   # reset
check('wrapped update', Spr.ancestors.first != Spr, true)
check('predicate not wrapped', Spr.instance_method(:updated?).owner, Spr)
check('private kept', [Spr.private_method_defined?(:update_b), Spr.public_method_defined?(:update_a)], [true, true])
check('result', Spr.new.update, [6, 4])
top, cnt = parse(vita_prof_sub_dump)
check('Spr#update', top['Spr#update'], [10.0, 24.0, 1.0])
check('Spr#update_a', top['Spr#update_a'], [1.0, 6.0, 1.0])
check('Spr#old_update_a', top['Spr#old_update_a'], [5.0, 5.0, 1.0])
check('Spr#update_b', top['Spr#update_b'], [7.0, 7.0, 1.0])
check('Spr#update_kw', top['Spr#update_kw'], [1.0, 1.0, 1.0])
check('counters', cnt, 'sub_calls=5 sub_skip=0 sub_lost=0')
# Subclass instance goes through the module prepended to Spr.
SprChild.new.update_a; top, = parse(vita_prof_sub_dump); check('subclass', top['Spr#update_a'], [1.0, 6.0, 1.0])
# Exception inside a measured method: propagates, stack restored.
e = (Spr.new.boom_update rescue $!); check('exception', e.class, ArgumentError)
Spr.new.update_a; top, cnt = parse(vita_prof_sub_dump)
check('after exception', [top['Spr#boom_update'], top['Spr#update_a'], cnt], [[2.0, 2.0, 1.0], [1.0, 6.0, 1.0], 'sub_calls=3 sub_skip=0 sub_lost=0'])
# TOP class: #update only.
check('top update_x not wrapped', Many.instance_method(:update_x).owner, Many)
3.times { Many.new.update }; top, = parse(vita_prof_sub_dump); check('Many#update', top['Many#update'], [9.0, 9.0, 3.0])
# A Fiber yields inside a measured method while the root runs measured methods.
f = Fiber.new { Fib.new.update_f }
f.resume
Spr.new.update_a
check('fiber result', f.resume, :f)
top, cnt = parse(vita_prof_sub_dump)
check('fiber not counted', top.key?('Fib#update_f'), false)
check('root during fiber', top['Spr#update_a'], [1.0, 6.0, 1.0])
check('fiber counters', cnt, 'sub_calls=2 sub_skip=1 sub_lost=0')
# EXTRA: Sprite_Battler's setup_* wrapped, predicates and core Sprite methods (flash) not.
vita_prof_sub_dump
check('battler result', Sprite_Battler.new.update, :e)
top, = parse(vita_prof_sub_dump)
check('battler update', top['Sprite_Battler#update'], [3.0, 6.0, 1.0])
check('battler setup', top['Sprite_Battler#setup_new_effect'], [3.0, 3.0, 1.0])
check('battler predicate', Sprite_Battler.instance_method(:animation?).owner, Sprite_Battler)
check('core not wrapped', Sprite_Battler.instance_method(:flash).owner, Sprite)
check('no hook failures', $marks, [])
puts $fails.zero? ? 'PASS prof-sub' : "FAIL #{$fails}"
$test_rc = $fails.zero? ? 0 : 1
