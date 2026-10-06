# Host test for MKXP_VITA_COMBO_FAST with LISA's Window_ComboSkillList (131): a fake Bitmap records
# every drawing operation; after random input changes (usable skills, skill, battler, fonts, size,
# skin, names) the contents must be exactly what the original redraw-every-time produces.
#   ruby t_combo_fast.rb SCRIPTS_DIR
D = ARGV[0] or abort 'usage'
class Color; attr_accessor :red, :green, :blue, :alpha; def initialize(r=0,g=0,b=0,a=255); @red=r;@green=g;@blue=b;@alpha=a; end
  def dup; Color.new(@red,@green,@blue,@alpha); end; def to_a; [@red,@green,@blue,@alpha]; end; end
class Rect; attr_accessor :x,:y,:width,:height; def initialize(*a); @x,@y,@width,@height=a; end; def to_a; [@x,@y,@width,@height]; end; end
class Font; class << self; attr_accessor :default_name, :default_size, :default_bold, :default_italic, :default_shadow, :default_outline, :default_color, :default_out_color; end
  attr_accessor :size, :bold, :italic, :color; def initialize; @size=24; @color=Color.new; end; end
Font.default_name = ['VL Gothic']; Font.default_size = 24; Font.default_bold = false; Font.default_italic = false
Font.default_shadow = false; Font.default_outline = true; Font.default_color = Color.new(255,255,255); Font.default_out_color = Color.new(0,0,0,128)
class Bitmap; attr_reader :ops, :width, :height, :font
  def initialize(w, h); @width = w; @height = h; @ops = []; @font = Font.new; end
  def clear; @ops = [[:clear]]; end; def disposed?; false; end
  def fill_rect(*a); @ops << [:fill, *a.map { |x| x.respond_to?(:to_a) ? x.to_a : x }]; end
  def gradient_fill_rect(*a); @ops << [:grad, *a.map { |x| x.respond_to?(:to_a) ? x.to_a : x }]; end
  def draw_text(*a); @ops << [:text, *a, @font.size, @font.bold, @font.italic, @font.color.to_a]; end; end
class Window_Base
  attr_accessor :y, :z, :opacity, :visible, :contents, :windowskin
  def initialize(x, y, w, h); @contents = Bitmap.new(w, h); @windowskin = :skin; @line_height = 24; end
  def standard_padding; 12; end; def fitting_height(n); n * 24 + 24; end
  def line_height; @line_height; end; attr_writer :line_height
  def hide; @visible = false; end; def show; @visible = true; end; def activate; end
  def normal_color; Color.new(255, 255, 255); end
  def reset_font_settings; contents.font.size = Font.default_size; contents.font.bold = Font.default_bold; contents.font.italic = Font.default_italic; end
  def draw_text(*a); contents.draw_text(*a); end
  def draw_text_ex(x, y, t); contents.draw_text(x, y, 999, 24, t); end
end
module Graphics; def self.width; 544; end; def self.height; 416; end; end
module YEA; module INPUT_COMBO; COMBO_TITLE = "Combo"; TITLE_SIZE = 20
  %w[L R X Y Z].each { |b| const_set("#{b}_SKILL_ON", "#{b}+"); const_set("#{b}_SKILL_OFF", "#{b}-") }; end; end
Sk = Struct.new(:id, :name, :icon_index, :combo_skill)
src = File.read(Dir[File.join(D, '131_*.rb')].first, encoding: 'UTF-8').gsub("\r\n", "\n")
eval(src[/(class Window_ComboSkillList < Window_Base.*?\nend # Window_ComboSkillList|class Window_ComboSkillList < Window_Base.*?\n^end)/m])
eval(File.read(File.join(__dir__, 'block.rb')))
abort 'FAIL not installed' unless Window_ComboSkillList.ancestors.first != Window_ComboSkillList
class Battler; attr_accessor :ok; def initialize; @ok = {}; end; def usable?(s); @ok[s.id]; end; end
def run(fast, seed)
  $vita_combo_fast = fast; rng = Random.new(seed)
  $data_skills = [nil] + (1..12).map { |i| Sk.new(i, "Skill#{i}", i, {}) }
  base = (1..3).map { |i| s = Sk.new(100 + i, "Base#{i}", 0, {}); [:L, :R, :X, :Y, :Z].sample(3, random: rng).each { |b| s.combo_skill[b] = rng.rand(1..12) }; s }
  bats = [Battler.new, Battler.new]
  w = Window_ComboSkillList.new; w.reveal(bats[0], base[0]); out = []
  2000.times do
    k = rng.rand
    if k < 0.45 then w.refresh_check(w.instance_variable_get(:@battler))                     # hp=/mp=/tp= with nothing else changed
    elsif k < 0.6 then bats.sample(random: rng).ok[rng.rand(1..12)] = [true, false].sample(random: rng); w.refresh_check(w.instance_variable_get(:@battler))
    elsif k < 0.67 then w.reveal(bats.sample(random: rng), base.sample(random: rng))
    elsif k < 0.70 then Font.default_size = [20, 24].sample(random: rng); w.refresh
    elsif k < 0.72 then Font.default_bold = !Font.default_bold; w.refresh
    elsif k < 0.74 then w.contents = Bitmap.new(300, [216, 240].sample(random: rng)); w.refresh   # create_contents
    elsif k < 0.76 then $data_skills[rng.rand(1..12)].name = "Renamed#{rng.rand(5)}"; w.refresh
    elsif k < 0.78 then w.line_height = [24, 26].sample(random: rng); w.refresh
    elsif k < 0.80 then w.windowskin = [:skin, :skin2].sample(random: rng); w.refresh
    elsif k < 0.82 then Font.default_color.red = [255, 200].sample(random: rng); w.refresh
    end
    out << w.contents.ops.map { |o| o.dup }
  end
  [out, $vita_combo_skip]
end
$vita_combo_skip = 0
ref, = run(false, 7)
Font.default_size = 24; Font.default_bold = false; Font.default_color.red = 255
s0 = $vita_combo_skip
opt, skips = run(true, 7)
diff = ref.each_index.find { |i| ref[i] != opt[i] }
ok = diff.nil? && skips - s0 > 300
puts "#{ok ? 'PASS' : 'FAIL'}  2000 steps: contents identical to redraw-every-time#{diff ? " (first difference at step #{diff})" : ''}; redraws skipped #{skips - s0}"
exit(ok ? 0 : 1)
