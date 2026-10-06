# Host test (d76): the def + zsuper wrappers in src/main.cpp (VitaDiagHooks.wrap, RUBY_PROF phases,
# VitaCrumb.wrap, SAVE_PROF, MAP_PROF, Window#refresh) behave like the define_method |*a, &b| versions
# they replace: return value, positional args, block, keywords, visibility, exceptions, the timing calls.
# Ruby code is extracted from main.cpp (C string literals) between "m = Module.new" and "owner.prepend(m)".
# usage: ruby t_def_wrappers.rb [path/to/main.cpp]
src = File.read(ARGV[0] || File.expand_path('../../../src/main.cpp', __dir__))
ruby = src.lines.map { |l| l =~ /^\s*"(.*)\\n"\s*$/ ? $1.gsub('\\"', '"').gsub('\\\\', '\\') + "\n" : "#--\n" }.join
blocks = ruby.scan(/^ *m = Module\.new\n *m\.const_set.*?^ *owner\.prepend\(m\)\n/m)
abort "FAIL: expected 5 wrapper blocks, got #{blocks.size}" unless blocks.size == 5
refresh = ruby[/^ *c\.prepend\(Module\.new do\n *def refresh.*?^ *end\)\n/m] or abort 'FAIL: refresh hook not found'

$log = []
def vita_diag_now; 100; end
def vita_diag_span(*a); $log << [:span, *a]; end
def vita_prof_enter(i); $log << [:enter, i] if $log; 7; end
def vita_prof(i, t); $log << [:prof, i, t] if $log; end
def vita_thread_cpu_ms; 0; end
def vita_diag_op(*a); $log << [:op, a[0], a[2]]; end
VITA_DIAG_OP_WIN_REFRESH = 3
module VitaCrumb; def self.log(s); $log << [:crumb, s]; end; end

# Reference: the d75 define_method form, same body shape.
def ref_wrap(owner, meth, priv)
  owner.prepend(Module.new do
    define_method(meth) { |*a, &b| $log << [:ref]; super(*a, &b) }
    ruby2_keywords(meth)
    private meth if priv
  end)
end

class Base
  def pos(x, y = 2, *r) = [x, y, r]
  def blk(x) = yield(x)
  def kw(a, k: 1) = [a, k]
  def boom = raise(ArgumentError, 'b')
  def q? = :q
  def set=(v); @v = v; end
  private def hidden(x) = x * 2
  def call_hidden(x) = hidden(x)
  def refresh = :r
end

fails = 0
check = ->(name, got, exp) { if got == exp then else fails += 1; puts "FAIL #{name}: #{got.inspect} != #{exp.inspect}" end }
calls = ->(o) {
  [o.pos(1), o.pos(1, 3, 4, 5), o.blk(5) { |v| v + 1 }, o.kw(1, k: 9), o.kw(2), o.q?, (o.set = 4),
   o.call_hidden(3), (o.boom rescue $!.class), o.respond_to?(:hidden), Base.private_method_defined?(:hidden)]
}
expected = calls.(Base.new)
blocks.each_with_index do |code, i|
  $vita_sfp = Hash.new(0); $vita_mpp = Hash.new(0)
  k = Class.new(Base)
  wrap = eval("->(owner, meth, tag, idx, min_us, det, priv) {\n#{code}}")
  %i[pos blk kw boom q? set= hidden].each { |m| wrap.(k, m, "T#{m}", 5, 0, nil, k.private_method_defined?(m)) }
  $log = []
  got = calls.(k.new)
  check.("block#{i} results", got, expected)
  check.("block#{i} hooks ran", $log.empty? && !($vita_sfp.any? || $vita_mpp.any?), false)
  check.("block#{i} visibility", k.private_method_defined?(:hidden) && k.public_method_defined?(:pos), true)
  # The same calls through the reference wrapper agree too (sanity for the harness).
  r = Class.new(Base); %i[pos blk kw boom q? set= hidden].each { |m| ref_wrap(r, m, r.private_method_defined?(m)) }
  check.("ref#{i}", calls.(r.new), expected)
end
# VitaDiagHooks.wrap detail block gets (self, args); RUBY_PROF passes the idx and the enter token.
k = Class.new(Base); $log = []
eval("->(owner, meth, tag, idx, min_us, det, priv) {\n#{blocks[0]}}").(k, :pos, 'P', 0, 50, ->(s, a) { a.sum }, false)
k.new.pos(1, 3, 4); check.('diag detail', $log.last, [:span, 'P', 100, 1 + 3 + 4, 50])
k = Class.new(Base); $log = []
eval("->(owner, meth, tag, idx, min_us, det, priv) {\n#{blocks[1]}}").(k, :pos, 'P', 11, 0, nil, false)
k.new.pos(1); check.('prof idx', $log, [[:enter, 11], [:prof, 11, 7]])
k = Class.new(Base); $log = []
eval("->(owner, meth, tag, idx, min_us, det, priv) {\n#{blocks[2]}}").(k, :pos, 'C', 0, 0, nil, false)
k.new.pos(Integer, 'x'); check.('crumb', $log, [[:crumb, 'C BEGIN Integer,String'], [:crumb, 'C END']])
$vita_sfp = Hash.new(0)
k = Class.new(Base); eval("->(owner, meth, tag, idx, min_us, det, priv) {\n#{blocks[3]}}").(k, :pos, 'S', 0, 0, nil, false)
k.new.pos(1); k.new.pos(1); check.('save_prof n', $vita_sfp['S#n'], 2)
$vita_sfp = nil; check.('save_prof off', k.new.pos(8), [8, 2, []])
# Window#refresh hook: outermost call only.
w = Class.new(Base) { def refresh = (@d = (@d || 0) + 1) == 1 ? [refresh, :outer] : :inner }
c = w; eval(refresh); $log = []; $vita_diag_in_refresh = false
check.('refresh', w.new.refresh, [:inner, :outer]); check.('refresh op', $log, [[:op, 3, nil]])
# Per-call allocations and objects still live after GC (T_IMEMO: the d75 Vita leak was one callinfo +
# callcache retained per define_method super call; on the host neither form retains, so this only
# guards against the new form being worse).
measure = ->(o) {
  100.times { o.q? }; GC.start; GC.disable
  a0 = GC.stat(:total_allocated_objects); 10000.times { o.q? }
  per = (GC.stat(:total_allocated_objects) - a0) / 10000.0
  GC.enable; GC.start; im0 = ObjectSpace.count_objects[:T_IMEMO]
  20000.times { o.q? }; GC.start; [per, ObjectSpace.count_objects[:T_IMEMO] - im0]
}
k = Class.new(Base); eval("->(owner, meth, tag, idx, min_us, det, priv) {\n#{blocks[1]}}").(k, :q?, 'Q', 1, 0, nil, false)
r = Class.new(Base); r.prepend(Module.new { define_method(:q?) { |*a, &b| super(*a, &b) }; ruby2_keywords(:q?) })
$log = nil
n_def, n_ref = measure.(k.new), measure.(r.new)
puts "def+zsuper allocs/call #{n_def[0]} imemo_retained/20k #{n_def[1]}; define_method allocs/call #{n_ref[0]} imemo_retained/20k #{n_ref[1]}"
check.('allocs not worse', n_def[0] <= n_ref[0] + 0.01, true)   # + one-time inline caches
check.('no imemo retention', n_def[1] < 100, true)
puts fails.zero? ? "PASS (#{blocks.size} blocks + refresh)" : "FAIL #{fails}"
exit(fails.zero? ? 0 : 1)
