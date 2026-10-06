# Host test for MKXP_VITA_MOG_CAP with the real Sprite_Base (043) and MOG Anti Animation Lag (140):
# random battles (sprites start/end/replace animations, sprites disposed, scene changes) with a fake
# Cache and a GPU-memory model. Checks:
#  1. the cap never disposes a bitmap used by a running animation (MOG itself does, when it empties
#     its list on a scene change or Game_Map#setup while a listed bitmap is in use again: those
#     disposals are recorded and not counted);
#  2. with plenty of memory the cap never fires: same Cache loads and the same garbage list as MOG alone;
#  3. with little memory, bitmaps kept in the garbage list stay bounded and every disposed one had
#     reference count 0 (= stock Sprite_Base would have disposed it already).
#   ruby t_mog_cap.rb SCRIPTS_DIR [mutate]
D = ARGV[0] or abort 'usage'
MUTATE = ARGV[1] == 'mutate'
$live_kb = 0
class Bitmap
  attr_reader :name, :kb
  def initialize(name, kb); @name = name; @kb = kb; @disposed = false; $live_kb += kb; end
  def dispose; return if @disposed; @disposed = true; $live_kb -= @kb; end
  def disposed?; @disposed; end
end
class Rect; def set(*a); end; attr_accessor :width, :height; end
class Sprite
  attr_accessor :x, :y, :ox, :oy, :z, :visible, :angle, :mirror, :zoom_x, :zoom_y, :opacity, :blend_type, :bitmap
  attr_reader :viewport, :src_rect
  def initialize(vp = nil); @viewport = vp; @x = @y = @ox = @oy = @z = 0; @opacity = 255; @src_rect = Rect.new; @disposed = false; end
  def dispose; @disposed = true; end; def disposed?; @disposed; end
  def width; 32; end; def height; 32; end; def update; end; def flash(*a); end
end
module Graphics; def self.width; 544; end; def self.height; 416; end; def self.frame_reset; end; end
module Cache
  @c = {}; @loads = []
  class << self; attr_reader :loads; end
  def self.reset; @c = {}; @loads = []; end
  def self.animation(n, h)
    k = [n, h]
    if @c[k].nil? || @c[k].disposed?
      @loads << k
      @c[k] = Bitmap.new(n, 4400)
    end
    @c[k]
  end
end
class Game_Temp; def initialize; end; end
class Game_System; def initialize; end; end
module SceneManager; def self.call(c); end; def self.goto(c); end; end
class Game_Map; def setup(id); end; end
class Scene_Base; def terminate; end; end
Frame = Struct.new(:cell_data)
class Cells; def initialize(n); @n = n; end; def [](i, j); i < @n ? (j == 0 ? i * 7 % 120 : 100) : nil; end; end
Anim = Struct.new(:animation1_name, :animation1_hue, :animation2_name, :animation2_hue, :frame_max, :frames, :timings, :position)
def src(n); File.read(Dir[File.join(D, "#{n}_*.rb")].first, encoding: 'UTF-8').gsub("\r\n", "\n"); end
eval(src('043')); eval(src('140'))
$gpu_total_kb = 0
def vita_gpu_free_kb; $gpu_total_kb - $live_kb; end
blk = File.read(File.join(__dir__, 'block.rb'))
blk = blk.sub('next false if (refs[b] || 0) > 0', 'next false if false') if MUTATE
eval(blk)
abort 'FAIL not installed' unless Sprite_Base.ancestors.first != Sprite_Base
$by_mog = {}
class << SceneManager
  alias t_dag dispose_animation_garbage
  def dispose_animation_garbage
    ($game_temp.animation_garbage || []).each { |b| $by_mog[b] = true unless b.disposed? }
    t_dag
  end
end

ANIMS = (0...40).map do |i|
  fm = 3 + i % 6
  Anim.new("A#{i % 25}", 0, i % 3 == 0 ? '' : "B#{i % 9}", i % 2 * 30, fm, (0...fm).map { Frame.new(Cells.new(8)) }, [], i % 4)
end
def run(seed, total_kb, cap)
  $by_mog = {}; $vita_mog_trimmed = 0; $vita_mog_cap = cap; $gpu_total_kb = total_kb; $live_kb = 0; Cache.reset
  Sprite_Base.class_variable_set(:@@_reference_count, {})
  $game_temp = Game_Temp.new; $game_system = Game_System.new
  rng = Random.new(seed); sprites = (0...10).map { Sprite_Base.new }; peak = 0; trace = []
  4000.times do |t|
    k = rng.rand
    s = sprites.sample(random: rng)
    if k < 0.06 then s.start_animation(ANIMS.sample(random: rng), rng.rand < 0.5)
    elsif k < 0.07 then s.dispose; sprites[sprites.index(s)] = Sprite_Base.new
    elsif k < 0.072 then SceneManager.goto(Object)
    elsif k < 0.073 then $game_map_setup = Game_Map.new.setup(1)
    end
    sprites.each(&:update)
    sprites.each do |sp|
      next unless sp.animation?
      (sp.instance_variable_get(:@ani_sprites) || []).each do |c|
        abort "FAIL seed=#{seed} t=#{t}: running animation shows disposed bitmap #{c.bitmap.name}" if c.visible && c.bitmap && c.bitmap.disposed? && !$by_mog[c.bitmap]
      end
      [:@ani_bitmap1, :@ani_bitmap2].each do |iv|
        b = sp.instance_variable_get(iv)
        abort "FAIL seed=#{seed} t=#{t} cap=#{$vita_mog_cap} total=#{$gpu_total_kb}: animation bitmap disposed while running" if b && b.disposed? && !$by_mog[b]
      end
    end
    g = $game_temp.animation_garbage
    gkb = g ? g.uniq.reject(&:disposed?).sum(&:kb) : 0
    peak = gkb if gkb > peak
    trace << (g ? g.map(&:object_id) : nil) if t % 50 == 0
  end
  [Cache.loads.dup, trace, peak, $vita_mog_trimmed]
end
fails = 0
30.times do |seed|
  # 2. plenty of memory: identical to MOG alone (object ids differ per run, compare Cache loads + list sizes)
  a = run(seed, 10_000_000, false); b = run(seed, 10_000_000, true)
  if a[0] != b[0] || a[1].map { |x| x && x.size } != b[1].map { |x| x && x.size } || b[3] != 0
    puts "FAIL seed=#{seed}: cap changed behaviour with plenty of memory"; fails += 1
  end
  # 3. tight memory: kept garbage bounded
  $vita_mog_trimmed = 0
  c = run(seed, 24 * 1024 + 4400 * 12, true)
  if c[2] > 4400 * 14
    puts "FAIL seed=#{seed}: garbage not capped (peak #{c[2]} KB)"; fails += 1
  end
  m = run(seed, 10_000_000, false)
  print "seed=#{seed} mog_peak_kb=#{m[2]} cap_peak_kb=#{c[2]} trimmed=#{c[3]} reloads=#{c[0].size - m[0].size}\n" if seed < 3
end
abort "FAIL #{fails}" if fails > 0
puts 'PASS'
