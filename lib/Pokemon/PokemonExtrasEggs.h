#pragma once

// Extras M3 rules, feature 1: eggs (docs/extras/features/01-eggs.md). Pure,
// storage-free, host-testable, like PokemonExtrasRules. The service hooks call
// these and do the writes (docs/extras/M3-HOOKS.md):
//   - a hatch appends an ordinary PokemonRecord, Origin::Caught, through the
//     same RecordMutationKind::Append path as a catch (SAVE-SAFETY R3);
//   - nothing here reads or writes reading stats (R4);
//   - every egg field lives in the extras file; no codec change (R1, R2).
//
// Needs species data, so it links PokemonTypes/PokemonSpecies/PokemonGame.
// Compiles to nothing without CROSSINK_POKEMON_EXTRAS (R11).

#include <array>
#include <cstdint>

#include "PokemonBattleTypes.h"
#include "PokemonExtras.h"
#include "PokemonGame.h"
#include "PokemonSpecies.h"
#include "PokemonTypes.h"

namespace pokemon::extras {

// ---------------------------------------------------------------------------
// Tunables
// ---------------------------------------------------------------------------
constexpr uint32_t EGG_FIND_MINUTES = 60;          // one roll per hourly reading boundary
constexpr uint32_t EGG_FIND_CHANCE_DENOMINATOR = 3;  // 1 in 3; EGG_MAX_MISSES misses guarantee the next
constexpr uint32_t EGG_SHINY_DENOMINATOR = 16;       // wild is SHINY_CHANCE_DENOMINATOR (1 in 64)
constexpr uint32_t EGG_UNCAUGHT_WEIGHT = 3;          // uncaught species weigh 3x, as upstream's wild rolls
constexpr uint8_t EGG_HATCH_LEVEL = 5;
constexpr uint8_t EGG_IV_FLOOR = 6;                  // hatched IVs 6..15 (wild 0..15, shiny 12..15)
constexpr uint8_t EGG_IV_MAX = 15;
constexpr uint8_t EGG_UNCOMMON_CAPTURE_RATE_MAX = 150;  // capture rate <= 150 and not rare: uncommon

enum class EggRarity : uint8_t {
  Common = 0,    // 300 pages
  Uncommon = 1,  // 600 pages
  Rare = 2,      // 1200 pages
};
constexpr uint16_t EGG_PAGES_COMMON = 300;
constexpr uint16_t EGG_PAGES_UNCOMMON = 600;
constexpr uint16_t EGG_PAGES_RARE = EGG_MAX_PAGES;  // 1200

// ---------------------------------------------------------------------------
// Pool and rarity
// ---------------------------------------------------------------------------
// Base stage, Acquisition::Wild (so never a legendary, never Mew, never an
// evolved form), and not Ditto (no Pokemon hatches as Ditto).
bool isEggPoolSpecies(const SpeciesData& species);
bool isEggPoolSpecies(uint16_t speciesId);
// Rare: Bulbasaur, Charmander, Squirtle, Eevee, Dratini, the fossils (Omanyte,
// Kabuto, Aerodactyl), Lapras, Snorlax, Chansey. Uncommon: capture rate
// <= EGG_UNCOMMON_CAPTURE_RATE_MAX. Common: the rest.
EggRarity eggRarity(uint16_t speciesId);
// Pages from find to hatch for this species; 0 when it is not in the pool.
uint16_t eggTotalPages(uint16_t speciesId);

// ---------------------------------------------------------------------------
// State helpers
// ---------------------------------------------------------------------------
bool hasEgg(const ExtrasState& state);
// An egg is held and has no pages left. The caller still checks storage room
// (catchBlockedByFullBox) before hatching: with Party and PC Box both full the
// egg simply waits at 0 pages.
bool isEggReadyToHatch(const ExtrasState& state);

// For the Pokemon menu row and the Party sleep screen ("412 pages to go").
// totalPages is never below pagesRemaining, so eggProgressPercent() stays in
// 0..100 even if the page tunables change while an egg is held.
struct EggProgress {
  bool held = false;
  bool ready = false;  // 0 pages left
  uint16_t pagesRemaining = 0;
  uint16_t totalPages = 0;
};
EggProgress eggProgress(const ExtrasState& state);

// ---------------------------------------------------------------------------
// Finding an egg
// ---------------------------------------------------------------------------
// How many hourly boundaries (multiples of EGG_FIND_MINUTES of
// PokemonState::lifetimeMinutes) one credit crossed. Pass lifetimeMinutes
// read before the credit and after its commit.
uint32_t eggFindBoundariesCrossed(uint32_t lifetimeMinutesBefore, uint32_t lifetimeMinutesAfter);

// Picks the egg's species from the pool, uncaught x3. False when the pool is
// empty or the random source is missing.
bool chooseEggSpecies(const PokemonState& mainState, const RandomSource& random, uint16_t& speciesId);

// One find roll per boundary crossed, stopping at the first find. Rolls only
// with the Eggs toggle on and no egg held. 1 in 3; after EGG_MAX_MISSES
// misses in a row the next roll always finds. A find stores the species
// (hidden from the player), its full page count and the pre-rolled shiny flag,
// and resets the miss counter. Uses the extras RNG only, never the draws
// inside applyCreditedMinutes(). Returns true when an egg was found.
// Extras-only change: the caller commits extras after the main credit (R6).
bool rollEggFind(ExtrasState& state, const PokemonState& mainState, uint32_t boundariesCrossed);

// ---------------------------------------------------------------------------
// Pages
// ---------------------------------------------------------------------------
// Counts `pages` verified forward page turns (and, from M4, Kindle pages)
// toward the held egg. Returns the pages actually counted (0 with no egg, the
// toggle off, or the egg already at 0), which is also what the
// EggPages100 daily quest receives. Never goes below 0.
uint16_t applyEggPages(ExtrasState& state, uint32_t pages);

// ---------------------------------------------------------------------------
// Hatching (crash safety: 01-eggs.md, SAVE-SAFETY R6)
// ---------------------------------------------------------------------------
// Order, all from the hatch flow on Pokemon menu open:
//   1. beginHatch(extras, requestedRecordId) and commit EXTRAS. requestedRecordId
//      is the main store's nextRecordId(), the id the append will use.
//   2. buildHatchedRecord(...) on a copy of the main state, then commit the
//      MAIN save with the returned Append mutation; write the egg IVs
//      (rollEggIvs) into the IV/EV store for that record id.
//   3. finishHatch(extras) and commit extras.
// On every extras load, resolvePendingHatch() settles a crash between 1 and 3:
// the record exists, so the egg is cleared; it does not, so the pending id is
// dropped and the hatch runs again at the next menu open. Never two records
// from one egg.

// Records the id the hatch will append. False (state untouched) when no egg is
// ready, the toggle is off, a hatch is already pending, or the id is 0.
bool beginHatch(ExtrasState& state, uint32_t requestedRecordId);

// Builds the hatched record exactly as resolveEncounter() builds a catch:
// level EGG_HATCH_LEVEL (totalXp = xpRequired(5)), Origin::Caught, the egg's
// shiny flag, `gender` (roll with chooseGenderForSpecies), `nickname` ("" for
// none), first free party slot or else the PC Box, species marked seen and
// caught. `mainState` and `mutation` change only on success (all or nothing).
// Requires state.pendingHatchRecordId == requestedRecordId (beginHatch first)
// and an empty mutation.
bool buildHatchedRecord(const ExtrasState& state, PokemonState& mainState, uint32_t requestedRecordId, Gender gender,
                        const char* nickname, RecordMutation& mutation);

// IVs for the hatched record: EGG_IV_FLOOR..15 per stat, or 12..15 when shiny
// (the same range upstream's rollShinyIvSet() uses). Written with the record's
// first IV/EV entry, before anything calls ensureIvEv() for it.
void rollEggIvs(const RandomSource& random, bool shiny, std::array<uint8_t, STAT_COUNT>& iv);

// Clears the egg after the main commit. Keeps the miss counter (0 since the find).
void finishHatch(ExtrasState& state);

enum class PendingHatch : uint8_t {
  None = 0,      // nothing pending: state untouched
  Finished = 1,  // the record is in the main save: egg cleared
  Retry = 2,     // the record is not there: pending id cleared, egg still ready
};
// `recordWithPendingId` is the main-save record whose id equals
// state.pendingHatchRecordId, or nullptr when none exists. It must also be the
// egg's species to count as the hatch.
PendingHatch resolvePendingHatch(ExtrasState& state, const PokemonRecord* recordWithPendingId);

}  // namespace pokemon::extras
