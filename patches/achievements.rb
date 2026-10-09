# Achievements for LISA: The Painful (MKXP_VITA_ACHIEVEMENTS).
#
# The game unlocks its achievements through Steam: events call Steam::Stats.unlock(:symbol), its script
# maps the symbol to a Steam id (TO_API_ID) and calls setAchievement / getAchievementStatus /
# callbackReceived of Steam's library. On the Vita there is no Steam: those were stubs, so every unlock
# stayed pending (saved by the game in steamstat.dat). This patch answers those three calls itself:
#  - an unlock is kept in achievements.dat in the game folder (one line "ID unix_time"), shown for
#    3 seconds in a window at the top of the screen, and reported to the game as done (pending ones
#    from earlier sessions arrive the same way: the game pushes them again every 60 frames);
#  - the title screen gets an "Achievements" command: count, list, descriptions.
# The game's own logic (when, which one, never twice) is unchanged. Titles and descriptions are
# Steam's (store page of LISA, app 335670), matched to the ids by Steam's global unlock percentages.
# An error while unlocking or showing the unlock window is logged (ACHIEVEMENTS ...) and does not stop
# the game; the list scene is ordinary game UI (d104: Time#strftime raised there on the Vita).
module VitaAchievements
  ROOT = File.dirname(VITA_PATCH_LOG) + '/'
  FILE = ROOT + 'achievements.dat'
  SHOW_FRAMES = 180
  # Steam id => [title, description] ('' = hidden on Steam), in Steam's order (most unlocked first).
  INFO = {
    'ACH_TERRY' => ["Hint Guy", "Recruit Terry Hints."],
    'ACH_AREA_1' => ["Let's Find Her", "Reach the first crossroads."],
    'ACH_NERN' => ["Annoying Guy", "Recruit Nern Guan."],
    'ACH_BUZZO_MEET' => ["Embrace the Joy", "Meet Buzzo."],
    'ACH_OLAN' => ["Drunk Guy", "Recruit Olan Hyot."],
    'ACH_RAGE' => ["Headbutt Guy", "Recruit Rage Ironhead."],
    'ACH_FARDY' => ["Violated Guy", "Recruit Fardy Hernandez."],
    'ACH_BEASTBORN' => ["Wild Guy", "Recruit Beastborn."],
    'ACH_HAIRMANITY' => ["Really? Hairmanity?", "Kill the Men's Hair Club Presidents."],
    'ACH_PERCY' => ["Poop Guy", "Recruit Percy Monsoon."],
    'ACH_RANDO_MEET' => ["The Face of Blood", "Meet the Red Skull Rando"],
    'ACH_AREA_2' => ["It's All Falling Down", "Reach the second crossroads."],
    'ACH_YAZAN' => ["Cat Guy", "Recruit Yazan Barghouti."],
    'ACH_SAVE_BUDDY' => ["Happy Reunion", "Save Buddy."],
    'ACH_AREA_3' => ["No Turning Back", "Reach the third crossroads."],
    'ACH_BIRDIE' => ["Super Drunk Guy", "Recruit Birdie Hall."],
    'ACH_AJEET' => ["Poke Guy", "Recruit Ajeet Mandeep."],
    'ACH_ROOSTER' => ["Red Hair Guy", "Recruit Rooster Coleman."],
    'ACH_BYE_I_GUESS' => ["Bye, I Guess...", "Race Columbo."],
    'ACH_BUFFALO' => ["American Guy", "Recruit Buffalo Van Dyke."],
    'ACH_RANDO_LANDO' => ["The End is Nigh", "Welcome to Hell."],
    'ACH_POTTY' => ["No...", "It can't be..."],
    'ACH_GARTH' => ["Art Guy", "Recruit Garth."],
    'ACH_BURNING_MAN' => ["Hot Snow", "Reach the Snowy Peak."],
    'ACH_DOCTOR' => ["The Doctor is in the House", "Kill the doctor."],
    'ACH_MADDOG' => ["Nipple Guy", "Recruit Mad Dog."],
    'ACH_QUEEN' => ["Lady Guy", "Recruit Queen Roger."],
    'ACH_BO' => ["Weird Guy", "Recruit Bo Wyatt."],
    'ACH_DICK' => ["Party Guy", "Recruit Dick Dickson."],
    'ACH_TIGERMAN' => ["Spear Guy", "Recruit Tiger Man."],
    'ACH_JACK' => ["Magic Guy", "Recruit Jack."],
    'ACH_FISHCAVE' => ["There's Something Fishy...", "Find the Fishmen."],
    'ACH_HARVEY' => ["Lawyer Fish Guy", "Recruit Harvey Alibastor."],
    'ACH_CRISP' => ["Many Swords Guy", "Recruit Crisp Ladaddy."],
    'ACH_RT' => ["Dirt Bag Guy", "Recruit RT."],
    'ACH_GEESE' => ["Rhyming Bird Guy", "Recruit Geese Thompson."],
    'ACH_SHOCKLORD' => ["Face First Guy", "Recruit Shocklord."],
    'ACH_TAG_CHAMP' => ["Arm-Shock 4 Life!", "Win the tag titles."],
    'ACH_EWC_CHAMP' => ["The Champ is Here!", "Break the Streak, win the title."],
    'ACH_HEADSHOT' => ["Look on the Brightside", "Watch a well executed suicide."],
    'ACH_OLLIE' => ["Sweaty Guy", "Recruit Ollie Nickels."],
    'ACH_RANGERS' => ["Salvation Ends", "Witness the fall of the Salvation Rangers."],
    'ACH_FLY' => ["Bug Guy", "Recruit Fly Minetti."],
    'ACH_SONNY' => ["Powerful Clothesline Guy", "Recruit Sonny Backluwitz."],
    'ACH_CARP' => ["Fish Guy", "Recruit Carp."],
    'ACH_HOTEL_SECRET' => ["Weekend Getaway", "Find the relaxing researchers."],
    'ACH_JOYFULL_END' => ["Joyful End", ""],
    'ACH_ONE_ARMS' => ["Kind of Selfish", ""],
    'ACH_MIKE' => ["10-4 Good Buddy", "Kill Satan."],
    'ACH_JOYLESS_END' => ["Joyless End", ""],
    'ACH_CLINT' => ["Cool Dude Guy", "Recruit Clint Olympic."],
    'ACH_NO_ARMS' => ["Selfless", ""],
    'ACH_ROULETTE' => ["I Love You Son", "Kill all the Rouletters."],
    'ACH_BUCKETS' => ["Mysterious Guy", "Recruit Buckets."],
    'ACH_TWO_ARMS' => ["Selfish", ""],
    'ACH_PAIN_MODE' => ["Sorry Master", "The birth of Red Skull."],
    'ACH_CREED' => ["Worth It", "Learn the ways of the Bob Friday."],
    'ACH_HORNMAN' => ["...", "..."],
  }.freeze

  @unlocked = {}   # Steam id => unix time
  @queue = []
  @window = nil
  @timer = 0
  @tick_failed = false

  def self.log(line)
    File.open(VITA_PATCH_LOG, 'a') { |f| f.puts "ACHIEVEMENTS #{line}" }
  rescue StandardError
    nil
  end

  def self.load
    return unless File.exist?(FILE)
    File.foreach(FILE) do |l|
      id, t = l.split
      @unlocked[id] = t.to_i if id && INFO.key?(id)
    end
  rescue StandardError => e
    log("load failed: #{e.class}: #{e.message}")
  end

  # The game's achievements (its TO_API_ID), in Steam's order.
  def self.ids
    game = ::TO_API_ID.values
    INFO.keys.select { |k| game.include?(k) }
  end

  def self.unlocked?(id)
    @unlocked.key?(id.to_s)
  end

  def self.unlocked_at(id)
    @unlocked[id.to_s]
  end

  # "YYYY-MM-DD" (UTC) of a unix time, by arithmetic: on the Vita, Time#strftime of a local time
  # raises TypeError (newlib gives no time zone name), so Time's formatting is not used.
  def self.date(unix)
    z = unix.to_i.div(86_400) + 719_468            # days since 0000-03-01
    era = z.div(146_097)
    doe = z - era * 146_097
    yoe = (doe - doe / 1460 + doe / 36_524 - doe / 146_096) / 365
    doy = doe - (365 * yoe + yoe / 4 - yoe / 100)
    mp = (5 * doy + 2) / 153
    d = doy - (153 * mp + 2) / 5 + 1
    m = mp < 10 ? mp + 3 : mp - 9
    y = yoe + era * 400 + (m <= 2 ? 1 : 0)
    format('%04d-%02d-%02d', y, m, d)
  end

  # Steam's setAchievement: true = done. Written to achievements.dat first: if that fails, false
  # (the game keeps it pending and tries again later).
  def self.unlock(id)
    id = id.to_s
    return true if @unlocked.key?(id)
    unless INFO.key?(id)
      log("unknown id #{id}")
      return false
    end
    t = Time.now.to_i
    File.open(FILE, 'a') { |f| f.puts "#{id} #{t}" }
    @unlocked[id] = t
    @queue << id
    log("unlock #{id} (#{@unlocked.size}/#{ids.size})")
    true
  rescue StandardError => e
    log("unlock #{id} failed: #{e.class}: #{e.message}")
    false
  end

  # Once per frame (from the game's Steam poll in Graphics.update): the unlock window.
  def self.tick
    if @window
      @window.update
      @timer -= 1
      @window.close if @timer == 0
      if @timer <= 0 && @window.close?
        @window.dispose
        @window = nil
      end
    end
    if !@window && (id = @queue.shift)
      @window = Window_VitaAchievement.new(INFO[id][0])
      @timer = SHOW_FRAMES
    end
  rescue StandardError => e
    unless @tick_failed
      @tick_failed = true
      log("window failed: #{e.class}: #{e.message}")
    end
    @queue.clear
    (@window.dispose rescue nil) if @window
    @window = nil
  end

  def self.install
    unless defined?(::Steam::Stats) && ::Steam::Stats.respond_to?(:setAchievement) &&
           ::Steam::Stats.respond_to?(:poll) && defined?(::TO_API_ID)
      log('not installed (no Steam::Stats of LISA)')
      return
    end
    load
    st = ::Steam::Stats
    st.define_singleton_method(:setAchievement) { |id| VitaAchievements.unlock(id) }
    st.define_singleton_method(:getAchievementStatus) { |id| VitaAchievements.unlocked?(id) }
    st.define_singleton_method(:callbackReceived) { true }
    poll = st.method(:poll)
    st.define_singleton_method(:poll) do |*a|
      r = poll.call(*a)
      VitaAchievements.tick
      r
    end
    log("installed ids=#{ids.size} unlocked=#{@unlocked.size}")
  end
end

class Window_VitaAchievement < Window_Base
  def initialize(title)
    w = 360
    super((Graphics.width - w) / 2, 8, w, fitting_height(2))
    self.z = 10_000
    self.openness = 0
    change_color(system_color)
    draw_text(0, 0, contents_width, line_height, 'Achievement unlocked', 1)
    change_color(normal_color)
    draw_text(0, line_height, contents_width, line_height, title, 1)
    open
  end
end

class Window_VitaAchievementList < Window_Selectable
  def initialize(x, y, w, h)
    @ids = VitaAchievements.ids
    super
    refresh
  end

  def item_max
    @ids ? @ids.size : 0
  end

  def draw_item(index)
    id = @ids[index]
    on = VitaAchievements.unlocked?(id)
    change_color(normal_color, on)
    draw_text(item_rect_for_text(index), (on ? '+ ' : '- ') + VitaAchievements::INFO[id][0])
  end

  def update_help
    id = @ids[index]
    return unless id
    desc = VitaAchievements::INFO[id][1]
    at = VitaAchievements.unlocked_at(id)
    desc = '???' if desc.empty? && !at
    status = at ? "Unlocked #{VitaAchievements.date(at)}" : 'Locked'
    @help_window.set_text("#{desc}\n#{status}")
  end
end

class Scene_VitaAchievements < Scene_MenuBase
  def start
    super
    ids = VitaAchievements.ids
    n = ids.count { |id| VitaAchievements.unlocked?(id) }
    @count_window = Window_Base.new(0, 0, Graphics.width, 48)
    @count_window.draw_text(0, 0, @count_window.contents_width, 24, "Achievements  #{n} / #{ids.size}", 1)
    @help_window = Window_Help.new(2)
    @help_window.y = Graphics.height - @help_window.height
    @list_window = Window_VitaAchievementList.new(0, 48, Graphics.width, Graphics.height - 48 - @help_window.height)
    @list_window.help_window = @help_window
    @list_window.set_handler(:cancel, method(:return_scene))
    @list_window.select(0)
    @list_window.activate
  end
end

class Window_TitleCommand < Window_Command
  alias vita_ach_make_command_list make_command_list
  def make_command_list
    vita_ach_make_command_list
    at = @list.index { |c| c[:symbol] == :shutdown } || @list.size
    @list.insert(at, { name: 'Achievements', symbol: :vita_achievements, enabled: true, ext: nil })
  end
end

class Scene_Title < Scene_Base
  alias vita_ach_create_command_window create_command_window
  def create_command_window
    vita_ach_create_command_window
    @command_window.set_handler(:vita_achievements, method(:command_vita_achievements))
  end

  def command_vita_achievements
    close_command_window
    SceneManager.call(Scene_VitaAchievements)
  end
end

VitaAchievements.install
