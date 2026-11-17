# M5 hooks: post-game rematches, Battle Tower, Day Care marker

Status 2026-11-17: built as planned below, with these changes made while building (see "As built").

Rules: `lib/Pokemon/PokemonExtrasPostGame.{h,cpp}`, tested by `PokemonExtrasPostGameTest`
(rules PR `feat/extras-m5-rules`). This file is the plan for the hooks PR. Every line below sits
behind `#if defined(CROSSINK_POKEMON_EXTRAS)`; flag off, the build is upstream's.

## Safety (SAVE-SAFETY R1-R6)
- `battleProgress` is read, never written. A rematch never calls `markGymDefeated()` or
  `captureHallOfFame()`: badges and the Hall of Fame stay as they are.
- XP only through the existing battle-end path (`battleVictoryXp` in `PokemonService`), items only
  through `grantTrainerReward()`. No new record kinds, no main-save format change.
- Order of writes, "extras first, lose rather than duplicate":
  - pass: `spendBattlePass()` / `startTowerRun()` → save extras → battle starts;
  - tower win: `recordTowerWin()` → save extras → (reward due) `grantTrainerReward()` on a main copy
    and commit main;
  - rematch win: battle flow's XP commit (main) → `recordRematchWin()` → save extras (a crash in
    between loses only the counter).
- New `Screen` values are in-memory only (R2 does not apply).

## Hook points

| Event | Where | Call |
|-------|-------|------|
| Minutes credited | `PokemonService::creditMinutes()` after `store_.commit`, next to the other extras checkpoints | `accrueBattlePasses(extras, committedState)`; save extras if it returned > 0 or the anchor moved |
| Gym list draw | `PokemonActivity` `Screen::GymList` render (~4414) | beaten rows: "Rematch" (+ "x N" from `rematchWinsFor`) when `postGameAvailable`; header shows passes held |
| Gym list confirm | `Screen::GymList` confirm (~3428): today `Available` only | `Defeated` and `canRematch()` → confirm prompt "Use a Battle Pass?" → `spendBattlePass` + save extras → `enterGymBattle(gymIndex)` with `rematch_ = true`. No pass: message "Battle Passes come from reading: 1 per hour" |
| Rematch team | the team getter at ~2076 (`return gymTeamFor(gymChallengeIndex_)`) | when `rematch_`: `buildRematchTeam(gymIndex, partyTopLevel, starterSpeciesId, team)`; party top level = highest level in the party |
| Rematch end | `finishGymChallenge()` (~2507) | when `rematch_`: skip `markGymDefeated()` and Hall of Fame; win → `recordRematchWin` + save extras, message "<Leader> defeated again!"; loss → existing loss message. Clear `rematch_` |
| Battle Tower menu row | Menu list (after Gym Battle) | only when `postGameAvailable`; opens new `Screen::Tower`: passes, current run wins, best streak, "Start run (1 pass)" / "Next battle" / "Leave tower" |
| Tower battle | reuse the road-trainer battle (`ROAD_TRAINER_INDEX` path) with a tower flag | team from `towerOpponent()` (`TowerOpponent::team`), party healed before each battle |
| Tower win | `finishTrainerBattle()` when the tower flag is set | `recordTowerWin` → save extras → `rewardDue`: `grantTrainerReward(copy, rewardLevel, …)` + main commit; back to `Screen::Tower` with "Win N" (+ item line) |
| Tower loss / Leave | same / Tower screen | `endTowerRun` + save extras; message "Run over: N wins (best M)" |
| Trainer Card | `renderTrainerCard()` next to "Books finished" | "Battle Passes: N" and "Tower best: M" once post-game is unlocked |
| Day Care marker | PC Box list render and the Pokémon detail screen | `isInDayCare(extras, recordId)`: "DC" tag in the list row, "In Day Care" line on the detail screen. Display only |
| Game reset | already covered: `resetExtrasProgress()` clears every post-game field |

A tower run survives sleep and reboot (`towerRunSeed` is stored); `towerOpponent()` rebuilds the
same next trainer from `(towerRunSeed, towerRunWins)`.

## Gate (on copies of the real save, as in M4)
- Champion not beaten on the real save: Rematch and Battle Tower stay hidden, nothing written to
  the main save. Seed a test copy with all 13 bits only in the simulator fixture, never on the card.
- Rematch win and loss, tower 5-win reward, crash between extras save and main commit: main save
  decodes as v11 (compat CHECK_ONLY), `battleProgress`, badges and Hall of Fame unchanged.
- Toggle off: no rows, no passes, nothing written.
- Screenshots: Gym list with Rematch, rematch win, Tower screen, 5th-win reward, Trainer Card,
  PC Box "DC" tag, detail "In Day Care".

## As built (2026-11-17, `feat/extras-m5-hooks`)
- **Battle Tower is the last row of the Gym Battle list** (after the Champion, subtitle "Battle
  Passes: N"), not a Menu button: the Menu is a fixed touch grid. Hidden until post-game is open.
- **Beaten gyms say "Rematch"** (or "Rematch (won N)") on the subtitle line; on the value slot it
  cut long names. No confirm prompt: a tap spends the pass, a forfeit loses it.
- **Tower screen** (one column): Battle Passes, Best run, [This run: N wins, next Lv L], then
  Start a run (1 pass) / Next battle / Leave the tower.
- **Tower battles** use the road-trainer battle path with `towerBattle_` set; AI skill 3 (Elite
  Four). Win XP: `battleVictoryXp(member level, player level)` summed over the trainer's team, to
  the Pokémon that finished the battle (road-trainer faints give EVs only). The party is healed
  (HP, PP, status) in the battle store **after each win**, not at the start of a run. Run or a loss
  ends the run.
- **Rematch team:** `buildRematchTeam()` with the party's highest level; the battle flow's own
  Champion final-slot swap is skipped for a rematch (already swapped and scaled).
- **Day Care marker:** PC Box subtitle `Lv. N  -  DC`; Summary shows "In Day Care" under the name.
- **Trainer Card:** "Battle Passes: N  -  Tower best: M" once post-game is open.
- Rematch and tower wins also count as a trainer win for the daily quests.

Gate results: see PROGRESS.md, M5 row.
