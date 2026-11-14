# M4 hooks: what Claude Code wires on the Mac

The pure M4 rules are done on branch `feat/extras-m4-rules` (chat, 2026-11-14), stacked on
`feat/extras-m3-hooks`. This file is the handoff for the device-side half: the reader and sync hooks,
the service work, the UI, the compat test, the gate, and the PR.
Read [SAVE-SAFETY.md](SAVE-SAFETY.md) first. Specs: [03-book-boss](features/03-book-boss.md),
[05-kindle-credit](features/05-kindle-credit.md). M3's hooks ([M3-HOOKS.md](M3-HOOKS.md),
`src/pokemon/PokemonServiceExtras.cpp`) are the pattern to follow.

**This is the only milestone that edits reader and sync source files.** Every hook there is one
line behind `#if defined(CROSSINK_POKEMON_EXTRAS)`, placed after the firmware's own work is done,
and must not change control flow. Anything that fails inside a hook is swallowed.

## What is already there

| File | What |
|------|------|
| `lib/Pokemon/PokemonExtrasKindle.{h,cpp}` | Positions (`fractionToPermille`), marks (`kindleHighWaterPermille`, `raiseKindleMark`, `noteLocalProgress`), pace (`kindleSecondsPerPage`), a sync (`planKindleCredit`, `applyKindleSync`), chunks (`nextKindleStep`), notice (`addKindleNotice`, `takeKindleNotice`) |
| `lib/Pokemon/PokemonExtrasBoss.{h,cpp}` | Finish (`onBookFinished`, `isBookCounted`, `bookEarnsBoss`, `chooseBossSpecies`), delivery (`hasHeldBoss`, `takeHeldBoss`, `holdBoss`, `bossLevel`, `deliverBoss`), `booksFinishedRibbons` |
| `test/pokemon_extras/PokemonExtrasKindleTest.cpp`, `PokemonExtrasBossTest.cpp` | 33 test groups; both in `extras-checks.yml` |

**No codec change.** Every field was already in file v1 (`rewardedBook*`, `heldBossSpeciesId`,
`ribbonCount`, `kindleMarks`, `kindlePendingNoticeMinutes`).

Decisions made in chat (tunables at the top of each header; change them there):
- **A book read mostly on the Kindle still earns its boss.** The spec's anti-farm rule (60 minutes of
  reading time on this device) would turn every Kindle-read book into "ribbon only", against the
  "Kindle counts the same" decision. So a book also qualifies when Kindle credit is on and its
  high-water mark is at 80% or more (`BOSS_MIN_SYNCED_PERMILLE`; 0 = device time only).
- Every boss, legendary or not, appears at the lead's level + 5. The held boss stores no level: it is
  computed at delivery.
- One boss is held at a time. A second qualifying book finished while one is still held is counted
  (ribbon +1) but brings no second boss (`BossAlreadyWaiting`). This only happens with a full queue.
- Several uncaught birds: one is picked at random. Rare pool: Dragonite, Snorlax, Lapras, Gyarados,
  Alakazam, Gengar, Machamp, Arcanine; uncaught weigh 3x.
- Reward: one Master Ball (item 10) per boss, also when one is already in the bag (upstream's own
  drops stop at one). Three Ultra Balls (item 9) only if the Master Ball count is at 255.
- A sync credits from a forward move of 2 per mille (0.2 points) or more; positions are floored to
  per mille. Pages and minutes round to the nearest whole.
- A measured pace is clamped to 10..300 seconds a page (junk-stat bound, not a reduced rate).
- Above the 600-minute guard the rest is dropped, the mark still goes to the synced position, and the
  egg pages shrink in the same proportion.

## 0. One id per book

Both features key on `extras::stableIdHash(<book id>)`. **Every hook must produce the same id for the
same book**: the Epub, Xtc and Txt readers, Book Stats, Home's book actions, and both sync paths.
Use the book's file path as the firmware already normalises it for its cache folder (the string the
per-book cache path is derived from). Add one helper, `extrasBookHash(path)` in
`src/pokemon/PokemonExtrasHooks.{h,cpp}`, and call nothing else. A renamed or moved file is a new
book to extras; that is accepted.

## 1. Local reading keeps the mark current

Today the service only knows the open book's progress in whole percent (`setBookProgressPercent`).
Add, in RAM only: `PokemonService::setExtrasOpenBook(uint32_t bookHash, uint16_t permille)`, called
from the same reader call sites that call `setBookProgressPercent` (hash 0 when no book is open).
No SD write.

In `applyExtrasAfterCredit()` (so at every credit checkpoint, including `flushOnExit`): with Kindle
credit on, `noteLocalProgress(copy, bookHash, permille)`. It returns true only when the mark moved,
so it adds no extras write of its own on most checkpoints. Update the function's early return: it must
also run when Kindle credit or Book boss are on.

## 2. A synced position was applied (feature 5)

Hook points, each after the firmware has saved the new position:
- `EpubReaderActivity::applyKeptPosition()`, after `KeptPosition::resolve(...)`, only in the branch
  where `saveProgress` succeeded.
- `KOReaderSyncActivity::saveProgressAndReturn()`, after `EpubReaderUtils::saveProgress` succeeded
  (it serves both apply paths, lines ~538 and ~1095).

Both call one function in `PokemonExtrasHooks`:
`extrasOnRemoteProgressApplied(bookPath, localFraction, remoteFraction, bookPages)`.
- `localFraction`: this reader's whole-book position **before** the remote one was applied. Capture it
  before `saveProgress` (the same number the sync screen shows as local progress).
- `remoteFraction`: the applied position as a whole-book fraction.
- `bookPages`: the book's page count on this reader, the value the stats screens use for the
  whole book (not the section's `pageCount`). Pass 0 if it is not known: the sync then earns nothing
  and only moves the mark.

The function builds a `KindleSync` (`fractionToPermille` for both positions; pace from
`BookReadingStats::load(cachePath).avgSecondsPerForwardPage` and
`GlobalReadingStats::load()` `totalReadingSeconds` / `totalPagesTurned`, all read-only, R4) and calls
`PokemonService::creditRemoteReading(sync)`:

1. Load extras; `credit = applyKindleSync(copy, sync)`. If the copy changed, **save extras now**. If
   that save fails, stop: credit nothing.
2. If `credit.droppedMinutes != 0`, log it (`LOG_INF`, book path and minutes).
3. `remaining = credit.minutes`. Loop: load the main state, `step = nextKindleStep(remaining,
   hasEncounterRoom(state))`.
   - `step.creditMinutes`: `creditMinutes(step.creditMinutes, remotePercent)`; subtract.
     `creditMinutes()` already runs `applyExtrasAfterCredit()`, so streak bonus, Day Care, quests and
     the egg-find roll all happen as for on-device reading.
   - `step.xpOnlyMinutes`: `awardFixedXp(leadId, step.xpOnlyMinutes)`, then stop.
   - If `creditMinutes` returns false, stop.
4. Load extras again (step 3 changed it): `applyEggPages(copy, credit.pages)` and
   `recordQuestEggPages(copy, counted)` when Eggs is on; `addKindleNotice(copy, creditedMinutes)` with
   the minutes that really went through; save extras.

A crash between 1 and 3 loses that sync's minutes and can never credit them twice (the mark is
already raised). Nothing here touches reading stats: no time, pages, sessions or streak days (R4).

Call it from the activity after its own state is consistent. A 600-minute credit is 10 main-save
commits: if that is visibly slow on the device, say so in the PR rather than deferring it silently.

## 3. A book was marked finished (feature 3)

One helper in `PokemonExtrasHooks`: `extrasOnBookCompleted(bookPath, cachePath)`. Call it only on a
not-completed to completed change, after the firmware's own stats write:
- `BookStatsActivity::applyCompletedState(true)`
- `EpubReaderActivity::setBookCompleted(true)` (it already returns early when nothing changes)
- `XtcReaderActivity.cpp` near `stats.isCompleted = isCompleted` (line ~857)
- `BookActions.cpp` (Home's mark-finished toggle, line ~318), when `completed` is true
- Check the TXT reader, the end-of-book options, the power-button shortcut and the chord: if they go
  through one of the above, no extra hook. List what you found in the PR.
- **Not** `NearbySyncActivity` (a completed flag arriving from another device is not the player
  finishing a book here).

The helper reads `BookReadingStats::load(cachePath).totalReadingSeconds` (read-only) and calls
`PokemonService::onExtrasBookFinished(BookFinish{extrasBookHash(bookPath), seconds})`:

1. Load extras and the main state; `result = onBookFinished(copy, state, book)`.
   `Ignored` / `AlreadyCounted`: return. Otherwise save extras.
2. `BossHeld`: try the delivery below right away.

**Delivery** (`deliverHeldExtrasBoss()`; call it from step 2, from `applyExtrasAfterCredit()` and on
Pokémon menu open, whenever `hasHeldBoss(extras)`):
1. Load the main state. No `hasEncounterRoom(state)`: return, the boss stays held.
2. `species = takeHeldBoss(copy)`; **save extras**. Stop if that fails.
3. `deliverBoss(state, species, leadLevel, random_, reward)`; roll shininess on the event at the
   queue's end as `creditMinutes()` does (`SHINY_CHANCE_DENOMINATOR`); `store_.commit(state)`.
4. If 3 fails: `holdBoss(copy, species)` and save extras again.

A crash between 2 and 3 loses that boss and never queues two.

## 4. UI

- Finish: "Something powerful was drawn to your reading..." when the result is `BossHeld`
  (a toast or the next Pokémon menu visit, whichever fits the reader's exit path; do not block the
  reader's own "finished" flow).
- Trainer Card: **Books finished** with `booksFinishedRibbons(extras)`, shown when it is above 0.
- Pokémon menu visit: `minutes = takeKindleNotice(copy)`; if not 0, show
  "Read on another device: N minutes credited" once and save extras.
- Home `!`: the boss is an ordinary encounter, so upstream's own notice already covers it.
- New strings: `STR_*` keys in `lib/I18n/translations/english.yaml` only.

## 5. Compat test (R10)

Extend `test/pokemon_extras_compat` on the v11 fixture:
- finish a book with 60+ minutes: one Encounter queued, one Master Ball, ribbon 1; finish it again:
  nothing; finish with a full queue: held, then delivered after an event is cleared; a crash between
  the extras save and the main commit: no boss, no second boss later;
- a 42-minute sync, the same sync again (nothing), and a guarded sync (600 minutes in 10 chunks, the
  rest dropped); a sync with a full queue (XP only for the rest);
- then decode with the upstream codec: version 11, 437-byte state, every record and every pending
  event valid.

## 6. Gate and PR

Same ladder as M3: native tests (the two new targets included), compat test, simulator on a throwaway
save, then a copy of the real save (never the SD card):
- every toggle off, read 10 minutes, apply a sync, mark a book finished: `pokemon-*.bin`, stats and
  sync files byte-identical to a run of the M3 build doing the same; extras files not rewritten;
- Kindle credit and Book boss on: only the expected changes. **Reading stats and sync files must be
  byte-identical with and without the two toggles** for the same scripted session
  (`global_stats.bin`, `reading_sessions/` apart from its random file name, `daily_reading/`,
  `synced_stats/`, the book's stats file, `sync_*`, `koreader.json`).
- The sync itself must behave exactly as on the M3 build: same applied position, same uploaded
  position afterwards.

Screenshots: the finish message, the boss encounter, the Trainer Card ribbon line, the
"Read on another device" notice. Record the firmware size in PROGRESS.md and open the PR into
`extras` from `feat/extras-m4-hooks`.

**After M4 is on the device** (Sergio, before anything else): Kindle to X4 Pro and X4 Pro to Kindle
sync on a real book, both directions, with a fresh SD backup taken first.
