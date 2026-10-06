require_relative 'stubs'
DATA=File.join(ENV.fetch('RGSS_DATA') { abort 'set RGSS_DATA to the game\'s Data/ folder' }, '')
def fmt(p); s=p.inspect; s.length>160 ? s[0,160]+'…' : s; end
def dump_list(out,list)
  (list||[]).each_with_index{|c,i| out.puts "    [#{i}] #{'  '*c.indent}#{c.code} #{fmt(c.parameters)}"}
end
infos=Marshal.load(File.binread(DATA+'MapInfos.rvdata2')) rescue {}
File.open(ARGV[0],'w'){|out|
  Dir[DATA+'Map[0-9]*.rvdata2'].sort.each{|f|
    id=f[/Map(\d+)/,1].to_i; m=Marshal.load(File.binread(f))
    out.puts "=== MAP #{id} name=#{infos[id] ? infos[id].iv(:name) : '?'} display=#{m.iv(:display_name)} size=#{m.iv(:width)}x#{m.iv(:height)}"
    (m.iv(:events)||{}).sort.each{|eid,e|
      (e.iv(:pages)||[]).each_with_index{|pg,pi|
        c=pg.iv(:condition); g=pg.iv(:graphic)
        conds=[]; conds<<"sw1=#{c.iv(:switch1_id)}" if c.iv(:switch1_valid); conds<<"sw2=#{c.iv(:switch2_id)}" if c.iv(:switch2_valid); conds<<"var#{c.iv(:variable_id)}>=#{c.iv(:variable_value)}" if c.iv(:variable_valid); conds<<"self=#{c.iv(:self_switch_ch)}" if c.iv(:self_switch_valid); conds<<"item=#{c.iv(:item_id)}" if c.iv(:item_valid); conds<<"actor=#{c.iv(:actor_id)}" if c.iv(:actor_valid)
        out.puts "  EV map=#{id} ev=#{eid} name=#{e.iv(:name).inspect} xy=#{e.iv(:x)},#{e.iv(:y)} page=#{pi} trigger=#{pg.iv(:trigger)} cond=[#{conds.join(' ')}] gfx=#{g.iv(:character_name)}:#{g.iv(:character_index)} move_type=#{pg.iv(:move_type)}"
        dump_list(out,pg.iv(:list))
      }
    }
  }
  ce=Marshal.load(File.binread(DATA+'CommonEvents.rvdata2'))
  ce.each{|c| next unless c; out.puts "=== COMMON #{c.iv(:id)} name=#{c.iv(:name).inspect} trigger=#{c.iv(:trigger)} switch=#{c.iv(:switch_id)}"; dump_list(out,c.iv(:list))}
}
