  VITA_MOG_LOG = '/dev/null'
  # FIX (MKXP_VITA_MOG_CAP): MOG Anti Animation Lag keeps every animation bitmap that finished
  # playing (a garbage list) until the scene changes, so it can be reused without reloading. In long
  # battles that filled the vitaGL pools (d61: 4.4 MiB sheets, 4-7 MiB free in every pool) and the
  # next textures spilled into the newlib heap. Here MOG keeps its behaviour while there is room;
  # when an animation ends and less than VITA_MOG_MIN_KB is free in the texture pools, the bitmaps of
  # that list that no animation uses now (Sprite_Base's own reference count is 0) are disposed and
  # dropped from the list. Cache reloads a disposed bitmap on its next use. Pixels on screen are the
  # same; only a repeated animation may be loaded again. Host test: tools/hosttests/mog-cap/.
  VITA_MOG_MIN_KB = 24 * 1024
  $vita_mog_cap = true
  $vita_mog_trimmed = 0
  ok = defined?(Game_Temp) && Game_Temp.method_defined?(:animation_garbage) &&
       Sprite_Base.class_variable_defined?(:@@_reference_count) &&
       (Sprite_Base.method_defined?(:execute_animation_garbage) || Sprite_Base.private_method_defined?(:execute_animation_garbage))
  File.open(VITA_MOG_LOG, 'a') { |f| f.puts "MOG_CAP #{ok ? 'INSTALLED' : 'NOT INSTALLED (no MOG Anti Animation Lag)'}" } rescue nil
  if ok
    module VitaMogCap
      @logged = 0
      def self.trim
        return unless $vita_mog_cap && $game_temp
        g = $game_temp.animation_garbage
        return if g.nil? || g.empty?
        free = vita_gpu_free_kb
        return if free >= VITA_MOG_MIN_KB
        refs = Sprite_Base.class_variable_get(:@@_reference_count)
        n = 0
        g.delete_if do |b|
          next true if b.disposed?
          next false if (refs[b] || 0) > 0     # used again by a running animation: keep
          b.dispose
          n += 1
          true
        end
        $vita_mog_trimmed += n
        if n > 0 && @logged < 30
          @logged += 1
          File.open(VITA_MOG_LOG, 'a') { |f| f.puts "MOG_CAP trimmed=#{n} free_kb_before=#{free} after=#{vita_gpu_free_kb}" } rescue nil
        end
      end
    end
    Sprite_Base.prepend(Module.new do
      def dispose_animation
        super
      ensure
        VitaMogCap.trim
      end
    end)
  end
