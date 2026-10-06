# Host test for MKXP_VITA_OBJ_HIST with the real GC: garbage alone writes nothing; objects kept alive
# write OBJ_HIST naming their class (count, Array first-element class, sample ivars); Graphics.update
# keeps its return value.
module Graphics; @fc = 0; def self.frame_count; @fc; end; def self.update; @fc += 1; :upd; end; end
class Game_Map; def map_id; 74; end; end
module SceneManager; def self.scene; :scene; end; end
$game_map = Game_Map.new
File.delete('qa.log') if File.exist?('qa.log')
eval(File.read(ARGV[0]))
fails = 0
check = ->(c, m) { puts "#{c ? 'PASS' : 'FAIL'}  #{m}"; fails += 1 unless c }
check.(Graphics.update == :upd, 'Graphics.update result kept')
599.times { Graphics.update }                                   # tick 600: base
garbage = nil
599.times { garbage = Array.new(200) { 'x' * 3 }; Graphics.update } # lots of garbage, nothing kept
GC.disable; garbage = Array.new(100_000) { Object.new }; garbage = nil; GC.enable   # unswept at the check
Graphics.update
check.(!File.exist?('qa.log'), 'garbage only (even unswept at the check): no report')
class LeakyEvent; def initialize(i); @id = i; @list = [i, i + 1]; @name = "ev#{i}"; end; end
$kept = []
40_000.times { |i| $kept << LeakyEvent.new(i) }                   # ~120k live objects kept
600.times { Graphics.update }
log = File.exist?('qa.log') ? File.read('qa.log') : ''
check.(log.start_with?('OBJ_HIST n=0 ') && log.include?('map=74'), 'one report after real growth')
check.(log =~ /classes .*LeakyEvent=40000/, 'kept class counted')
check.(log =~ /array_first .*Integer=4\d{4}/, 'Array first-element class counted')
check.(log.include?('LeakyEvent{@id:Integer,@list:Array[2],@name:String[') , 'sample ivars of the kept class')
600.times { Graphics.update }
check.(File.read('qa.log').scan('OBJ_HIST n=').size == 1, 'no new report without new growth')
File.delete('qa.log')
puts "fails=#{fails}"; exit(fails == 0 ? 0 : 1)
