# 3. Book-finish boss

## What the player sees
- Marking a book finished triggers "Something powerful was drawn to your reading..." and a boss
  encounter waits in the Pokémon menu.
- The Trainer Card gains a **Books finished** ribbon count.

## Rules
- Trigger: a book goes from not-completed to completed (the existing "mark finished" action).
- Each book rewards once, ever. Un-marking and re-marking does nothing.
- Anti-farm: the book needs at least 60 minutes of recorded reading time on this device
  (`BookReadingStats::totalReadingSeconds`, read-only). Below that, only the ribbon count rises.
- Boss choice, first match wins:
  1. an uncaught legendary bird (144, 145, 146)
  2. Mewtwo (150) if uncaught
  3. otherwise a rare species (Dragonite, Snorlax, Lapras, Gyarados, Alakazam...) at leader level + 5
- Reward item alongside: one Master Ball if a bag item for it exists, else three Ultra Balls
  (Claude Code: check the item table; `BallKind::Master` exists in `PokemonBattle.h`).
- Queue full: the boss is held in extras and queued at the next checkpoint.

## State
`rewardedBookCount u16`, `rewardedBookHashes[128] u32` (FNV-1a of the book's stable id; when full,
drop the oldest), `heldBossSpeciesId u16`, `ribbonCount u16`.

## Hooks
- `BookStatsActivity::applyCompletedState(true)`.
- `XtcReaderActivity.cpp` where `stats.isCompleted` is set.
- Claude Code: list every other place a book becomes completed (end-of-book options, power-button
  shortcut, chord, TXT reader) and hook each one through a single helper.

## Safety
- The boss is an ordinary `PendingEventKind::Encounter` (R2, R3).
- The hook runs after the firmware's own stats update and never changes it (R4).
- Mew (151) is never a boss. It stays under upstream's own rule (`mewIsReady()`: the other 150
  caught first).

## Tests
Once-per-book, anti-farm threshold, boss order against Pokédex state, Mew never chosen, held boss when queue is full.

## Tunables
Minimum reading minutes, boss order, rare pool, level offset.
