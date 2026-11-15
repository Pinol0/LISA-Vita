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
if SESSION == 'logdir'                # MKXP_VITA_LOG_DIR: logs in logs/, the game folder given apart
  FileUtils.mkdir_p(File.join(WORK, 'logs'))
  VITA_PATCH_LOG = File.join(WORK, 'logs', 'qa.log')
  VITA_GAME_DIR = WORK + '/'
else
  VITA_PATCH_LOG = File.join(WORK, 'qa.log')
end

# --- the RGSS / game classes the patch touches (only what it uses) ---
module Graphics
  def self.width; $gw || 544; end
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
  def standard_padding; 12; end
  def contents; @contents ||= Bitmap.new([contents_width, 1].max, [respond_to?(:contents_height) ? contents_height : @height - 24, 1].max); end
  def reset_font_settings; contents.font.size = 24; end
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
  def col_max; 1; end
  def spacing; 32; end
  def item_height; 24; end
  def item_width; (@width - standard_padding * 2 + spacing) / col_max - spacing; end
  def item_rect(i); Rect.new(i % col_max * (item_width + spacing), i / col_max * item_height, item_width, item_height); end
  def contents_height; [(item_max + col_max - 1) / col_max, 1].max * item_height; end
  def update; end
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
# Bitmap as far as achievement_icons.rb uses it: a file's size from its PNG header (GAME_DIR, case-
# insensitive like the Vita's card), every drawing call recorded, source rects checked against the source.
class Rect
  attr_accessor :x, :y, :width, :height
  def initialize(x, y, w, h); @x, @y, @width, @height = x, y, w, h; end
  def to_a; [@x, @y, @width, @height]; end
end
class Color; def initialize(*a); @a = a; end; end
class Font; attr_accessor :size; def initialize; @size = 24; end; end
class Bitmap
  attr_reader :width, :height, :path, :ops
  attr_accessor :font
  def initialize(*a)
    if a.size == 1
      dir, base = File.split(File.join($game_dir, a[0]))
      f = Dir.exist?(dir) && Dir.children(dir).find { |c| c.downcase == base.downcase + '.png' }
      raise Errno::ENOENT, a[0] unless f
      @path = a[0]
      @width, @height = File.binread(File.join(dir, f), 24, 16).unpack('NN')
    else
      @width, @height = a
    end
    @ops = []; @font = Font.new; @disposed = false; $bitmaps << self
  end
  def check(src, r)
    raise 'source disposed' if src.disposed?
    $oob << [src.path, r.to_a] if r.x < 0 || r.y < 0 || r.width <= 0 || r.height <= 0 || r.x + r.width > src.width || r.y + r.height > src.height
  end
  def blt(x, y, src, r, op = 255); check(src, r); @ops << [:blt, x, y, src, r.to_a, op]; end
  def stretch_blt(dr, src, r, op = 255); check(src, r); @ops << [:stretch, dr.to_a, src, r.to_a, op]; end
  def fill_rect(*a); @ops << [:fill, *a]; end
  def clear; @ops.clear; end
  def dispose; @disposed = true; end
  def disposed?; @disposed; end
end
$bitmaps = []; $oob = []
$game_dir = WORK

# --- the game: its Steam scripts, then (from the second launch) the patch, as MKXP_VITA_PATCHES does ---
steam = !File.exist?(File.join(SCRIPTS, '148_SteamConfig.rb')) ? %w[SteamConfig SteamAPI].map { |n| Dir[File.join(SCRIPTS, "*_#{n}.rb")].first } :
                               %w[148_SteamConfig.rb 149_SteamAPI.rb].map { |f| File.join(SCRIPTS, f) }
steam.each { |f| eval(File.read(f), TOPLEVEL_BINDING, 'eval') }
if SESSION != 'before'
  patch = ENV['PATCH'] || File.join(__dir__, '../../../patches/achievements.rb')
  eval(File.read(patch), TOPLEVEL_BINDING, 'patches/achievements.rb')
  eval(File.read(ENV['ICONS']), TOPLEVEL_BINDING, 'patches/achievement_icons.rb') if SESSION.start_with?('icons')
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
when 'logdir'
  Steam::Stats.unlock(:terry)
  check.(File.exist?(File.join(WORK, 'achievements.dat')) && !File.exist?(File.join(WORK, 'logs', 'achievements.dat')) &&
         File.read(VITA_PATCH_LOG).include?('ACHIEVEMENTS unlock ACH_TERRY'), 'logs in logs/: achievements.dat stays in the game folder, the log line in logs/qa.log')
when 'joyful'                         # LISA: The Joyful's own Steam scripts (JOYFUL_SCRIPTS in run.sh)
  check.(File.read(VITA_PATCH_LOG).include?('ACHIEVEMENTS installed ids=10 unlocked=0'), "installed: The Joyful's 10 achievements")
  r = Steam::Stats.unlock(:terry)       # an event of The Painful left in The Joyful's maps: no id there
  frames.(600)
  log = File.read(VITA_PATCH_LOG)
  check.(r == false && dat.().empty? && log.scan('ACHIEVEMENTS unknown id').size == 1, "a symbol without an id: not unlocked, logged once in 600 frames (#{log.scan('unknown id').size})")
  check.(Steam::Stats.unlock(:yado) == true && dat.() == ['ACH_YADO'], 'a Joyful achievement unlocks')
  check.(VitaAchievements.ids.size == 10 && VitaAchievements.ids.include?('ACH_YADO') && !VitaAchievements.ids.include?('ACH_TERRY'), 'the list: the 10 of The Joyful')
when 'again'                          # next launch: what was unlocked is still there
  check.(File.read(VITA_PATCH_LOG).lines.last.include?('installed ids=58 unlocked=4'), 'next launch: 4 unlocked read back')
  check.(Steam::Stats.getAchievementStatus('ACH_SAVE_BUDDY') == true && Steam::Stats.unlock(:save_buddy) == false, 'the game sees them as unlocked (no second unlock)')
  check.(Steam::Stats.setAchievement('ACH_YADO') == false && !dat.().include?('ACH_YADO'), "an id of the other game (The Joyful's) is not unlocked in The Painful")
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
when 'icons'                          # achievement_icons.rb on synthetic graphics (sizes only) and actors
  png = lambda do |rel, w, h|
    f = File.join(WORK, 'Graphics', rel + '.png'); FileUtils.mkdir_p(File.dirname(f))
    File.binwrite(f, "\x89PNG\r\n\x1a\n".b + [13].pack('N') + 'IHDR' + [w, h].pack('NN'))
  end
  png.('Faces/Boys', 384, 192); png.('Faces/Boys6', 384, 192); png.('Faces/Decoy', 384, 192)
  png.('Characters/$buddy', 120, 160)          # 40x40 frames: scaled up 2x (not 2.4x) to 80x80
  png.('Characters/chars5', 384, 256)          # 8 characters of 32x32 frames
  png.('Battlers/rando', 200, 150)             # file name in another case than the patch's 'Rando'
  png.('Pictures/lisa small', 87, 20)
  actor = Struct.new(:name, :face_name, :face_index)
  $data_actors = [nil, actor.new('Brad', 'Boys', 2), actor.new('Terry', 'Boys', 3), actor.new('', 'Boys', 0),
                  actor.new('Bo Wy', 'Decoy', 0), actor.new('Bo', 'Boys6', 3)]
  Steam::Stats.unlock(:save_buddy); Steam::Stats.unlock(:area_1); frames.(2)
  pop = popups.().last
  st = pop.contents.ops.find { |o| o[0] == :stretch }
  check.(st && st[1][2..3] == [48, 48] && st[2].width == 96 && st[2].disposed? && pop.texts.map(&:first) == ['Achievement unlocked', 'Happy Reunion'],
         "unlock window: the icon at 48x48 left of the text, then disposed (#{st && st[1].inspect})")
  frames.(400)
  sc = Scene_VitaAchievements.new; sc.start
  lw = $windows.grep(Window_VitaAchievementList).last
  check.(lw.col_max == 5 && lw.item_rect(1).to_a == [104, 0, 100, 100] && lw.height == 224 && lw.y == 84,
         "grid: 5 columns of 100x100, two whole rows between count and help (cols #{lw.col_max}, h #{lw.height}, y #{lw.y})")
  check.($bitmaps.count(&:path) == 2,
         'opening the list loads no sheet at once (icons are cut a few per frame)')
  25.times { lw.update }
  ops = lw.contents.ops
  blt = ->(id) { r = lw.item_rect(VitaAchievements.ids.index(id)); ops.find { |o| o[0] == :blt && o[1] == r.x + 2 && o[2] == r.y + 2 } }
  t = blt.('ACH_TERRY')
  check.(t && t[5] == 64 && t[3].ops[0][0] == :blt && t[3].ops[0][3].path == 'Graphics/Faces/Boys' && t[3].ops[0][4] == [288, 0, 96, 96],
         "locked recruit: Terry's face (Boys, index 3), faded (#{t && [t[5], t[3].ops[0][4]].inspect})")
  b = blt.('ACH_SAVE_BUDDY')
  check.(b && b[5] == 255 && b[3].ops[0][0] == :stretch && b[3].ops[0][1] == [8, 8, 80, 80] && b[3].ops[0][3] == [40, 0, 40, 40],
         "unlocked character: middle frame of $buddy, whole-factor scale, full opacity (#{b && b[3].ops[0].values_at(1, 3).inspect})")
  bo = blt.('ACH_BO')
  check.(bo && bo[3].ops[0][3].path == 'Graphics/Faces/Boys6' && bo[3].ops[0][4] == [288, 0, 96, 96],
         "recruit by whole name: Bo Wyatt is Bo, not the actor Bo Wy (#{bo && bo[3].ops[0][3].path})")
  po = blt.('ACH_POTTY')
  check.(po && po[3].ops[0][3] == [224, 0, 32, 32] && po[3].ops[0][1] == [0, 0, 96, 96], "8-character sheet: character 2's middle frame (#{po && po[3].ops[0].values_at(1, 3).inspect})")
  ra = blt.('ACH_RANDO_MEET')
  check.(ra && ra[3].ops[0][1] == [0, 12, 96, 72], "large battler scaled down to fit (#{ra && ra[3].ops[0][1].inspect})")
  hid = lw.item_rect(VitaAchievements.ids.index('ACH_JOYLESS_END'))
  check.(lw.texts.any? { |x| x[0] == '?' } && !ops.any? { |o| o[0] == :blt && o[2] == hid.y + 2 && o[1] == hid.x + 2 },
         'locked and hidden on Steam: "?" and no icon (no spoiler)')
  log = File.read(VITA_PATCH_LOG)
  check.(log.scan('ACHIEVEMENTS icon ACH_BYE_I_GUESS').size == 1 && lw.texts.any? { |x| x[0] == 'B' && x[1] == [:normal, false] },
         'missing file: logged, the cell shows the initial')
  check.($bitmaps.select(&:path).all?(&:disposed?) && $bitmaps.select { |x| x.width == 96 && x.height == 96 && !x.path }.all?(&:disposed?) && $oob.empty?,
         "every sheet and icon disposed, every cut inside its sheet (#{$oob.first(2).inspect})")
  lw.select(VitaAchievements.ids.index('ACH_SAVE_BUDDY'))
  hw = $windows.grep(Window_Help).last
  check.(hw.text =~ /\AHappy Reunion  \(Unlocked \d{4}-\d\d-\d\d\)\nSave Buddy\.\z/, "help: title, date, description (#{hw.text.inspect})")
  Scene_VitaAchievements.new.start; 25.times { $windows.grep(Window_VitaAchievementList).last.update }
  check.(File.read(VITA_PATCH_LOG).scan('ACHIEVEMENTS icon ACH_BYE_I_GUESS').size == 1, 'a missing file is logged once')
  $gw = 736; Scene_VitaAchievements.new.start
  check.($windows.grep(Window_VitaAchievementList).last.col_max == 7, 'Wide: 7 columns')
  ic = VitaAchievementIcons.bitmap('ACH_LISA')
  check.(ic && ic.ops[0][0] == :fill && ic.ops[1][1..2] == [4, 38], "picture: on a card, centred (#{ic && ic.ops.map { |o| o[0..2] }.inspect})")
when 'icons_real'                     # every icon of the game cut from its real graphics (GAME_DIR)
  $game_dir = ENV['GAME_DIR']
  Object.const_set(:RPG, Module.new) unless defined?(RPG)
  begin   # Actors data with classes made on demand (only the fields are read)
    $data_actors = Marshal.load(File.binread(File.join($game_dir, 'Data/Actors.rvdata2')))
  rescue ArgumentError => e
    raise unless e.message =~ /undefined class\/module (\S+)/
    names = Regexp.last_match(1).split('::')
    names.inject(Object) { |m, n| m.const_defined?(n, false) ? m.const_get(n) : m.const_set(n, Class.new { def method_missing(n, *) = instance_variable_get("@#{n}") }) }
    retry
  end
  ids = VitaAchievements.ids
  bad = ids.reject { |id| (b = VitaAchievementIcons.bitmap(id)) && b.ops.any? }
  check.(bad.empty? && $oob.empty? && $bitmaps.select(&:path).all?(&:disposed?),
         "all #{ids.size} icons cut from the game's files, inside their sheets, sheets disposed (#{bad.first(4).inspect} #{$oob.first(2).inspect})")
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
