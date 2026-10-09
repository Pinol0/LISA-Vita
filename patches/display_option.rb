# "Screen" in LISA's options menu (MKXP_VITA_DISPLAY_MENU): 1:1 | Original | Stretch | Wide.
#  - 1:1:       the game's 544x416 pixels, not scaled, centred;
#  - Original:  the game's proportions, scaled to the screen height (black side bars; the default);
#  - Stretch:   the same picture stretched to the whole screen (wider pixels);
#  - Wide:      real widescreen: the game draws 736x416 (23x13 tiles, about the Vita's 16:9) through
#               RGSS3's Graphics.resize_screen, so more of the map is shown. LISA was made for 544x416:
#               scenes and events placed by hand for it may look wrong (said in the help text).
#
# LISA's options menu is Yanfly's System Options (Options/Quit in the main menu, opened with Circle).
# The entry goes after "Battle Animations" and is changed with left / right like that script's own
# entries; the script itself is not changed. The mode is the port's (Graphics.vita_display_mode, kept
# in display.cfg in the game folder); Wide is applied here, before the game starts and when chosen
# (the camera is centred again on the player). qa.log: DISPLAY_OPTION ...
module VitaDisplayOption
  NAME = 'Screen'
  MODES = [2, 0, 1, 3].freeze                    # port modes: pixel, original, stretch, wide
  LABELS = ['1:1', 'Original', 'Stretch', 'Wide'].freeze
  HELP = ["1:1: the game's pixels, not scaled\n(small and sharp).",
          "Original: the game's proportions, scaled to\nthe screen height (black side bars).",
          "Stretch: the same picture stretched to the\nwhole screen (wider pixels).",
          "Wide: real widescreen, more map is shown.\nSome scenes and events may look wrong."].freeze
  WIDE = [736, 416].freeze
  NORMAL = [544, 416].freeze

  def self.log(line)
    File.open(VITA_PATCH_LOG, 'a') { |f| f.puts "DISPLAY_OPTION #{line}" }
  rescue StandardError
    nil
  end

  def self.usable?
    defined?(::Window_SystemOptions) && ::Window_SystemOptions.method_defined?(:make_command_list) &&
      ::Window_SystemOptions.method_defined?(:cursor_change) && Graphics.respond_to?(:vita_display_mode=) &&
      Graphics.respond_to?(:resize_screen)
  end

  # Index in LABELS of the port's mode (unknown: Original).
  def self.index
    MODES.index(Graphics.vita_display_mode) || 1
  end

  # The RGSS screen for the mode: 736x416 for Wide, 544x416 otherwise; the camera follows.
  def self.apply_size
    w, h = Graphics.vita_display_mode == 3 ? WIDE : NORMAL
    return if Graphics.width == w && Graphics.height == h
    ok = Graphics.resize_screen(w, h)
    $game_player.center($game_player.x, $game_player.y) if $game_map && $game_player
    log("screen #{w}x#{h} -> #{ok.inspect}")
  end

  def self.choose(i)
    Graphics.vita_display_mode = MODES[i]
    apply_size
  end
end

if VitaDisplayOption.usable?
  class Window_SystemOptions < Window_Command
    alias vita_display_make_command_list make_command_list
    def make_command_list
      vita_display_make_command_list
      at = @list.index { |c| c[:symbol] == :animations }
      at = at ? at + 1 : @list.index { |c| c[:symbol] == :to_title } || @list.size
      @list.insert(at, { name: VitaDisplayOption::NAME, symbol: :vita_screen, enabled: true, ext: nil })
      @help_descriptions[:vita_screen] = VitaDisplayOption::HELP[VitaDisplayOption.index]
    end

    alias vita_display_draw_item draw_item
    def draw_item(index)
      return vita_display_draw_item(index) unless @list[index][:symbol] == :vita_screen
      reset_font_settings
      rect = item_rect(index)
      contents.clear_rect(rect)
      third = contents.width / 5   # the name; the four options share the rest
      draw_text(0, rect.y, third, line_height, @list[index][:name], 1)
      cur = VitaDisplayOption.index
      w = (contents.width - third) / VitaDisplayOption::LABELS.size
      VitaDisplayOption::LABELS.each_with_index do |label, i|
        change_color(normal_color, i == cur)
        draw_text(third + i * w, rect.y, w, line_height, label, 1)
      end
    end

    alias vita_display_cursor_change cursor_change
    def cursor_change(direction)
      return vita_display_cursor_change(direction) unless current_symbol == :vita_screen
      cur = VitaDisplayOption.index
      nxt = direction == :right ? [cur + 1, VitaDisplayOption::MODES.size - 1].min : [cur - 1, 0].max
      return if nxt == cur
      VitaDisplayOption.choose(nxt)
      @help_descriptions[:vita_screen] = VitaDisplayOption::HELP[nxt]
      Sound.play_cursor
      draw_item(index)
      update_help
    end
  end
  VitaDisplayOption.apply_size
  VitaDisplayOption.log("installed mode=#{Graphics.vita_display_mode} screen=#{Graphics.width}x#{Graphics.height}")
else
  VitaDisplayOption.log('not installed (no Yanfly System Options of LISA, or no Graphics.vita_display_mode)')
end
