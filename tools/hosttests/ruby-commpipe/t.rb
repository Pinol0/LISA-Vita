q = Queue.new
cnt = 0
trap("USR1") { cnt += 1 }
t1 = Thread.new { 150.times { |i| q << i; sleep 0.01 }; q << :done }
t2 = Thread.new { n = 0; loop { x = q.pop; break if x == :done; n += 1 }; n }
t3 = Thread.new { 30.times { Process.kill(:USR1, Process.pid); sleep 0.05 } }
m = Mutex.new; cv = ConditionVariable.new; ready = false
t4 = Thread.new { sleep 1.2; m.synchronize { ready = true; cv.signal } }
m.synchronize { cv.wait(m, 5) until ready }
File.open(ARGV[0] || "/dev/null", "w") { |f| 100.times { f.write("x") }; sleep 0.3; 100.times { f.write("y") } }
t1.join; got = t2.value; t3.join; t4.join
sleep 0.2
puts "OK items=#{got} signals=#{cnt}"
