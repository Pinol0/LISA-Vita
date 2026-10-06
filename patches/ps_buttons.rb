# PlayStation button icons for LISA's combo skills (the PC game shows the keys W A S D).
#
# Ships only the port's own drawings (ps_buttons_overlay.png: triangle, square, cross, circle in the
# colours of the letters they replace) and this code; the player's game files are not changed.
#  - IconSet: when the game loads Graphics/System/Iconset, the 28 key-letter icons (W A S D in 7
#    colour sets) are replaced by the overlay's. W -> triangle, A -> square, S -> cross, D -> circle,
#    as the Vita port maps the buttons (R = W, X = A, Y = S, Z = D). Q is kept.
#  - Skill descriptions: the key letters at their start, e.g. "(WSS) A blast of flaming emotion.",
#    become the icons of the buttons, in the colours of the fighting style that owns the combo
#    (notetags <combo skill R/X/Y/Z: id> and <combo special RYY: id>). Three descriptions of the
#    original game differ from the sequence that really triggers the combo (Running Typhoon, Muscle
#    Call, Hell's Call): they show the real one.
# qa.log: PS_BUTTONS iconset=... / PS_BUTTONS skills=... corrected=...
module VitaPsButtons
  OVERLAY = VITA_PATCH_DIR + 'ps_buttons_overlay'
  SLOTS = [[4, [8, 9, 10, 11]], [16, [3, 4, 5, 6]], [16, [8, 9, 10, 11]], [16, [12, 13, 14, 15]],
           [17, [3, 4, 5, 6]], [17, [8, 9, 10, 11]], [17, [12, 13, 14, 15]]]
  LETTER = { 'W' => 'R', 'A' => 'X', 'S' => 'Y', 'D' => 'Z' }
  DEFAULT = { 'W' => 72, 'A' => 73, 'S' => 74, 'D' => 75 }

  def self.log(line)
    File.open(VITA_PATCH_LOG, 'a') { |f| f.puts "PS_BUTTONS #{line}" }
  rescue StandardError
    nil
  end

  def self.patch_iconset(bmp)
    return :size if bmp.width != 384 || bmp.height != 432   # not LISA's IconSet layout: leave it
    overlay = Bitmap.new(OVERLAY)
    n = 0
    SLOTS.each do |row, cols|
      cols.each do |col|
        rect = Rect.new(col * 24, row * 24, 24, 24)
        bmp.clear_rect(rect)
        bmp.blt(rect.x, rect.y, overlay, rect)
        n += 1
      end
    end
    overlay.dispose
    n
  end

  # [new descriptions by skill id, corrected count] for the skills array (RPG::Skill or nil entries).
  def self.skill_descriptions(skills)
    owners = {}
    skills.each do |p|
      next unless p
      note = p.note.to_s
      buttons = {}
      note.scan(/<combo skill ([LRXYZ]): *(\d+)>/i) do |b, id|
        s = skills[id.to_i]
        buttons[b.upcase] = s.icon_index if s
      end
      note.scan(/<combo special (.*?): *(\d+)>/i) { |seq, id| (owners[id.to_i] ||= []) << [buttons, seq.upcase] }
    end
    out = {}
    corrected = 0
    skills.each do |s|
      next unless s
      d = s.description.to_s
      m = d.match(/\A\(([WASD]+)\)/) or next
      letters = m[1].chars
      seq = letters.map { |l| LETTER[l] }.join
      all = owners[s.id] || []
      own = all.find { |_, sq| sq == seq }
      buttons = (own || all.first || [{}])[0]
      unless own
        seqs = all.map { |_, sq| sq }.uniq
        if seqs.size == 1 && seqs[0] =~ /\A[RXYZ]+\z/
          letters = seqs[0].tr('RXYZ', 'WASD').chars
          corrected += 1
        end
      end
      icons = letters.map { |l| buttons[LETTER[l]] || DEFAULT[l] }
      out[s.id] = ('(' + icons.map { |i| "\\I[#{i}]" }.join + ')' + d[m[0].size..-1]).force_encoding(d.encoding)
    end
    [out, corrected]
  end

  def self.patch_skills
    return unless $data_skills
    out, corrected = skill_descriptions($data_skills)
    out.each { |id, text| $data_skills[id].description = text }
    log "skills=#{out.size} corrected=#{corrected}"
  end
end

class << Cache
  prepend(Module.new do
    def system(filename)
      bmp = super
      if filename.to_s.downcase == 'iconset' && !bmp.disposed? && !bmp.instance_variable_get(:@vita_ps_buttons)
        bmp.instance_variable_set(:@vita_ps_buttons, true)
        r = (VitaPsButtons.patch_iconset(bmp) rescue $!)
        VitaPsButtons.log "iconset=#{r.is_a?(Exception) ? "#{r.class}: #{r.message}" : r}"
      end
      bmp
    end
  end)
end

class << DataManager
  prepend(Module.new do
    def load_database
      r = super
      begin
        VitaPsButtons.patch_skills
      rescue StandardError => e   # only the patch: errors of the game's own loading are not caught
        VitaPsButtons.log "skills failed #{e.class}: #{e.message}"
      end
      r
    end
  end)
end
