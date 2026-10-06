  VITA_SPC_LOG = File.join(ENV['TMPDIR'] || '/tmp', 'spc_qa.log')
  # PERF FIX (MKXP_VITA_SPRITE_NATIVE): Sprite_Character#update_position in C (vita-sprite-native.cpp)
  # for the character classes whose screen_x/screen_y/screen_z gave exactly the same values as the
  # C code in a self-test here (Integer/Float positions, scrolled displays, jumps, priorities,
  # graphic names). Falls back to the Ruby method with an animation running (move_animation),
  # on looping maps (adjust_x/adjust_y wrap) and for any value the C code does not handle.
  begin
    owner_of = lambda do |c, m|
      um = (c.instance_method(m) rescue nil)
      um = um.super_method while um && !um.owner.is_a?(Class)
      um && um.owner
    end
    ok = owner_of.call(Sprite_Character, :update_position) == Sprite_Character &&
         owner_of.call(Sprite_Character, :move_animation) == Sprite_Character &&
         %i[adjust_x adjust_y display_x display_y].all? { |m| owner_of.call(Game_Map, m) == Game_Map }
    classes = {}
    if ok
      saved_map = $game_map
      rng = Random.new(1234)
      # One test map for all cases (d53: a new RPG::Map per case allocated 2400 Tables, ~82 MB of
      # C++ heap that Ruby's GC does not see -> bad_alloc in Table::resize -> abort at boot).
      gm = Game_Map.allocate
      gm.instance_variable_set(:@map, RPG::Map.new(17, 13))
      [Game_Player, Game_Follower, Game_Event, Game_Vehicle].each do |k|
        next unless %i[screen_x screen_y screen_z].all? { |m| owner_of.call(k, m) == Game_CharacterBase }
        good = true
        600.times do |i|
          dxv = [rng.rand(120), rng.rand(1200) / 32.0, rng.rand(12000) / 1000.0, -rng.rand(40) / 8.0].sample(random: rng)
          dyv = [rng.rand(30), rng.rand(300) / 32.0, rng.rand(3000) / 1000.0].sample(random: rng)
          gm.instance_variable_set(:@display_x, dxv)
          gm.instance_variable_set(:@display_y, dyv)
          $game_map = gm
          c = k.allocate
          x = rng.rand(130); y = rng.rand(33)
          c.instance_variable_set(:@real_x, [x, x.to_f, x - rng.rand(8) / 8.0, x + rng.rand(1000) / 997.0].sample(random: rng))
          c.instance_variable_set(:@real_y, [y, y.to_f, y - rng.rand(8) / 8.0].sample(random: rng))
          peak = [0, 5, 10, 13].sample(random: rng)
          c.instance_variable_set(:@jump_peak, peak)
          c.instance_variable_set(:@jump_count, peak > 0 ? rng.rand(peak * 2 + 1) : 0)
          c.instance_variable_set(:@priority_type, rng.rand(3))
          c.instance_variable_set(:@character_name, ['', '!Door', '$Big', 'People1', '!$Obj'].sample(random: rng))
          c.instance_variable_set(:@tile_id, [0, 0, 5].sample(random: rng))
          ruby = [c.screen_x, c.screen_y, c.screen_z]
          nat = vita_spc_eval(c)
          unless nat && nat[0] == ruby[0] && nat[1] == ruby[1] && nat[2] == ruby[2] &&
                 nat[0].to_i == ruby[0].to_i && nat[1].to_i == ruby[1].to_i
            good = false
            File.open(VITA_SPC_LOG, 'a') { |f| f.puts "SPRITE_NATIVE_MISMATCH #{k} ruby=#{ruby.inspect} c=#{nat.inspect}" } rescue nil
            break
          end
        end
        classes[k] = true if good
      end
      $game_map = saved_map
    end
    File.open(VITA_SPC_LOG, 'a') { |f| f.puts "SPRITE_NATIVE #{ok && !classes.empty? ? 'INSTALLED' : 'NOT INSTALLED'} classes=#{classes.keys.join(',')}" } rescue nil
    if ok && !classes.empty?
      VITA_SPC_CLASSES = classes
      $vita_spc_noloop = false
      Spriteset_Map.prepend(Module.new do
        def update
          m = $game_map
          $vita_spc_noloop = $vita_sprite_native != false && !m.loop_horizontal? && !m.loop_vertical?
          super
        ensure
          $vita_spc_noloop = false
        end
      end)
      Sprite_Character.prepend(Module.new do
        def update_position
          return super if @animation || !$vita_spc_noloop || !VITA_SPC_CLASSES[@character.class]
          super unless vita_spc_pos(self, @character)
        end
      end)
    end
  rescue Exception => e
    $game_map = saved_map if defined?(saved_map)
    File.open(VITA_SPC_LOG, 'a') { |f| f.puts "SPRITE_NATIVE ERROR #{e.class}: #{e.message}" } rescue nil
  end
