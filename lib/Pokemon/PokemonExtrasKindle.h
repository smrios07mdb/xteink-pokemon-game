#pragma once

// Extras M4 rules, feature 5: Kindle credit (docs/extras/features/05-kindle-credit.md).
// Pure, storage-free, host-testable. The hooks call these and do the writes
// (docs/extras/M4-HOOKS.md):
//   - the Pokemon save changes only through PokemonService::creditMinutes()
//     and awardFixedXp() (SAVE-SAFETY R3);
//   - the firmware's reading stats and sync state are only read, by the
//     caller, and passed in as plain numbers (R4);
//   - every field lives in the extras file; no codec change (R1, R2).
//
// Needs no species data: links PokemonExtras.cpp only. Compiles to nothing
// without CROSSINK_POKEMON_EXTRAS (R11).

#include <cstdint>

#include "PokemonExtras.h"

namespace pokemon::extras {

// ---------------------------------------------------------------------------
// Tunables
// ---------------------------------------------------------------------------
// A sync must move the book forward by at least this much to earn anything
// (2 per mille = more than 0.1 percentage points once both ends are floored).
constexpr uint16_t KINDLE_MIN_DELTA_PERMILLE = 2;
// Mis-sync guard: one sync credits at most this many minutes; the rest is
// dropped (and logged by the caller). 0 disables the guard.
constexpr uint16_t KINDLE_GUARD_MINUTES = 600;
// creditMinutes() is fed at most this many minutes at a time, so the hourly
// rolls fire as they would have while reading.
constexpr uint16_t KINDLE_CHUNK_MINUTES = 60;
// Pace: the book's own average, else the reader's global average, else this.
constexpr uint16_t KINDLE_DEFAULT_SECONDS_PER_PAGE = 60;
// A measured pace outside this range is treated as junk (a skimmed or idle
// book) and clamped. This is not a reduced rate: it only bounds bad stats.
constexpr uint16_t KINDLE_PACE_MIN_SECONDS = 10;
constexpr uint16_t KINDLE_PACE_MAX_SECONDS = 300;

// ---------------------------------------------------------------------------
// Positions
// ---------------------------------------------------------------------------
// A book position as a fraction 0.0..1.0, floored to per mille (0..1000).
// Out-of-range and NaN inputs clamp to 0 or 1000.
uint16_t fractionToPermille(float fraction);

// ---------------------------------------------------------------------------
// High-water marks
// ---------------------------------------------------------------------------
// One mark per document: the highest position already credited by a sync or
// read on the X4 Pro. `documentHash` is stableIdHash() of the book's stable
// id; every hook must use the same id for the same book (M4-HOOKS.md).
// 0 for an unknown document.
uint16_t kindleHighWaterPermille(const ExtrasState& state, uint32_t documentHash);

// Raises the document's mark to `permille` if that is higher (never lowers
// it) and moves it to the most-recently-used end; a new document drops the
// least recently used mark when all KINDLE_MARK_CAPACITY are taken. Returns
// true when the state changed. Ignores documentHash 0. Not gated on the
// toggle: use noteLocalProgress() from the hooks.
bool raiseKindleMark(ExtrasState& state, uint32_t documentHash, uint16_t permille);

// Local reading (each credit checkpoint, with the open book's position): keeps
// the mark current so pages read on the X4 Pro are never credited again by a
// later sync. Does nothing with the Kindle credit toggle off. Returns true
// when the state changed (extras needs a save).
bool noteLocalProgress(ExtrasState& state, uint32_t documentHash, uint16_t permille);

// ---------------------------------------------------------------------------
// Pace
// ---------------------------------------------------------------------------
// What the caller read from the firmware's reading stats (read-only, R4).
// Zero means "not known".
struct KindlePace {
  uint16_t bookSecondsPerPage = 0;    // BookReadingStats::avgSecondsPerForwardPage
  uint32_t globalReadingSeconds = 0;  // GlobalReadingStats::totalReadingSeconds
  uint32_t globalPagesTurned = 0;     // GlobalReadingStats::totalPagesTurned
};
// Seconds per page: the book's average, else global seconds / pages, else
// KINDLE_DEFAULT_SECONDS_PER_PAGE; a measured value is clamped to
// KINDLE_PACE_MIN_SECONDS..KINDLE_PACE_MAX_SECONDS.
uint16_t kindleSecondsPerPage(const KindlePace& pace);

// ---------------------------------------------------------------------------
// A sync was applied
// ---------------------------------------------------------------------------
struct KindleSync {
  uint32_t documentHash = 0;
  uint16_t localPermille = 0;   // this reader's position before the remote one was applied
  uint16_t remotePermille = 0;  // the position that was applied
  uint32_t bookPages = 0;       // the book's page count on this reader; 0 = not known
  KindlePace pace{};
};

struct KindleCredit {
  uint16_t minutes = 0;         // to credit through creditMinutes(), already limited by the guard
  uint16_t droppedMinutes = 0;  // cut by the guard (saturates at 65535); log it
  uint32_t pages = 0;           // pages for the egg, scaled down with the guard
  uint16_t highWaterPermille = 0;  // the document's mark after this sync

  bool operator==(const KindleCredit&) const = default;
};

// What a sync would earn, without changing anything. Zero credit when the
// toggle is off, the document hash is 0, the remote position is not at least
// KINDLE_MIN_DELTA_PERMILLE beyond the higher of the local position and the
// document's mark, or the page count is not known.
//   pages   = forward delta x bookPages
//   minutes = pages x kindleSecondsPerPage(), full rate, no daily cap
KindleCredit planKindleCredit(const ExtrasState& state, const KindleSync& sync);

// planKindleCredit(), plus: raises the document's mark to the higher of the
// local and remote positions (also when nothing is earned, so a repeat sync
// or a jump back and forward earns nothing). Returns the same plan.
//
// Crash safety (R6; "extras first, lose rather than duplicate"): save EXTRAS
// with the raised mark BEFORE crediting the minutes to the main save. A crash
// in between loses those minutes; it can never credit them twice. If the
// extras save fails, credit nothing.
KindleCredit applyKindleSync(ExtrasState& state, const KindleSync& sync);

// ---------------------------------------------------------------------------
// Crediting in chunks
// ---------------------------------------------------------------------------
// The next step for `remainingMinutes`. With queue room (hasEncounterRoom() on
// the main state, PokemonExtrasRules.h): creditMinutes(creditMinutes, ...) for
// up to KINDLE_CHUNK_MINUTES. Without room: stop rolling and grant all the
// remaining minutes as XP only (awardFixedXp to the lead, 1 XP per minute).
// Both fields are 0 when nothing remains.
struct KindleStep {
  uint16_t creditMinutes = 0;
  uint16_t xpOnlyMinutes = 0;
};
KindleStep nextKindleStep(uint16_t remainingMinutes, bool queueHasRoom);

// ---------------------------------------------------------------------------
// Notice ("Read on another device: 42 minutes credited")
// ---------------------------------------------------------------------------
// Add the minutes that were really credited, after the main save commit
// (saturates at 65535). Shown on the next Pokemon menu visit.
void addKindleNotice(ExtrasState& state, uint16_t minutes);
// Returns the minutes to show (0 = nothing) and clears them. Save extras after.
uint16_t takeKindleNotice(ExtrasState& state);

}  // namespace pokemon::extras
