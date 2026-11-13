# 5. Kindle credit

Sergio reads the same books on a Kindle (KOReader) and syncs progress to the X4 Pro. Today those
pages earn nothing. Decision (2026-11-13): for the game, Kindle reading counts exactly like
on-device reading. Same rate, same rewards.

## What the player sees
After a sync moves the book forward: "Read on another device: 42 minutes credited" on the next
Pokémon menu visit. Everything those minutes would have earned on the X4 Pro has happened: XP,
wild encounters, trainers, item finds, healing, egg progress, quest progress, Battle Passes.

## Rules
- Trigger: a synced remote position is applied and it is ahead of the local position by more than
  0.1 percentage points.
- Pages = forward delta (fraction of book) x the book's page count.
- Minutes = pages x this reader's own average minutes per page (firmware reading stats, read-only;
  prefer the book's `avgSecondsPerForwardPage`, else the global totals, else 1.0).
- Credit those minutes through the normal reading path, `PokemonService::creditMinutes(minutes,
  bookProgressPercent)`, at 100%. No separate rate, no daily cap.
- Pages also count toward the egg (feature 1).
- High-water mark per book: credit only progress beyond the highest fraction already credited
  or read on the X4 Pro for that document. Syncing twice, or jumping back and forward, earns
  nothing. This is duplicate protection, not a reduced rate.
- Mis-sync guard: one sync credits at most 600 minutes. A wrong match that jumps a book from 0 to
  100% should not be able to dump days of progress in one step. Anything above the guard is
  dropped and logged. Tunable; set to 0 to disable.
- Large credits: feed `creditMinutes` in chunks of at most 60 so hourly rolls fire as they would
  have, and stop early if the pending-event queue is full (remaining minutes still grant XP).

## State
`kindleMarks[64]`: `{documentHash u32, highWaterPermille u16}` (drop oldest when full),
`kindlePendingNoticeMinutes u16`.

## Hooks (one line each, behind the build flag)
- `EpubReaderActivity::applyKeptPosition()` after `saveProgress` succeeds.
- `KOReaderSyncActivity`, the branch that applies the remote position.
- Both call `extras::onRemoteProgressApplied(documentId, localFraction, remoteFraction, bookPages)`.
- Local reading also raises the high-water mark (at each checkpoint), so pages read on the X4 Pro
  are never credited a second time by a later sync.

## Safety
- The Pokémon save changes only through the existing `creditMinutes` path (R3). That includes the
  game's own Trainer Card reading time (`lifetimeMinutes`), which is part of the Pokémon save.
- The firmware's reading statistics are a different thing and stay untouched (R4): no time, pages,
  sessions, or streak days are added for Kindle reading. So the stats screens keep showing only
  what was read on the X4 Pro, and a Kindle-only day does not extend the streak.
- This is the only feature that edits reader and sync source files. The hook must run after the
  sync logic has finished and must not alter its control flow; any failure inside it is swallowed.
- After M4 lands on the device: verify Kindle to X4 Pro and X4 Pro to Kindle sync still work.

## Tests
Delta to minutes math, pace fallbacks, high-water mark (repeat sync, backward jump, local reading
then sync), mis-sync guard, chunking, full-queue behavior. Compat test after a large credit.

## Tunables
Mis-sync guard (600 minutes), chunk size.
