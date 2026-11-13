# Save safety

Goal: nothing in this fork can damage Sergio's existing Pokémon save or reading statistics, and he
can return to upstream firmware at any time with both intact.

## What must stay untouched

| Data | Files (SD card, under `/.crosspoint/`) |
|------|-----------------------------------------|
| Pokémon save | `pokemon-a.bin`, `pokemon-b.bin` (state v11), `pokemon-moves-{a,b}.bin`, `pokemon-battle-{a,b}.bin`, `pokemon-ivev-{a,b}.bin`, `pokemon-hof-{a,b}.bin` |
| Reading stats | `global_stats.bin` (+ `.bak`), `reading_sessions/`, `synced_stats/`, and each book's `BookReadingStats` file inside that book's cache folder |
| Sync state | `sync_state.bin`, `sync_pending.*`, `sync_parked.bin`, `sync_ids.bin`, `koreader.json` |

Extras writes exactly one new pair: `pokemon-extras-a.bin` / `pokemon-extras-b.bin`.

## Rules

- **R1. No format changes.** Do not change the version, size, or layout of any existing codec
  (`PokemonStoreCodec`, `PokemonBattleStoreCodec`, `PokemonMovesetStoreCodec`,
  `PokemonIvEvStoreCodec`, `PokemonHallOfFameCodec`, `PokemonSaveBundleCodec`, `GlobalReadingStats`).
  The main save stays v11.
- **R2. No new values in old files.** Do not add members to enums that are persisted in existing
  files: `PendingEventKind`, `Origin`, `RecordFlag`, `DashboardNotice`, `EvolutionItem`,
  `SLEEP_SCREEN_MODE`, or any `settings.json` / `state.json` key. Upstream firmware may reject or
  misread a value it does not know. New kinds of state go in the extras file.
- **R3. Mutate the main save only through existing `PokemonService` operations.** A hatched egg is
  appended exactly like a catch (`Origin::Caught`). Bonus XP goes through `awardFixedXp`. A boss is
  an ordinary `PendingEventKind::Encounter`. Items go through the existing bag grant. If a feature
  seems to need a new kind of main-save write, stop and ask.
- **R4. Reading stats and sync state are read-only.** Extras may call existing loaders
  (`GlobalReadingStats::load()`, `currentReadingStreak()`, `DailyReadingStats::summarize()`, per-book
  stats readers). Extras never calls a save/record function on them and never changes what the
  reader records. Kindle credit goes into the Pokémon game only; it must not add time, pages, or
  streak days to the firmware's reading statistics.
- **R5. Extras is optional at runtime.** A missing or corrupt extras file means "empty extras": the
  game loads and plays normally, features start from zero, nothing is reported as an error to the
  main save path. A failed extras write never blocks or rolls back a main-save commit.
- **R6. Order of writes.** When one action changes both, commit the main save first, then extras.
  Design each feature so a crash between the two cannot duplicate or lose anything in the main
  save. Each spec says how (see the "Crash safety" line in 01-eggs.md for the pattern).
- **R7. Nothing destructive.** No extras feature releases, deletes, or overwrites a Pokémon, an
  item count downward, or a badge. (Nuzlocke, the one feature that would, is deferred.)
- **R8. Save Transfer stays as is.** Do not add extras files to the transfer bundle allowlist
  (file ids 0–12). Extras do not travel in v1.
- **R9. Updater points at the fork** (PLAN M0 step 3), so Check for Updates cannot silently replace
  this build with upstream.
- **R10. Compat test on every PR** (`test/pokemon_extras_compat`): start from a v11 fixture save,
  run every extras operation through the host-test service, then decode the result with the
  unmodified upstream codec and assert `validateState()` and `validateRecord()` pass for every
  record, the state size is still 437 bytes, and the version is still 11.
- **R11. Flag-off equals upstream.** Building without `CROSSINK_POKEMON_EXTRAS` must compile no
  extras code. CI builds one flag-off simulator target to prove it.

## Backup (before the first flash of any fork build, and before M4)

Run on the Mac with the SD card mounted (script from M0):

```sh
scripts/extras/backup_sd_save.sh /Volumes/<SD> ~/x4pro-backups
```

It copies the whole `/.crosspoint/` folder to a timestamped directory and prints a SHA-256 list.
The Pokémon save alone is `pokemon-*.bin`; the stats are the files in the table above.

## Restore / going back to upstream

1. Download upstream's `xteink-pokemon-x4-pro-firmware-latest.bin`, copy to the SD card, and use
   **Settings > System > SD Card Firmware Update**.
2. The existing save loads as is (R1, R2).
3. Optional: delete `pokemon-extras-a.bin` and `pokemon-extras-b.bin`. Upstream ignores them.
4. If anything looks wrong: `scripts/extras/restore_sd_save.sh ~/x4pro-backups/<timestamp> /Volumes/<SD>`.

## Test ladder (each step must pass before the next)

1. Native tests: `test/pokemon_extras`, `test/pokemon_extras_compat`, and the existing suite.
2. Simulator with a throwaway save.
3. Simulator with a **copy** of Sergio's real save in `fs_/.crosspoint/` (never commit it).
4. Device, after a fresh backup.
