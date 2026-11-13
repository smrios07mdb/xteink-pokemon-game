# 8. Daily quests

## What the player sees
Pokémon menu > **Quests**: three small goals with progress, for example
"Read 30 minutes 18/30", "Win a battle 0/1", "Catch a Pokémon 1/1 ✓".
Each completed quest gives an item. All three give a bonus.

## Rules
- Three quests per day, picked from the pool with a seed derived from the local day number, no
  duplicates.
- No valid clock: the set rotates every 120 credited reading minutes instead.
- Pool (v1): read 20 / 40 / 60 minutes; win a wild battle; win a trainer battle; catch any Pokémon;
  catch a species new to the Pokédex; turn 100 pages toward an egg (only offered while an egg is
  held); level up any Pokémon.
- Rewards: one ball or medicine per quest (existing bag grant); all three: one Rare Candy.
- Unfinished quests expire at rotation. Unclaimed rewards are granted automatically at rotation.

## State
`questDayAnchor u32`, `questIds[3] u8`, `questProgress[3] u16`, `questDoneMask u8`,
`questRewardedMask u8`.

## Hooks
`creditMinutes()` (minutes, level-ups; Kindle credit arrives through the same call and counts), battle end (wild/trainer wins), catch path, egg page
counter, Pokémon menu (new row and screen), Home accessory `!` when a reward is ready (from
extras, not `DashboardNotice`).

## Safety
Rewards go through the existing bag functions (R3). Reading stats are read-only (R4).

## Tests
Deterministic selection per day, no duplicates, egg quest only with an egg, rotation with and
without a clock, each event increments the right quest, rewards granted once.

## Tunables
Pool, targets, rewards, rotation length without a clock.
