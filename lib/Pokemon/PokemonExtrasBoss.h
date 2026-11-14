#pragma once

// Extras M4 rules, feature 3: book-finish boss (docs/extras/features/03-book-boss.md).
// Pure, storage-free, host-testable. The hooks call these and do the writes
// (docs/extras/M4-HOOKS.md):
//   - the boss is an ordinary PendingEventKind::Encounter and its item goes
//     into the bag like any drop (SAVE-SAFETY R2, R3);
//   - the book's reading time is only read, by the caller, and passed in (R4);
//   - every field lives in the extras file; no codec change (R1).
//
// Needs species data, so it links PokemonTypes/PokemonSpecies/PokemonGame, plus
// PokemonExtrasRules (queue room), PokemonExtrasQuests (bag grant) and
// PokemonExtrasKindle (synced progress). Compiles to nothing without
// CROSSINK_POKEMON_EXTRAS (R11).

#include <array>
#include <cstdint>

#include "PokemonExtras.h"
#include "PokemonGame.h"
#include "PokemonTypes.h"

namespace pokemon::extras {

// ---------------------------------------------------------------------------
// Tunables
// ---------------------------------------------------------------------------
// Anti-farm: a finished book earns a boss only with this much recorded reading
// time on this device (BookReadingStats::totalReadingSeconds) ...
constexpr uint32_t BOSS_MIN_READING_SECONDS = 60U * 60U;
// ... or, with Kindle credit on, when the book's high-water mark (read here or
// credited from a sync) has reached this position. Kindle reading counts like
// on-device reading (decision 2026-11-13), so a book read mostly on the Kindle
// still earns its boss. 0 = device time only.
constexpr uint16_t BOSS_MIN_SYNCED_PERMILLE = 800;
// Every boss appears at the party lead's level plus this (clamped to 2..100).
constexpr uint8_t BOSS_LEVEL_OFFSET = 5;
// Boss order, first match wins: an uncaught legendary bird, Mewtwo if
// uncaught, otherwise one of the rare pool. Mew (151) is never a boss.
constexpr std::array<uint16_t, 3> BOSS_LEGENDARY_BIRDS{144, 145, 146};
constexpr uint16_t BOSS_MEWTWO = 150;
// Dragonite, Snorlax, Lapras, Gyarados, Alakazam, Gengar, Machamp, Arcanine.
constexpr std::array<uint16_t, 8> BOSS_RARE_POOL{149, 143, 131, 130, 65, 94, 68, 59};
constexpr uint32_t BOSS_UNCAUGHT_WEIGHT = 3;  // rare pool: uncaught species weigh 3x
// Reward alongside the boss: one Master Ball; three Ultra Balls if the bag
// cannot take another Master Ball.
constexpr uint8_t BOSS_ITEM_ULTRA_BALL = 9;
constexpr uint8_t BOSS_ITEM_MASTER_BALL = 10;
constexpr uint8_t BOSS_ULTRA_BALL_COUNT = 3;

// ---------------------------------------------------------------------------
// A book was marked finished
// ---------------------------------------------------------------------------
struct BookFinish {
  // stableIdHash() of the book's stable id: the same id the Kindle credit
  // hooks use for this book (M4-HOOKS.md).
  uint32_t bookHash = 0;
  uint32_t deviceReadingSeconds = 0;  // BookReadingStats::totalReadingSeconds, read-only
};

enum class BookFinishResult : uint8_t {
  Ignored = 0,             // toggle off or hash 0: nothing changed
  AlreadyCounted = 1,      // this book was counted before: nothing changed
  RibbonOnly = 2,          // counted, ribbon +1; not enough reading for a boss
  BossAlreadyWaiting = 3,  // counted, ribbon +1; an earlier boss is still held, so no new one
  BossHeld = 4,            // counted, ribbon +1; a boss is now held for delivery
};

bool isBookCounted(const ExtrasState& state, uint32_t bookHash);
// The anti-farm rule above.
bool bookEarnsBoss(const ExtrasState& state, const BookFinish& book);

// Call when a book goes from not-completed to completed, after the firmware's
// own stats update. Each book counts once, ever: un-marking and re-marking
// does nothing (the list keeps the last REWARDED_BOOK_CAPACITY books, oldest
// dropped). The boss is never queued here: it is held in heldBossSpeciesId and
// delivered by the steps below, so a full queue and a crash are handled the
// same way. Uses the extras RNG. Extras-only change: save extras after any
// result other than Ignored / AlreadyCounted.
BookFinishResult onBookFinished(ExtrasState& state, const PokemonState& mainState, const BookFinish& book);

// The boss for the current Pokedex. False when the random source is missing.
bool chooseBossSpecies(const PokemonState& mainState, const RandomSource& random, uint16_t& speciesId);

// ---------------------------------------------------------------------------
// Delivering the held boss
// ---------------------------------------------------------------------------
// Try right after onBookFinished(), at every credit checkpoint and on Pokemon
// menu open, while hasHeldBoss() and hasEncounterRoom(mainState):
//   1. species = takeHeldBoss(copy); save EXTRAS.
//   2. deliverBoss(mainStateCopy, species, leadLevel, random, reward); commit
//      the MAIN save (roll shininess on the queued event as creditMinutes() does).
//   3. If step 2 fails, put the species back (holdBoss) and save extras again.
// A crash between 1 and 2 loses that boss; it can never queue two (R6,
// "extras first, lose rather than duplicate").
bool hasHeldBoss(const ExtrasState& state);
// Returns the held species (0 = none) and clears it.
uint16_t takeHeldBoss(ExtrasState& state);
// Puts a species back after a failed main commit. False (state untouched) for
// an invalid species or when another boss is already held.
bool holdBoss(ExtrasState& state, uint16_t speciesId);

uint8_t bossLevel(uint8_t leaderLevel);

struct BossReward {
  uint8_t itemId = 0;  // 0 = the bag could take neither ball
  uint8_t count = 0;
};

// Queues the boss as an ordinary wild Encounter at bossLevel(leaderLevel),
// marks it seen, puts the reward in the bag and refreshes the dashboard
// notice. All or nothing: returns false and leaves `mainState` unchanged when
// there is no queue room outside PENDING_EVENT_PROMPT_RESERVE, the species is
// not a valid boss (0, Mew, out of range) or the random source is missing. A
// full bag slot never blocks the boss: the reward is then smaller or none.
bool deliverBoss(PokemonState& mainState, uint16_t speciesId, uint8_t leaderLevel, const RandomSource& random,
                 BossReward& reward);

// For the Trainer Card ("Books finished").
uint16_t booksFinishedRibbons(const ExtrasState& state);

}  // namespace pokemon::extras
