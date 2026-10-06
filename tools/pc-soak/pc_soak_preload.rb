# PC soak shim for the desktop mkxp-z build: runs the same tools/soak/soak.rb as the Vita build.
SOAK_SRC = ENV['SOAK_SRC'] || File.expand_path('soak.rb', Dir.pwd)   # tools/soak/soak.rb
VITA_SOAK_ROOT = Dir.pwd + '/'
def vita_trace(m); end
# Per-loop memory sample (the Vita build dumps its heap ledger here instead).
def vita_heap_ledger_dump(reason)
  rss = File.read('/proc/self/status')[/VmRSS:\s+(\d+)/, 1].to_i
  gs = GC.stat
  File.open(VITA_SOAK_ROOT + 'pc_mem.log', 'a') do |f|
    f.puts "#{Time.now.strftime('%H:%M:%S')} #{reason} rss_kb=#{rss} heap_live_slots=#{gs[:heap_live_slots]} " \
           "heap_pages=#{gs[:heap_allocated_pages]} malloc_increase_kb=#{gs[:malloc_increase_bytes] / 1024} " \
           "oldmalloc_kb=#{gs[:oldmalloc_increase_bytes] / 1024} gc=#{gs[:count]}"
  end
end
# The desktop build here has no Ogg decoder: audio calls that cannot find a decodable file are skipped
# (PC run measures memory of the game logic; the audio thread leak was Vita-specific and is fixed).
class << Audio
  %i[bgm_play bgs_play me_play se_play].each do |m|
    alias_method :"pc_soak_#{m}", m
    define_method(m) { |*a| begin; send(:"pc_soak_#{m}", *a); rescue Errno::ENOENT; end }
  end
end
module Kernel
  alias_method :pc_soak_rgss_main, :rgss_main
  def rgss_main(*args, &blk)
    TOPLEVEL_BINDING.eval(File.read(SOAK_SRC), 'soak.rb', 1)
    $pc_soak_r_once = true
    class << Input
      alias_method :pc_soak_press, :press?
      def press?(k)
        if (k == :R || k == 18) && $pc_soak_r_once
          $pc_soak_r_once = false
          return true
        end
        pc_soak_press(k)
      end
    end
    vita_heap_ledger_dump('boot')
    pc_soak_rgss_main(*args, &blk)
  end
end
