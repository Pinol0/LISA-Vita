# Host test for MKXP_VITA_INTERP_HYBRID with LISA's own Game_Interpreter (040_Game_Interpreter.rb).
#   ruby t_interp_hybrid.rb SCRIPTS_DIR DUMP_DIR MODE OUT   (MODE: fiber | hybrid)
# run.sh runs both modes on the same random event lists and compares the per-frame traces: commands
# executed (frame, index, code), running?, @index, switches, variables, script/message logs, with
# Marshal round trips of the interpreter (save/load) at random frames.
SCRIPTS, DUMP, MODE, OUT = ARGV
module RPG
  class EventCommand
    attr_accessor :code, :indent, :parameters
    def initialize(code = 0, indent = 0, parameters = []) = (@code, @indent, @parameters = code, indent, parameters)
  end
end
class Switches < Hash; def [](k) = fetch(k, false); end
class Variables < Hash; def [](k) = fetch(k, 0); end
class Msg
  attr_accessor :face_name, :face_index, :background, :position
  def initialize = (@busy = 0; @texts = [])
  def add(t) = (@texts << t; @busy = 3)
  def busy? = @busy > 0
  def tick = (@busy -= 1 if @busy > 0)
  def texts = @texts
end
class FakeSE; def initialize(n) = @n = n; def play = $log << [:se, $frame, @n]; end
class FakeMap; def map_id = 1; def need_refresh=(v); end; end
$game_switches = Switches.new; $game_variables = Variables.new; $game_message = Msg.new; $game_map = FakeMap.new
$log = []
src = File.read(File.join(SCRIPTS, '040_Game_Interpreter.rb')).gsub("\r\n", "\n")
eval(src, TOPLEVEL_BINDING, 'Game_Interpreter')
# as main.cpp right after the Game_Interpreter section
$vita_interp_snap = (Game_Interpreter.instance_methods(false) + Game_Interpreter.private_instance_methods(false)).to_h { |m| [m, Game_Interpreter.instance_method(m)] }
# trace hook in front of execute_command, as the Vita's profiler hooks are (prepended before install)
Game_Interpreter.prepend(Module.new { def execute_command; c = @list[@index]; $log << [$frame, @index, c.code]; raise 'HANG: >20000 commands in one frame' if ($cmds += 1) > 20000; super; end })
hy = File.read(File.join(DUMP, 'VITA_INTERP_HYBRID.rb')).sub(/^r = \(VitaInterpHybrid\.install.*\z/m, '')
eval(hy, TOPLEVEL_BINDING)
if MODE == 'hybrid'
  r = VitaInterpHybrid.install
  abort "install: #{r}" unless r == 'INSTALLED'
end

C = RPG::EventCommand
$labels = 0
def gen(rng, depth = 0, budget = [40])
  out = []
  n = rng.rand(2..7)
  n.times do
    break if budget[0] <= 0
    budget[0] -= 1
    k = rng.rand(100)
    if k < 18 then out << [121, [s = rng.rand(1..6), s, rng.rand(2)]]   # switches 1-6
    elsif k < 34 then out << [122, [v = rng.rand(1..5), v, rng.rand(3), rng.rand(2) == 0 ? 0 : 2, rng.rand(0..4), rng.rand(5..9)]]   # variables 1-5
    elsif k < 40 then out << [108, ["comment"]]
    elsif k < 48 then out << [230, [rng.rand(1..3)]]
    elsif k < 52 then out << [101, ["", 0, 0, 2]] << [401, ["line #{rng.rand(100)}"]]
    elsif k < 56 then out << [355, ["$log << [:script, $frame, $game_variables[1]]"]]
    elsif k < 58 then out << [122, [7, 7, 0, 4, "$frame % 5"]]
    elsif k < 60 then out << [115, []]
    elsif k < 78 && depth < 3
      cond = case rng.rand(4)
             when 0 then [0, rng.rand(1..6), rng.rand(2)]
             when 1, 2 then [1, rng.rand(1..6), 0, rng.rand(0..6), rng.rand(6)]
             else [12, "$frame.even?"]
             end
      out << [111, cond, :open]
      out.concat(gen(rng, depth + 1, budget))
      out << [0, [], :close]
      if rng.rand(2) == 0
        out << [411, [], :else]; out.concat(gen(rng, depth + 1, budget)); out << [0, [], :close]
      end
      out << [412, []]
    elsif k < 86 && depth < 2   # bounded loop: its own counter (variable 10 + depth), break at 3
      cv = 10 + depth
      out << [122, [cv, cv, 0, 0, 0]] << [112, [], :open]
      out << [122, [cv, cv, 1, 0, 1]]
      out.concat(gen(rng, depth + 1, budget))
      out << [111, [1, cv, 0, 3, 1], :open] << [113, []] << [0, [], :close] << [412, []]
      out << [0, [], :close] << [413, []]
    elsif k < 92 then lb = "L#{$labels += 1}"; out << [119, [lb]] << [122, [5, 5, 1, 0, 100]] << [118, [lb]]   # forward jump
    else out << [250, [FakeSE.new(rng.rand(9))]]
    end
  end
  out
end
def build(items)
  indent = 0; list = []
  # RPG Maker layout: a block's closing 0 is at the block's (inner) indent; 411/412/413 at the outer one.
  items.each do |code, params, mark|
    list << C.new(code, indent, params)
    indent += 1 if mark == :open || mark == :else
    indent -= 1 if mark == :close
  end
  list << C.new(0, 0, [])
end
trace = []
srand(1)
40.times do |case_no|
  rng = Random.new(1000 + case_no)
  list = build(gen(rng))
  $game_switches.clear; $game_variables.clear; $game_message = Msg.new; $log = []
  it = Game_Interpreter.new
  restart_gap = rng.rand(0..2)
  idle = 0
  saves = Array.new(3) { rng.rand(1..60) }
  (1..80).each do |f|
    $frame = f
    $cmds = 0
    $game_message.tick
    if !it.running?
      idle += 1
      (it.setup(list); idle = 0) if idle > restart_gap
    end
    it = Marshal.load(Marshal.dump(it)) if saves.include?(f)
    it.update
    trace << [case_no, f, it.running?, it.instance_variable_get(:@index), $game_switches.sort, $game_variables.sort, $log.dup, $game_message.texts.size]
    $log.clear
  end
end
File.binwrite(OUT, Marshal.dump(trace))
puts "#{MODE}: #{trace.size} frames traced"
