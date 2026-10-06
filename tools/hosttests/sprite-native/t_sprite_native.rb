# Host test for MKXP_VITA_SPRITE_NATIVE: LISA's screen_x/screen_y/screen_z (033 + Event Jitter Fix
# 122) against the C arithmetic (vita-sprite-native.h), bit for bit, plus the integer the sprite
# setter keeps (Float#to_i == truncation toward zero, as mkxp-z's NUM2LONG).
#   gcc -O2 -ffp-contract=off spc_math.c -o spc_math -lm && ruby t_sprite_native.rb SCRIPTS_DIR
require 'open3'
D = ARGV[0] or abort 'usage'
module RPG; class Map; attr_accessor :width, :height, :scroll_type; def initialize(w, h); @width = w; @height = h; @scroll_type = 0; end; end; end
module Graphics; def self.width; 544; end; def self.height; 416; end; end
class Game_Map; def screen_tile_x; 17; end; def screen_tile_y; 13; end
  def width; @map.width; end; def height; @map.height; end
  def loop_horizontal?; @map.scroll_type == 2 || @map.scroll_type == 3; end
  def loop_vertical?; @map.scroll_type == 1 || @map.scroll_type == 3; end; end
load File.join(D, '033_Game_CharacterBase.rb')
src = File.read(File.join(D, '122_Event_Jitter_Fix.rb'), encoding: 'UTF-8').gsub("\r\n", "\n")
Game_Map.class_eval(src[/class Game_Map[^\n]*\n(.*)\nend/m, 1])
rng = Random.new(99); cases = []; inputs = []
200000.times do
  gm = Game_Map.allocate; gm.instance_variable_set(:@map, RPG::Map.new(130, 33))
  dxv = [rng.rand(120), rng.rand(1200) / 32.0, rng.rand(12000) / 1000.0, -rng.rand(40) / 8.0, rng.rand * 130].sample(random: rng)
  dyv = [rng.rand(30), rng.rand(300) / 32.0, rng.rand(3000) / 1000.0, rng.rand * 33].sample(random: rng)
  gm.instance_variable_set(:@display_x, dxv); gm.instance_variable_set(:@display_y, dyv); $game_map = gm
  c = Game_CharacterBase.allocate; x = rng.rand(130); y = rng.rand(33)
  c.instance_variable_set(:@real_x, [x, x.to_f, x - rng.rand(8) / 8.0, x + rng.rand(1000) / 997.0, rng.rand * 130].sample(random: rng))
  c.instance_variable_set(:@real_y, [y, y.to_f, y - rng.rand(8) / 8.0, rng.rand * 33].sample(random: rng))
  peak = [0, 5, 10, 13].sample(random: rng)
  c.instance_variable_set(:@jump_peak, peak); c.instance_variable_set(:@jump_count, peak > 0 ? rng.rand(peak * 2 + 1) : 0)
  c.instance_variable_set(:@priority_type, rng.rand(3))
  c.instance_variable_set(:@character_name, ['', '!Door', '$Big', 'People1'].sample(random: rng)); c.instance_variable_set(:@tile_id, [0, 5].sample(random: rng))
  cases << [c.screen_x, c.screen_y, c.screen_z]
  f = ->(v) { v.to_f == v ? [v.to_f].pack('G').unpack1('H*') && ('%a' % v.to_f) : ('%a' % v.to_f) }
  inputs << [c.instance_variable_get(:@real_x), c.instance_variable_get(:@real_y), dxv, dyv].map { |v| '%a' % v.to_f }.join(' ') +
            " #{c.instance_variable_get(:@jump_count)} #{peak} #{c.instance_variable_get(:@priority_type)}"
end
out, st = Open3.capture2(File.join(__dir__, 'spc_math'), stdin_data: inputs.join("\n") + "\n")
res = out.split("\n").map(&:split)
bad = 0
cases.each_with_index do |(rx, ry, rz), i|
  sx, sy, sz, ix, iy = res[i]
  ok = Float(sx) == rx.to_f && [rx.to_f].pack('G') == [Float(sx)].pack('G') && [ry.to_f].pack('G') == [Float(sy)].pack('G') &&
       sz.to_i == rz && ix.to_i == rx.to_i && iy.to_i == ry.to_i && rx.is_a?(Float) && ry.is_a?(Float)
  if !ok && bad < 5 then puts "MISMATCH case #{i}: ruby=#{[rx, ry, rz].inspect} c=#{res[i].inspect} in=#{inputs[i]}" end
  bad += 1 unless ok
end
puts "#{bad.zero? ? 'PASS' : 'FAIL'}  #{cases.size} cases bit-identical (screen_x, screen_y, screen_z and the setter integers), #{bad} mismatches"
exit(bad.zero? ? 0 : 1)
