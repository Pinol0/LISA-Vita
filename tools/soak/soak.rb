# DEBUG ONLY (MKXP_VITA_DEBUG_SOAK): resource-churn soak test, active only when R is held at boot.
# New game (no save, no checkpoint), then forever: map transfer x3, main menu, battle, over 18 maps
# of the first areas (tilesets, parallaxes, BGMs) and 8 troops, picked from the map data (code 201
# transfer targets reachable from map 41, code 301 troops). Messages/battles advance by pulsing C.
# Menu and battle are opened exactly as the game does (Scene_Map#call_menu, Game_Interpreter#command_301).
# Saving, quitting, game over and title are neutralised and logged: soak never writes save files.
module VitaSoak
  ROUTE = [[41, 0, 10], [42, 38, 22], [29, 5, 10], [45, 7, 10], [48, 27, 1], [50, 5, 13],
           [55, 1, 27], [57, 6, 9], [103, 27, 17], [80, 4, 10], [58, 7, 10], [59, 4, 10],
           [305, 25, 10], [77, 52, 15], [60, 1, 23], [11, 96, 16], [280, 3, 9], [13, 9, 10]]
  TROOPS = [3, 4, 5, 12, 1, 22, 13, 15]
  # Turbo timings (user request: answers in 15-20 min of hardware time, not 1-2 h): one loop of the
  # 18 maps in ~2.5 min instead of ~9. Same work per step (map load, battle, menu), just closer.
  STEP_FRAMES = 120      # 2 s on each map
  MENU_FRAMES = 60
  BATTLE_FRAMES = 300    # then the enemies are knocked out (normal victory)
  PULSE = 20
  LOG_PATH = VITA_SOAK_ROOT + 'soak.log'
  @active = false
  @step = 0
  @frames = 0
  @map_i = 0
  @troop_i = 0
  class << self
    attr_accessor :active, :frames
    attr_reader :step
  end

  # A failed write is reported on $stdout (stdout.log, opened at boot): d35 soak, every File.open
  # failed silently after ~6 h and the run looked stuck.
  def self.log(msg)
    line = "f=#{Graphics.frame_count} step=#{@step} #{msg}"
    begin
      File.open(LOG_PATH, 'a') { |f| f.puts line }
    rescue Exception => e
      @log_failures = (@log_failures || 0) + 1
      if @log_failures <= 3 || (@log_failures % 100).zero?
        $stdout.puts "SOAK_LOG_FAILED ##{@log_failures} #{e.class}: #{e.message} | #{line}"
        $stdout.flush rescue nil
      end
    end
    vita_trace("SOAK #{msg}") rescue nil
  end

  def self.ledger(tag)
    vita_gl_ledger_dump("soak step=#{@step} #{tag}") if respond_to?(:vita_gl_ledger_dump, true)
  end

  def self.start_new_game
    DataManager.setup_new_game
    m = ROUTE[0]
    $game_map.setup(m[0])
    $game_player.moveto(m[1], m[2])
    $game_player.refresh
    $game_map.autoplay
    log("START map=#{m[0]}")
  end

  def self.unblock_interpreter
    return unless $game_map.interpreter.running?
    $game_map.interpreter.clear
    $game_message.clear
    log('INTERPRETER_CLEARED')
  end

  # Called from Scene_Map#update once per frame. An exception here is logged, never fatal.
  def self.map_tick(scene)
    map_tick_inner(scene)
  rescue Exception => e
    @frames = 0
    log("SOAK_ERROR #{e.class}: #{e.message.tr("\n", ' ')} @ #{(e.backtrace || []).first(3).join(' | ')}")
  end

  def self.map_tick_inner(scene)
    @frames += 1
    log("ALIVE map=#{$game_map.map_id} scene=#{scene.class}") if (Graphics.frame_count % 3600).zero?
    return if @frames < STEP_FRAMES
    if $game_player.transfer?
      # Watchdog: a reserved transfer should complete within a few frames of the step.
      log("TRANSFER_PENDING #{@frames} frames map=#{$game_map.map_id}") if @frames == STEP_FRAMES + 600
      return
    end
    @frames = 0
    @step += 1
    ledger("before action=#{@step % 5}")
    case @step % 5
    when 2
      log("MENU from map=#{$game_map.map_id}")
      scene.send(:call_menu)   # as the game: SceneManager.call + Window_MenuCommand::init_command_position
    when 4
      troop = TROOPS[@troop_i % TROOPS.size]
      @troop_i += 1
      return log("BATTLE_SKIPPED troop=#{troop} (no such troop)") unless $data_troops[troop]
      unblock_interpreter
      $game_party.members.each(&:recover_all)
      log("BATTLE troop=#{troop} map=#{$game_map.map_id}")
      BattleManager.setup(troop, true, true)
      $game_player.make_encounter_count
      SceneManager.call(Scene_Battle)
    else
      @map_i += 1
      m = ROUTE[@map_i % ROUTE.size]
      loop_census if (@map_i % ROUTE.size).zero?
      unblock_interpreter
      log("TRANSFER map=#{m[0]} x=#{m[1]} y=#{m[2]}")
      $game_player.reserve_transfer(m[0], m[1], m[2], 2)
    end
  end

  def self.io_name(io)
    io.respond_to?(:path) && io.path ? io.path : "fd#{io.fileno}"
  rescue Exception
    '?'
  end

  # Once per route loop: Ruby object counts by type and the newlib heap ledger (per-caller live bytes).
  def self.loop_census
    c = ObjectSpace.count_objects
    log("LOOP #{@map_i / ROUTE.size} objects total=#{c[:TOTAL] - c[:FREE]} string=#{c[:T_STRING]} array=#{c[:T_ARRAY]} " \
        "hash=#{c[:T_HASH]} object=#{c[:T_OBJECT]} data=#{c[:T_DATA]} struct=#{c[:T_STRUCT]} gc=#{GC.count}")
    # Live objects per class (one pass over the heap), top 20 with the change since the previous loop.
    per = Hash.new(0)
    ObjectSpace.each_object { |o| per[(o.class rescue BasicObject)] += 1 }
    prev = @class_prev || {}
    top = per.sort_by { |k, v| -v }.first(20)
    log("LOOP #{@map_i / ROUTE.size} classes " + top.map { |k, v| "#{k}=#{v}(#{v - prev.fetch(k, v)})" }.join(' '))
    growers = per.map { |k, v| [k, v - prev.fetch(k, 0)] }.select { |k, d| d > 0 && prev.key?(k) }.sort_by { |k, d| -d }.first(10)
    log("LOOP #{@map_i / ROUTE.size} growers " + growers.map { |k, d| "#{k}+#{d}" }.join(' ')) unless growers.empty?
    @class_prev = per
    # Open Ruby IO objects (file descriptors held by the game scripts), by path.
    ios = ObjectSpace.each_object(IO).reject { |io| io.closed? rescue true }
    paths = Hash.new(0)
    ios.each { |io| paths[io_name(io)] += 1 }
    log("LOOP #{@map_i / ROUTE.size} open_io=#{ios.size} " + paths.sort_by { |k, v| -v }.first(8).map { |k, v| "#{k}=#{v}" }.join(' '))
    vita_heap_ledger_dump("loop #{@map_i / ROUTE.size}") if respond_to?(:vita_heap_ledger_dump, true)
  rescue Exception => e
    log("LOOP_CENSUS_ERROR #{e.class}: #{e.message}")
  end

  # C is pulsed in battles and while a message is shown. A message window can stay open after the
  # interpreter was cleared by a forced transfer ($game_message no longer busy): pulse it too.
  def self.message_window_open?
    mw = SceneManager.scene && SceneManager.scene.instance_variable_get(:@message_window)
    mw && !mw.disposed? && mw.openness > 0
  rescue Exception
    false
  end

  def self.pulse?
    @active && (Graphics.frame_count % PULSE).zero? &&
      ($game_message.busy? || SceneManager.scene_is?(Scene_Battle) || message_window_open?)
  end

  # Unattended runs: tell the system the console is in use (no automatic standby / screen off).
  def self.keep_awake
    vita_power_tick if @active && (Graphics.frame_count % 60).zero? && respond_to?(:vita_power_tick, true)
  end

  # LISA battles wait for the Yanfly Input Combo keys (L R X Y Z) after "execute": in battle, one
  # of them is pulsed every 10 frames (offset from the C pulse), cycling through the five.
  COMBO_KEYS = { L: 17, R: 18, X: 14, Y: 15, Z: 16 }
  def self.combo_pulse?(key)
    return false unless @active && SceneManager.scene_is?(Scene_Battle)
    f = Graphics.frame_count
    return false unless f % 10 == 5
    sym = COMBO_KEYS.keys[(f / 10) % COMBO_KEYS.size]
    key == sym || key == COMBO_KEYS[sym]
  end
end

class << SceneManager
  alias vita_soak_first_scene_class first_scene_class
  def first_scene_class
    Input.update
    if Input.press?(:R)
      VitaSoak.active = true
      $vita_debug_checkpoint_done = true   # never record the debug checkpoint during a soak
      VitaSoak.log('SOAK_ENABLED (R held at boot)')
      VitaSoak.start_new_game
      return Scene_Map
    end
    vita_soak_first_scene_class
  end

  alias vita_soak_exit exit
  def exit
    return vita_soak_exit unless VitaSoak.active
    VitaSoak.log('EXIT_BLOCKED')
    goto(Scene_Map)
  end
end

class << Input
  alias vita_soak_trigger trigger?
  def trigger?(key)
    return true if (key == :C || key == 13) && VitaSoak.pulse?
    return true if VitaSoak.combo_pulse?(key)
    vita_soak_trigger(key)
  end
end

class << DataManager
  alias vita_soak_save_game save_game
  def save_game(index)
    return vita_soak_save_game(index) unless VitaSoak.active
    VitaSoak.log("SAVE_BLOCKED slot=#{index}")
    false
  end
end

module VitaSoakMap
  def update
    VitaSoak.keep_awake
    VitaSoak.map_tick(self) if VitaSoak.active
    super
  end
end
Scene_Map.prepend(VitaSoakMap)

module VitaSoakMenu
  def update
    VitaSoak.keep_awake
    super
    return unless VitaSoak.active
    VitaSoak.frames += 1
    if VitaSoak.frames >= VitaSoak::MENU_FRAMES
      VitaSoak.frames = 0
      VitaSoak.log('MENU_RETURN')
      return_scene
    end
  end
end
Scene_Menu.prepend(VitaSoakMenu)

module VitaSoakBattle
  # Elapsed time from Graphics.frame_count: LISA's battle waits (combo window, effects) loop on
  # update_basic, so counting Scene_Battle#update calls under-measured (d20b: 10000-frame battles).
  def start
    @vita_soak_t0 = Graphics.frame_count
    @vita_soak_won = false
    @vita_soak_waiting_logged = false
    VitaSoak.frames = 0 if VitaSoak.active
    super
  end

  # The timeout ends the battle the way a player does: by winning. In a safe state (out of the
  # turn, no message, combo window closed) every enemy is knocked out with the game's own die, then
  # the pulsed inputs drive the next turn to the normal victory (d22-d24: a forced escape via
  # process_abort left a LISA sprite on the disposed battle viewport -> "disposed viewport", the
  # same RGSSError mkxp-z raises upstream). Nothing is forced after that; a stuck battle is logged.
  def vita_soak_safe?
    return false if BattleManager.in_turn? || BattleManager.aborting? || $game_message.busy?
    combo = instance_variable_get(:@input_combo_skill_window)
    return false if combo && !combo.disposed? && combo.visible
    true
  end

  def update
    VitaSoak.keep_awake
    super
    return unless VitaSoak.active
    elapsed = Graphics.frame_count - @vita_soak_t0
    if !@vita_soak_won && elapsed >= VitaSoak::BATTLE_FRAMES && vita_soak_safe?
      @vita_soak_won = true
      $game_troop.members.each { |e| e.die if e.alive? }
      VitaSoak.log("BATTLE_KO_ENEMIES (timeout, #{elapsed} frames, safe state)")
    elsif elapsed >= VitaSoak::BATTLE_FRAMES * 3 && !@vita_soak_waiting_logged
      @vita_soak_waiting_logged = true
      VitaSoak.log("BATTLE_STILL_RUNNING (#{elapsed} frames, won=#{!!@vita_soak_won})")
    end
  end

  def terminate
    super
    return unless VitaSoak.active
    VitaSoak.frames = 0
    $game_party.members.each(&:recover_all)
    VitaSoak.log('BATTLE_END')
    VitaSoak.ledger('after battle')
  end
end
Scene_Battle.prepend(VitaSoakBattle)

[Scene_Save, Scene_Load].each do |klass|
  klass.prepend(Module.new do
    def start
      super
      return unless VitaSoak.active
      VitaSoak.log("#{self.class}_BLOCKED")
      return_scene
    end
  end)
end

module VitaSoakGameover
  def start
    return super unless VitaSoak.active
    super
    VitaSoak.log('GAMEOVER_SKIPPED')
    $game_party.members.each(&:recover_all)
    SceneManager.goto(Scene_Map)
  end
end
Scene_Gameover.prepend(VitaSoakGameover)

module VitaSoakTitle
  def start
    return super unless VitaSoak.active
    super
    VitaSoak.log('TITLE_SKIPPED: new game again')
    VitaSoak.start_new_game
    SceneManager.goto(Scene_Map)
  end
end
Scene_Title.prepend(VitaSoakTitle)
