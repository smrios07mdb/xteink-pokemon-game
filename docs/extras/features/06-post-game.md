# 6. Post-game: rematches and Battle Tower

Unlocks when the Champion is beaten (bit 12 of `PokemonState::battleProgress`).

## What the player sees
- **Gym Battle** list: beaten leaders show **Rematch**. Their teams are stronger each time.
- New menu row **Battle Tower**: fight trainer after trainer; the Trainer Card shows the best run.
- Both cost a **Battle Pass**. Passes come from reading.

## Rules
- Battle Pass: 1 per 60 credited reading minutes after unlock, hold at most 3. Clock-independent.
- Rematch: one pass. The leader's real team, each level raised to
  `max(original, partyTopLevel - (13 - gymIndex))`, capped at 100. XP through the existing
  `battleVictoryXp` path. Does not call `markGymDefeated()` and does not touch badges or the Hall
  of Fame.
- Battle Tower: one pass per run. Teams of 3 built with the road-trainer generator
  (`lib/Pokemon/PokemonTrainers.*`) from a stored seed, level `min(100, 50 + 2 x wins)`. Party is
  healed between battles. The run ends on a loss or when the player leaves. Every 5th win grants an
  item via `grantTrainerReward()`.
- Losing costs nothing but the pass.

## State
`battlePasses u8`, `passMinuteAnchor u32` (lifetimeMinutes at last accrual),
`rematchWins[13] u8`, `towerBestStreak u16`, `towerRunSeed u32`, `towerRunWins u16`.

## Hooks
`PokemonService::creditMinutes()` (pass accrual); `PokemonActivity` Gym list and battle-end flow;
new `Screen` values in `PokemonActivity` (an in-memory enum, not persisted, so R2 does not apply).

## Safety
Uses existing battle and XP paths only (R3). `battleProgress` is read, never written.

## Tests
Pass accrual and cap, rematch level scaling for all 13, tower level curve, reward every 5 wins,
run seed reproducibility.

## Tunables
Minutes per pass, pass cap, level formulas, reward interval.
