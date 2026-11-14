# M2 hooks: what Claude Code wires on the Mac

The pure M2 rules are done on branch `feat/extras-m2-rules` (chat, 2026-11-13). This file is the
handoff for the device-side half: the service hooks, the UI, the gate, and the PR. Read
[SAVE-SAFETY.md](SAVE-SAFETY.md) first. Specs: [02-streaks](features/02-streaks.md),
[07-day-care](features/07-day-care.md), [10-party-sleep-screen](features/10-party-sleep-screen.md).

## What is already there

| File | What |
|------|------|
| `lib/Pokemon/PokemonExtrasRules.{h,cpp}` | Streak bonus (`applyStreakBonus`, `streakBonusPercent`), milestones (`dueStreakMilestone`, `markStreakMilestoneRewarded`), rare encounter (`hasEncounterRoom`, `queueRareEncounter`), Day Care (`sendToDayCare`, `clearDayCareIfHolds`, `clearDayCare`, `dayCareXpCap`, `applyDayCareMinutes`) |
| `lib/Pokemon/PokemonExtrasLayout.{h,cpp}` | `partySleepLayout()` boxes, `xpBarFillWidth()`, `eggProgressPercent()` |
| `test/pokemon_extras/PokemonExtrasRulesTest.cpp`, `PokemonExtrasLayoutTest.cpp` | 24 test groups; both in `extras-checks.yml` |

No codec change: every field M2 needs was already in file v1 (989 bytes).

## 1. creditMinutes() hook (streaks + Day Care)

At the end of `PokemonService::creditMinutes()`, after the main commit and `healPartyOnRead`, one
`#if defined(CROSSINK_POKEMON_EXTRAS)` call into a new extras helper in `src/pokemon/`. Best effort:
it never changes the function's return value (R5).

1. Load extras (`devicePokemonExtrasStore()`); stop if `readOnly()`. Work on a copy of `state()`.
2. Read the streak (read-only, R4) and fill `StreakInput`:
   `ok = getCurrentLocalReadingStatsDateTime(dt)`; if ok,
   `streakDays = GlobalReadingStats::load().currentReadingStreak(&dt.date)` and
   `todayIndex = readingStatsDayIndex(dt.date)`. Measure the cost of `GlobalReadingStats::load()` on the
   device; if it is slow, cache the streak for the day. Never call any save on it.
3. **Bonus XP:** `xp = applyStreakBonus(copy, input, minutes)`; if `xp > 0`,
   `awardFixedXp(leaderId, xp)`. On failure, put back the old `streakXpRemainderPermille`.
4. **Milestone:** `m = dueStreakMilestone(copy, input)`. If `m != 0` and `hasEncounterRoom(mainState)`:
   save a copy, `markStreakMilestoneRewarded(copy, input, m)`, **commit extras first** (intent, see
   the header comment: losing one bonus encounter on a crash is fine, two never), then reload the main
   state, `queueRareEncounter(state, leaderLevel, bookProgressPercent, random_)`, roll shininess on the
   last queued event exactly as `creditMinutes()` does, and `store_.commit(state, {})`. If any of that
   fails, restore the two milestone fields and commit extras again. No room: do nothing; the next
   checkpoint retries.
5. **Day Care:** if `dayCareRecordId != 0`: read the record; missing or now in the party, so
   `clearDayCare` (stale, silent). Else `xp = applyDayCareMinutes(copy, minutes, record.totalXp,
   leaderLevelAfterCredit)` and grant it (see box safety below). On failure, put back
   `dayCareMinuteRemainder`.
6. Commit extras once at the end if the copy changed.

**Box safety of `awardFixedXp` (checked in chat, confirm on the Mac):** `grantXp()` calls
`queueMoveLearnIfNeeded()`, which only acts for a record with a battle entry or a saved custom
moveset (a boxed Pokémon can have the latter, so it could get a MoveLearn prompt), and
`queueEvolutionIfEligible()` on a level-up, which queues an Evolution prompt for any record.
`raiseCurrentHpByMaxHpGain()` is a no-op without a battle entry. So: if the Pokémon menu can resolve
MoveLearn/Evolution prompts for a boxed record, use `awardFixedXp`. If it cannot, apply the XP as a
plain record `Replace` of `totalXp` and let the existing catch-up paths handle evolution and moves
when it is withdrawn, as the spec says. Write down which one you chose and why in the PR.

## 2. Day Care UI and clearing

- PC Box > a Pokémon: a **Send to Day Care** action (boxed only, slot empty, feature on). Take back:
  **Take from Day Care** on that Pokémon (or on the Pokémon menu line). Use `sendToDayCare()`'s
  result for the message. Keep any change to `collectionActions()` inside `#if`; `CollectionAction`
  is not persisted, but the persisted enums in R2 stay untouched.
- Pokémon menu line: "Day Care: Eevee Lv 14" when occupied.
- After a successful `withdrawPokemon()`, `releasePokemon()`, or any other path that moves a boxed
  record into the Party: `clearDayCareIfHolds(id)` and commit extras. `reset()` already clears it.
- New strings: `STR_*` keys in `lib/I18n/translations/english.yaml` only.

## 3. Trainer Card

"Streak: 6 days (+30% XP)" from the same `StreakInput` and `streakBonusPercent()`. Hide the line when
the clock is invalid or the Streaks toggle is off.

## 4. Party sleep screen

- In `SleepActivity`, before the `switch` on the configured mode (around line 617 now), inside `#if`:
  if the toggle is on and `renderPartySleepScreen()` returns true, return. Any failure (save, art,
  extras, `layout.valid == false`) returns false and the configured screen draws as before.
- **No writes on the sleep path.** Read extras without seeding or committing anything (if
  `PokemonExtrasStore::load()` can write, for example to seed the RNG, add a read-only peek).
- Lines: lead art (`PokemonArt`), nickname, "Lv 14", XP bar (`levelXpProgress()` +
  `xpBarFillWidth()`), today's reading (`DailyReadingStats::summarize()`), streak, and the egg line.
  Eggs arrive in M3, so until then the egg line only shows if an extras file holds an egg; show
  "Egg: N pages to go" (the egg's total pages are an M3 decision).
- Check that wake-from-sleep still reopens the book and that the Game Boy wake-into-game path
  (`lastSleepFromGameBoy`) is unchanged.

## 5. Compat test (R10)

Extend `test/pokemon_extras_compat`: run a streak bonus, a milestone encounter and Day Care XP through
the host-test service on the v11 fixture, then decode with the upstream codec (version 11, 437-byte
state, every record valid).

## 6. Gate and PR

Same ladder as M1: native tests (`PokemonExtrasRulesTest`, `PokemonExtrasLayoutTest` included),
compat test, simulator on a throwaway save, then a copy of the real save:
- every toggle off, read 10 minutes: `pokemon-*.bin`, stats and sync files byte-identical to a
  run of the M1 build;
- toggles on: only the expected changes (lead XP, at most one queued encounter, the Day Care
  Pokémon's XP); the main save still decodes as upstream v11.

Screenshots: Settings > Extras (unchanged), PC Box action, Pokémon menu with Day Care line,
Trainer Card streak line, sleep screen with and without an egg and with no valid clock. Record the
firmware size in PROGRESS.md and open the PR into `extras` from `feat/extras-m2-rules`.
