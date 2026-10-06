  VITA_COMBO_LOG = '/dev/null'
  # PERF FIX (MKXP_VITA_COMBO_FAST): LISA's input-combo list (Window_ComboSkillList, Yanfly Input
  # Combo Skills) is redrawn from scratch at every hp=/mp=/tp= of the actor while it is shown
  # (d60: 174 redraws x ~19 ms in a few battles, one dropped frame each). What it draws depends only
  # on: battler, skill, the combo skills (id, name, icon), whether each is usable now, the contents
  # bitmap and its size, the windowskin, line_height and the Font defaults. When all of these are
  # the same as at the last real redraw, the redraw would produce the same pixels: skipped. Only
  # this window draws into its contents. Installed only if the class has the expected methods.
  $vita_combo_fast = true
  $vita_combo_skip = 0
  ok = defined?(Window_ComboSkillList) &&
       (Window_ComboSkillList.instance_methods(false) + Window_ComboSkillList.private_instance_methods(false)).sort ==
         [:draw_background_colour, :draw_combo_skills, :draw_combo_title, :draw_horz_line, :initialize, :refresh, :refresh_check, :reveal, :text_setting].sort
  File.open(VITA_COMBO_LOG, 'a') { |f| f.puts "COMBO_FAST #{ok ? 'INSTALLED' : 'NOT INSTALLED'}" } rescue nil
  if ok
    Window_ComboSkillList.prepend(Module.new do
      def vita_combo_snapshot
        col = ->(c) { c ? [c.red, c.green, c.blue, c.alpha] : nil }
        usable = [:L, :R, :X, :Y, :Z].map do |b|
          id = @skill && @skill.combo_skill[b]
          sk = id && $data_skills[id]
          sk ? [id, sk.name.dup, sk.icon_index, (@battler ? @battler.usable?(sk) : nil)] : nil
        end
        [@battler.object_id, @skill.object_id, (@combo_skills || []).map { |k| k.id }, usable,
         contents.object_id, contents.width, contents.height, windowskin.object_id, line_height,
         (Font.default_name.dup rescue nil), Font.default_size, Font.default_bold, Font.default_italic,
         Font.default_shadow, Font.default_outline, col.call(Font.default_color), col.call(Font.default_out_color)]
      end
      def refresh
        return super unless $vita_combo_fast
        snap = vita_combo_snapshot
        if @vita_combo_snap == snap && !contents.disposed?
          $vita_combo_skip += 1
          return
        end
        super
        @vita_combo_snap = vita_combo_snapshot
      end
    end)
  end
