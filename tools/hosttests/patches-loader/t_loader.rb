# Host test for the MKXP_VITA_PATCHES loader (heredoc in src/main.cpp), app0:patches/ -> ./tmp_patches/.
#   ruby t_loader.rb
require 'fileutils'
src = `python3 #{File.join(__dir__, 'extract.py')} #{File.join(__dir__, '../../../src/main.cpp')}`
src = src.gsub("'app0:patches/'", "'./tmp_patches/'")
fails = 0
check = ->(c, m) { puts "#{c ? 'PASS' : 'FAIL'}  #{m}"; fails += 1 unless c }
FileUtils.mkdir_p('tmp_patches'); File.delete('qa.log') if File.exist?('qa.log')
File.write('tmp_patches/a.rb', "$order = ($order || []) << :a\n")
File.write('tmp_patches/b.rb', "$order << :b\nraise ArgumentError, 'broken patch'\n")
File.write('tmp_patches/c.rb', "$order = ($order || []) << :c\n$c_dir = VITA_PATCH_DIR\n")
File.write('tmp_patches/index.txt', "# comment\nc.rb\n\na.rb\r\nb.rb\n")
eval(src, TOPLEVEL_BINDING)
log = File.read('qa.log')
check.($order == [:c, :a, :b], "index.txt order, comments/blank lines skipped (#{$order.inspect})")
check.(log.include?("PATCH_LOADED c.rb\nPATCH_LOADED a.rb\nPATCH_FAILED b.rb: ArgumentError: broken patch"), 'loaded/failed logged, the broken one does not stop the loader')
check.($c_dir == './tmp_patches/', 'patches see VITA_PATCH_DIR')
File.delete('tmp_patches/index.txt'); File.delete('qa.log')
eval(src.sub('VITA_PATCH_DIR = ', 'VITA_PATCH_DIR2 = ').sub('VITA_PATCH_LOG = ', 'VITA_PATCH_LOG2 = ').gsub('VITA_PATCH_DIR +', "'./tmp_patches/' +").gsub('VITA_PATCH_LOG,', "'./qa.log',"), TOPLEVEL_BINDING)
check.(File.read('qa.log').start_with?('PATCHES none (Errno::ENOENT)'), 'no index.txt: logged, nothing loaded')
FileUtils.rm_r('tmp_patches'); File.delete('qa.log')
puts "fails=#{fails}"; exit(fails == 0 ? 0 : 1)
