require_relative 'stubs'
DATA=File.join(ENV.fetch('RGSS_DATA') { abort 'set RGSS_DATA to the game\'s Data/ folder' }, '')
maps={}; Dir[DATA+'Map[0-9]*.rvdata2'].each{|f| id=f[/Map(\d+)/,1].to_i; maps[id]=Marshal.load(File.binread(f)) }
edges=Hash.new{|h,k|h[k]=[]}; troops=Hash.new{|h,k|h[k]=[]}
maps.each{|id,m| (m.iv(:events)||{}).each{|eid,e| (e.iv(:pages)||[]).each{|pg| (pg.iv(:list)||[]).each{|c| p=c.iv(:parameters)
  edges[id] << [p[1],p[2],p[3]] if c.iv(:code)==201 && p[0]==0
  troops[id] << p[1] if c.iv(:code)==301 && p[0]==0 }}}}
spawn={}; edges.each{|a,l| l.each{|t,x,y| spawn[t]||=[x,y]}}
dist={41=>0}; order=[41]; q=[41]
until q.empty?; a=q.shift; edges[a].uniq.each{|t,x,y| next if dist[t]||!maps[t]; dist[t]=dist[a]+1; q<<t; order<<t}; end
seen={}; route=[]
order.each{|id| m=maps[id]; next unless spawn[id] && dist[id]<=20
  key=[m.iv(:tileset_id), m.iv(:parallax_name), (m.iv(:bgm).iv(:name) rescue '')]
  next if seen[key]; seen[key]=1; route << [id,*spawn[id]] }
route=route.first(18)
tr=[]; order.each{|id| next unless dist[id]<=20; troops[id].each{|t| tr<<t unless tr.include?(t)} }
puts "ROUTE=#{route.inspect}"; puts "TROOPS=#{tr.first(8).inspect}"
