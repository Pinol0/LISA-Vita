  # PERF FIX (MKXP_VITA_EVENT_FAST): exact fast path of Game_Event#update for still events.
  # d41: 151 events on map 11 cost 12.9 ms/frame, 138 of them fixed decorations. For an event that
  # is not moving/jumping, not route-forced, move_type 0, not autorun/parallel, no step anime and
  # resting on its original pattern with anime_count 0, the whole update chain of the stock VX Ace
  # scripts (update_animation, update_stop, update_self_movement, check_event_trigger_auto,
  # interpreter) only does `@stop_count += 1 unless @locked`. Installed only if those methods are
  # still the stock ones (owner check); host test against LISA's scripts: tools/hosttests/event-fast/.
  # Toggled together with the off-screen sprite skip (L+R+START).
  $vita_event_fast = true
  $vita_ev_fast_n = 0
  ok = [[Game_Event, :update, Game_Event], [Game_Event, :update_stop, Game_Event],
        [Game_Event, :check_event_trigger_auto, Game_Event], [Game_Event, :update_self_movement, Game_Event],
        [Game_Character, :update, Game_CharacterBase], [Game_Character, :update_stop, Game_Character],
        [Game_CharacterBase, :update_stop, Game_CharacterBase], [Game_CharacterBase, :update_animation, Game_CharacterBase],
        [Game_CharacterBase, :update_anime_count, Game_CharacterBase]].all? do |c, m, owner|
    # Skip modules prepended in front of the method (our profiler/probe hooks: RGSS3 scripts are
    # Ruby 1.9 code and cannot prepend). d43: the profiler's Game_Event#update module failed the check.
    um = (c.instance_method(m) rescue nil)
    um = um.super_method while um && !um.owner.is_a?(Class)
    um && um.owner == owner
  end
  File.open(VITA_EVENT_FAST_LOG, 'a') { |f| f.puts "EVENT_FAST #{ok ? 'INSTALLED' : 'NOT INSTALLED (scripts override the update chain)'}" } rescue nil
  if ok
    Game_Event.prepend(Module.new do
      def update
        if $vita_event_fast && @interpreter.nil? && @trigger != 3 && !@move_route_forcing && @move_type == 0 &&
           @jump_count == 0 && @real_x == @x && @real_y == @y && !@step_anime && @pattern == @original_pattern &&
           @anime_count == 0
          @stop_count += 1 unless @locked
          $vita_ev_fast_n += 1
          return
        end
        super
      end
    end)
  end
