  VITA_SAVE_LOG = '/dev/null'
  # PERF FIX (MKXP_VITA_SAVE_FAST): the save/load screen opened in 2-3 s (d45-d48): 16 slots, each
  # Window_SaveFile#refresh reads the same header twice (draw_party_characters, draw_playtime) and
  # every empty slot raises Errno::ENOENT twice (load_header's `rescue nil`). Here load_header
  # returns nil at once for a file that does not exist (same result as the rescued exception), and
  # a second read of the same slot in the same frame is rebuilt from the first one with Marshal
  # (new objects each call, as from the file). The memo is dropped on every save and delete.
  # Installed only if DataManager's header methods are the stock ones. Host test:
  # tools/hosttests/save-fast/.
  $vita_save_fast = true
  dm = DataManager.singleton_class
  # our own aliases (vita_*: soak) are not game scripts
  names = (dm.instance_methods(false) + dm.private_instance_methods(false)).grep(/header|save_file|save_game/).reject { |n| n.to_s.start_with?('vita_') }.sort
  ok = names == [:delete_save_file, :load_header, :load_header_without_rescue, :make_save_header, :save_file_exists?, :save_game, :save_game_without_rescue].sort
  File.open(VITA_SAVE_LOG, 'a') { |f| f.puts "SAVE_FAST #{ok ? 'INSTALLED' : 'NOT INSTALLED'} names=#{names.inspect}" } rescue nil
  if ok
    module VitaSaveFast
      @memo = {}
      @frame = -1
      class << self; attr_accessor :memo, :frame; end
    end
    dm.prepend(Module.new do
      def load_header(index)
        return super unless $vita_save_fast
        fn = make_filename(index)
        return nil unless File.exist?(fn)   # File.open would raise ENOENT -> rescue nil
        if VitaSaveFast.frame != Graphics.frame_count
          VitaSaveFast.memo = {}
          VitaSaveFast.frame = Graphics.frame_count
        end
        if (bytes = VitaSaveFast.memo[index])
          return (Marshal.load(bytes) rescue nil)
        end
        h = super
        VitaSaveFast.memo[index] = Marshal.dump(h) rescue nil
        h
      end
      def save_game_without_rescue(index)
        VitaSaveFast.memo = {}
        super
      ensure
        VitaSaveFast.memo = {}
      end
      def delete_save_file(index)
        VitaSaveFast.memo = {}
        super
      ensure
        VitaSaveFast.memo = {}
      end
    end)
  end
