module Graphics; @f=0; def self.frame_count; @f; end; def self.tick; @f+=1; end; end
module SceneManager; def self.scene; nil; end; end
module Cache; def self.load_bitmap(a,b,h=0); sleep 0.31 if b=="slow"; [a,b,h]; end; end

  # LIGHT DIAGNOSTIC (MKXP_VITA_SLOW_LOAD_LOG): outermost Cache.load_bitmap calls >= 300 ms.
  Cache.singleton_class.prepend(Module.new do
    def load_bitmap(folder_name, filename, hue = 0)
      return super if $vita_slow_load_in
      $vita_slow_load_in = true
      # Same file loaded >= 8 times in one frame (d36: Graphics/System/Window, 2-3 s stalls): who calls it.
      key = "#{folder_name}#{filename}"
      if $vita_rl_frame == Graphics.frame_count && $vita_rl_key == key
        $vita_rl_n += 1
        if $vita_rl_n == 8
          File.open(File.join(ENV['TMPDIR'] || '/tmp', 'slow_load.log'), 'a') { |f| f.puts "REPEATED_LOAD frame=#{Graphics.frame_count} path=#{key} scene=#{SceneManager.scene.class} map=#{($game_map.map_id rescue '?')} caller=#{caller(1, 12).join(' | ')}" } rescue nil
        end
      else
        $vita_rl_frame = Graphics.frame_count
        $vita_rl_key = key
        $vita_rl_n = 1
      end
      t0 = Time.now
      begin
        super
      ensure
        $vita_slow_load_in = false
        ms = ((Time.now - t0) * 1000).round
        if ms >= 300
          File.open(File.join(ENV['TMPDIR'] || '/tmp', 'slow_load.log'), 'a') { |f| f.puts "SLOW_LOAD frame=#{Graphics.frame_count} ms=#{ms} path=#{folder_name}#{filename} hue=#{hue} scene=#{SceneManager.scene.class}" } rescue nil
        end
      end
    end
  end)
raise unless Cache.load_bitmap("Graphics/System/","Window")==["Graphics/System/","Window",0]
9.times { Cache.load_bitmap("Graphics/System/","Window") }
Graphics.tick; Cache.load_bitmap("Graphics/Pictures/","slow",5)
puts File.read(File.join(ENV['TMPDIR'] || '/tmp', 'slow_load.log'))
