# Host test (d80): MKXP_VITA_SAVE_WRITE_ONCE and MKXP_VITA_CRUMB_BUFFER, on the Ruby a build embeds:
#   python3 tools/check_embedded_ruby.py BUILD --dump BUILD/ruby-dump
#   ruby t_save_crumb.rb BUILD/ruby-dump
require 'tmpdir'
$stdout.sync = true
D = ARGV[0] or abort 'usage: DUMP_DIR'
$fails = 0
def check(n, got, exp) = (got == exp ? nil : ($fails += 1; puts "FAIL #{n}: #{got.inspect[0, 200]} != #{exp.inspect[0, 200]}"))
tmp = Dir.mktmpdir

# ---- SAVE_WRITE_ONCE ----
class Thing; def initialize; @a = [1, 2.5, 'é', :sym, { k: [nil, true] }]; @big = 'x' * 70000; @n = 2**70; end; end
class Custom; def initialize(v) = @v = v; def marshal_dump = [@v, :c]; def marshal_load(a) = @v = a[0]; end
class Old; def initialize(v) = @v = v; def _dump(l) = @v.to_s; def self._load(s) = new(s); end
header = { characters: [['Brad', 0]], playtime_s: '01:02:03' }
contents = { system: Thing.new, list: Array.new(3000) { |i| [i, "s#{i}", Custom.new(i), Old.new(i)] }, str: "è\xff".b }
save = ->(path) { File.open(path, 'wb') { |f| Marshal.dump(header, f); Marshal.dump(contents, f) } }
save.(File.join(tmp, 'ref.rvdata2'))
ref_text = File.open(File.join(tmp, 'ref_text'), 'w') { |f| Marshal.dump(header, f) }   # text-mode port
lim_ref = (Marshal.dump([[[1]]], File.open(File.join(tmp, 'l1'), 'wb'), 1) rescue $!.class)
eval(File.read(File.join(D, 'VITA_SAVE_ONCE.rb')))
check('installed', Marshal.singleton_class.ancestors.first != Marshal.singleton_class, true)
save.(File.join(tmp, 'new.rvdata2'))
check('same bytes', File.binread(File.join(tmp, 'new.rvdata2')), File.binread(File.join(tmp, 'ref.rvdata2')))
loaded = File.open(File.join(tmp, 'new.rvdata2'), 'rb') { |f| [Marshal.load(f), Marshal.load(f)] }
check('loads back', loaded[0], header)
class CountFile < File; attr_reader :writes; def write(*a) = (@writes = (@writes || 0) + 1; super); end
cf = CountFile.open(File.join(tmp, 'count'), 'wb'); r = Marshal.dump(contents, cf); cf.close
check('one write per dump', cf.writes, 1)
check('returns port', r.equal?(cf), true)
check('text-mode port same bytes', (File.open(File.join(tmp, 'new_text'), 'w') { |f| Marshal.dump(header, f) }; File.binread(File.join(tmp, 'new_text'))), File.binread(File.join(tmp, 'ref_text')))
check('limit forwarded', (Marshal.dump([[[1]]], File.open(File.join(tmp, 'l2'), 'wb'), 1) rescue $!.class), lim_ref)
check('string dump unchanged', Marshal.dump(header), Marshal.dump(header))
sio = Object.new; def sio.write(s) = (@d = (@d || +'') << s; s.size); def sio.data = @d
Marshal.dump(header, sio); check('non-File port untouched', sio.data, Marshal.dump(header))
bad = File.open(File.join(tmp, 'bad'), 'wb'); e = (Marshal.dump([1, proc {}], bad) rescue $!.class); bad.close
check('error propagates', e, TypeError)
check('nothing written on error', File.size(File.join(tmp, 'bad')), 0)

# ---- CRUMB_BUFFER ----
src = File.read(File.join(D, 'VITA_CRUMB.rb'))
mod = src[/^module VitaCrumb\n.*?^end\n/m] or abort 'FAIL module VitaCrumb not found'
check('tick hook present', src.include?('def update_basic; super; VitaCrumb.tick; end'), true)
check('at_exit flush present', src.include?('at_exit { VitaCrumb.flush }'), true)
module Graphics; class << self; attr_accessor :frame_count; end; end
Graphics.frame_count = 100
path = File.join(tmp, 'breadcrumb.log')
mod = mod.sub(/PATH = '[^']*'/, "PATH = #{path.inspect}")
abort 'FAIL PATH not replaced' unless mod.include?(path)
eval(mod)
VitaCrumb.log('BOOT'); VitaCrumb.log('A BEGIN Integer')
check('buffered, nothing written yet', File.exist?(path), false)
Graphics.frame_count = 130; VitaCrumb.tick
check('first tick flushes (timer starts at 0)', File.read(path), "f=100 BOOT\nf=100 A BEGIN Integer\n")
Graphics.frame_count = 140; VitaCrumb.log('B')
Graphics.frame_count = 170; VitaCrumb.tick
check('not before 60 frames from the last flush', File.read(path).lines.size, 2)
Graphics.frame_count = 190; VitaCrumb.tick
check('flushed at 60 frames', File.read(path).lines.last, "f=140 B\n")
Graphics.frame_count = 300; VitaCrumb.tick
check('empty buffer: no write', File.read(path).lines.size, 3)
300.times { |i| VitaCrumb.log("x#{i}") }
check('flush at 256 lines', File.read(path).lines.size, 3 + 256)
VitaCrumb.flush
check('explicit flush keeps order', File.read(path).lines.last(2), ["f=300 x298\n", "f=300 x299\n"])
File.chmod(0o444, path) unless Process.uid.zero?
VitaCrumb.log('lost?'); check('write error swallowed', (VitaCrumb.flush; :ok), :ok)
puts $fails.zero? ? 'PASS save-crumb' : "FAIL #{$fails}"
exit($fails.zero? ? 0 : 1)
