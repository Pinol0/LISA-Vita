$t=0
def vita_prof_now; $t+=1; end
def vita_prof_enter(i); $t+=1; end
$prof=Hash.new(0); $ev=Hash.new(0)
def vita_prof(i,t0); $prof[i]+=1; end
def vita_prof_ev(i,t0); $ev[i]+=1; end
def vita_prof_map(m,n); $map=[m,n]; end
module H; def self.cls(n); Object.const_defined?(n) ? Object.const_get(n) : nil; end; end
module Input; def self.update; :in; end; end
module GC; def self.measure_total_time=(x); end; end
class Game_Map; def initialize; @events={1=>1,2=>2}; end; def setup(id); :ok; end; def update(m=false); update_events; update_interpreter; :u; end; private; def update_events; end; def update_interpreter; end; end
class Game_Event; def initialize(i); @id=i; end; def update; :ev; end; end
class Game_Player; def update; end; end
class Sprite_Character; def update; :sc; end; end
class Sprite_Shadow < Sprite_Character; def update; super; :sh; end; end
  # Light profiler (MKXP_VITA_RUBY_PROF): Ruby time per script phase and per map event.
  GC.measure_total_time = true
  [[:Input, :update, 0, true], [:Game_Map, :update, 1], [:Game_Map, :update_events, 2],
   [:Game_Map, :update_interpreter, 3], [:Game_Player, :update, 4], [:Spriteset_Map, :update, 5],
   [:Scene_Base, :update_all_windows, 6], [:Spriteset_Battle, :update, 7],
   [:DataManager, :load_header, 13, true], [:Window_Base, :initialize, 14], [:Scene_Base, :start, 15],
   [:Window_SaveFile, :draw_party_characters, 16], [:Window_SaveFile, :draw_playtime, 17],
   [:Window_Base, :draw_character, 18], [:Cache, :character, 19, true], [:Bitmap, :clear, 20]].each do |cname, meth, idx, sing|
    c = H.cls(cname)
    next unless c
    owner = sing ? c.singleton_class : c
    next unless owner.method_defined?(meth) || owner.private_method_defined?(meth)
    priv = owner.private_method_defined?(meth)
    owner.prepend(Module.new do
      define_method(meth) do |*a, &b|
        t0 = vita_prof_enter(idx)
        begin
          super(*a, &b)
        ensure
          vita_prof(idx, t0)
        end
      end
      ruby2_keywords(meth)
      private meth if priv
    end)
  end
  # Character sprites by class (Galv's Character Effects adds reflect/mirror/shadow/icon sprites):
  # outermost update only, so a subclass calling super is not counted twice.
  [[:Sprite_Character, 8], [:Sprite_Reflect, 9], [:Sprite_Mirror, 10], [:Sprite_Shadow, 11], [:Sprite_Icon, 12]].each do |cname, idx|
    c = H.cls(cname)
    next unless c && c.method_defined?(:update)
    # def (not define_method |*a|): no Array per call; vita_prof_now is a Fixnum (no Bignum).
    m = Module.new
    m.module_eval("def update; return super if $vita_prof_spr; $vita_prof_spr = true; t0 = vita_prof_enter(#{idx}); begin; super; ensure; $vita_prof_spr = false; vita_prof(#{idx}, t0); end; end")
    c.prepend(m)
  end
  Game_Event.prepend(Module.new { def update; t0 = vita_prof_enter(21); super; ensure; vita_prof_ev(@id, t0); end })
  Game_Map.prepend(Module.new { def setup(map_id); r = super; vita_prof_map(map_id, @events.size); r; end })
raise unless Sprite_Character.new.update==:sc
raise unless Sprite_Shadow.new.update==:sh
raise "counts #{$prof}" unless $prof[8]==1 && $prof[11]==1
m=Game_Map.new; raise unless m.setup(5)==:ok; raise unless m.update(true)==:u
raise unless Game_Event.new(3).update==:ev && $ev[3]==1
GC.start; a0=GC.stat(:total_allocated_objects); s=Sprite_Character.new; 1000.times{ s.update }; a1=GC.stat(:total_allocated_objects)
puts "allocs per sprite update: #{(a1-a0)/1000.0}"
p $prof, $map
