# Host test for MKXP_VITA_MAP_PROF: fake Scene_Map / Game_Map / Spriteset_Map / Cache / Audio / Graphics.
# Checks: installs; a slow perform_transfer writes one MAP_LOAD line with the nested methods (wall/cpu/n);
# return values, arguments, blocks and private-ness are unchanged; fast roots write nothing; nested roots
# are counted once; Scene_Map#update >= 500 ms writes MAP_SLOW_UPDATE.
$cpu = 0.0
def vita_thread_cpu_ms; $cpu; end
class Object; private; def load_data(f); sleep 0.07; $cpu += 10; "data:#{f}"; end; end
class Bitmap; def initialize(w, h); @w = w; end; attr_reader :w; end
module Cache; def self.load_bitmap(f, n, hue = 0); Bitmap.new(n.size, 1); end; end
module Audio; def self.bgm_play(*a); sleep 0.05; :bgm; end; end
module Graphics; @fc = 0; def self.frame_count; @fc; end; def self.update; @fc += 1; end; end
class Game_Player; def perform_transfer; $game_map.setup(74); end; end
class Game_Map
  attr_reader :map_id
  def setup(id); @map_id = id; @data = load_data("Map#{id}"); setup_events; Audio.bgm_play('x', 100); end
  private def setup_events; [1, 2].map { |i| Game_Event.new(i) }; end
end
class Game_Event; def initialize(i); @i = i; end; end
class Spriteset_Map; def initialize; Cache.load_bitmap('Graphics/', 'abc'); end; def update; end; end
class Scene_Map
  def perform_transfer; $game_player.perform_transfer; @spriteset = Spriteset_Map.new; :done; end
  def start; :started; end
  def update; sleep($slow || 0); end
end
%w[Window_MapName Game_Interpreter].each { |c| Object.const_set(c, Class.new) }
$game_map = Game_Map.new; $game_player = Game_Player.new
File.delete('qa.log') if File.exist?('qa.log')
eval(File.read(ARGV[0]))
fails = 0
check = ->(c, m) { puts "#{c ? 'PASS' : 'FAIL'}  #{m}"; fails += 1 unless c }
sc = Scene_Map.new
check.(sc.perform_transfer == :done, 'return value kept')
log = File.read('qa.log').lines
check.(log.size == 1 && log[0].start_with?('MAP_LOAD root=perform_transfer map=->74 ') || log[0].start_with?('MAP_LOAD root=perform_transfer map=0->74 '), 'one MAP_LOAD line (root, map from->to)')
l = log[0]
check.(l =~ /load_data=(\d+)\/10\/1/ && $1.to_i >= 70, 'load_data wall >= 70 ms, cpu 10, 1 call')
check.(l.include?('Game_Event.new=') && l =~ /Game_Event\.new=\d+\/0\/2/, 'Game_Event.new counted twice')
check.(l =~ /map_setup=\d+\/10\/1/ && l.include?('setup_events=') && l.include?('Audio.bgm_play=') && l.include?('Cache.load_bitmap=') && l.include?('Bitmap.new=') && l.include?('Spriteset_Map.new='), 'nested methods listed')
check.(l =~ /wall_ms=(\d+) cpu_ms=10 gc=\d+/ && $1.to_i >= 120, 'root wall/cpu')
check.(Game_Map.private_method_defined?(:setup_events) && Object.private_method_defined?(:load_data), 'private methods stay private')
check.(Cache.load_bitmap('a', 'xyz').w == 3 && load_data('q') == 'data:q', 'arguments and results unchanged outside a root')
sc.start
check.(File.read('qa.log').lines.size == 1, 'fast root (< 100 ms) writes nothing')
$slow = 0.55; sc.update; $slow = 0
check.(File.read('qa.log').lines.last.start_with?('MAP_SLOW_UPDATE map=74 '), 'slow Scene_Map#update logged')
File.delete('qa.log')
puts "fails=#{fails}"; exit(fails == 0 ? 0 : 1)
