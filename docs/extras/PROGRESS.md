# Extras: progress log

One row per milestone. Claude Code fills this in at the end of each milestone PR.

| Milestone | Date | PR | Firmware size (bytes / % of 6,553,600) | Native tests | Compat test | Simulator screenshots | On device since | Notes |
|-----------|------|----|-----------------------------------------|--------------|-------------|-----------------------|-----------------|-------|
| Baseline (upstream `ace5fe0`) | 2026-11-13 | n/a | 5,995,232 / 91.5% (upstream's published `xteink-pokemon-x4-pro-firmware-v2.1.0.bin`) | 2229 of 2230 pass (Linux, GCC, cloud workspace) | n/a | n/a | | `AssetCompanionStoreTest` does not compile here (`-Werror=restrict` in an snprintf call); unrelated to Pokémon. Run `python3 scripts/gen_i18n.py lib/I18n/translations lib/I18n/` before CMake or nothing builds |
| M0 fork and safety net | 2026-11-13 | #1 (setup), this PR (finish) | 5,995,424 / 91.5% (`pio run -e pokemon-x4-pro` on the Mac; 558,176 bytes free; RAM 37.4%) | same as baseline | n/a | Simulator Home and Pokémon menu rendered from a copy of the real save (not committed) | | Done: Release workflow green on `feat/extras-m0-setup`, PR #1 merged. Mac: PlatformIO 6.2.0 (pipx, Python 3.13), simulator smoke test `--pokemon` passes, firmware builds, SD backup taken (`~/x4pro-backups/x4pro-20261113-184131`, 810 files), real save loads in the simulator (Magnemite Lv. 11). Firmware is 192 bytes larger than upstream's v2.1.0 (updater URL). The 96.5% in upstream's CLAUDE.md is stale: real headroom is ~558 KB, not ~230 KB. Mac build notes below |
| M1 extras foundation | | | | | | | | |
| M2 streaks, day care, sleep screen | | | | | | | | |
| M3 eggs, daily quests | | | | | | | | |
| M4 book boss, Kindle credit | | | | | | | | Re-test Kindle sync both ways |
| M5 post-game | | | | | | | | |

## Mac build notes (Apple clang, macOS 26, Apple Silicon)

- Tools: `brew install cmake pkg-config sdl2 wolfssl expat`; `pipx install --python python3.13 platformio` (pio 6.2.0 under Python 3.14 was not tried). Set `PKG_CONFIG_PATH=/opt/homebrew/opt/expat/lib/pkgconfig`.
- The simulator does not compile with Apple clang as shipped (upstream builds with GCC on Linux). Workaround used, nothing committed:
  `PLATFORMIO_BUILD_FLAGS=-Wno-c++11-narrowing` (3 narrowing errors in `FileBrowserActivity.cpp`, `PokemonActivity.cpp`) and a one-line local edit in `src/activities/home/HomeActivity.cpp:2274` (`pokemonAccessoryRect_ = {};` to `= Rect();`, because `Rect`'s constructor is `explicit`). Homebrew GCC 15 does not work with the macOS SDK headers.
- `pio run -e pokemon-x4-pro` deletes the simulator build dir; rebuild the simulator after a firmware build.
- Pokémon art is not in git: unzip `xteink-pokemon-sd-card-assets.zip` from the upstream release into `fs_/` (gives `fs_/pokemon`).
- Screenshots need SDL's default video driver (a window opens briefly); `SDL_VIDEODRIVER=offscreen` wrote none on macOS. A frame must be redrawn after the screenshot time, so schedule a key press after it. `CROSSINK_SIMULATOR_START_POKEMON` belongs to the smoke test (it exits by itself); for a real-save session boot to Home and navigate with `CROSSPOINT_SIM_INPUT_SCRIPT` (e.g. `3000:BACK;5000:UP;7000:ENTER`; BACK first, because the copy lacks the last-open book and shows an alert).
- Run the simulator against a copy of `fs_` (`CROSSPOINT_SIM_SD=<copy>`), so the fixture in `fs_/.crosspoint` stays pristine.
