# CLAUDE.md

A desktop music player for a local library: C++20 and Qt 6 Widgets, with a GStreamer or Qt Multimedia
playback backend, SQLite storage, and optional play-count sync through AWS Lambda and DynamoDB. Runs on
macOS, Windows and Linux.

| Document | Contents | Read |
| --- | --- | --- |
| [roadmap.md](roadmap.md) | Progress (✅ / 🟡 / ⬜) and known issues | Before starting a task |
| [docs/architecture.md](docs/architecture.md) | Design principles, classes and responsibilities, startup, playback, threading | Before changing design or code structure |
| [docs/expressions.md](docs/expressions.md) | The expression language: song filters, status bar and window title | Before changing search, autoplaylists or display expressions |
| [docs/monitored-library.md](docs/monitored-library.md) | Watched folders, scans, the file system watcher, autoplaylists, Playlist Search | Before changing the library or its watcher |
| [docs/info-panel.md](docs/info-panel.md) | The info panel's Lyrics and Properties tabs | Before changing the info panel |
| [docs/spectrum.md](docs/spectrum.md) | The Spectrum tab, the `src/spectrum/` module, the players' audio taps | Before changing the spectrum or the players' audio path |
| [docs/plugins.md](docs/plugins.md) | Plan: plugins (DLLs with capabilities, Lyrics first), the plugin API and its boundary | Before changing the plugin API or a plugin |
| [docs/cloud-sync.md](docs/cloud-sync.md) | Play-count sync: DynamoDB model, Lambda API (`infra/aws/lambda`) | Before changing play stats or sync |
| [README.md](README.md) | For users: features, build, query syntax, play stats | When a user-visible feature or the build changes |
| [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) | Third-party components and their licenses | Before adding or updating a dependency |

## Working

- Commit and push only when asked. Commit messages start with the history's prefixes: `feat:`, `fix:`,
  `refactor:`, `test:`, `docs:`, `build:`, `chore:`.
- A feature larger than a few classes starts with a plan in `docs/<topic>.md` (titled "Plan: ...",
  marked "Status: implemented" once done), reviewed before building.
- Before calling a change done: build the app and the tests, run the whole test suite, and pass
  `scripts/format.sh --check` (commands below). For playback, audio output or UI, also run the app and
  check the change by hand. Then review the whole diff against this file: no stale code or docs, and
  each sentence in the doc it belongs in.

## Writing the docs

- docs/architecture.md holds the design shared by the whole app; a topic doc holds one feature's design,
  linked from architecture.md. Both describe only the approach in use; each important choice says why,
  what was rejected and why, and its cost.
- roadmap.md records progress and known issues. README.md is for users.
- Concise but complete. No history, verification narratives or build statistics (those go in commit
  messages). When a decision changes, rewrite its section instead of appending notes.

## Commands (macOS and Linux)

From `src/`. After cloning: `git submodule update --init --recursive`.

| Command | Does |
| --- | --- |
| `cmake -S . -B build && cmake --build build -j` | Build the app |
| `./build/EXEC.app/Contents/MacOS/EXEC` (macOS), `./build/EXEC` (Linux) | Run the app |
| `cmake -S . -B build-tests -DBUILD_TESTING=ON && cmake --build build-tests -j` | Build the app and the tests |
| `ctest --test-dir build-tests --output-on-failure` | Run the tests |
| `../scripts/format.sh` / `../scripts/format.sh --check` | Format / check every C++ source with clang-format (`.clang-format`) |

- Run `ctest` without `-j`: the test executables share one QSettings folder, which each one deletes when
  it starts.
- A new source file goes in `PROJECT_SOURCES` in `src/CMakeLists.txt` (or the module's own
  CMakeLists.txt, as `src/spectrum/`); a new test `tst_<name>.cpp` goes in `TEST_NAMES` in
  `src/Tests/CMakeLists.txt`, with `QT_QPA_PLATFORM=offscreen` if it shows a widget.

## Commands (Windows)

PowerShell. Configure, build, run and test as in README.md, "Windows (vcpkg)"; format with
`scripts/format.ps1` (`-Check` to only report). Match `GST_PLUGIN_PATH` to the build folder's
`CMAKE_BUILD_TYPE` (in its `CMakeCache.txt`).

| Command | Does |
| --- | --- |
| `& "<Visual Studio>\Common7\Tools\Launch-VsDevShell.ps1" -Arch amd64 -HostArch amd64 -SkipAutomaticLocation` | Load the MSVC environment into the current shell; without it linking fails with `cannot open file 'kernel32.lib'` |
| `build-tests\Tests\<name>.exe -o <file>,txt` | Write one test's results to a file; the test executables print nothing to the console |

Without Qt's `bin` on `PATH`, the app and tests exit with `0xc0000135`.

## Dependencies

- Add a library only after checking that it is maintained and its license: the app is MIT and uses Qt
  under the LGPL, so no GPL. List it in THIRD_PARTY_NOTICES.md.
- `third_party/taglib` (a TagLib fork, see README.md) and `third_party/efsw` (pinned to a release tag) are
  submodules: don't edit them from this repository; change a pin deliberately.

## Tests and user data

- Tests never touch the developer's library, settings or cloud data. They use temporary directories, the
  test QSettings folder (`src/Tests/test_qsettings_bootstrap.cpp`), `ScopedHomeDir` for files under the
  home folder, and `MYPLAYER_USE_DUMMY_AUDIO_PLAYER` for the playback backend. Cloud sync tests reach no
  network. `tst_gstaudioplayer` plays a generated file through the default audio output at volume 0, and
  skips without one.
- Test changes that write tags or remove songs on generated files (`src/Tests/testaudiofiles.h`) or a
  copy, never on the developer's library.
- Never print or commit AWS credentials or the sync UUID.

## Engineering principles

- **Modern practices and libraries.** C++20 (`std::span`, `std::numbers`, designated initializers where
  clearer) and Qt 6 (function-pointer `connect`, Qt Test). Prefer Qt and the standard library; add a
  library as in "Dependencies".
- **Structured and modular.** Code lives where docs/architecture.md puts it, in focused classes with clear
  boundaries: backend classes hold state and operations and never depend on the UI; presenters connect
  them to widgets; `MainWindow` only wires subsystems together. A self-contained feature can be its own
  library (as `src/spectrum/`, which depends on Qt only). One responsibility per class; no catch-all
  utility files.
- **Explicit and minimal.** Implement exactly what the task needs, in clear, direct code: no abstraction
  layers, configuration flags or generic machinery for hypothetical needs. Name things for what they do.
- **Simple, documented design.** Every important choice is documented ("Writing the docs"). Every
  non-trivial code flow (ordering, threads and locks, timing, state that spans calls or signals) has a
  comment at the code saying why it works that way. A flow that can't be explained briefly is too
  complex: simplify it instead of documenting around it.
- **No fallbacks or workarounds in finalized code.** When something doesn't work, find and fix the
  cause. Temporary probe or debug code is removed before the change is done. Fallbacks that are part of
  the documented design (e.g. `directsoundsink` on Windows when `wasapi2sink` is missing) are design, not
  workarounds.
- **No over-defensive logic.** Don't guard against states that can't happen, re-validate what the type
  system or an earlier layer guarantees, or catch and ignore. Let real errors surface with useful
  context; handle them only where there's a meaningful response.
- **Focused tests.** Qt Test executables in `src/Tests/` cover the backend's logic, presenters and
  widgets. Real audio devices other than `tst_gstaudioplayer`'s check, system media controls and looks
  are checked by hand. Cover what the logic needs, without redundant tests: merge or remove a test that
  another already covers. Don't move code or add interfaces or test-only hooks only to test it.

## Conventions

- Play stats belong to a song's identity (normalized `title|artist|album`): songs with the same identity
  share them.
- The main window's layout lives in `src/mainwindow.ui`; its custom widgets (`LyricsPanel`,
  `SpectrumWidget`, ...) are promoted there (`<customwidget>`).
- Settings are `QSettings` keys named `<area>/<name>` (e.g. `infopanel/current_tab`), documented in the
  feature's doc.
