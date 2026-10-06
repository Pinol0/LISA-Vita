# Host test for MKXP_VITA_GALV_SHADOW_KEEP with LISA's Galv Character Effects (135), Game_CharacterBase
# (033), Sprite_Base (043) and Sprite_Character (044). A fake mkxp-z scene keeps the drawing order
# (z, then insertion order; a z change re-inserts after the equal-z elements). Each step: characters
# move/turn, every other step the parallel event's char_effects(1,true), sometimes a light source
# moves or a character's shadow flag changes; then the spriteset updates. The drawn effect sprites
# (order + every visible property) must be identical with and without the fix, step after step.
#   ruby t_galv_shadow.rb SCRIPTS_DIR DUMP_DIR
S, DUMP = ARGV
$stdout.sync = true
class Color; attr_reader :v; def initialize(*a) = @v = a; def ==(o) = o.is_a?(Color) && o.v == v; end
class Rect; attr_reader :v; def initialize(*a) = @v = a.empty? ? [0, 0, 0, 0] : a; def set(*a) = @v = a; end
class Bitmap; attr_reader :width, :height, :name; def initialize(n, w, h) = (@name, @width, @height = n, w, h); def disposed? = false; end
module Cache; def self.character(n) = Bitmap.new(n, n.start_with?('$') ? 96 : 384, n.start_with?('$') ? 128 : 256); def self.system(n) = Bitmap.new(n, 512, 512); end
$scene = []
class Sprite
  ATTRS = %i[x y ox oy opacity angle mirror blend_type visible bitmap color wave_amp wave_speed zoom_x zoom_y bush_depth bush_opacity tone viewport]
  attr_accessor(*ATTRS)
  attr_reader :z, :src_rect
  def initialize(vp = nil) = (@viewport = vp; @z = 0; @src_rect = Rect.new; @disposed = false; @x = @y = 0; @opacity = 255; @visible = true; @angle = 0; $scene << self; reinsert)   # mkxp: a new Sprite has angle 0
  def z=(v); return if v == @z; @z = v; reinsert; end
  def reinsert = ($scene.delete(self); i = $scene.index { |e| e.z > @z } || $scene.size; $scene.insert(i, self))
  def dispose = (@disposed = true; $scene.delete(self))
  def disposed? = @disposed
  def update; end
  def flash(*a); end
  def width = @src_rect.v[2]; def height = @src_rect.v[3]
  def state = [self.class.name, (character.instance_variable_get(:@tag) rescue nil), instance_variable_get(:@source), x, y, z, ox, oy, opacity, angle, mirror, blend_type, visible, bitmap && bitmap.name, src_rect.v, color && color.v, wave_amp]
end
class Viewport; end
module Graphics; def self.width = 544; def self.height = 416; end
class Game_Map
  attr_accessor :events, :display_x, :display_y
  def initialize = (@events = {}; @display_x = 0.0; @display_y = 0.0)
  def setup(id); end
  def width = 60; def height = 30; def loop_horizontal? = false; def loop_vertical? = false
  def adjust_x(x) = x - @display_x; def adjust_y(y) = y - @display_y
  def vehicles = []
  def region_id(x, y) = 0
  def method_missing(n, *a) = n.to_s.end_with?("?") ? false : 0   # bush?, ladder?, terrain_tag, ...
  def respond_to_missing?(*) = true
end
class Game_Followers; def each(&b) = [].each(&b); end
class Scene_Base; end
class Scene_Map < Scene_Base; end
class Spriteset_Map
  def initialize = (@viewport1 = Viewport.new; @character_sprites = [])
  def refresh_characters; end
  def dispose_characters; end
  def update; end
end
class Game_Interpreter; end
load File.join(S, '033_Game_CharacterBase.rb')
class Game_CharacterBase; def screen_z = 100; end
class Game_Character < Game_CharacterBase; end
class Game_Event < Game_Character
  def initialize(map_id, event) = (init_public_members; init_private_members; @id = event; @tag = "ev#{event}")
end
class Game_Vehicle < Game_Character; def initialize(type) = (init_public_members; init_private_members); end
class Game_Follower < Game_Character; def initialize(i, p) = (init_public_members; init_private_members); def refresh; end; end
class Game_Player < Game_Character
  def initialize = (init_public_members; init_private_members; @tag = 'player')
  def refresh; end
  def followers = Game_Followers.new
end
class Game_Battler; end
class Game_Actor < Game_Battler; def initialize(id); end; end
module RPG; class Actor; end; end
module SceneManager; class << self; attr_accessor :scene; end; def self.scene_is?(k) = scene.is_a?(k); end
load File.join(S, '043_Sprite_Base.rb') rescue (class Sprite_Base < Sprite; def animation? = !!@anim; def update = super; end)
load File.join(S, '044_Sprite_Character.rb')
src = File.read(Dir[File.join(S, '135_*.rb')].first).gsub("\r\n", "\n")
eval(src, TOPLEVEL_BINDING, 'galv')
fix = File.read(File.join(DUMP, 'VITA_GALV_SHADOW.rb')).sub(/^r = \(VitaGalvShadow\.install.*\z/m, '')
eval(fix, TOPLEVEL_BINDING)
abort 'galv not loaded' unless defined?(Sprite_Shadow)
class Interp < Game_Interpreter; end
def run(fix, seed)
  $scene = []
  rng = Random.new(seed)
  $game_map = Game_Map.new
  $game_player = Game_Player.new
  names = ['$art', '$martyspiderwater', 'People1']
  (1..10).each do |i|
    e = Game_Event.new(1, i); e.moveto(rng.rand(5..50), rng.rand(5..25))
    e.instance_variable_set(:@character_name, names.sample(random: rng)); e.instance_variable_set(:@character_index, rng.rand(8))
    e.shadow = rng.rand < 0.8
    $game_map.events[i] = e
  end
  $game_player.moveto(30, 20); $game_player.instance_variable_set(:@character_name, 'People1')
  sc = Scene_Map.new; sc.spriteset = Spriteset_Map.new; SceneManager.scene = sc
  ss = sc.spriteset
  Spriteset_Map.prepend(VitaGalvShadow::Hooks) if fix && !Spriteset_Map.ancestors.include?(VitaGalvShadow::Hooks)
  $vita_fix = fix
  it = Interp.new
  log = []
  $kept ||= 0
  1200.times do |t|
    before = ss.instance_variable_get(:@shadow_sprites)
    $game_map.events.each_value do |e|
      r = rng.rand
      if r < 0.05 then e.instance_variable_set(:@real_x, e.real_x + 0.125)
      elsif r < 0.07 then e.instance_variable_set(:@direction, [2, 4, 6, 8].sample(random: rng))
      elsif r < 0.09 then e.instance_variable_set(:@pattern, rng.rand(3))
      elsif r < 0.095 then e.transparent = !e.transparent
      end
    end
    $game_player.instance_variable_set(:@real_y, $game_player.real_y + 0.0625) if rng.rand < 0.1
    if t.even?   # the parallel event: char_effects(1,true) + shadow_source(...)
      it.char_effects(1, true)
      it.shadow_source(10, 26, 0); it.shadow_source(22, 26, 1); it.shadow_source(34, 26, 2)
    end
    it.shadow_source(rng.rand(5..50), 26, rng.rand(3)) if rng.rand < 0.02            # a light moves
    # events that update after the parallel one move in the same frame: a whole tile, across the
    # distance where the shadow fades out (opacity 80 - 10 * distance), before the spriteset update
    $game_map.events.each_value { |e| e.instance_variable_set(:@real_x, e.real_x + [-1, 1].sample(random: rng)) if rng.rand < 0.08 }
    if rng.rand < 0.01 then e = $game_map.events[rng.rand(1..10)]; e.shadow = !e.shadow end   # plan changes
    if rng.rand < 0.005 then sp = ss.instance_variable_get(:@shadow_sprites).sample(random: rng); sp&.dispose end
    $game_map.display_x += 0.03125 if rng.rand < 0.2
    if rng.rand < 0.02   # another z=0 sprite created between two refreshes (drawing order test)
      o = Sprite.new; o.instance_variable_set(:@other, t)
      def o.state = ['other', @other, z]
    end
    ss.update
    $kept += 1 if t.even? && fix && ss.instance_variable_get(:@shadow_sprites).equal?(before)
    $recreated = ($recreated || 0) + 1 if t.even? && fix && !ss.instance_variable_get(:@shadow_sprites).equal?(before)
    log << $scene.map(&:state)
  end
  log
end
abort 'not installed' unless Spriteset_Map.method_defined?(:refresh_effects)
fails = 0
[3, 7, 11, 19, 23, 42].each do |seed|
  # the original first (the fix module is not in the chain yet), then the fixed one
  a = run(false, seed)
  b = run(true, seed)
  same = a == b
  diff = a.each_index.find { |i| a[i] != b[i] }
  puts "#{same ? 'PASS' : 'FAIL'}  seed #{seed}: 1200 steps, drawing order + visible state of every sprite identical#{same ? '' : " (first difference at step #{diff})"}"
  fails += 1 unless same
end
puts "kept (no recreation) #{$kept} times, recreated (plan changed, sprite disposed) #{$recreated} times"
fails += 1 if $kept < 1000 || ($recreated || 0) < 10
exit fails
