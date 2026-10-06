  # PERF FIX (MKXP_VITA_OFFSCREEN_SPRITES): character sprites far off screen are not updated.
  # d39/d40: Sprite_Character#update ~0.16 ms per sprite per frame (Float math allocates on 32-bit
  # Ruby), 100-180 sprites on the busy maps. Events (game logic) are untouched: only the sprite of a
  # character >= 3 tiles (+ its cell size) outside the screen skips its update, and only when no
  # animation/balloon is running or requested (an event waiting for one must not stall), on maps
  # that do not loop. The last full update happens once the character is already out, so the
  # sprite is left off screen. Toggle at runtime: L+R+START ($vita_offscreen_skip, qa.log).
  $vita_offscreen_skip = true
  $vita_off_ok = false
  $vita_skip_n = 0
  Spriteset_Map.prepend(Module.new do
    def update
      m = $game_map
      if $vita_offscreen_skip && !m.loop_horizontal? && !m.loop_vertical?
        $vita_off_x0 = m.display_x.to_i
        $vita_off_y0 = m.display_y.to_i
        $vita_off_w = m.screen_tile_x
        $vita_off_h = m.screen_tile_y
        $vita_off_ok = true
      end
      super
    ensure
      $vita_off_ok = false
    end
  end)
  # MKXP_VITA_SPRITE_FAST_NATIVE: the same update in C (vita-sprite-fast-native.cpp).
  if defined?(VitaOffscreenNative)
    # Character readers that are, in every character class, Game_CharacterBase's own attr method
    # for that name (owner Game_CharacterBase: no override in a subclass or prepended module; no
    # bytecode: attr_reader/attr_accessor; original_name = the name: not an alias of another attr):
    # it returns @name, so the C code reads that instance variable directly. (UnboundMethod#==
    # between a subclass's and the base's method is false on Ruby 3.1, hence the explicit checks.)
    vita_cb_classes = ObjectSpace.each_object(Class).select { |k| k <= Game_CharacterBase }
    vita_direct = %w[x y real_x real_y pattern direction opacity blend_type bush_depth transparent priority_type
                     animation_id balloon_id].select do |m|
      vita_cb_classes.all? do |k|
        um = (k.instance_method(m) rescue nil)
        um && um.owner == Game_CharacterBase && RubyVM::InstructionSequence.of(um).nil? && um.original_name == m.to_sym
      end
    end
    vita_sprite_fast_native_bind(Sprite_Character, Game_CharacterBase, vita_direct)
    File.open('/dev/null', 'a') { |f| f.puts "SPRITE_FAST_NATIVE direct=#{vita_direct.join(',')}" } rescue nil
    Sprite_Character.prepend(VitaOffscreenNative)
  else
  Sprite_Character.prepend(Module.new do
    def vita_offscreen?
      c = @character
      return false unless c && instance_of?(Sprite_Character)
      return false if @balloon_sprite || animation? || c.animation_id > 0 || c.balloon_id > 0
      mx = (@cw || 32) / 64 + 3
      my = (@ch || 32) / 32 + 3
      tx = c.x - $vita_off_x0
      ty = c.y - $vita_off_y0
      tx < -mx || tx > $vita_off_w + mx || ty < -my || ty > $vita_off_h + my
    end
    def update
      if $vita_off_ok && vita_offscreen?
        if @vita_off_ready
          $vita_skip_n += 1
          @vita_off_skipped = true   # sprite left stale: no SPRITE_FAST snapshot this frame
          return
        end
        @vita_off_skipped = false
        super
        @vita_off_ready = true
      else
        @vita_off_skipped = false
        @vita_off_ready = false
        super
      end
    end
  end)
  end
  VITA_SPRITE_FAST_LOG = '/dev/null'
  # PERF FIX (MKXP_VITA_SPRITE_FAST): exact fast path of Sprite_Character#update for an unchanged
  # sprite. d44: on-screen character sprites cost 10-14 ms/frame on the busy maps, almost all still
  # decorations. With no animation/balloon running or requested and the same character, graphic,
  # pattern, direction, opacity, blend type, bush depth, transparency, real position, not jumping,
  # same priority and the same raw map display position as at the last full update, the stock
  # chain (update_bitmap/src_rect/position/other/balloon, setup_new_effect, Sprite_Base#update)
  # writes the same values again: only Sprite#update (C: flash, wave) is still called. Installed
  # only if those methods are the stock ones (owner check through our prepended modules).
  # Host test with LISA's scripts: tools/hosttests/sprite-fast/. Toggled with L+R+START.
  $vita_sprite_fast = true
  $vita_sfast_n = 0
  $vita_sscroll_n = 0
  $vita_sf_on = false
  owner_of = lambda do |c, m|
    um = (c.instance_method(m) rescue nil)
    um = um.super_method while um && !um.owner.is_a?(Class)
    um && um.owner
  end
  ok = [[Sprite_Character, %i[update update_bitmap graphic_changed? update_src_rect update_position update_other update_balloon setup_new_effect]],
        [Sprite_Base, %i[update update_animation animation?]],
        [Game_CharacterBase, %i[screen_x screen_y screen_z shift_y jump_height jumping? object_character?]],
        [Game_Map, %i[adjust_x adjust_y display_x display_y]]].all? do |c, ms|
    ms.all? { |m| owner_of.call(c, m) == c }
  end
  ok &&= Sprite.instance_method(:update).owner == Sprite
  File.open(VITA_SPRITE_FAST_LOG, 'a') { |f| f.puts "SPRITE_FAST #{ok ? 'INSTALLED' : 'NOT INSTALLED (scripts override the update chain)'}" } rescue nil
  if ok
    Sprite.send(:alias_method, :vita_csprite_update, :update)
    Spriteset_Map.prepend(Module.new do
      def update
        m = $game_map
        $vita_sf_dx = m.instance_variable_get(:@display_x)
        $vita_sf_dy = m.instance_variable_get(:@display_y)
        $vita_sf_on = $vita_sprite_fast
        super
      ensure
        $vita_sf_on = false
      end
    end)
    # MKXP_VITA_SPRITE_FAST_NATIVE: the same update in C (vita-sprite-fast-native.cpp).
    if defined?(VitaSpriteFastNative)
      Sprite_Character.prepend(VitaSpriteFastNative)
    else
    Sprite_Character.prepend(Module.new do
      def update
        c = @character
        if $vita_sf_on && @vsf_ok && c && @vsf_c.equal?(c) && !@balloon_sprite && !animation? &&
           c.animation_id == 0 && c.balloon_id == 0 && !graphic_changed? && !c.jumping? &&
           @vsf_rx == c.real_x && @vsf_ry == c.real_y &&
           @vsf_pat == c.pattern && @vsf_dir == c.direction && @vsf_op == c.opacity &&
           @vsf_bt == c.blend_type && @vsf_bd == c.bush_depth && @vsf_tr == c.transparent &&
           @vsf_pt == c.priority_type
          if @vsf_dx == $vita_sf_dx && @vsf_dy == $vita_sf_dy
            $vita_sfast_n += 1
            vita_csprite_update
            return
          end
          # Perf fix (MKXP_VITA_SPRITE_SCROLL, d86): only the map display moved (the screen scrolls
          # while the player walks: d85, every visible sprite took the full update, ~110 us each,
          # 4-5 ms per frame). With every other input unchanged the stock chain rewrites the same
          # bitmap, src_rect, opacity, blend, bush depth and visibility; only x/y/z follow the
          # display: Sprite#update (C) + update_position, then the new display goes in the snapshot.
          $vita_sscroll_n += 1
          vita_csprite_update
          update_position
          @vsf_dx = $vita_sf_dx; @vsf_dy = $vita_sf_dy
          return
        end
        super
        # No snapshot while jumping: the first landed frame changes y with the same inputs (host test).
        if @vita_off_skipped || !$vita_sf_on || !c || !instance_of?(Sprite_Character) || c.jumping?
          @vsf_ok = false
        else
          @vsf_ok = true
          @vsf_c = c
          @vsf_dx = $vita_sf_dx; @vsf_dy = $vita_sf_dy
          @vsf_rx = c.real_x; @vsf_ry = c.real_y
          @vsf_pat = c.pattern; @vsf_dir = c.direction; @vsf_op = c.opacity
          @vsf_bt = c.blend_type; @vsf_bd = c.bush_depth; @vsf_tr = c.transparent
          @vsf_pt = c.priority_type
        end
      end
    end)
    end
  end
