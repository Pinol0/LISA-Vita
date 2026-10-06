# Host test for MKXP_VITA_SPRITE_FAST (+ OFFSCREEN_SPRITES) against LISA's own scripts.
#   python3 gen_blocks.py && ruby t_sprite_fast.rb SCRIPTS_DIR
D = (defined?($test_args) && $test_args ? $test_args[0] : ARGV[0]) or abort 'usage: t_sprite_fast.rb SCRIPTS_DIR'
class Rect; attr_reader :v; def initialize; @v = [0, 0, 0, 0]; end; def set(*a); @v = a; end; end
class Bitmap; attr_reader :width, :height, :name; def initialize(n, w, h); @name = n; @width = w; @height = h; end; end
module Cache; def self.character(n); Bitmap.new(n, n.start_with?('$') ? 96 : 384, n.start_with?('$') ? 128 : 256); end
  def self.tileset(n); Bitmap.new(n, 512, 512); end; end
class Sprite   # mkxp-z's C Sprite, as plain state
  attr_accessor :x, :y, :z, :ox, :oy, :opacity, :blend_type, :bush_depth, :visible, :bitmap, :viewport
  attr_reader :src_rect, :c_updates
  def initialize(vp = nil); @viewport = vp; @src_rect = Rect.new; @c_updates = 0; @x = @y = @z = 0; end
  def update; @c_updates += 1; end
  def width; @src_rect.v[2]; end; def height; @src_rect.v[3]; end
  def dispose; end
  def state; [x, y, z, ox, oy, opacity, blend_type, bush_depth, visible, bitmap && bitmap.name, src_rect.v.dup, c_updates]; end
end
class Game_Map; attr_accessor :display_x_raw
  def initialize; @display_x = 10.0; @display_y = 3.0; @map = Struct.new(:width, :height).new(130, 33); end
  def width; 130; end; def height; 33; end; def loop_horizontal?; false; end; def loop_vertical?; false; end
  def screen_tile_x; 17; end; def screen_tile_y; 13; end
  def scroll(dx, dy); @display_x += dx; @display_y += dy; end
  def display_x; (@display_x * 32).floor.to_f / 32; end; def display_y; (@display_y * 32).floor.to_f / 32; end
end
class Spriteset_Map; def initialize(s); @s = s; end; def update; @s.each(&:update); end; end
load File.join(D, '033_Game_CharacterBase.rb')
src = File.read(File.join(D, '122_Event_Jitter_Fix.rb'), encoding: 'UTF-8').gsub("\r\n", "\n")
Game_Map.class_eval(src[/def adjust_x.*?\n  end\n.*?def adjust_y.*?\n  end\n/m])  # LISA's adjust_x/adjust_y
load File.join(D, '043_Sprite_Base.rb')
load File.join(D, '044_Sprite_Character.rb')
$game_map = Game_Map.new
Sprite_Character.prepend(Module.new { def update; super; end })   # like the profiler hook on the Vita
REF = Sprite_Character.instance_method(:update).super_method       # LISA's update (below the hook)
class Game_Vehicle_T < Game_CharacterBase; def transparent; @transparent || false; end; end   # as LISA's Game_Vehicle
eval(File.read(File.join(__dir__, 'blocks.rb')))
$native_fail = 0
if defined?(vita_sprite_fast_native_direct)
  d = vita_sprite_fast_native_direct
  $native_fail += 1 if d.include?('transparent')   # redefined in a character subclass: must stay a call
  $native_fail += 1 unless (%w[x y real_x real_y pattern direction opacity blend_type bush_depth priority_type animation_id balloon_id] - d).empty?
  $stderr.puts "native direct readers: #{d.join(',')}"
end
abort 'FAIL SPRITE_FAST not installed' unless Sprite.method_defined?(:vita_csprite_update)
def mkchar(rng)
  c = Game_CharacterBase.allocate
  { :@x => rng.rand(130), :@y => rng.rand(33), :@tile_id => 0, :@character_name => ['!Door', '$Big', 'People1'].sample(random: rng),
    :@character_index => rng.rand(8), :@pattern => 1, :@original_pattern => 1, :@direction => 2, :@opacity => 255,
    :@blend_type => 0, :@bush_depth => 0, :@transparent => false, :@priority_type => 1, :@jump_count => 0, :@jump_peak => 0,
    :@animation_id => 0, :@balloon_id => 0 }.each { |k, v| c.instance_variable_set(k, v) }
  c.instance_variable_set(:@real_x, c.x.to_f); c.instance_variable_set(:@real_y, c.y.to_f); c
end
def step(c, rng)
  r = rng.rand
  if r < 0.05 then c.instance_variable_set(:@real_x, c.real_x + 0.125)
  elsif r < 0.08 then c.instance_variable_set(:@pattern, rng.rand(3))
  elsif r < 0.10 then c.instance_variable_set(:@direction, [2, 4, 6, 8].sample(random: rng))
  elsif r < 0.11 then c.instance_variable_set(:@opacity, rng.rand(256))
  elsif r < 0.12 then c.transparent = !c.transparent
  elsif r < 0.125 then c.instance_variable_set(:@character_index, rng.rand(8))
  elsif r < 0.13 then c.instance_variable_set(:@jump_peak, 5); c.instance_variable_set(:@jump_count, 10)
  elsif r < 0.135 then c.instance_variable_set(:@bush_depth, rng.rand(2) * 8)
  elsif r < 0.14 then c.instance_variable_set(:@priority_type, rng.rand(3))
  end
  jc = c.instance_variable_get(:@jump_count); c.instance_variable_set(:@jump_count, jc - 1) if jc > 0
end
def visible_now(c)   # sprite area intersects the 544x416 screen (generous: 2 tiles)
  sx = $game_map.adjust_x(c.real_x) * 32; sy = $game_map.adjust_y(c.real_y) * 32
  sx > -128 && sx < 544 + 128 && sy > -128 && sy < 416 + 160
end
def run(offscreen, seed)
  $vita_offscreen_skip = offscreen
  rng = Random.new(seed); rng2 = Random.new(seed)
  $game_map = Game_Map.new; ca = Array.new(150) { mkchar(rng) }
  $game_map2 = nil; cb = Array.new(150) { mkchar(rng2) }
  sa = ca.map { |c| Sprite_Character.new(nil, c) }
  sb = []; cb.each { |c| s = Sprite_Character.allocate; Sprite.instance_method(:initialize).bind(s).call(nil)
    s.instance_variable_set(:@use_sprite, true); s.instance_variable_set(:@ani_duration, 0)
    s.instance_variable_set(:@character, c); s.instance_variable_set(:@balloon_duration, 0); REF.bind(s).call; sb << s }
  ssa = Spriteset_Map.new(sa); bad = 0; fast0 = $vita_sfast_n
  map_rng = Random.new(seed + 1)
  600.times do
    d = map_rng.rand < 0.3 ? [[-0.125, 0.0, 0.125].sample(random: map_rng), 0.0] : [0.0, 0.0]
    $game_map.scroll(*d)
    ca.each { |c| step(c, rng) }; cb.each { |c| step(c, rng2) }
    ssa.update
    sb.each { |s| REF.bind(s).call }
    sa.each_with_index do |s, i|
      r = sb[i]   # reference sprite = true position: compare when its rectangle touches the screen
      next if offscreen && !(r.x - r.ox < 544 && r.x - r.ox + r.src_rect.v[2] > 0 && r.y - r.oy < 416 && r.y - r.oy + r.src_rect.v[3] > 0)
      a = s.state; b = sb[i].state
      a[-1] = b[-1] = 0 if offscreen      # off-screen skipping legitimately skips Sprite#update calls
      bad += 1 if a != b
    end
  end
  [bad, $vita_sfast_n - fast0]
end
fails = 0
b, f = run(false, 11); puts "#{b.zero? ? 'PASS' : 'FAIL'}  exact (off-screen skip OFF): 150 sprites x 600 frames, #{b} mismatches, fast path #{f} times"; fails += 1 unless b.zero? && f > 1000
b, f = run(true, 12); puts "#{b.zero? ? 'PASS' : 'FAIL'}  with off-screen skip ON: on-screen sprites identical, #{b} mismatches, fast path #{f} times"; fails += 1 unless b.zero?
fails += $native_fail
puts "scroll path #{$vita_sscroll_n} times" if defined?($vita_sscroll_n) && $vita_sscroll_n
puts "fails=#{fails}"; exit fails
