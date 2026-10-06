# Host test for MKXP_VITA_EVENT_FAST: the fast path must leave every instance variable exactly as
# LISA's own update chain does. Loads the game's scripts 033/034/039 + Yanfly's near_the_screen?.
#   ruby t_event_fast.rb <dir with extracted scripts>   (see HANDOFF: Scripts.rvdata2 extraction)
D = ARGV[0] or abort 'usage: t_event_fast.rb SCRIPTS_DIR'
module Graphics; def self.width; 544; end; def self.height; 416; end; end
class Game_Map; attr_accessor :display_x, :display_y
  def width; 130; end; def height; 33; end
  def adjust_x(x); x - @display_x; end; def adjust_y(y); y - @display_y; end
  def loop_horizontal?; false; end; def loop_vertical?; false; end; end
$game_map = Game_Map.new; $game_map.display_x = 10.0; $game_map.display_y = 2.0
class Game_Interpreter; def running?; true; end; def update; @n = (@n || 0) + 1; end; end
load File.join(D, '033_Game_CharacterBase.rb')
load File.join(D, '034_Game_Character.rb')
load File.join(D, '039_Game_Event.rb')
src = File.read(Dir[File.join(D, '127_*.rb')].first, encoding: 'UTF-8'); i = src.index('def near_the_screen?'); yan = src[i...src.index("\n      end", i) + 10]
Game_Event.class_eval(yan)
class Game_Event; def start; @starting = true; end; def move_type_random; @moved = :random; end
  def move_type_toward_player; @moved = :toward; end; def move_type_custom; @moved = :custom; end
  def update_routine_move; @routine = (@routine || 0) + 1; end; def update_jump; @jumped = true; end; end
VITA_EVENT_FAST_LOG = '/dev/null'
# As on the Vita (d43): diagnostic modules are prepended in front of Game_Event#update before the
# fast path is installed (profiler, freeze probe); the owner check must look through them.
Game_Event.prepend(Module.new { def update; super; end })
Game_CharacterBase.prepend(Module.new { def update_stop; super; end })
reference = Game_Event.instance_method(:update).super_method   # LISA's chain, below the test's prepend
before = Game_Event.ancestors.size
eval(File.read(File.join(__dir__, 'gen_block.rb')))
abort 'FAIL fast path not installed' unless Game_Event.ancestors.size == before + 1
srand(7)
def rnd_event
  e = Game_Event.allocate
  x = rand(130); y = rand(33)
  moving = rand < 0.15
  e.instance_variable_set(:@x, x); e.instance_variable_set(:@y, y)
  e.instance_variable_set(:@real_x, moving ? x - 0.25 : (rand < 0.5 ? x : x.to_f))
  e.instance_variable_set(:@real_y, y.to_f)
  e.instance_variable_set(:@jump_count, rand < 0.1 ? 3 : 0)
  e.instance_variable_set(:@move_route_forcing, rand < 0.1)
  e.instance_variable_set(:@move_type, rand < 0.7 ? 0 : rand(1..3))
  e.instance_variable_set(:@move_frequency, rand(1..5)); e.instance_variable_set(:@move_speed, rand(1..6))
  e.instance_variable_set(:@trigger, [0, 0, 1, 2, 3, 4].sample)
  e.instance_variable_set(:@interpreter, e.instance_variable_get(:@trigger) == 4 ? Game_Interpreter.new : nil)
  e.instance_variable_set(:@step_anime, rand < 0.15); e.instance_variable_set(:@walk_anime, rand < 0.5)
  op = rand(3); e.instance_variable_set(:@original_pattern, op)
  e.instance_variable_set(:@pattern, rand < 0.8 ? op : rand(3))
  e.instance_variable_set(:@anime_count, [0, 0, 0, 0.0, 1, 7, 20].sample)
  e.instance_variable_set(:@stop_count, rand(0..400)); e.instance_variable_set(:@locked, rand < 0.1)
  e.instance_variable_set(:@list, []); e.instance_variable_set(:@event, Struct.new(:id).new(1))
  e
end
def state(e) e.instance_variables.sort.map { |v| [v, (x = e.instance_variable_get(v)).is_a?(Game_Interpreter) ? x.instance_variable_get(:@n) : x] } end
fails = 0; fast = 0; n = 20000
n.times do
  a = rnd_event; b = Marshal.load(Marshal.dump(a)) rescue (b = a.clone)
  b = a.dup
  b.instance_variable_set(:@interpreter, a.instance_variable_get(:@interpreter) && Game_Interpreter.new)
  before = $vita_ev_fast_n
  3.times { a.update }                 # with the fast path
  fast += 1 if $vita_ev_fast_n > before
  3.times { reference.bind(b).call }   # LISA's chain only
  fails += 1 if state(a) != state(b)
end
puts "#{fails == 0 ? 'PASS' : 'FAIL'}  #{n} random events x 3 frames, identical state (fast path taken by #{fast})"
exit(fails == 0 ? 0 : 1)
