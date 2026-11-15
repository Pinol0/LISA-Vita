# Achievement icons (MKXP_VITA_ACH_ICONS), loaded after patches/achievements.rb.
#
# Steam's icons are not ours to ship, so each icon is cut at run time from the player's own copy of the
# game: a recruit's face (the actor named in "Recruit X." of Actors data, at its face index), or a
# battler / character sprite / picture named in ICONS below (names only, no image data here).
#  - the list becomes a grid of 96x96 icons (5 columns at 544, 7 in Wide); a locked achievement's icon
#    is faded, a locked one hidden on Steam shows "?" (endings would be spoilers); title, description
#    and date of the selected one below. Icons are cut a few per frame (no wait when the list opens).
#  - the unlock window shows the icon at 48x48 left of the text.
# Each sheet is loaded, cut into a 96x96 bitmap and disposed (nothing kept in Cache). A missing file
# or a failed cut is logged once (ACHIEVEMENTS icon ...) and leaves the cell without an icon.
module VitaAchievementIcons
  SIZE = 96
  # Steam id => [kind, file(, index of the character in an 8-character sheet)]. Not listed: recruits.
  ICONS = {
    # LISA: The Painful
    'ACH_AREA_1' => [:actor, 'Brad'],
    'ACH_BUZZO_MEET' => [:battler, 'buzzo'],
    'ACH_HAIRMANITY' => [:battler, 'Presidents (2)'],
    'ACH_RANDO_MEET' => [:battler, 'Rando'],
    'ACH_AREA_2' => [:char, '$Brad bike wheelie'],
    'ACH_SAVE_BUDDY' => [:char, '$buddy'],
    'ACH_AREA_3' => [:char, '$Bradbulldozer'],
    'ACH_BYE_I_GUESS' => [:battler, 'Columbo'],
    'ACH_RANDO_LANDO' => [:battler, 'Rando Archer'],
    'ACH_POTTY' => [:char, 'chars5', 2],
    'ACH_BURNING_MAN' => [:battler, 'Flamer'],
    'ACH_DOCTOR' => [:battler, 'Doctor'],
    'ACH_FISHCAVE' => [:char, '$fish'],
    'ACH_TAG_CHAMP' => [:battler, 'Sunset Leader'],
    'ACH_EWC_CHAMP' => [:battler, 'death'],
    'ACH_HEADSHOT' => [:battler, 'lunatic'],
    'ACH_RANGERS' => [:battler, 'Blue'],
    'ACH_HOTEL_SECRET' => [:char, '$science'],
    'ACH_JOYFULL_END' => [:char, 'Brad Joy', 0],
    'ACH_ONE_ARMS' => [:char, '$Brad bike 1arm'],
    'ACH_MIKE' => [:battler, 'mike'],
    'ACH_JOYLESS_END' => [:char, '$braddead'],
    'ACH_NO_ARMS' => [:char, '$Brad bike 0arm'],
    'ACH_ROULETTE' => [:char, '$gang2'],
    'ACH_TWO_ARMS' => [:char, '$Brad bike'],
    'ACH_PAIN_MODE' => [:char, '$scout'],
    'ACH_CREED' => [:char, '$Brad Jump 0arm'],
    'ACH_HORNMAN' => [:char, '$braddiearms'],
    # LISA: The Joyful
    'ACH_RANDO_MASK' => [:char, '$randobuddyface'],
    'ACH_HINT_MASK' => [:char, '$buddyjumphint'],
    'ACH_JOY_MASK' => [:char, '$buddyjump joy'],
    'ACH_YADO' => [:battler, 'Yado'],
    'ACH_W2D' => [:char, '$w2d'],
    'ACH_BUZZO_DONE' => [:battler, 'buzzo'],
    'ACH_LISA' => [:picture, 'lisa small'],
    'ACH_BRAD' => [:battler, 'brad'],
    'ACH_MARTY' => [:battler, 'Marty3'],
    'ACH_GREEN' => [:char, '$science'],
  }.freeze

  @failed = {}

  # [kind, file, index] of an achievement, or nil.
  def self.source(id)
    return ICONS[id] if ICONS.key?(id)
    desc = VitaAchievements::INFO[id] ? VitaAchievements::INFO[id][1] : ''
    return nil unless desc =~ /\ARecruit (.+?)\.?\z/
    name = Regexp.last_match(1)
    actor = ($data_actors || []).find { |a| a && !a.name.empty? && (name == a.name || name.start_with?(a.name + ' ')) }
    actor ? [:face, actor.face_name, actor.face_index] : nil
  end

  # A new 96x96 bitmap with the icon (the caller disposes it), or nil.
  def self.bitmap(id)
    src = source(id)
    src = [:face, *actor_face(src[1])] if src && src[0] == :actor
    return nil unless src && src[1]
    kind, file, index = src
    sheet = Bitmap.new({ face: 'Graphics/Faces/', battler: 'Graphics/Battlers/', char: 'Graphics/Characters/',
                         picture: 'Graphics/Pictures/' }.fetch(kind) + file)
    begin
      cut(kind, file, index.to_i, sheet)
    ensure
      sheet.dispose
    end
  rescue StandardError => e
    unless @failed[id]
      @failed[id] = true
      VitaAchievements.log("icon #{id} #{src.inspect} failed: #{e.class}: #{e.message}")
    end
    nil
  end

  def self.actor_face(name)
    a = ($data_actors || []).find { |x| x && x.name == name }
    a ? [a.face_name, a.face_index] : [nil, 0]
  end

  def self.cut(kind, file, index, sheet)
    out = Bitmap.new(SIZE, SIZE)
    case kind
    when :face      # 4x2 faces of 96x96
      out.blt(0, 0, sheet, Rect.new(index % 4 * SIZE, index / 4 * SIZE, SIZE, SIZE))
    when :battler
      fit(out, sheet, Rect.new(0, 0, sheet.width, sheet.height))
    when :char      # the middle frame facing down; "$" = one character, else 4x2 characters
      if file.include?('$')
        cw, ch, bx, by = sheet.width / 3, sheet.height / 4, 0, 0
      else
        cw, ch = sheet.width / 12, sheet.height / 8
        bx, by = index % 4 * 3 * cw, index / 4 * 4 * ch
      end
      fit(out, sheet, Rect.new(bx + cw, by, cw, ch))
    when :picture   # a title text: on a light card
      h = [sheet.height + 12, SIZE].min
      out.fill_rect(2, (SIZE - h) / 2, SIZE - 4, h, Color.new(225, 215, 190))
      out.blt((SIZE - sheet.width) / 2, (SIZE - sheet.height) / 2, sheet, Rect.new(0, 0, sheet.width, sheet.height))
    end
    out
  end

  # Centred in the 96x96 box: scaled down to fit, or up by a whole factor (pixel art stays sharp).
  def self.fit(out, sheet, r)
    s = [SIZE.to_f / r.width, SIZE.to_f / r.height].min
    s = s.floor if s >= 1
    w = [(r.width * s).to_i, 1].max
    h = [(r.height * s).to_i, 1].max
    out.stretch_blt(Rect.new((SIZE - w) / 2, (SIZE - h) / 2, w, h), sheet, r)
  end
end

class Window_VitaAchievementList < Window_Selectable
  CELL_W = 98    # least cell width: 5 columns of 100 at 544, 7 of 98 at 736
  CELL_H = 100

  def col_max
    [(width - standard_padding * 2 + spacing) / (CELL_W + spacing), 1].max
  end

  def spacing
    4
  end

  def item_height
    CELL_H
  end

  def refresh
    contents.clear
    @todo = []
    item_max.times do |i|
      r = item_rect(i)
      contents.fill_rect(r.x + 1, r.y + 1, r.width - 2, r.height - 2, Color.new(0, 0, 0, 96))
      @todo << i
    end
  end

  def update
    super
    3.times { (i = @todo.shift) ? draw_item(i) : break } if @todo && !@todo.empty?
  end

  def draw_item(index)
    id = @ids[index]
    r = item_rect(index)
    x = r.x + (r.width - VitaAchievementIcons::SIZE) / 2
    y = r.y + (r.height - VitaAchievementIcons::SIZE) / 2
    on = VitaAchievements.unlocked?(id)
    if !on && VitaAchievements::INFO[id][1].empty?
      contents.font.size = 48
      change_color(normal_color, false)
      draw_text(r, '?', 1)
      reset_font_settings
      return
    end
    icon = VitaAchievementIcons.bitmap(id)
    if icon
      contents.blt(x, y, icon, Rect.new(0, 0, icon.width, icon.height), on ? 255 : 64)
      icon.dispose
    else
      change_color(normal_color, on)
      draw_text(r, VitaAchievements::INFO[id][0][0, 1], 1)
    end
  end

  def update_help
    id = @ids[index]
    return unless id
    title, desc = VitaAchievements::INFO[id]
    at = VitaAchievements.unlocked_at(id)
    desc = '???' if desc.empty? && !at
    status = at ? "Unlocked #{VitaAchievements.date(at)}" : 'Locked'
    @help_window.set_text("#{title}  (#{status})\n#{desc}")
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
    room = @help_window.y - 48
    rows = [(room - 24) / Window_VitaAchievementList::CELL_H, 1].max
    h = rows * Window_VitaAchievementList::CELL_H + 24
    @list_window = Window_VitaAchievementList.new(0, 48 + (room - h) / 2, Graphics.width, h)
    @list_window.help_window = @help_window
    @list_window.set_handler(:cancel, method(:return_scene))
    @list_window.select(0)
    @list_window.activate
  end
end

class Window_VitaAchievement < Window_Base
  ICON = 48

  # achievements.rb passes the title only: the id is the game's achievement with that title (unique).
  def initialize(title)
    id = VitaAchievements.ids.find { |k| VitaAchievements::INFO[k][0] == title }
    w = 400
    super((Graphics.width - w) / 2, 8, w, fitting_height(2))
    self.z = 10_000
    self.openness = 0
    icon = id && VitaAchievementIcons.bitmap(id)
    tx = 0
    if icon
      contents.stretch_blt(Rect.new(0, (contents.height - ICON) / 2, ICON, ICON), icon, Rect.new(0, 0, icon.width, icon.height))
      icon.dispose
      tx = ICON + 8
    end
    change_color(system_color)
    draw_text(tx, 0, contents_width - tx, line_height, 'Achievement unlocked', 1)
    change_color(normal_color)
    draw_text(tx, line_height, contents_width - tx, line_height, title, 1)
    open
  end
end

