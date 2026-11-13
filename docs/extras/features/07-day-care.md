# 7. Day Care

## What the player sees
PC Box > a Pokémon > **Send to Day Care**. One Pokémon at a time. It gains XP while the lead
trains. The Pokémon menu shows "Day Care: Eevee Lv 14".

## Rules
- Only a boxed Pokémon (not in the Party) can be in Day Care.
- It earns 50% of the reading XP the lead earns (1 XP per 2 credited minutes; keep the odd minute
  as a remainder in extras).
- It cannot pass the lead's level.
- Withdrawing it to the Party, releasing it, or resetting the game clears the Day Care slot.

## State
`dayCareRecordId u32`, `dayCareMinuteRemainder u8`.

## Hooks
`PokemonService::creditMinutes()` after commit; PC actions list; `withdrawPokemon`,
`releasePokemon`, `reset`.

## Safety
- XP through `awardFixedXp(dayCareRecordId, n)`.
- **Claude Code must verify first** that `awardFixedXp` is safe for a boxed record: it raises
  current HP and may queue MoveLearn/Evolution prompts, and the battle store only holds the Party.
  If it is not box-safe, apply XP as a plain record `Replace` of `totalXp` and rely on
  `queueEvolutionIfEligible()` (idempotent, backfills) and move learning when the Pokémon is next
  withdrawn. Do not queue prompts for a boxed Pokémon if the UI cannot resolve them.
- A stale `dayCareRecordId` (record no longer exists) is cleared silently.

## Tests
Half-rate remainder, level cap at the lead's level, slot cleared on withdraw/release/reset.

## Tunables
Rate, level cap rule.
