# 2. Reading streak bonus

## What the player sees
- Trainer Card shows "Streak: 6 days (+30% XP)".
- Every 7th streak day (7, 14, 21, ...) a rare wild Pokémon is guaranteed to appear.

## Rules
- Streak length comes from the firmware's existing reading stats:
  `GlobalReadingStats::currentReadingStreak(today)`. Extras does not keep its own calendar.
- Bonus: +5% reading XP per streak day, capped at +50% (10 days).
- Reading XP is 1 per minute, so the bonus is fractional. Keep a per-mille remainder in extras and
  grant whole XP through `awardFixedXp(leaderId, n)` when it accumulates.
- Milestone encounter: once per milestone, queue an ordinary wild `Encounter` drawn from the rare
  pool at the leader's level. If the pending queue has no free slot beyond the 2 reserved, retry at
  the next checkpoint.
- No valid clock (`getCurrentLocalReadingStatsDateTime` fails): bonus is 0, nothing else changes.
- Known limit: the streak is the firmware's own calendar of on-device reading days. A day read only
  on the Kindle does not extend it, because extras may not write reading statistics (R4). Kindle
  minutes still receive the current streak bonus when they are credited.

## State
`streakXpRemainderPermille u16`, `lastRewardedStreakMilestone u16`, `lastRewardedStreakDay u32`.

## Hooks
`PokemonService::creditMinutes()` after commit; Trainer Card render.

## Safety
Read-only on reading stats (R4). XP through `awardFixedXp` only (R3). Respects `MAXIMUM_TOTAL_XP`.

## Tests
Remainder math over many small credits, cap at 10 days, milestone granted once, no-clock path.

## Tunables
Percent per day, cap, milestone interval.
