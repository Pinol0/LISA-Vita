  VITA_REGION_INCR_LOG = '/dev/null'
  # PERF FIX (MKXP_VITA_REGION_INCR): Galv's Region Effects recreates every region-effect sprite
  # (up to 20) at each step on an effect region (refresh_region_effects = dispose all + create all):
  # d46, 60-90 ms every 16 frames while walking. Here only the new effect gets a new sprite and the
  # expired ones are disposed; a kept sprite is moved to the end of its z group (z changed and
  # restored: mkxp-z re-inserts it after the equal-z elements, as a new sprite is inserted), in
  # effectlist order, so the drawing order is the same as with the recreated sprites. A sprite with
  # an animation or balloon running is recreated as before. Host test: tools/hosttests/region-incr/.
  # Toggled with L+R+START.
  $vita_region_incr = true
  ok = defined?(Spriteset_Map) && Spriteset_Map.method_defined?(:refresh_region_effects) &&
       Sprite_Character.method_defined?(:character)
  File.open(VITA_REGION_INCR_LOG, 'a') { |f| f.puts "REGION_INCR #{ok ? 'INSTALLED' : 'NOT INSTALLED (no Galv Region Effects)'}" } rescue nil
  if ok
    Spriteset_Map.prepend(Module.new do
      def refresh_region_effects
        old = @region_sprites
        return super unless $vita_region_incr && old.is_a?(Array) && $game_map.effectlist && $game_map.r_events
        pool = {}
        old.each do |s|
          next if s.nil? || s.disposed? || s.animation? || s.instance_variable_get(:@balloon_sprite)
          (pool[s.character] ||= []) << s
        end
        list = []
        $game_map.effectlist.each_with_index do |id, i|
          ev = $game_map.r_events[id]
          s = (pool[ev] && pool[ev].shift)
          if s
            z = s.z
            s.z = z + 1   # re-insert at the end of the z group, as a newly created sprite
            s.z = z
            list[i] = s
          else
            list[i] = Sprite_Character.new(@viewport1, ev)
          end
        end
        kept = {}
        list.each { |s| kept[s.object_id] = true if s }
        old.each { |s| s.dispose if s && !kept[s.object_id] && !s.disposed? }
        @region_sprites = list
      end
    end)
  end
