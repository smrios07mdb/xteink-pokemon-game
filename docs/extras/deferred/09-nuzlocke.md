# 9. Nuzlocke mode (DEFERRED, do not build)

Parked on 2026-11-13 at Sergio's request. Kept only as a starting point if he revives it.

Off by default. The only feature that can delete Pokémon. Ships last (M6).

## What the player sees
- **Extras > Nuzlocke**: a screen explaining the rules, then two confirmations.
- A Pokémon that faints is released when the battle ends, and is listed in a **Memorial** screen
  (species, nickname, level, the book it fell in).
- In each book, only the first wild encounter can be caught. Later ones can be fought; balls are
  disabled with a short explanation.

## Rules
- Faint = release at battle end, through the existing `releasePokemon()`.
- Softened wipe (open decision in PLAN.md): if every party member fainted, the last one standing
  is kept at 1 HP so the party is never empty.
- First encounter per book: keyed by the book hash at the time the encounter was generated. Boss
  encounters (feature 3) and egg hatches are exempt.
- Turning the mode off is always allowed and takes effect immediately. Nothing is restored.
- Gym, Elite Four and Champion battles count: fainted Pokémon are released.

## State
`nuzlockeEnabled` (toggle bit), `nuzlockeBackupDone` bit, `encounterUsedBookHashes[64] u32`,
`memorial[48]`: `{speciesId u16, level u8, nickname[12]}`.

## Hooks
Battle end in `PokemonActivity`; ball selection in battle; encounter generation (to stamp the
book hash into extras, not into `PendingEvent`).

## Safety
- R7: before the first enable, copy every `pokemon-*.bin` to `/.crosspoint/pokemon-extras-backup/`
  and verify the copy. If the copy fails, do not enable.
- The enable flow states plainly that released Pokémon cannot be recovered.
- The release uses the existing service call, so IV/EV, moveset and battle entries are cleaned up
  the same way as a manual release. The Hall of Fame is a snapshot and is unaffected.

## Tests
Release on faint, softened wipe, first-encounter rule per book, exemptions, toggle off mid-book,
backup failure blocks enabling.

## Tunables
Strict vs softened wipe, exemptions.
