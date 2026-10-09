# Diagnostic (MKXP_VITA_SKY_SNAP), not for release: the sky of LISA's intro looks wrong on the Vita
# (no colour gradient) in some scenes. On the intro maps (26-40: Baby Found ... Brad's Cliff) the
# screen is saved as a PNG twice per map, 90 and 600 frames after entering it, with the state that
# draws the sky: parallax (file, ox/oy, bitmap size, opacity), screen tone, viewport 1 tone, display
# and parallax origins. Files: sky_mapNNN_fF.png in the game folder. qa.log: SKY_SNAP ...
module VitaSkySnap
  ROOT = File.dirname(VITA_PATCH_LOG) + '/'
  MAPS = (26..40)
  SHOTS = [90, 600]
  @map = nil
  @frames = 0
  @done = {}

  def self.log(line)
    File.open(VITA_PATCH_LOG, 'a') { |f| f.puts "SKY_SNAP #{line}" }
  rescue StandardError
    nil
  end

  def self.tone(t)
    t ? format('(%d,%d,%d,%d)', t.red, t.green, t.blue, t.gray) : '-'
  end

  def self.tick
    scene = SceneManager.scene
    return unless scene.is_a?(Scene_Map) && $game_map
    id = $game_map.map_id
    if id != @map
      @map = id
      @frames = 0
    end
    @frames += 1
    return unless MAPS.include?(id) && SHOTS.include?(@frames) && !@done[[id, @frames]]
    @done[[id, @frames]] = true
    file = format('sky_map%03d_f%d.png', id, @frames)
    shot = Graphics.snap_to_bitmap
    saved = shot.vita_save_png(ROOT + file)
    shot.dispose
    set = scene.instance_variable_get(:@spriteset)
    par = set && set.instance_variable_get(:@parallax)
    vp1 = set && set.instance_variable_get(:@viewport1)
    bmp = par && par.bitmap
    log("map=#{id} f=#{@frames} file=#{file} saved=#{saved} parallax=#{$game_map.parallax_name.inspect} " \
        "plane=#{par ? "ox=#{par.ox} oy=#{par.oy} opacity=#{par.opacity} visible=#{par.visible} z=#{par.z} " \
        "zoom=#{par.zoom_x},#{par.zoom_y} blend=#{par.blend_type} tone=#{tone(par.tone)}" : '-'} " \
        "bitmap=#{bmp ? "#{bmp.width}x#{bmp.height}" : '-'} screen_tone=#{tone($game_map.screen.tone)} " \
        "vp1_tone=#{tone(vp1 && vp1.tone)} display=#{$game_map.display_x},#{$game_map.display_y} " \
        "parallax_xy=#{$game_map.instance_variable_get(:@parallax_x)},#{$game_map.instance_variable_get(:@parallax_y)} " \
        "loop=#{$game_map.instance_variable_get(:@parallax_loop_x)},#{$game_map.instance_variable_get(:@parallax_loop_y)}")
  rescue StandardError => e
    log("failed: #{e.class}: #{e.message}")
  end
end

module Graphics
  class << self
    alias vita_sky_snap_update update
    def update(*args)
      r = vita_sky_snap_update(*args)
      VitaSkySnap.tick
      r
    end
  end
end
VitaSkySnap.log('installed')
