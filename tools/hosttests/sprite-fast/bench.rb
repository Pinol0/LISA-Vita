# Micro-benchmark (host, relative only): cost of one Sprite_Character#update that takes the
# SPRITE_FAST path, Ruby blocks vs the C modules. usage (inside out/tn): bench.rb SCRIPTS_DIR ruby|native
mode = $test_args[1]
if mode == 'ruby'
  Object.send(:remove_const, :VitaSpriteFastNative)
  Object.send(:remove_const, :VitaOffscreenNative)
end
here = File.expand_path('.')
src = File.read(File.join(here, 't_sprite_fast.rb')).gsub('__dir__', here.inspect)
eval(src[0...src.index('def mkchar(rng)')], TOPLEVEL_BINDING)   # classes, LISA scripts, blocks.rb
eval(src[src.index('def mkchar(rng)')...src.index('def step(c, rng)')], TOPLEVEL_BINDING)
$game_map = Game_Map.new
rng = Random.new(5)
sprites = Array.new(40) { s = Sprite_Character.new(nil, mkchar(rng)); s }
$vita_sprite_fast = true; $vita_offscreen_skip = false
ss = Spriteset_Map.new(sprites)
3.times { ss.update }   # snapshots taken
n0 = $vita_sfast_n
t = Process.clock_gettime(Process::CLOCK_MONOTONIC)
5000.times { ss.update }
dt = Process.clock_gettime(Process::CLOCK_MONOTONIC) - t
fast = $vita_sfast_n - n0
puts format('%-6s fast-path updates %d, %.2f us each', mode, fast, dt * 1e6 / (40 * 5000))
