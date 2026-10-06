  VITA_MR_LOG = '/dev/null'
  # PERF FIX (MKXP_VITA_MAP_REFRESH_FAST): exact shortcut of Game_Map#refresh. Writing any switch or
  # variable (even the same value) sets need_refresh, and LISA's parallel events write every frame:
  # d52, every event recomputed its page every frame (pages.reverse.find + conditions_met?). The
  # page of an event depends only on the switches, variables, self switches, items and party members
  # named in its pages' conditions; if those values are the same as at the last full refresh (same
  # map, same events, same Galv region events), every event with a page would get the same page
  # again, so only those are skipped. Everything else runs as in the full refresh: events without a
  # page or erased (setup_page(nil) each time), common events, refresh_tile_events, need_refresh.
  # Installed only if the refresh chain is the stock one (+ Galv Region Effects). Host test:
  # tools/hosttests/map-refresh/. Toggled with L+R+START.
  $vita_map_refresh_fast = true
  $vita_mr_full = 0
  $vita_mr_skip = 0
  owner_of = lambda do |c, m|
    um = (c.instance_method(m) rescue nil)
    um = um.super_method while um && !um.owner.is_a?(Class)
    um && um.owner
  end
  names = (Game_Map.instance_methods(false) + Game_Map.private_instance_methods(false)).grep(/refresh/).sort
  galv = names.include?(:galv_region_effects_gm_refresh)
  expected = [:need_refresh, :need_refresh=, :refresh, :refresh_tile_events]
  expected = (expected + [:galv_region_effects_gm_refresh]).sort if galv
  ok = names == expected.sort &&
       [[Game_Event, :refresh], [Game_Event, :find_proper_page], [Game_Event, :conditions_met?], [Game_Event, :setup_page],
        [Game_CommonEvent, :refresh]].all? { |c, m| owner_of.call(c, m) == c } &&
       (Game_Event.instance_methods(false) + Game_Event.private_instance_methods(false)).grep(/refresh|conditions_met|proper_page/).sort ==
         [:conditions_met?, :find_proper_page, :refresh]
  File.open(VITA_MR_LOG, 'a') { |f| f.puts "MAP_REFRESH_FAST #{ok ? 'INSTALLED' : 'NOT INSTALLED'} galv=#{galv} names=#{names.inspect}" } rescue nil
  if ok
    module VitaMapRefresh
      # Ids named in the conditions of every page of these events.
      def self.deps(events)
        sw = {}; vr = {}; ss = {}; it = {}; ac = {}
        events.each do |e|
          ev = e.instance_variable_get(:@event)
          mid = e.instance_variable_get(:@map_id)
          next unless ev
          ev.pages.each do |pg|
            c = pg.condition
            sw[c.switch1_id] = true if c.switch1_valid
            sw[c.switch2_id] = true if c.switch2_valid
            vr[c.variable_id] = true if c.variable_valid
            ss[[mid, ev.id, c.self_switch_ch]] = true if c.self_switch_valid
            it[c.item_id] = true if c.item_valid
            ac[c.actor_id] = true if c.actor_valid
          end
        end
        [sw.keys, vr.keys, ss.keys, it.keys, ac.keys]
      end
      # Actors read without $game_actors[] (which creates the actor): one not created yet cannot be a member.
      def self.values(d)
        sw, vr, ss, it, ac = d
        [sw.map { |i| $game_switches[i] }, vr.map { |i| $game_variables[i] }, ss.map { |k| $game_self_switches[k] },
         it.map { |i| $game_party.has_item?($data_items[i]) }, ac.map { |i| a = $game_actors.instance_variable_get(:@data)[i]; a ? $game_party.members.include?(a) : false }]
      end
    end
    Game_Map.prepend(Module.new do
      def refresh
        r = instance_variable_get(:@r_events)
        snap = @vita_mr_snap
        if $vita_map_refresh_fast && snap && snap[0] == @map_id && snap[1].equal?(@events) && snap[2] == @events.size &&
           snap[3] == (r ? r.keys : nil) && VitaMapRefresh.values(snap[4]) == snap[5]
          r.each_value { |e| e.refresh if e.instance_variable_get(:@erased) || !e.instance_variable_get(:@page) } if r
          @events.each_value { |e| e.refresh if e.instance_variable_get(:@erased) || !e.instance_variable_get(:@page) }
          @common_events.each { |e| e.refresh }
          refresh_tile_events
          @need_refresh = false
          $vita_mr_skip += 1
          return
        end
        res = super
        evs = @events.values
        evs += r.values if r
        d = VitaMapRefresh.deps(evs)
        @vita_mr_snap = [@map_id, @events, @events.size, (r ? r.keys : nil), d, VitaMapRefresh.values(d)]
        $vita_mr_full += 1
        res
      end
    end)
  end
