# Host test for patches/display_option.rb with LISA's own options menu (142_Options_Menu.rb, Yanfly
# System Options): "Screen: 1:1 | Original | Stretch | Wide" after "Battle Animations", left / right set
# Graphics.vita_display_mode; Wide resizes the RGSS screen to 736x416 (and back) and centres the camera;
# help text per mode; Wide applied when the patch loads; the other entries unchanged; not installed
# without the port's Graphics.vita_display_mode=.
#   ruby t_display_option.rb SCRIPTS_DIR [no_binding|boot_wide|legacy_stretch]   (PATCH=file: another version)
SCRIPTS = ARGV[0] or abort 'usage'
MODE = ARGV[1].to_s
NO_BINDING = MODE == 'no_binding'
VITA_PATCH_DIR = 'app0:patches/'
VITA_PATCH_LOG = File.join(Dir.pwd, "qa_display_option_#{$$}.log")

module Graphics
  @mode = { 'boot_wide' => 3, 'legacy_stretch' => 1 }.fetch(MODE, 0)   # as display.cfg left it
  @w = 544
  @h = 416
  def self.width; @w; end
  def self.height; @h; end
  def self.resize_screen(w, h); $resizes = ($resizes || []) << [w, h]; @w, @h = w, h; true; end
  unless NO_BINDING
    def self.vita_display_mode; @mode; end
    def self.vita_display_mode=(v); $sets = ($sets || []) << v; @mode = v; end
  end
end
class Player; attr_reader :x, :y; def initialize; @x, @y = 10, 6; end; def center(x, y); $centered = ($centered || []) << [x, y, Graphics.width]; end; end
module Sound; def self.play_cursor; $cursor_sounds = ($cursor_sounds || 0) + 1; end; end
module Input; def self.press?(_); false; end; end
module SceneManager; def self.call(_); end; def self.goto(_); end; def self.exit; end; end
module RPG; class AudioFile; def play(*); end; end; end
Tone = Struct.new(:red, :green, :blue, :gray)
Color = Struct.new(:red, :green, :blue, :alpha)
class Game_System; def initialize; end; def window_tone; @wt ||= Tone.new(0, 0, 0, 0); end; end
class Game_Character; end
class Game_Player < Game_Character; def dash?; false; end; end
class Scene_Base; end
class Scene_Battle < Scene_Base; def show_fast?; false; end; def show_normal_animation(*); end; end
class Scene_MenuBase < Scene_Base; end
class Scene_Menu < Scene_MenuBase; end
Rect = Struct.new(:x, :y, :width, :height)
class Contents
  attr_reader :width, :ops
  def initialize(w); @width = w; @ops = []; end
  def clear_rect(r); @ops << [:clear, r.y]; end
end
class Window_Base
  attr_reader :contents, :width, :height
  def initialize(x, y, w, h); @width, @height = w, h; @contents = Contents.new(w - 24); @color = [:normal, true]; end
  def line_height; 24; end
  def normal_color; :normal; end
  def text_color(n); n; end
  def change_color(c, enabled = true); @color = [c, enabled]; end
  def reset_font_settings; @color = [:normal, true]; end
  def draw_text(*a); r = a[0].is_a?(Rect) ? a[0] : nil; y = r ? r.y : a[1]; @contents.ops << [:text, y, (r ? a[1] : a[4]).to_s, @color[1]]; end
  def draw_gauge(*); end
end
class Window_Message < Window_Base; def clear_flags; end; end
class Window_Help < Window_Base; attr_reader :text; def set_text(t); @text = t; end; end
class Window_Selectable < Window_Base
  attr_reader :index
  def item_rect(i); Rect.new(0, i * 24, @contents.width, 24); end
  def item_rect_for_text(i); item_rect(i); end
  def select(i); @index = i; update_help; end
  def cursor_right(wrap = false); end
  def cursor_left(wrap = false); end
end
class Window_Command < Window_Selectable
  def initialize(x, y); @list = []; make_command_list; super(x, y, window_width, window_height); @index = 0; end
  def add_command(name, symbol, enabled = true, ext = nil); @list << { name: name, symbol: symbol, enabled: enabled, ext: ext }; end
  def command_name(i); @list[i][:name]; end
  def current_symbol; @list[@index] && @list[@index][:symbol]; end
  def current_ext; @list[@index] && @list[@index][:ext]; end
  def refresh; @list.size.times { |i| draw_item(i) }; end
  def list; @list; end
end
$game_system = Game_System.new

eval(File.read(File.join(SCRIPTS, '142_Options_Menu.rb')), TOPLEVEL_BINDING, 'eval')
patch = ENV['PATCH'] || File.join(__dir__, '../../../patches/display_option.rb')
eval(File.read(patch), TOPLEVEL_BINDING, 'patches/display_option.rb')
$game_system = Game_System.new

fails = 0
check = ->(c, m) { puts "#{c ? 'PASS' : 'FAIL'}  #{m}"; fails += 1 unless c }
log = File.read(VITA_PATCH_LOG); File.delete(VITA_PATCH_LOG)
begin
  boot_resizes = ($resizes || []).dup
  help = Window_Help.new(0, 0, 544, 72)
  w = Window_SystemOptions.new(help)
  syms = w.list.map { |c| c[:symbol] }
  row = ->(i) { w.contents.ops.select { |o| o[0] == :text && o[1] == i * 24 }.last(5).map { |o| [o[2], o[3]] } }
  lit = ->(i) { (r = row.(i)).size == 5 && r[0] == ['Screen', true] ? r[1..].select { |t| t[1] }.map(&:first) : r.inspect }
  case MODE
  when 'no_binding'
    check.(log.include?('DISPLAY_OPTION not installed') && !syms.include?(:vita_screen), 'without Graphics.vita_display_mode=: not installed, menu unchanged')
  when 'boot_wide'
    check.(boot_resizes == [[736, 416]] && Graphics.width == 736 && log.include?('installed mode=3 screen=736x416'), "saved Wide: 736x416 before the game starts (#{boot_resizes.inspect})")
    check.(lit.(syms.index(:vita_screen)) == ['Wide'], 'Wide lit')
  when 'legacy_stretch'
    check.(lit.(syms.index(:vita_screen)) == ['Stretch'] && boot_resizes.empty?, 'saved Stretch: Stretch lit, no resize')
  else
    $game_map = true
    $game_player = Player.new
    check.(log.include?('DISPLAY_OPTION installed mode=0 screen=544x416') && boot_resizes.empty?, 'installed; Original: no resize at load')
    i = syms.index(:vita_screen)
    check.(i && syms[i - 1] == :animations && syms.count(:vita_screen) == 1, "Screen right after Battle Animations (#{syms.inspect})")
    check.(syms - [:vita_screen] == YEA::SYSTEM::COMMANDS, "the script's own entries unchanged and in order")
    check.(row.(i).map(&:first) == ['Screen', '1:1', 'Original', 'Stretch', 'Wide'] && lit.(i) == ['Original'], "four options, Original lit (#{row.(i).inspect})")
    w.select(i)
    check.(help.text == "Original: the game's proportions, scaled to\nthe screen height (black side bars).", 'help: Original')
    $cursor_sounds = 0
    w.cursor_right
    check.(Graphics.vita_display_mode == 1 && $resizes.nil? && lit.(i) == ['Stretch'] && help.text.start_with?('Stretch: ') && $cursor_sounds == 1, 'right: Stretch, no resize, help, sound')
    w.cursor_right
    check.(Graphics.vita_display_mode == 3 && $resizes == [[736, 416]] && $centered == [[10, 6, 736]], "right: Wide, screen 736x416, camera centred (#{$resizes.inspect} #{$centered.inspect})")
    check.(help.text == "Wide: real widescreen, more map is shown.\nSome scenes and events may look wrong." && lit.(i) == ['Wide'], 'Wide: disclaimer in the help, redrawn')
    w.cursor_right
    check.($sets == [1, 3] && $cursor_sounds == 2, 'right again: nothing')
    w.cursor_left
    check.(Graphics.vita_display_mode == 1 && $resizes.last == [544, 416] && Graphics.width == 544 && $centered.size == 2, 'left: Stretch, screen back to 544x416, camera centred')
    w.cursor_left
    check.(Graphics.vita_display_mode == 0 && $resizes.size == 2, 'left: Original, no resize')
    w.cursor_left
    check.(Graphics.vita_display_mode == 2 && lit.(i) == ['1:1'] && help.text.start_with?('1:1: '), 'left: 1:1')
    w.cursor_left
    check.($sets == [1, 3, 1, 0, 2] && $cursor_sounds == 5, 'left again: nothing')
    a = syms.index(:animations); w.select(a)
    before = $game_system.animations?
    w.cursor_left
    check.(before == true && $game_system.animations? == false && Graphics.vita_display_mode == 2, "the script's own toggles still work (Battle Animations off, Screen untouched)")
  end
rescue StandardError => e
  check.(false, "#{e.class}: #{e.message} @ #{e.backtrace.first}")
end
puts "fails=#{fails}"
exit(fails == 0 ? 0 : 1)
