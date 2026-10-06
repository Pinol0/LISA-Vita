# Host test for MKXP_VITA_MAP_REFRESH_FAST with LISA's own Game_Event (039), Game_Switches /
# Variables / SelfSwitches (014-016), Game_CommonEvent (032), Game_Map#refresh (031) and Galv's
# alias (137). Two identical worlds get the same random writes (relevant and irrelevant ids, same
# values rewritten, items, party, erase, new region events); after every refresh every event must be
# in the same state. ruby t_map_refresh.rb SCRIPTS_DIR
D = ARGV[0] or abort 'usage'
def scr(n) File.read(Dir[File.join(D, n + '_*.rb')].first, encoding: 'UTF-8').gsub("\r\n", "\n") end
module RPG
  class Event; attr_accessor :id, :x, :y, :pages; def initialize(id, pages); @id = id; @x = 0; @y = 0; @pages = pages; end
    class Page
      class Condition; attr_accessor :switch1_valid, :switch2_valid, :variable_valid, :self_switch_valid, :item_valid, :actor_valid,
        :switch1_id, :switch2_id, :variable_id, :variable_value, :self_switch_ch, :item_id, :actor_id; end
      class Graphic; attr_accessor :tile_id, :character_name, :character_index, :direction, :pattern
        def initialize; @tile_id = 0; @character_name = ''; @character_index = 0; @direction = 2; @pattern = 0; end; end
      attr_accessor :condition, :graphic, :move_type, :move_speed, :move_frequency, :move_route, :walk_anime, :step_anime,
        :direction_fix, :through, :priority_type, :trigger, :list
      def initialize; @condition = Condition.new; @graphic = Graphic.new; @move_type = 0; @move_speed = 3; @move_frequency = 3
        @move_route = Struct.new(:list, :repeat, :skippable, :wait).new([], true, false, false); @walk_anime = true; @step_anime = false
        @direction_fix = false; @through = false; @priority_type = 1; @trigger = 0; @list = []; end
    end
  end
  class CommonEvent; attr_accessor :id, :trigger, :switch_id, :list; def parallel?; @trigger == 2; end; end
end
class Game_Interpreter; end
class Game_Actor; end
class Game_Actors; def initialize; @data = []; end; def [](i); @data[i] ||= Game_Actor.new; end; end
class Game_Party; attr_accessor :actors_list, :items; def initialize; @actors_list = []; @items = {}; end
  def members; @actors_list.map { |i| $game_actors[i] }; end
  def has_item?(item) item && (@items[item] || 0) > 0 end; end
class Game_Map_Base; end
eval(scr('014')); eval(scr('015')); eval(scr('016'))
load Dir[File.join(D, '033_*.rb')].first; load Dir[File.join(D, '034_*.rb')].first
eval(scr('039'))
eval(scr('032'))
class Game_Map
  attr_accessor :need_refresh, :events, :common_events, :map_id
  def bush?(x, y) false end; def ladder?(x, y) false end; def terrain_tag(x, y) 0 end; def region_id(x, y) 0 end
  def valid?(x, y) true end; def passable?(*a) true end; def check_passage(*a) true end
  def setup_starting_event; end
  def width; 50; end; def height; 30; end; def loop_horizontal?; false; end; def loop_vertical?; false; end
  def round_x(x) x end; def round_y(y) y end; def events_xy(x, y) [] end; def events_xy_nt(x, y) [] end; def x_with_direction(x, d) x end; def y_with_direction(y, d) y end
end
g = scr('031'); Game_Map.class_eval(g[/(  def refresh\n.*?\n  end\n)/m, 1] + g[/(  def refresh_tile_events\n.*?\n  end\n)/m, 1])
r = scr('137'); Game_Map.class_eval(r[/(  alias galv_region_effects_gm_refresh refresh\n  def refresh\n.*?\n  end\n)/m, 1])
Game_Map.send(:attr_accessor, :r_events)
eval(File.read(File.join(__dir__, 'block.rb')))
abort 'FAIL: not installed' unless Game_Map.ancestors.first != Game_Map
def mkpage(rng)
  pg = RPG::Event::Page.new; c = pg.condition
  if rng.rand < 0.5 then c.switch1_valid = true; c.switch1_id = rng.rand(1..12) end
  if rng.rand < 0.2 then c.switch2_valid = true; c.switch2_id = rng.rand(1..12) end
  if rng.rand < 0.4 then c.variable_valid = true; c.variable_id = rng.rand(1..8); c.variable_value = rng.rand(0..5) end
  if rng.rand < 0.3 then c.self_switch_valid = true; c.self_switch_ch = %w[A B C D].sample(random: rng) end
  if rng.rand < 0.15 then c.item_valid = true; c.item_id = rng.rand(1..4) end
  if rng.rand < 0.15 then c.actor_valid = true; c.actor_id = rng.rand(1..4) end
  pg.graphic.character_name = ['', '!Door', 'People1', '$Big'].sample(random: rng); pg.graphic.tile_id = rng.rand < 0.1 ? 5 : 0
  pg.priority_type = rng.rand(3); pg.trigger = [0, 1, 2, 3, 4].sample(random: rng); pg.move_type = rng.rand(4)
  pg
end
def world(seed, fast)
  $vita_map_refresh_fast = fast
  rng = Random.new(seed)
  $game_switches = Game_Switches.new; $game_variables = Game_Variables.new; $game_self_switches = Game_SelfSwitches.new
  $game_party = Game_Party.new; $game_actors = Game_Actors.new; $data_items = [nil, :i1, :i2, :i3, :i4]
  $game_map = Game_Map.new; $game_map.map_id = 7; $game_map.events = {}; $game_map.r_events = {}
  $game_map.common_events = Array.new(3) { |i| ce = RPG::CommonEvent.new; ce.id = i + 1; ce.trigger = 2; ce.switch_id = rng.rand(1..12); ce.list = []; Game_CommonEvent.new(i + 1) rescue nil }.compact
  $data_common_events = [nil] + Array.new(3) { |i| ce = RPG::CommonEvent.new; ce.id = i + 1; ce.trigger = 2; ce.switch_id = rng.rand(1..12); ce.list = []; ce }
  $game_map.common_events = (1..3).map { |i| Game_CommonEvent.new(i) }
  40.times { |i| $game_map.events[i + 1] = Game_Event.new(7, RPG::Event.new(i + 1, Array.new(rng.rand(1..4)) { mkpage(rng) })) }
  $game_map.refresh
  states = []; rid = 100
  1500.times do
    k = rng.rand
    if k < 0.25 then $game_switches[rng.rand(1..20)] = [true, false].sample(random: rng)
    elsif k < 0.45 then $game_variables[rng.rand(1..12)] = rng.rand(0..6)
    elsif k < 0.55 then i = rng.rand(1..12); $game_variables[i] = $game_variables[i]   # same value rewritten
    elsif k < 0.62 then $game_self_switches[[7, rng.rand(1..40), %w[A B C D].sample(random: rng)]] = [true, false].sample(random: rng)
    elsif k < 0.67 then $game_party.items[[:i1, :i2, :i3, :i4].sample(random: rng)] = rng.rand(0..2)
    elsif k < 0.72 then a = rng.rand(1..4); $game_party.actors_list.include?(a) ? $game_party.actors_list.delete(a) : $game_party.actors_list << a
    elsif k < 0.74 then $game_map.events[rng.rand(1..40)].erase
    elsif k < 0.78 then rid += 1; $game_map.r_events[rid] = Game_Event.new(7, RPG::Event.new(rid, [mkpage(rng)])); $game_map.r_events.delete($game_map.r_events.keys.first) if $game_map.r_events.size > 5
    elsif k < 0.80 then e = $game_map.events[rng.rand(1..40)]; e.instance_variable_set(:@character_name, 'Changed') # move route set_graphic
    end
    $game_map.refresh if $game_map.need_refresh || rng.rand < 0.3
    states << ($game_map.events.values + $game_map.r_events.values).map { |e| e.instance_variables.sort.map { |v| x = e.instance_variable_get(v); [v, x.is_a?(RPG::Event::Page) ? e.instance_variable_get(:@event).pages.index(x) : (x.is_a?(Game_Interpreter) ? :interp : x.is_a?(Struct) ? :route : x.is_a?(RPG::Event) ? [:event, x.id] : x)] } } +
              [$game_map.common_events.map { |c| c.instance_variable_get(:@interpreter).nil? }, $game_map.instance_variable_get(:@tile_events).map(&:id)]
  end
  [states, $vita_mr_skip, $vita_mr_full]
end
$vita_mr_skip = $vita_mr_full = 0
ref, = world(3, false)
s0 = $vita_mr_skip; f0 = $vita_mr_full
opt, skips, fulls = world(3, true)
diff = ref.each_index.find { |i| ref[i] != opt[i] }
ok = diff.nil? && (skips - s0) > 100
puts "#{ok ? 'PASS' : 'FAIL'}  1500 steps, 40 events + Galv region events: identical states#{diff ? " (first difference at step #{diff})" : ''}; refresh skipped #{skips - s0}, full #{fulls - f0}"
exit(ok ? 0 : 1)
