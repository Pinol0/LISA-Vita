Equivalence test for MKXP_VITA_EVENT_FAST against LISA's own scripts.
1. Extract the scripts: ruby -rzlib -e 'd=Marshal.load(File.binread(".../Data/Scripts.rvdata2")); d.each_with_index{|(i,n,c),k| File.write("OUT/%03d_%s.rb"%[k,n.to_s.gsub(/[^\w\-]+/,"_")], Zlib::Inflate.inflate(c))}'
2. Regenerate gen_block.rb from src/main.cpp (the MKXP_VITA_EVENT_FAST Ruby block) when it changes.
3. ruby t_event_fast.rb OUT   -> PASS (identical instance variables after 3 frames, 20000 random events).
Mutation check (d42): dropping `unless @locked` or the anime_count condition makes it FAIL.
