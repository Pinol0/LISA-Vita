# Host test for MKXP_VITA_SAVE_FAST with LISA's DataManager (005): load_header results (values and
# fresh objects) equal the original for valid, corrupt and missing slots, repeated reads in one
# frame, after a save in the same frame, after a delete, across frames. ruby t_save_fast.rb SCRIPTS_DIR
require 'tmpdir'
D = ARGV[0] or abort 'usage'
module Graphics; @frame_count = 0; class << self; attr_accessor :frame_count; end; end
class Game_System; def on_before_save; end; def playtime_s; "01:02:03"; end; end
class Game_Party; def characters_for_savefile; [["Brad", 0], ["$nern", 1]]; end; end
src = File.read(Dir[File.join(D, '005_*.rb')].first, encoding: 'UTF-8').gsub("\r\n", "\n")
eval(src)
module DataManager; def self.make_save_contents; { :x => 1 }; end; end
$game_system = Game_System.new; $game_party = Game_Party.new
Dir.chdir(Dir.mktmpdir)
def setup_files
  Dir['Save*.rvdata2'].each { |f| File.delete(f) }
  File.open('Save01.rvdata2', 'wb') { |f| Marshal.dump({ :characters => [["Brad", 0]], :playtime_s => "00:10:00" }, f); Marshal.dump({}, f) }
  File.open('Save02.rvdata2', 'wb') { |f| f.write("garbage") }
  File.open('Save03.rvdata2', 'wb') { |f| Marshal.dump({ :characters => [], :playtime_s => "02:00:00" }, f) }
end
def scenario
  out = []
  setup_files
  $game_system = Game_System.new
  Graphics.frame_count = 10
  16.times { |i| 2.times { h = DataManager.load_header(i); out << h } }
  ids = out.compact.map(&:object_id)
  Graphics.frame_count = 11
  out << DataManager.load_header(0)
  DataManager.save_game_without_rescue(3)            # same frame: slot 4 written
  out << DataManager.load_header(3) << DataManager.load_header(3)
  out << DataManager.load_header(0)                  # slot 1 read (memoized) ...
  $game_system.define_singleton_method(:playtime_s) { "03:03:03" }
  DataManager.save_game_without_rescue(0)            # ... then overwritten in the same frame
  out << DataManager.load_header(0) << DataManager.load_header(0)
  File.open('Save01.rvdata2', 'wb') { |f| Marshal.dump({ :characters => [["X", 2]], :playtime_s => "09:09:09" }, f) }   # external change
  Graphics.frame_count = 12
  out << DataManager.load_header(0)
  DataManager.delete_save_file(2)
  out << DataManager.load_header(2) << DataManager.load_header(2)
  [out, ids.uniq.size == ids.size]
end
$vita_save_fast = false
ref, = scenario
eval(File.read(File.join(__dir__, 'block.rb')))
abort 'FAIL not installed' unless DataManager.singleton_class.ancestors.first != DataManager.singleton_class
$vita_save_fast = true
opt, fresh = scenario
ok = ref == opt && fresh
puts "#{ok ? 'PASS' : 'FAIL'}  #{ref.size} load_header results identical to LISA's (valid/corrupt/missing, same frame, save, delete, external change); fresh objects per call: #{fresh}"
exit(ok ? 0 : 1)
