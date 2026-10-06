# Host test for patches/ps_buttons.rb:
#  1. skills: with the real Skills.rvdata2, every description equals the one of the verified offline
#     tool (tools/ps-icons/patch_skill_descriptions.rb -> tools/ps-icons/Skills.rvdata2), all 812 skills;
#  2. IconSet: Cache.system('Iconset') gets the 28 slots cleared and blitted from the overlay, once
#     per bitmap; other files untouched; a bitmap of another size is left alone;
#  3. DataManager.load_database keeps its result and does not swallow the game's own errors.
#   ruby t_ps_buttons.rb ORIGINAL_Skills.rvdata2      (PATCH=file: test another version, e.g. a mutation)
require_relative '../../ps-icons/rpgstub'
SRC = ARGV[0] or abort 'usage'
class RPG::BaseItem; attr_accessor :id, :description, :note, :icon_index; end
VITA_PATCH_DIR = 'app0:patches/'
VITA_PATCH_LOG = 'qa_test.log'
class Rect; attr_reader :x, :y, :width, :height; def initialize(*a); @x, @y, @width, @height = a; end; end
class Bitmap
  attr_reader :ops, :width, :height, :path
  def initialize(path, w = 384, h = 432); @path = path; @width = w; @height = h; @ops = []; @disposed = false; end
  def clear_rect(r); @ops << [:clear, r.x, r.y, r.width, r.height]; end
  def blt(x, y, src, r); @ops << [:blt, x, y, src.path, r.x, r.y, r.width, r.height]; end
  def dispose; @disposed = true; end
  def disposed?; @disposed; end
end
module Cache
  @c = {}
  def self.system(f); @c[f] ||= Bitmap.new("Graphics/System/#{f}"); end
  def self.reset(c); @c = c; end
end
module DataManager
  def self.load_database; raise 'game error' if $fail; $data_skills = Marshal.load(File.binread(SRC)); :loaded; end
end
File.delete(VITA_PATCH_LOG) if File.exist?(VITA_PATCH_LOG)
PATCH = ENV['PATCH'] || File.join(__dir__, '../../../patches/ps_buttons.rb')
eval(File.read(PATCH), TOPLEVEL_BINDING, 'ps_buttons.rb')
fails = 0
check = ->(c, m) { puts "#{c ? 'PASS' : 'FAIL'}  #{m}"; fails += 1 unless c }
check.(DataManager.load_database == :loaded, 'load_database result kept')
ref = Marshal.load(File.binread(File.join(__dir__, '../../ps-icons/Skills.rvdata2')))
diff = $data_skills.each_index.reject { |i| ($data_skills[i] && $data_skills[i].description) == (ref[i] && ref[i].description) }
check.(diff.empty? && $data_skills.compact.size == 812, "all 812 descriptions equal the offline tool's (#{diff.size} differ)")
check.(File.read(VITA_PATCH_LOG).include?('PS_BUTTONS skills=65 corrected=3'), 'qa.log: skills=65 corrected=3')
b = Cache.system('Iconset')
slots = b.ops.select { |o| o[0] == :clear }.map { |o| [o[2] / 24, o[1] / 24] }
check.(slots.size == 28 && slots.include?([4, 8]) && slots.include?([17, 15]) && !slots.include?([16, 7]) && !slots.include?([4, 7]), '28 slots, Q (4,7) and (16,7) kept')
check.(b.ops.each_slice(2).all? { |c, l| c[0] == :clear && l[0] == :blt && l[3] == 'app0:patches/ps_buttons_overlay' && l[1, 2] == c[1, 2] && l[4, 4] == c[1, 4] }, 'each slot: clear then blt of the same cell from the overlay')
Cache.system('Iconset')
check.(b.ops.size == 56, 'patched once per bitmap')
check.(Cache.system('Window').ops.empty?, 'other system files untouched')
Cache.reset('Iconset' => (small = Bitmap.new('Graphics/System/Iconset', 100, 100)))
Cache.system('Iconset')
check.(small.ops.empty? && File.read(VITA_PATCH_LOG).include?('iconset=size'), 'IconSet of another size left alone (logged)')
$fail = true
check.((DataManager.load_database rescue $!.message) == 'game error', "the game's own load error is not swallowed")
File.delete(VITA_PATCH_LOG)
puts "fails=#{fails}"; exit(fails == 0 ? 0 : 1)
