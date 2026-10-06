# Host test for MKXP_VITA_OFFSCREEN_SPRITES (generated from src/main.cpp): ruby t_offscreen.rb
class Game_Map; attr_accessor :display_x, :display_y, :loop_h
  def initialize; @display_x = 10.0; @display_y = 5.0; @loop_h = false; end
  def loop_horizontal?; @loop_h; end; def loop_vertical?; false; end
  def screen_tile_x; 17; end; def screen_tile_y; 13; end; end
class Char; attr_accessor :x, :y, :animation_id, :balloon_id; def initialize(x,y); @x=x; @y=y; @animation_id=0; @balloon_id=0; end; end
class Sprite_Base; def animation?; @anim; end; attr_accessor :anim; end
class Sprite_Character < Sprite_Base; attr_reader :full
  def initialize(c); @character = c; @full = 0; @cw = 32; @ch = 32; end
  def update; @full += 1; end; end
class Sprite_Reflect < Sprite_Character; end
class Spriteset_Map; def initialize(s); @s = s; end; def update; @s.each(&:update); end; end
$game_map = Game_Map.new

  # PERF FIX (MKXP_VITA_OFFSCREEN_SPRITES): character sprites far off screen are not updated.
  # d39/d40: Sprite_Character#update ~0.16 ms per sprite per frame (Float math allocates on 32-bit
  # Ruby), 100-180 sprites on the busy maps. Events (game logic) are untouched: only the sprite of a
  # character >= 3 tiles (+ its cell size) outside the screen skips its update, and only when no
  # animation/balloon is running or requested (an event waiting for one must not stall), on maps
  # that do not loop. The last full update happens once the character is already out, so the
  # sprite is left off screen. Toggle at runtime: L+R+START ($vita_offscreen_skip, qa.log).
  $vita_offscreen_skip = true
  $vita_off_ok = false
  $vita_skip_n = 0
  Spriteset_Map.prepend(Module.new do
    def update
      m = $game_map
      if $vita_offscreen_skip && !m.loop_horizontal? && !m.loop_vertical?
        $vita_off_x0 = m.display_x.to_i
        $vita_off_y0 = m.display_y.to_i
        $vita_off_w = m.screen_tile_x
        $vita_off_h = m.screen_tile_y
        $vita_off_ok = true
      end
      super
    ensure
      $vita_off_ok = false
    end
  end)
  Sprite_Character.prepend(Module.new do
    def vita_offscreen?
      c = @character
      return false unless c && instance_of?(Sprite_Character)
      return false if @balloon_sprite || animation? || c.animation_id > 0 || c.balloon_id > 0
      mx = (@cw || 32) / 64 + 3
      my = (@ch || 32) / 32 + 3
      tx = c.x - $vita_off_x0
      ty = c.y - $vita_off_y0
      tx < -mx || tx > $vita_off_w + mx || ty < -my || ty > $vita_off_h + my
    end
    def update
      if $vita_off_ok && vita_offscreen?
        if @vita_off_ready
          $vita_skip_n += 1
          return
        end
        super
        @vita_off_ready = true
      else
        @vita_off_ready = false
        super
      end
    end
  end)
$fails = 0
def check(c, m) puts((c ? "PASS  " : "FAIL  ") + m); $fails += 1 unless c end
on = Char.new(15, 10); off = Char.new(60, 10); s_on = Sprite_Character.new(on); s_off = Sprite_Character.new(off)
refl = Sprite_Reflect.new(Char.new(60, 10))
ss = Spriteset_Map.new([s_on, s_off, refl])
3.times { ss.update }
check(s_on.full == 3, "on-screen sprite updated every frame")
check(s_off.full == 1, "off-screen sprite: one full update, then skipped")
check(refl.full == 3, "subclasses (Galv sprites) never skipped")
check($vita_skip_n == 2, "skip counter")
s_off.update; check(s_off.full == 2, "outside Spriteset_Map#update: always a full update")
off.x = 30; ss.update; check(s_off.full == 3, "within the 3-tile margin: updated")
off.x = 60; ss.update; ss.update; check(s_off.full == 4, "left again: one full update, then skipped")
off.animation_id = 5; ss.update; check(s_off.full == 5, "animation requested: updated")
off.animation_id = 0; s_off.anim = true; ss.update; check(s_off.full == 6, "animation running: updated")
s_off.anim = nil; off.balloon_id = 1; ss.update; check(s_off.full == 7, "balloon requested: updated")
off.balloon_id = 0; ss.update; ss.update; check(s_off.full == 8, "back to skipping")
$game_map.loop_h = true; ss.update; check(s_off.full == 9, "looping map: never skipped")
$game_map.loop_h = false; $vita_offscreen_skip = false; ss.update; check(s_off.full == 10, "toggle off: updated")
$vita_offscreen_skip = true; $game_map.display_x = 50.0; ss.update; check(s_off.full == 11 && s_on.full > 0, "map scrolled: character now on screen -> updated")
big = Char.new(10 - 6, 10); sb = Sprite_Character.new(big); sb.instance_variable_set(:@cw, 384); ss2 = Spriteset_Map.new([sb]); $game_map.display_x = 10.0
2.times { ss2.update }; check(sb.full == 2, "wide sprite (384 px) 6 tiles left: margin covers it")
puts "fails=#{$fails}"; exit($fails)
