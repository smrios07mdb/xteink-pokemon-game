#pragma once

// Extras M2 rules (docs/extras/features/02-streaks.md, 07-day-care.md): pure,
// storage-free, host-testable. The service hooks call these and do the writes:
//   - XP only through PokemonService::awardFixedXp() (SAVE-SAFETY R3);
//   - the milestone encounter is an ordinary PendingEventKind::Encounter (R3);
//   - reading stats are only read, by the caller, and passed in (R4).
//
// Unlike PokemonExtras.cpp this file needs species data (rare pool, XP curve),
// so it links PokemonTypes/PokemonSpecies/PokemonGame. Compiles to nothing
// without CROSSINK_POKEMON_EXTRAS (R11).

#include <cstdint>

#include "PokemonExtras.h"
#include "PokemonGame.h"
#include "PokemonSpecies.h"
#include "PokemonTypes.h"

namespace pokemon::extras {

// ---------------------------------------------------------------------------
// 2. Reading streak bonus
// ---------------------------------------------------------------------------
// Tunables.
constexpr uint16_t STREAK_BONUS_PERMILLE_PER_DAY = 50;  // +5% per streak day
constexpr uint16_t STREAK_BONUS_CAP_DAYS = 10;           // capped at +50%
constexpr uint16_t STREAK_MILESTONE_INTERVAL = 7;        // rare encounter every 7th day

// What the caller read from the firmware's reading stats (read-only, R4).
// clockValid: getCurrentLocalReadingStatsDateTime() succeeded.
// streakDays: GlobalReadingStats::currentReadingStreak(&today).
// todayIndex: readingStatsDayIndex(today).
// The firmware's streak may end yesterday when today's reading is not
// recorded yet; the rules below tolerate that.
struct StreakInput {
  bool clockValid = false;
  uint16_t streakDays = 0;
  uint32_t todayIndex = 0;
};

// Current bonus in per mille of reading XP: 0..500. 0 when the feature is off
// or there is no valid clock.
uint16_t streakBonusPermille(const ExtrasState& state, const StreakInput& input);
// The same as a whole percent, for the Trainer Card ("+30% XP").
uint8_t streakBonusPercent(const ExtrasState& state, const StreakInput& input);

// Adds the bonus for `baseXp` reading XP (1 XP per credited minute, Kindle
// minutes included) to streakXpRemainderPermille and returns the whole XP to
// grant now through awardFixedXp(leaderId, n). The remainder keeps the
// fraction (< 1000). No bonus (feature off, no clock) leaves the remainder
// untouched and returns 0.
//
// Crash safety: the caller grants the returned XP first (main save, R6), then
// saves extras. If extras is not saved, the old remainder stays and those
// minutes are never credited again, so nothing is granted twice; at most the
// fraction is lost. If awardFixedXp fails, do not save the new remainder.
uint32_t applyStreakBonus(ExtrasState& state, const StreakInput& input, uint32_t baseXp);

// The milestone (7, 14, 21, ...) the current streak has reached and not been
// rewarded for, or 0. A milestone is rewarded once per streak: a broken and
// restarted streak earns its milestones again. If a checkpoint was missed, a
// later one grants the highest milestone reached (one encounter, not several).
uint16_t dueStreakMilestone(const ExtrasState& state, const StreakInput& input);
// Records `milestone` (from dueStreakMilestone) as rewarded for this streak.
//
// Crash safety (R6, same idea as the egg's pendingHatchRecordId): mark and
// save extras BEFORE committing the encounter to the main save. A crash in
// between loses one bonus encounter but can never queue two. If the main
// commit fails, restore the two fields from a copy and save extras again.
void markStreakMilestoneRewarded(ExtrasState& state, const StreakInput& input, uint16_t milestone);

// ---------------------------------------------------------------------------
// Rare wild encounter (streak milestones; reusable by later features)
// ---------------------------------------------------------------------------
// True when the queue has a free slot outside PENDING_EVENT_PROMPT_RESERVE,
// the same room rule upstream uses for its own encounters and trainers.
bool hasEncounterRoom(const PokemonState& state);

// The rare pool: wild-caught species of the rarest encounter tier (capture
// rate below 60, the tier upstream weighs 1) plus the four starters, with
// upstream's book-progress gate on middle and final stages. Never a
// legendary, never Mew (they are not Acquisition::Wild).
bool isRarePoolSpecies(const SpeciesData& species, uint8_t bookProgressPercent);

// Picks a species from the rare pool; uncaught species weigh 3x, as upstream
// does for its own wild encounters.
bool chooseRareSpecies(const PokemonState& state, uint8_t bookProgressPercent, const RandomSource& random,
                       uint16_t& speciesId);

// Queues an ordinary wild Encounter (R3) from the rare pool at `level`
// (clamped to 2..100), marks it seen, and refreshes the dashboard notice.
// Returns false and leaves `state` unchanged when there is no room
// (retry at the next checkpoint) or the pool is empty. Shininess is not
// rolled here: the caller rolls it like creditMinutes() does
// (SHINY_CHANCE_DENOMINATOR) on the event it finds at the queue's end.
bool queueRareEncounter(PokemonState& state, uint8_t level, uint8_t bookProgressPercent, const RandomSource& random);

// ---------------------------------------------------------------------------
// 7. Day Care
// ---------------------------------------------------------------------------
constexpr uint8_t DAY_CARE_MINUTES_PER_XP = 2;  // 50% of the lead's reading XP

bool isInParty(const PokemonState& state, uint32_t recordId);

enum class DayCareSendResult : uint8_t {
  Sent = 0,
  AlreadyThere = 1,  // this record is already in Day Care: nothing changed
  Occupied = 2,      // another Pokemon is there; one at a time
  InParty = 3,       // only a boxed Pokemon can go
  Disabled = 4,      // Extras > Day Care is off
  InvalidRecord = 5, // record id 0
};

// PC Box > a Pokemon > Send to Day Care. The caller has already read the
// record from the main save (so it exists).
DayCareSendResult sendToDayCare(ExtrasState& state, const PokemonState& mainState, uint32_t recordId);

// Clears the slot if it holds `recordId`. Call after a successful
// withdrawPokemon() / releasePokemon(), and when the record no longer exists
// (stale id, cleared silently). Returns true when it cleared something.
bool clearDayCareIfHolds(ExtrasState& state, uint32_t recordId);

// Clears the slot whatever it holds (player takes the Pokemon back, or a
// stale id). Game reset already clears it through resetExtrasProgress().
void clearDayCare(ExtrasState& state);

// The highest totalXp a Day Care Pokemon may reach: the last XP of the
// lead's level, so it can reach that level but never pass it.
uint32_t dayCareXpCap(uint8_t leaderLevel);

// Credits `minutes` of the lead's reading to the Day Care Pokemon at half
// rate (the odd minute stays in dayCareMinuteRemainder) and returns the XP to
// grant it, already limited by dayCareXpCap(leaderLevel) given its current
// `dayCareTotalXp`. Returns 0 (state untouched) when the slot is empty or the
// feature is off. A Pokemon already at or above the cap gains 0 but the
// remainder still advances.
//
// Crash safety: same as applyStreakBonus (grant first, then save extras; a
// lost save only loses the odd minute).
uint32_t applyDayCareMinutes(ExtrasState& state, uint16_t minutes, uint32_t dayCareTotalXp, uint8_t leaderLevel);

}  // namespace pokemon::extras
