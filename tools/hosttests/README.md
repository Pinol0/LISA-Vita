# Host tests

Every test here checks a piece of the port on the PC: either the real source file built for the
host (C/C++: `src/*.cpp` with small stubs of the Vita APIs) or the real Ruby a build embeds
(`check_embedded_ruby.py --dump`, or the blocks of `src/main.cpp`) run on the game's own scripts.
They prove host behaviour only: GPU/vitaGL behaviour, timing and memory on the Vita need hardware.

## Run everything

```sh
# once: host Ruby 3.1.6 + the port's Ruby patch, game scripts extracted from your copy, audio fixtures
tools/hosttests/setup_env.sh ~/hosttest-env /path/to/game/Data/Scripts.rvdata2
# the Ruby a build embeds
python3 tools/check_embedded_ruby.py build-dNN --dump ~/hosttest-env/dump-dNN
# all tests (GAME_DIR: your extracted game, Data/ + Graphics/; AUDIO_DIR: its Audio/ folder)
VITASDK=... AUDIO_DIR=/path/to/game/Audio tools/hosttests/run_all.sh ~/hosttest-env ~/hosttest-env/dump-dNN /path/to/game
```

`run_all.sh ENV DUMP GAME name...` runs only the named tests. Logs: `ENV/results/<test>.log`.
Tests that check themselves with mutations (a deliberately broken copy of the code that must FAIL)
print the mutants' results after a `mutation` line; those FAIL lines are expected.

No game file belongs in this folder: scripts, images and sounds come from your own copy at run
time. `audio/fixtures/` holds synthetic WAVs (and the PCM they must decode to) made for the tests.

Not run by `run_all.sh`, kept for reference: `ruby-prof/prof_test.rb` and `slow-load/slow_test.rb`
(snapshots of older versions of their blocks).
