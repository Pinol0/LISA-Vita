# Host stress test of the libruby v8 coroutine thread pool (coroutine/pthread Context.c with
# COROUTINE_VITA via -DRB_VITA_FIBER_HOST_TEST, cont.c with transfer_exit): run with that miniruby.
fails = 0
check = ->(c, m) { puts "#{c ? 'PASS' : 'FAIL'}  #{m}"; $stdout.flush; fails += 1 unless c }
threads = -> { Dir.children('/proc/self/task').size }
t0 = threads.call

# 1. many short Fibers (finish -> transfer_exit -> back to the pool, reused)
sum = 0
20_000.times { |i| sum += Fiber.new { |x| x * 2 }.resume(i) }
check.(sum == 20_000 * 19_999, "20000 finished Fibers return their values (sum #{sum})")
check.(threads.call <= t0 + 6, "threads stay bounded (#{t0} -> #{threads.call})")

# 2. Fibers left suspended and collected by the GC (coroutine_join -> release -> back to the pool)
3.times do
  2000.times { f = Fiber.new { Fiber.yield 1; :never }; f.resume }
  GC.start
end
GC.start
check.(threads.call <= t0 + 6, "suspended Fibers destroyed by GC, threads bounded (#{threads.call})")

# 3. exceptions inside Fibers reach the resumer; the thread is reused afterwards
ok = 0
2000.times { begin; Fiber.new { raise ArgumentError, 'x' }.resume; rescue ArgumentError; ok += 1; end }
check.(ok == 2000, "2000 exceptions propagated from Fibers")

# 4. nested Fibers and many switches with values
outer = Fiber.new do
  inner = Fiber.new { |a| loop { a = Fiber.yield(a + 1) } }
  v = 0
  50_000.times { v = inner.resume(v) }
  v
end
check.(outer.resume == 50_000, "nested: 50000 resume/yield round trips keep the values")

# 5. objects reachable only from a suspended Fiber's stack survive GC (stack bounds of reused threads)
fibers = Array.new(50) do |k|
  Fiber.new do
    local = Array.new(200) { |i| "s#{k}-#{i}" * 3 }
    Fiber.yield
    local.each_with_index.all? { |s, i| s == "s#{k}-#{i}" * 3 }
  end
end
fibers.each(&:resume)
5.times { GC.start; Array.new(100_000) { 'garbage' * 4 } }
check.(fibers.all?(&:resume), "50 suspended Fibers keep their stack-only objects across GCs")

# 6. like LISA's parallel interpreters: Fibers recreated every 'frame', some waiting several frames
alive = []
frames_ok = true
5000.times do |frame|
  alive.reject! { |f| !f.alive? }
  alive << Fiber.new { (frame % 3).times { Fiber.yield }; frame } while alive.size < 6
  alive.each { |f| r = f.resume; frames_ok &&= (r.nil? || r.is_a?(Integer)) }
  GC.start if frame % 500 == 0
end
check.(frames_ok && threads.call <= t0 + 12, "5000 frames of recreated/waiting Fibers (threads #{threads.call})")

# 7. a Fiber resumed from another thread's... (not supported across Ruby threads): skip. Fiber#raise:
f = Fiber.new { begin; Fiber.yield; rescue => e; e.message; end }
f.resume
check.(f.raise(RuntimeError, 'injected') == 'injected', 'Fiber#raise into a suspended Fiber')

puts "fails=#{fails} threads #{t0} -> #{threads.call}"
exit(fails == 0 ? 0 : 1)
