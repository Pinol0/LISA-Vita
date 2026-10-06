# Runs the SPRITE_NATIVE install block (self-test included) on the host with LISA's character
# classes; vita_spc_eval/vita_spc_pos are provided by the C test program through a pipe.
require 'open3'
D = ARGV[0]
module RPG; class Map; attr_accessor :width, :height, :scroll_type; def initialize(w, h); @width = w; @height = h; @scroll_type = 0; end; end
  class Event; end; class MoveRoute; end; end
module Graphics; def self.width; 544; end; def self.height; 416; end; end
class Game_Map; def screen_tile_x; 17; end; def screen_tile_y; 13; end
  def width; @map.width; end; def height; @map.height; end
  def loop_horizontal?; @map.scroll_type == 2 || @map.scroll_type == 3; end
  def loop_vertical?; @map.scroll_type == 1 || @map.scroll_type == 3; end; end
%w[033 034 035 036 038 039].each { |n| load Dir[File.join(D, n + '_*.rb')].first }
src = File.read(File.join(D, '122_Event_Jitter_Fix.rb'), encoding: 'UTF-8').gsub("\r\n", "\n")
Game_Map.class_eval(src[/class Game_Map[^\n]*\n(.*)\nend/m, 1])
class Sprite; attr_accessor :x, :y, :z; end
class Sprite_Base < Sprite; end
class Sprite_Character < Sprite_Base; attr_accessor :character
  def update_position; move_animation(@character.screen_x - x, @character.screen_y - y); self.x = @character.screen_x; self.y = @character.screen_y; self.z = @character.screen_z; end
  def move_animation(dx, dy); end; end
class Spriteset_Map; def update; end; end
$io_in, $io_out, = Open3.popen2(File.join(__dir__, 'spc_math'))
def vita_spc_eval(c)
  $io_in.puts [c.instance_variable_get(:@real_x), c.instance_variable_get(:@real_y), $game_map.instance_variable_get(:@display_x), $game_map.instance_variable_get(:@display_y)].map { |v| '%a' % v.to_f }.join(' ') +
    " #{c.instance_variable_get(:@jump_count)} #{c.instance_variable_get(:@jump_peak)} #{c.instance_variable_get(:@priority_type)}"
  $io_in.flush; r = $io_out.gets.split; [Float(r[0]), Float(r[1]), r[2].to_i]
end
eval(File.read(File.join(__dir__, 'install_block.rb')))
log = File.read(File.join(ENV['TMPDIR'] || '/tmp', 'spc_qa.log')) rescue ''
puts log.lines.last(3)
ok = log.include?('SPRITE_NATIVE INSTALLED') && log.include?('Game_Event') && !log.include?('ERROR') && !log.include?('MISMATCH')
puts "#{ok ? 'PASS' : 'FAIL'}  install block on the host: self-test passed and installed"
exit(ok ? 0 : 1)
