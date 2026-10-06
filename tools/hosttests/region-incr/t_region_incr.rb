# Host test for MKXP_VITA_REGION_INCR against Galv's Region Effects (script 137).
# A fake mkxp-z scene keeps the drawing order (sorted by z; equal z: insertion order, a z change
# re-inserts after the equal-z elements). The incremental refresh must give the same drawing order
# of the same characters as the original dispose-all + create-all, step after step.
#   python3 gen_block.py && ruby t_region_incr.rb SCRIPTS_DIR
D = ARGV[0] or abort 'usage: t_region_incr.rb SCRIPTS_DIR'
$scene = []
class Sprite
  attr_reader :z
  def initialize(vp = nil); @z = 0; @disposed = false; $scene << self; reinsert; end
  def z=(v); return if v == @z; @z = v; reinsert; end
  def reinsert; $scene.delete(self); i = $scene.index { |e| e.z > @z } || $scene.size; $scene.insert(i, self); end
  def dispose; @disposed = true; $scene.delete(self); end
  def disposed?; @disposed; end
end
class Sprite_Base < Sprite; def animation?; !!@anim; end; attr_accessor :anim; end
class Sprite_Character < Sprite_Base
  attr_accessor :character
  def initialize(vp, c); super(vp); @character = c; update; end
  def update; self.z = @character.z; end
end
class Ev; attr_accessor :z, :name; def initialize(n, z); @name = n; @z = z; end; end
class Game_Map; attr_accessor :effectlist, :r_events; end
module Region_Effects; MAX_EFFECTS = 20; end
class Spriteset_Map
  def initialize; @viewport1 = nil; @region_sprites = []; end
  attr_reader :region_sprites
end
src = File.read(Dir[File.join(D, '137_*.rb')].first, encoding: 'UTF-8').gsub("\r\n", "\n")
body = src[/class Spriteset_Map.*?\nend/m]
Spriteset_Map.class_eval(body.sub(/\Aclass Spriteset_Map[^\n]*\n/, '').sub(/end\z/, '').gsub(/^\s*alias .*$/, ''))  # Galv's methods
eval(File.read(File.join(__dir__, 'block.rb')))
abort 'FAIL not installed' unless Spriteset_Map.ancestors.first != Spriteset_Map
def drawing(ss) $scene.map { |s| [s.character.name, s.z] } end
fails = 0
[[false], [true]].each do |(with_others)|
  results = {}
  [false, true].each do |incr|
    $vita_region_incr = incr; $scene = []
    rng = Random.new(5); $game_map = Game_Map.new; $game_map.effectlist = []; $game_map.r_events = {}
    others = with_others ? Array.new(5) { |i| Sprite_Character.new(nil, Ev.new("char#{i}", [0, 100, 200].sample(random: rng))) } : []
    ss = Spriteset_Map.new; eid = 0; log = []
    3000.times do |t|
      eid += 1
      $game_map.effectlist.push(eid); $game_map.effectlist.shift if $game_map.effectlist.count > Region_Effects::MAX_EFFECTS
      $game_map.r_events[eid] = Ev.new("dust#{eid}", [0, 100].sample(random: rng))
      if rng.rand < 0.05   # something else created in between (e.g. refresh_characters)
        others << Sprite_Character.new(nil, Ev.new("late#{t}", 100))
      end
      ss.region_sprites.sample(random: rng)&.anim = true if rng.rand < 0.05
      ss.refresh_region_effects
      ss.region_sprites.each { |s| s.anim = nil if rng.rand < 0.5 }
      eid = 0 if eid >= Region_Effects::MAX_EFFECTS
      log << drawing(ss)
    end
    results[incr] = log
  end
  same = results[false] == results[true]
  diff = results[false].each_index.find { |i| results[false][i] != results[true][i] }
  puts "#{same ? 'PASS' : 'FAIL'}  3000 steps#{with_others ? ' with other sprites created in between' : ''}: identical drawing order#{same ? '' : " (first difference at step #{diff})"}"
  fails += 1 unless same
end
puts "fails=#{fails}"; exit fails
