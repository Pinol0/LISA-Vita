require_relative 'stubs'
DATA=File.join(ENV.fetch('RGSS_DATA') { abort 'set RGSS_DATA to the game\'s Data/ folder' }, '')
infos=Marshal.load(File.binread(DATA+'MapInfos.rvdata2'))
maps={}
Dir[DATA+'Map[0-9]*.rvdata2'].each{|f| id=f[/Map(\d+)/,1].to_i; maps[id]=Marshal.load(File.binread(f)) }
edges=Hash.new{|h,k|h[k]=[]}; troops=Hash.new{|h,k|h[k]=[]}
maps.each{|id,m| (m.iv(:events)||{}).each{|eid,e| (e.iv(:pages)||[]).each{|pg| (pg.iv(:list)||[]).each{|c|
  p=c.iv(:parameters)
  if c.iv(:code)==201 && p[0]==0 then edges[id] << [p[1],p[2],p[3]] end
  if c.iv(:code)==301 && p[0]==0 then troops[id] << p[1] end
}}}}
dist={41=>0}; q=[41]
until q.empty?; a=q.shift; break if dist[a]>=40; edges[a].uniq.each{|t,x,y| next if dist[t]; dist[t]=dist[a]+1; q<<t}; end
dist.sort_by{|k,v|[v,k]}.each{|id,d| m=maps[id]; next unless m
  spawn=edges.values.flatten(1).find{|t,x,y| t==id}
  puts "map=#{id} depth=#{d} name=#{infos[id] ? infos[id].iv(:name) : '?'} size=#{m.iv(:width)}x#{m.iv(:height)} tileset=#{m.iv(:tileset_id)} parallax=#{m.iv(:parallax_name)} bgm=#{(m.iv(:bgm).iv(:name) rescue '')} spawn=#{spawn.inspect} troops=#{troops[id].uniq.inspect} enc=#{(m.iv(:encounter_list)||[]).map{|e|e.iv(:troop_id)}.inspect}" }
