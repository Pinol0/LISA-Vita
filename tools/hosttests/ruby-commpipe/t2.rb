t = Time.now; sleep 1.5; a = Time.now - t
r = 5.times.map { |i| Thread.new { sleep 0.05; i * 2 } }.map(&:value)
q = Queue.new; th = Thread.new { q.pop }; sleep 0.1; q << 7; v = th.value
cnt = 0; trap("USR2") { cnt += 1 }; 3.times { Process.kill(:USR2, $$); sleep 0.05 }
puts "OK slept=#{a.round(2)} threads=#{r.inspect} q=#{v} sig=#{cnt}"
