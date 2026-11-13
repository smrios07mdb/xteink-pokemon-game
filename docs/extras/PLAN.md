# Extras: build plan

Personal fork of `thebitsworld/xteink-pokemon-game` for one device: Sergio's Xteink X4 Pro.
Base: upstream `main` at `ace5fe0` (v2.1.0, 2026-11). Written 2026-11-13.

"Extras" is the name for everything this fork adds. Eight features, built in five milestones
(after M0 setup).

**Read [SAVE-SAFETY.md](SAVE-SAFETY.md) before writing any code. Its rules override every spec.**

## Features

| # | Feature | Spec | Milestone |
|---|---------|------|-----------|
| 1 | Eggs that hatch by page turns | [features/01-eggs.md](features/01-eggs.md) | M3 |
| 2 | Reading streak bonus | [features/02-streaks.md](features/02-streaks.md) | M2 |
| 3 | Book-finish boss encounter | [features/03-book-boss.md](features/03-book-boss.md) | M4 |
| 5 | Kindle credit (Kindle reading counts like on-device reading) | [features/05-kindle-credit.md](features/05-kindle-credit.md) | M4 |
| 6 | Post-game: gym rematches and Battle Tower | [features/06-post-game.md](features/06-post-game.md) | M5 |
| 7 | Day Care | [features/07-day-care.md](features/07-day-care.md) | M2 |
| 8 | Daily quests | [features/08-daily-quests.md](features/08-daily-quests.md) | M3 |
| 10 | Party sleep screen | [features/10-party-sleep-screen.md](features/10-party-sleep-screen.md) | M2 |

(Numbers match the original pitch. Feature 4, "books as routes", was not selected. Feature 9,
Nuzlocke, is deferred: its draft spec is parked in [deferred/09-nuzlocke.md](deferred/09-nuzlocke.md)
and must not be built until Sergio asks for it.)

## Architecture

All extras state lives in one new side file. Nothing existing changes format.

```
lib/Pokemon/PokemonExtras.h/.cpp        pure rules, no storage, host-testable (like PokemonGame)
lib/Pokemon/PokemonExtrasCodec.h/.cpp   versioned byte layout + CRC32 (PokemonCrc32), append-only
src/pokemon/PokemonExtrasStore.h/.cpp   /.crosspoint/pokemon-extras-{a,b}.bin, double-buffered
                                        (copy the PokemonIvEvStore write-verify-flip pattern)
test/pokemon_extras/                    codec + rules tests
test/pokemon_extras_compat/             proves the main save stays upstream-readable (SAVE-SAFETY R10)
```

- **Build flag:** `-DCROSSINK_POKEMON_EXTRAS=1`, set only in `[env:pokemon-x4-pro]` and
  `[env:pokemon-x4-pro-simulator]`. Every line of extras code sits behind
  `#if defined(CROSSINK_POKEMON_EXTRAS)`. With the flag off, the build behaves exactly like upstream.
- **Runtime toggles:** one on/off per feature, stored in the extras file (never in `settings.json`),
  shown under **Pokémon > Settings > Extras**. Defaults: all on, except Party sleep screen, which
  is off.
- **Hooks into existing code are one-liners** that call into `PokemonExtras*`. Keep each hook inside
  an `#if` block so upstream merges stay small. The known hook points:

| Event | Where it is today |
|-------|-------------------|
| Reading minutes credited | `PokemonService::creditMinutes()` (`src/pokemon/PokemonService.cpp`) after `store_.commit` |
| Verified page turn | `PokemonService::onSuccessfulPageTurn()` (called from the Epub/Xtc/Txt reader activities) |
| Book marked finished | `BookStatsActivity::applyCompletedState(true)`, `XtcReaderActivity.cpp` near `stats.isCompleted = isCompleted` |
| Synced position applied | `EpubReaderActivity::applyKeptPosition()`, `KOReaderSyncActivity` apply path |
| Battle result | `PokemonActivity` battle end, `PokemonService::resolveTrainer()`, `markGymDefeated()` |
| Catch | the catch path that appends a `PokemonRecord` (`RecordMutationKind::Append`) |
| Sleep | `SleepActivity::render*` (`src/activities/boot_sleep/SleepActivity.cpp`) |
| Game reset | `PokemonService::reset()` (also resets the extras store, same as the IV/EV store) |

- **Facts the specs rely on** (verified in the code at `ace5fe0`):
  - Reading XP is 1 XP per credited minute; `MAXIMUM_TOTAL_XP` is 8340.
  - Credit arrives in checkpoints (`PokemonTracker::CHECKPOINT_MINUTES` = 5), so that is the natural
    moment to persist extras too. Never write to the SD card on every page turn.
  - `PokemonService::awardFixedXp(recordId, xp)` grants XP with full level-up handling.
  - Champion defeated = bit 12 of `PokemonState::battleProgress` (`CHAMPION_GYM_INDEX` = 13).
  - Streak and daily reading time already exist: `GlobalReadingStats::currentReadingStreak()`,
    `DailyReadingStats::summarize()`. Read them; never write them.
  - The pending-event queue holds 16 with 2 slots reserved (`PENDING_EVENT_PROMPT_RESERVE`).

## Budgets (X4 Pro)

- **Flash:** upstream notes put `pokemon-x4-pro` at about 96.5% of 6,553,600 bytes, so roughly
  230 KB is free. Record the firmware size at every milestone in [PROGRESS.md](PROGRESS.md).
  Target: all eight features in under 150 KB. If a milestone would cross 99%, stop and report.
- **Internal RAM:** tight (upstream measured about 58 KB free at Home with Bluetooth up). Extras
  state stays under 2 KB, is loaded lazily, and uses no large static buffers.
- **Strings:** add new `STR_*` keys to `lib/I18n/translations/english.yaml` only. Confirm other
  languages fall back to English before relying on it.

## Who does what

| | Claude (claude.ai chat, "X4 Pro OS" project) | Claude Code (Sergio's Mac) | Sergio |
|-|-----------------------------------------------|----------------------------|--------|
| Can | Design, specs, pure rules in `lib/Pokemon`, native CMake tests, balance math, review | Fork/clone, `pio` builds, simulator runs and screenshots, device wiring in `src/`, UI screens, CI | Back up the SD card, flash, play-test |
| Cannot | Compile firmware (no PlatformIO registry access), create the fork | — | — |

Handoff rule: Claude chat writes rules + tests as a patch or PR against `lib/Pokemon` and
`test/`; Claude Code wires them into `src/`, builds, and proves it in the simulator. If the Claude
GitHub connector is given access to the fork, chat can push branches and review PRs directly;
otherwise patches go through Sergio.

## Branches and releases

- `main`: mirror of upstream. Never commit here. Update with `git fetch upstream && git merge --ff-only upstream/main`.
- `extras`: integration branch and the fork's default branch.
- `feat/extras-<milestone>-<topic>`: one branch and one PR per milestone (or per feature if large).
  (Not `extras/...`: git cannot have a branch `extras` and a branch `extras/x` at the same time.)
- Test firmware: **Actions > Release > Run workflow** on the branch gives a downloadable
  `firmware-x4-pro.bin` artifact with no release created.
- Tagged builds: `v2.1.0-extras.N-x4pro`. The `-x4pro` suffix makes the workflow build the X4 Pro
  firmware only and publish it as a prerelease.

## Milestones

Each milestone ends with: native tests green (minus the three upstream-known failures listed in
`CLAUDE.md`), the compat test green, simulator screenshots of every new screen, firmware size
recorded, and a row added to [PROGRESS.md](PROGRESS.md). Sergio play-tests on the device for at
least two days before the next milestone starts.

### M0: fork and safety net
Status 2026-11-13: steps 1, 3, 4 and 6 are done (see PROGRESS.md). Steps 2, 5 and 7 need the Mac.
1. `gh repo fork thebitsworld/xteink-pokemon-game --clone`; add `upstream` remote; create `extras`
   from `main`; apply the plan patch; set `extras` as default branch.
2. Enable Actions on the fork. Run the Release workflow manually on `extras`; confirm the X4 Pro
   artifact builds unchanged.
3. Point the updater at the fork: in `[env:pokemon-x4-pro]` change `CROSSINK_OTA_RELEASE_URL` to
   `https://api.github.com/repos/<fork-owner>/xteink-pokemon-game/releases/latest`. Otherwise
   **Check for Updates** would install upstream firmware over this build.
4. Baseline: first `python3 scripts/gen_i18n.py lib/I18n/translations lib/I18n/` (the tests need the
   generated i18n headers), then `cmake -S test -B /tmp/crossink-tests -G 'Unix Makefiles' && cmake --build /tmp/crossink-tests -j 6 && ctest --test-dir /tmp/crossink-tests`; record which tests fail before any change.
5. Baseline simulator: `pio run -e pokemon-x4-pro-simulator -t run_simulator`.
6. Add `scripts/extras/backup_sd_save.sh` and `restore_sd_save.sh` (SAVE-SAFETY, "Backup").
7. Copy Sergio's real save into the simulator's `fs_/.crosspoint/` and confirm it loads. This copy is
   the fixture for every later milestone. Do not commit it.

### M1: extras foundation
- Chat: `PokemonExtras` state struct, codec (version 1), rules skeleton, `test/pokemon_extras`.
- Claude Code: `PokemonExtrasStore`, build flag, **Extras** settings screen with the toggles,
  `reset()` wiring, `test/pokemon_extras_compat`, CI running both tests.
- Exit: with every toggle off, a simulator session produces a main save byte-identical in
  layout/version to upstream's (compat test), and deleting the extras files changes nothing else.

### M2: Streaks, Day Care, Party sleep screen
Lowest risk: read-only on stats, XP only through `awardFixedXp`.

### M3: Eggs, Daily quests
Adds the first new records (hatched Pokémon) through the existing catch/append path.

### M4: Book-finish boss, Kindle credit
The only milestone that touches reader and sync source files. After it, re-test Kindle ↔ X4 Pro
sync in both directions on the device before anything else.

### M5: Post-game (rematches, Battle Tower)
Largest UI work. Reuses the gym battle flow and the road-trainer team generator.

## Decisions (Sergio, 2026-11-13)

1. Mew stays under upstream's rule (catch the other 150 first). It is never a book boss.
2. Kindle reading counts the same as on-device reading for the game: full rate, same rewards.
3. Nuzlocke is deferred. No extras feature may release or delete a Pokémon.
