# 1. Eggs

## What the player sees
- While reading, the party sometimes finds an egg. The Pokémon menu shows an **Egg** row with a
  progress bar ("412 pages to go").
- Every verified page turn brings it closer. When it reaches zero, the next visit to the Pokémon
  menu shows "The egg hatched!" with the species art, then the usual nickname question.
- Hatched Pokémon start at level 5, are more often a species not yet caught, have better hidden
  stats than a wild catch, and are shiny 1 time in 16 (wild is 1 in 64).

## Rules
- One egg at a time (v1).
- Egg find: rolled at each hourly reading boundary, 1 in 3, guaranteed after 3 misses, only when
  no egg is held. Uses the extras RNG, not the draws inside `applyCreditedMinutes()` (its tests
  script exact draw sequences).
- Species: a base-stage species (never a legendary, never an evolved form), uncaught species
  weighted x3, chosen when the egg is found and stored hidden.
- Pages to hatch: 300 common, 600 uncommon, 1200 rare (starters, Eevee, Dratini, fossils, Lapras,
  Snorlax, Chansey). Claude chat supplies the rarity table.
- Only verified manual page turns count (the same signal `onSuccessfulPageTurn` uses), forward only.
  Pages read on the Kindle also count, as reported by Kindle credit (feature 5).
- Party and PC Box both full: the egg waits at 0 pages until there is room.

## State (extras file)
`eggSpeciesId u16`, `eggPagesRemaining u16`, `eggFlags u8` (shiny, pre-rolled),
`eggMisses u8`, `pendingHatchRecordId u32`.

## Hooks
- `PokemonService::onSuccessfulPageTurn()`: decrement an in-RAM counter only.
- `PokemonService::creditMinutes()` after commit: flush the counter to extras; roll egg find at the
  hourly boundary.
- Pokémon menu open: if `eggPagesRemaining == 0`, run the hatch flow.
- Home accessory `!`: also shown when a hatch is waiting. Read this from extras; do not set
  `DashboardNotice` (SAVE-SAFETY R2).

## Safety
- Hatching appends a normal `PokemonRecord` through the same service path as a catch,
  `Origin::Caught`, with IV/EV written through `ensureIvEv`. No new `Origin` value.
- Crash safety: before appending, store the record id the hatch will use in
  `pendingHatchRecordId`. On load, if that record exists in the main save, clear the egg; if not,
  the hatch simply runs again. Never two records from one egg.

## Tests
Rules: find roll and pity, species weighting, page countdown, full-storage wait.
Compat: hatch into a v11 fixture, decode with the upstream codec.

## Tunables
Find chance, pages per rarity, shiny odds, IV floor.
