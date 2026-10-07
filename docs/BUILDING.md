# Building

The build runs on Linux with [VitaSDK](https://vitasdk.org/). It needs `git`, `curl`, `xxd`,
`patch`, `cmake` (3.16+) and `make`.

```sh
export VITASDK=/usr/local/vitasdk      # your VitaSDK
deps/setup.sh --ruby-lib /path/to/libruby-static.a
cmake -S . -B build -C options/release.cmake
make -C build -j"$(nproc)"
```

The package is `build/lisa_vita.vpk` (published as `LISA-Vita-alpha1.vpk`).

`deps/setup.sh` leaves an existing `deps/mkxp-z` or `deps/vitaGL` as it is: after pulling a new
version of this repository, delete them (and `build/`) so the updated patches are applied.

## What `deps/setup.sh` does

| Dependency | Base | Port changes |
|---|---|---|
| mkxp-z | `826929e` ([mkxp-z/mkxp-z](https://github.com/mkxp-z/mkxp-z)) | `deps/patches/mkxp-z.patch`; the shader headers in `build/shader/` are generated with `xxd -i` |
| vitaGL | `16fe309` ([Rinnegatamante/vitaGL](https://github.com/Rinnegatamante/vitaGL)) | `deps/patches/vitaGL.patch`; built with `NO_SPLASHSCREEN=1 LOG_ERRORS=1 MKXP_DIAG=1 SINGLE_THREADED_GC=1 MKXP_NO_FBO_DEPTH=1 HAVE_SHADER_CACHE=1` |
| Ruby | 3.1.6 (ruby-lang.org, checked by sha256) | `deps/patches/ruby-3.1.6-vita.patch` |
| vita-compat | `deps/vita-compat/` | POSIX shims Ruby needs on the Vita (`libvitacompat.a`) |

Everything is placed under `deps/` (ignored by git); an existing directory is left as it is.
The patched vitaGL is linked from `deps/vitaGL/` with its own `vitaGL.h`: the vitaGL installed in
your VitaSDK is not touched.

## Ruby (`libruby-static.a`)

The port links a static Ruby 3.1.6 cross-compiled for the Vita (`--host=arm-vita-eabi`,
`--disable-shared`, `--disable-jit-support`, `--without-gmp --without-openssl`, linked against
`libvitacompat.a`). The source changes are all in `deps/patches/ruby-3.1.6-vita.patch`:

- `coroutine/pthread/Context.{c,h}`, `cont.c`: Fibers on a small pool of pthreads (the Vita has no
  `ucontext`/assembly coroutines for this ABI), with cooperative switching;
- `thread.c`, `thread_pthread.c`: the timer thread's socket pair is repaired after standby
  (on the Vita `pipe2()` is a local socket pair, and standby closes sockets);
- `file.c`: device paths such as `ux0:` are absolute (path expansion, `require`);
- `dln.c`, `process.c`, `gc.c`: no dynamic loading, no child processes, no fatal-signal handlers;
- `inits.c`: initialisation order fix-up.

`deps/setup.sh` prepares the patched sources and the `config.h` the headers need, but **does not
build the library yet**: pass a prebuilt `libruby-static.a` with `--ruby-lib` (one is attached to
the Releases), or build it yourself. The prebuilt archive was produced with the recipe in
`deps/ruby-recipe/`:

- `make-dryrun-commands.tsv`: the compile command of every archive member;
- `build_libruby.py`: rebuilds every member with those commands (`gc.o` and `process.o` use
  `config-oldcfg.h`, the other members `config.h`);
- `vita_encinit.c`: registers the built-in encodings (no dynamic loading on the Vita).

`configure` was regenerated with Autoconf 2.72 from the unchanged `configure.ac`. A plain
`configure && make` build of the library from the patched sources is planned but has not been
validated on hardware yet.

## Options

Every fix and diagnostic is a CMake option (see `CMakeLists.txt`); `options/release.cmake` holds
the values of the release build. Without it, the options keep their defaults (most fixes off).

## Host tests

`tools/hosttests/` contains host-side tests of the port's code (C++ and Ruby), each with mutants that
must fail (see `tools/hosttests/README.md`). `setup_env.sh` builds the host Ruby 3.1.6 they use and
extracts the scripts from your own copy of the game (never part of this repository); `run_all.sh`
runs them all. `tools/frametime.py <folder with perf.log>` summarizes the frame times of a session.
Example of a single test that needs no game files (after `deps/setup.sh`):

```sh
VITASDK=/usr/local/vitasdk tools/hosttests/cpu-paging/run.sh /tmp/cpu-paging-root/
```
