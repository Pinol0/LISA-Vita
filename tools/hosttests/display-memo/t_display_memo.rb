# Host test for MKXP_VITA_DISPLAY_MEMO with LISA's "Event Jitter Fix" (122_Event_Jitter_Fix.rb).
#   ruby t_display_memo.rb SCRIPTS_DIR DUMP_DIR   (DUMP_DIR: check_embedded_ruby.py --dump of a build)
S, D = ARGV
$stdout.sync = true
$fails = 0
def check(n, got, exp) = (got == exp ? nil : ($fails += 1; puts "FAIL #{n}: #{got.inspect} != #{exp.inspect}"))
class Game_Map
  attr_reader :display_x, :display_y
  def initialize = (@display_x = 0; @display_y = 0; @map = Struct.new(:width, :height).new(130, 33))
  def set(x, y) = (@display_x = x; @display_y = y)
  def width = 130; def height = 33; def loop_horizontal? = false; def loop_vertical? = false
  def screen_tile_x = 17; def screen_tile_y = 13
end
src = File.read(Dir[File.join(S, '122_*.rb')].first).gsub("\r\n", "\n")
eval(src, TOPLEVEL_BINDING)
REF_X = Game_Map.instance_method(:display_x); REF_Y = Game_Map.instance_method(:display_y)
memo = File.read(File.join(D, 'VITA_DISPLAY_MEMO.rb')).sub(/^r = \(VitaDisplayMemo\.install.*\z/m, '')
eval(memo, TOPLEVEL_BINDING)
# purity check rejects impure variants (checked before installing on the real class)
k1 = Class.new { def display_x = (@display_x * 32).floor.to_f / 32 }
k2 = Class.new { def display_x = (@other * 32).floor.to_f / 32 }
k3 = Class.new { def display_x = (@display_x * rand(2)).to_f }
k4 = Class.new { def display_x(a = 1) = @display_x * a }
k5 = Class.new { def display_x = (@display_x * 32).round.to_f / 32 }
check('pure accepted', VitaDisplayMemo.pure?(k1.instance_method(:display_x), :@display_x), true)
check('other ivar rejected', VitaDisplayMemo.pure?(k2.instance_method(:display_x), :@display_x), false)
check('method call rejected', VitaDisplayMemo.pure?(k3.instance_method(:display_x), :@display_x), false)
check('argument rejected', VitaDisplayMemo.pure?(k4.instance_method(:display_x), :@display_x), false)
check('round not in the allowed sends', VitaDisplayMemo.pure?(k5.instance_method(:display_x), :@display_x), false)
check('attr_reader not memoized', VitaDisplayMemo.pure?(Class.new { attr_reader :display_x }.instance_method(:display_x), :@display_x), false)
check('install', VitaDisplayMemo.install, 'INSTALLED')
rng = Random.new(3)
m1 = Game_Map.new; m2 = Game_Map.new
vals = [0, 1, 7, 0.0, 1.0, 3.125, 3.125, 10.03125, -0.5, 129.96875, 2**40, 1e-9]
bad = 0
20000.times do
  m = rng.rand < 0.1 ? m2 : m1
  r = rng.rand
  if r < 0.3 then m.set(vals.sample(random: rng), m.instance_variable_get(:@display_y))
  elsif r < 0.5 then m.set(m.instance_variable_get(:@display_x), vals.sample(random: rng))
  elsif r < 0.6 then m.set(m.instance_variable_get(:@display_x).dup, m.instance_variable_get(:@display_y))   # same value, new object
  elsif r < 0.7 then m.set(m.instance_variable_get(:@display_x) + 0.03125, m.instance_variable_get(:@display_y))
  end
  a = [m.display_x, m.display_y, m.adjust_x(5.5), m.adjust_y(3)]
  b = [REF_X.bind(m).call, REF_Y.bind(m).call]; b += [5.5 - b[0], 3 - b[1]]
  bad += 1 unless a.zip(b).all? { |p, q| p.eql?(q) }
end
check('values identical (eql?) to the jitter fix, 20000 steps, 2 maps', bad, 0)
m1.set(3.0, 4.0); 50.times { m1.display_x; m1.display_y }
GC.disable
a0 = GC.stat(:total_allocated_objects); 1000.times { }; base = GC.stat(:total_allocated_objects) - a0   # the measurement itself
a0 = GC.stat(:total_allocated_objects); 1000.times { m1.display_x; m1.display_y }
d = GC.stat(:total_allocated_objects) - a0 - base; GC.enable
check('no allocation when unchanged (2000 calls, beyond the measurement)', d <= 0, true)
m1.set(3.0, 4.0); REF_X.bind(m1).call
GC.disable; a0 = GC.stat(:total_allocated_objects); 1000.times { REF_X.bind(m1).call }; r = GC.stat(:total_allocated_objects) - a0 - base; GC.enable
puts "jitter fix display_x: #{(r / 1000.0).round(2)} objects per call; memoized: 0"
puts $fails.zero? ? 'PASS display-memo' : "FAIL #{$fails}"
exit($fails.zero? ? 0 : 1)
