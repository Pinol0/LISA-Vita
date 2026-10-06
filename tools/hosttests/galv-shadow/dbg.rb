src = File.read('t_galv_shadow.rb')
src = src.sub(/^fails = 0\n.*\z/m, '')
eval(src)
a = run(false, 3); b = run(true, 3)
i = 192
puts "step #{i}: #{a[i].size} vs #{b[i].size} sprites"
a[i].zip(b[i]).each_with_index { |(x, y), k| puts "  #{k}: #{x.inspect}\n     #{y.inspect}" if x != y }
puts "step 191 equal? #{a[191] == b[191]}"
