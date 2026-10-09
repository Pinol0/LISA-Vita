# Host test for patches/achievements.rb with LISA's own Steam scripts (148_SteamConfig, 149_SteamAPI).
#   ruby t_achievements.rb SCRIPTS_DIR SESSION WORK_DIR      (PATCH=file: test another version)
# One process per "launch" of the game: run.sh calls before / after / again / nowrite / badwindow.
require 'fileutils'
require 'zlib'   # built into the port (MKXP_VITA_ZLIB); LISA's Steam script uses it for steamstat.dat
SCRIPTS, SESSION, WORK = ARGV
abort 'usage' unless WORK
# As on the Vita (d104: crash opening the list): Time#strftime of a local time raises, newlib gives
# no time zone name. The patch must not format dates through it.
class Time
  def strftime(*)
    raise TypeError, 'wrong argument type false (expected String)'
  end
end
Dir.chdir(WORK)                       # the game's folder (CHDIR_GAME_ROOT): steamstat.dat lands here
VITA_PATCH_DIR = 'app0:patches/'
VITA_PATCH_LOG = File.join(WORK, 'qa.log')

# --- the RGSS / game classes the patch touches (only what it uses) ---
module Graphics
  def self.width; 544; end
  def self.height; 416; end
  def self.update; $frames = ($frames || 0) + 1; end
end
class Window_Base
  attr_accessor :x, :y, :width, :height, :z, :openness, :help_window
  attr_reader :texts, :disposed
  def initialize(x, y, w, h); @x, @y, @width, @height = x, y, w, h; @openness = 255; @texts = []; @disposed = false; $windows << self; end
  def fitting_height(n); n * 24 + 24; end
  def line_height; 24; end
  def contents_width; @width - 24; end
  def system_color; :system; end
  def normal_color; :normal; end
  def change_color(c, enabled = true); @color = [c, enabled]; end
  def draw_text(*a); raise 'window broken' if $broken_window; @texts << [a.last.is_a?(Integer) ? a[-2] : a.last, @color]; end
  def open; @opening = true; end
  def close; @closing = true; end
  def update; @openness = [@openness + 64, 255].min if @opening; (@openness = [@openness - 64, 0].max; @opening = false) if @closing; end
  def close?; @openness == 0; end
  def dispose; @disposed = true; end
end
class Window_Help < Window_Base
  attr_reader :text
  def initialize(n); super(0, 0, 544, n * 24 + 24); end
  def set_text(t); @text = t; end
end
class Window_Selectable < Window_Base
  attr_reader :index, :handlers
  def initialize(x, y, w, h); super; @index = -1; @handlers = {}; end
  def refresh; item_max.times { |i| draw_item(i) }; end
  def item_rect_for_text(i); [0, i * 24, contents_width, 24]; end
  def draw_text(*a); a.size == 2 ? super(*a[0], a[1]) : super; end
  def select(i); @index = i; update_help if @help_window; end
  def activate; end
  def set_handler(s, m); @handlers[s] = m; end
end
class Window_Command < Window_Selectable
  attr_reader :list
  def initialize; @list = []; make_command_list; super(0, 0, 200, 24 * @list.size + 24); end
  def add_command(name, symbol, enabled = true, ext = nil); @list << { name: name, symbol: symbol, enabled: enabled, ext: ext }; end
end
class Window_TitleCommand < Window_Command   # as LISA's 094 (New Game / Continue / Shut Down)
  def make_command_list; add_command('New Game', :new_game); add_command('Continue', :continue); add_command('Shut Down', :shutdown); end
end
class Scene_Base; def start; end; end
class Scene_MenuBase < Scene_Base; def return_scene; $returned = true; end; end
class Scene_Title < Scene_Base
  attr_reader :command_window
  def create_command_window; @command_window = Window_TitleCommand.new; @command_window.set_handler(:new_game, :ng); end
  def close_command_window; @closed = true; end
end
module SceneManager; def self.call(c); $called = c; end; end
$windows = []

# --- the game: its Steam scripts, then (from the second launch) the patch, as MKXP_VITA_PATCHES does ---
%w[148_SteamConfig.rb 149_SteamAPI.rb].each { |f| eval(File.read(File.join(SCRIPTS, f)), TOPLEVEL_BINDING, 'eval') }
if SESSION != 'before'
  patch = ENV['PATCH'] || File.join(__dir__, '../../../patches/achievements.rb')
  eval(File.read(patch), TOPLEVEL_BINDING, 'patches/achievements.rb')
end
fails = 0
check = ->(c, m) { puts "#{c ? 'PASS' : 'FAIL'}  #{m}"; fails += 1 unless c }
frames = ->(n) { n.times { Graphics.update } }
dat = -> { File.exist?('achievements.dat') ? File.read('achievements.dat').lines.map { |l| l.split[0] } : [] }
popups = -> { $windows.select { |w| w.is_a?(Window_VitaAchievement) } }

begin
case SESSION
when 'before'                         # the port without the patch: Steam stubs, unlocks stay pending
  Steam::Stats.unlock(:meet_rando)
  frames.(200)
  check.(File.exist?('steamstat.dat'), 'without the patch the game keeps the unlock pending in steamstat.dat')
when 'after'
  check.(File.read(VITA_PATCH_LOG).include?('ACHIEVEMENTS installed ids=58 unlocked=0'), 'installed: the game\'s 58 achievements')
  frames.(400)                        # the game pushes its pending unlocks (every 60 frames); 3 s window
  check.(dat.() == ['ACH_RANDO_MEET'], "pending unlock of an earlier session arrives (#{dat.().inspect})")
  check.(popups.().size == 1 && popups.()[0].texts.map(&:first) == ['Achievement unlocked', 'The Face of Blood'], 'window: "Achievement unlocked" + Steam title')
  check.(popups.()[0].disposed && popups.()[0].z == 10_000, 'window shown above the game, then closed and disposed')
  r = Steam::Stats.unlock(:save_buddy)
  check.(r == true && dat.() == %w[ACH_RANDO_MEET ACH_SAVE_BUDDY], 'a new unlock is kept at once')
  Steam::Stats.unlock(:save_buddy); frames.(400)
  check.(dat.().size == 2 && popups.().size == 2, 'never twice (no second line, one window each)')
  Steam::Stats.unlock(:area_1); Steam::Stats.unlock(:area_2); frames.(2)
  check.(popups.().size == 3, 'two at once: shown one after the other')
  frames.(400)
  check.(popups.().size == 4 && popups.().last.texts.map(&:first)[1] == "It's All Falling Down", 'the second one follows')
  s = Scene_Title.new; s.create_command_window
  names = s.command_window.list.map { |c| c[:name] }
  check.(names == ['New Game', 'Continue', 'Achievements', 'Shut Down'], "title: Achievements before Shut Down (#{names.inspect})")
  s.command_window.handlers[:vita_achievements].call
  check.($called == Scene_VitaAchievements, 'the command opens the list')
  sc = Scene_VitaAchievements.new; sc.start
  cw = $windows.find { |w| w.texts.any? { |t| t[0].to_s.start_with?('Achievements  ') } }
  check.(cw && cw.texts[0][0] == 'Achievements  4 / 58', "count: #{cw && cw.texts[0][0]}")
  lw = $windows.grep(Window_VitaAchievementList).last
  check.(lw.texts.size == 58 && lw.texts[0] == ['- Hint Guy', [:normal, false]] && lw.texts.include?(['+ Happy Reunion', [:normal, true]]), 'list: 58, locked dimmed, unlocked marked')
  hw = $windows.grep(Window_Help).last
  i = VitaAchievements.ids.index('ACH_SAVE_BUDDY'); lw.select(i)
  check.(hw.text =~ /\ASave Buddy\.\nUnlocked \d{4}-\d\d-\d\d\z/, "help: description + date (#{hw.text.inspect})")
  lw.select(VitaAchievements.ids.index('ACH_JOYLESS_END'))
  check.(hw.text == "???\nLocked", "hidden on Steam and locked: ??? (#{hw.text.inspect})")
  lw.select(0)
  check.(hw.text == "Recruit Terry Hints.\nLocked", 'locked: description shown')
  ts = [0, 951_782_400, 951_868_799, 1_791_491_856, 4_107_542_400, 4_107_628_800] + Array.new(2000) { rand(0..4_200_000_000) }
  bad = ts.reject { |t| u = Time.at(t).utc; VitaAchievements.date(t) == format('%04d-%02d-%02d', u.year, u.month, u.day) }
  check.(bad.empty?, "date of #{ts.size} unix times (leap days, 2100) = Ruby's UTC date (#{bad.first(3).inspect} differ)")
when 'again'                          # next launch: what was unlocked is still there
  check.(File.read(VITA_PATCH_LOG).lines.last.include?('installed ids=58 unlocked=4'), 'next launch: 4 unlocked read back')
  check.(Steam::Stats.getAchievementStatus('ACH_SAVE_BUDDY') == true && Steam::Stats.unlock(:save_buddy) == false, 'the game sees them as unlocked (no second unlock)')
  frames.(300)
  check.(popups.().empty?, 'nothing shown again')
when 'nowrite'                        # achievements.dat cannot be written: the game keeps it pending
  FileUtils.rm_f('achievements.dat'); FileUtils.mkdir_p('achievements.dat')
  frames.(5)
  r = Steam::Stats.unlock(:doctor)
  check.(r == false && File.read(VITA_PATCH_LOG).include?('ACHIEVEMENTS unlock ACH_DOCTOR failed'), 'write error: not done, logged')
  FileUtils.rm_rf('achievements.dat')   # the card can be written again
  frames.(200)
  check.(dat.().include?('ACH_DOCTOR'), 'the game keeps it pending and pushes it again: kept once writing works')
when 'badwindow'                      # the window cannot be drawn: logged once, the game goes on
  $broken_window = true
  Steam::Stats.unlock(:terry); frames.(3); Steam::Stats.unlock(:nern); frames.(3)
  log = File.read(VITA_PATCH_LOG)
  check.(log.scan('ACHIEVEMENTS window failed').size == 1 && dat.().include?('ACH_TERRY') && dat.().include?('ACH_NERN'), 'window error: logged once, unlocks kept, game goes on')
end
rescue StandardError => e          # e.g. the Vita's strftime TypeError in the list (d104)
  check.(false, "#{SESSION}: #{e.class}: #{e.message} @ #{e.backtrace.first}")
end
puts "fails=#{fails}"
exit(fails == 0 ? 0 : 1)
