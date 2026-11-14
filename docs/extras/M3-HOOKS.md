# M3 hooks: what Claude Code wires on the Mac

The pure M3 rules are done on branch `feat/extras-m3-rules` (chat, 2026-11-14). This file is the
handoff for the device-side half: the service hooks, the UI, the compat test, the gate, and the PR.
Read [SAVE-SAFETY.md](SAVE-SAFETY.md) first. Specs: [01-eggs](features/01-eggs.md),
[08-daily-quests](features/08-daily-quests.md). M2's hooks
([M2-HOOKS.md](M2-HOOKS.md), `src/pokemon/PokemonServiceExtras.cpp`) are the pattern to follow.

## What is already there

| File | What |
|------|------|
| `lib/Pokemon/PokemonExtrasEggs.{h,cpp}` | Pool and rarity (`isEggPoolSpecies`, `eggRarity`, `eggTotalPages`), find (`eggFindBoundariesCrossed`, `rollEggFind`, `chooseEggSpecies`), pages (`applyEggPages`), progress (`eggProgress`, `isEggReadyToHatch`), hatch (`beginHatch`, `buildHatchedRecord`, `rollEggIvs`, `finishHatch`, `resolvePendingHatch`) |
| `lib/Pokemon/PokemonExtrasQuests.{h,cpp}` | Data (`questTarget`, `questRewardItem`, `questKindAt`), choice (`chooseQuestSet`, `questSeedForDay`), rotation and minutes (`updateDailyQuests`), events (`recordQuestBattleWin`, `recordQuestCatch`, `recordQuestEggPages`, `recordQuestLevelUp`), rewards (`questRewardsReadyMask`, `takeQuestRewards`, `grantQuestItem`) |
| `test/pokemon_extras/PokemonExtrasEggsTest.cpp`, `PokemonExtrasQuestsTest.cpp` | 34 test groups; both in `extras-checks.yml` |

**No codec change.** Every field was already in file v1. Two notes on how existing fields are used:
- The egg's total pages are not stored: `eggTotalPages(eggSpeciesId)` derives them from the rarity
  table, and `eggProgress()` never reports a total below the pages left.
- `questDayAnchor` holds the reading-stats day index (bit 31 clear) or, with no valid clock, bit 31
  set plus the credited minutes since the set was chosen (`QUEST_CLOCKLESS_ANCHOR_BIT`).

Decisions made in chat (tunables at the top of each header; change them there):
- Rarity: rare (1200 pages) = Bulbasaur, Charmander, Squirtle, Eevee, Dratini, Omanyte, Kabuto,
  Aerodactyl, Lapras, Snorlax, Chansey. Uncommon (600) = capture rate <= 150. Common (300) = the rest.
  Pikachu is common (capture rate 190). Ditto is not in the pool.
- Hatched IVs 6..15 per stat (shiny 12..15, upstream's shiny range). Shiny 1 in 16, rolled at find.
- Quests: at most one reading quest per set (Read 20/40/60 overlap). Rewards: Potion / Super Potion /
  Hyper Potion for Read 20/40/60, Poké Ball for a wild win or any catch, Great Ball for a trainer win
  or a new species, Super Potion for 100 egg pages or a level-up; Rare Candy for all three.
- Rewards are claimed on the Quests screen; ready rewards of an expiring set are granted at rotation.

## 1. On every extras load: settle a pending hatch

Where the extras store is loaded for the service (first use after boot), if `pendingHatchRecordId != 0`:
read that record from the main store, call `resolvePendingHatch(copy, found ? &record : nullptr)`,
commit extras. `Finished` = the hatch had reached the main save; `Retry` = it had not, and the egg
hatches again at the next menu open. Never skip this: it is what makes "never two records from one
egg" true.

## 2. creditMinutes() hook (extend `applyExtrasAfterCredit`)

Update the early return: it must also run when Eggs or Daily quests are on.

1. **Lifetime minutes:** `creditMinutes()` must pass `lifetimeMinutes` from before the credit (read it
   before `applyCreditedMinutes`); after the commit, `boundaries = eggFindBoundariesCrossed(before, after)`.
2. **Egg pages:** flush the in-RAM page counter (section 3): `counted = applyEggPages(copy, pages)`,
   then `recordQuestEggPages(copy, counted)`. Reset the RAM counter only once extras commits.
3. **Egg find:** `rollEggFind(copy, mainState, boundaries)`. Extras only.
4. **Quests:** `QuestClock` from the same clock read as the streak (`getCurrentLocalReadingStatsDateTime`,
   `readingStatsDayIndex`; read-only, R4). `updateDailyQuests(copy, clock, hasEgg(copy), minutes, expired)`.
   Level-ups: compare the lead's level before and after the credit; if it rose,
   `recordQuestLevelUp(copy)` (once per credit is enough for a 1-step quest).
   If `expired.count > 0`, grant them with the reward order in section 6.
5. Commit extras once at the end if the copy changed (as now).

Kindle minutes arrive through the same `creditMinutes()` (M4), so they count for quests with no extra
work. Kindle pages for eggs are M4's job (feature 5 reports them).

## 3. Page turns

`PokemonService::onSuccessfulPageTurn()`: when the tracker counts the turn (reading session active,
forward, verified; the same signal it already uses), increment an in-RAM `uint32_t` only. **No SD
write per page.** It is flushed at the next credit checkpoint (section 2). Pages turned after the last
checkpoint of a session are flushed by `flushOnExit` when it credits; pages lost to a crash are fine.

## 4. Hatch flow (Pokémon menu open)

If `isEggReadyToHatch(extras)` and the toggle is on:
1. Storage: if `catchBlockedByFullBox(partyCount, totalRecords)`, show the Egg row at 0 pages and stop
   (the egg waits).
2. Show "The egg hatched!" with the species art, then the usual nickname question.
3. `id = store_.nextRecordId()`; `beginHatch(copy, id)`; **commit extras**. Stop if that fails.
4. Roll gender with `chooseGenderForSpecies(species, random_, gender)`. Load the main state;
   `buildHatchedRecord(copy, state, id, gender, nickname, mutation)`; commit the main save with
   `mutation` through the same store path `resolveEncounter()` uses for a catch (Append, record id `id`).
5. IVs: `rollEggIvs(random_, shiny, entry.iv)` and upsert the IV/EV entry for `id` **before anything
   calls `ensureIvEv(id)`** (so the floor sticks; `ensureIvEv` finds the entry and keeps it).
6. `finishHatch(copy)`; commit extras. Then the quest events of a catch (section 5).

If step 4 fails, run `resolvePendingHatch(copy, nullptr)` and commit extras (the egg stays ready).

## 5. Battle and catch events

- Wild battle won: `recordQuestBattleWin(copy, false)`. Road trainer won (`resolveTrainer(true, ...)`)
  or gym/Elite Four/Champion won (`markGymDefeated`): `recordQuestBattleWin(copy, true)`.
- Catch (`resolveEncounter` Catch, `attemptBattleCatch` success) and a hatch:
  `recordQuestCatch(copy, newToPokedex)` with `newToPokedex` read before the catch marked it caught.
- Battle XP level-ups: `recordQuestLevelUp(copy)` if the level rose.
- Each: load extras, apply, commit extras if changed. Extras failures never block the battle or catch.

## 6. Quests screen and rewards

- Pokémon menu > **Quests**: three rows from `questKindAt`, `questProgress`, `questTarget`
  ("Read 30 minutes 18/30", "Win a battle 0/1", "Catch a Pokémon 1/1 ✓").
  Opening it calls `updateDailyQuests(copy, clock, hasEgg, 0, expired)` first (a new day may have begun).
- **Claim** (one button for all ready rewards): `rewards = takeQuestRewards(copy)`; **commit extras
  first**; then load the main state, `grantQuestItem(state, id)` for each, `store_.commit(state)`. If the
  main commit fails, restore the extras copy and commit it again. A crash in between loses those items,
  never duplicates them. Same order for `expired` rewards at rotation.
- `grantQuestItem` mirrors upstream's file-local `creditItem()` (bag +1, "items received" summary,
  dashboard notice refresh). It only accepts bag items (ids 7..83). If you would rather call an
  upstream function, there is none public for "one given item"; do not edit `PokemonGame.cpp` for it.

## 7. Home `!` and sleep screen

- Home accessory `!`: also when `isEggReadyToHatch(extras)` or `questRewardsReadyMask(extras) != 0`.
  Read from extras; never set `DashboardNotice` for it (R2).
- Pokémon menu **Egg** row: "412 pages to go" with a bar from `eggProgress()`.
- Party sleep screen: use `eggProgress()` for the egg line; `totalPages` is now known, so the bar can
  use `eggProgressPercent(pagesRemaining, totalPages)`. Still read-only on the sleep path.
- New strings: `STR_*` keys in `lib/I18n/translations/english.yaml` only.

## 8. Compat test (R10)

Extend `test/pokemon_extras_compat`: on the v11 fixture, find an egg, count it down, hatch it through
the host-test service (all three commits), run a crash between commit 1 and 2 and between 2 and 3 and
check one record per egg; finish a quest set and claim its rewards; then decode with the upstream codec
(version 11, 437-byte state, every record valid, hatched record `Origin::Caught`).

## 9. Gate and PR

Same ladder as M2: native tests (the two new targets included), compat test, simulator on a throwaway
save, then a copy of the real save (never the SD card):
- every toggle off, read 10 minutes: `pokemon-*.bin`, stats and sync files byte-identical to a run of
  the M2 build (`extras` at the time);
- toggles on: only the expected changes (one egg found, its pages counted, one hatch appended as a
  catch, quest items in the bag); the main save still decodes as upstream v11.

Screenshots: Pokémon menu with the Egg row and Quests row, the hatch screen, the Quests screen before
and after claiming, the Home `!`, the Party sleep screen with the egg bar. Record the firmware size in
PROGRESS.md and open the PR into `extras` from a new branch `feat/extras-m3-hooks` (off `extras` after
this branch merges).
