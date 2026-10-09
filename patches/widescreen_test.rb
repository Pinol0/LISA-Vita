# Test only (MKXP_VITA_WIDE_TEST): LISA drawn at 736x416 (23x13 tiles, about the Vita's 16:9) through
# RGSS3's own Graphics.resize_screen, before the game starts. The game was made for 544x416: maps
# narrower than 23 tiles, pictures and scenes placed by hand for 544 pixels will show it. Not a feature.
# qa.log: WIDESCREEN_TEST ...
ok = Graphics.resize_screen(736, 416) rescue $!
File.open(VITA_PATCH_LOG, 'a') { |f| f.puts "WIDESCREEN_TEST resize_screen(736, 416) -> #{ok.inspect} now #{Graphics.width}x#{Graphics.height}" } rescue nil
