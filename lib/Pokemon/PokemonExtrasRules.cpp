#if defined(CROSSINK_ENABLE_POKEMON) && defined(CROSSINK_POKEMON_EXTRAS)

#include "PokemonExtrasRules.h"

#include <algorithm>

namespace pokemon::extras {
namespace {

// Upstream's rarest encounter tier (PokemonGame.cpp encounterWeight() == 1)
// and its uncaught multiplier. Mirrored rather than shared: upstream keeps
// them file-local, and this fork does not edit upstream's game rules.
constexpr uint8_t RARE_CAPTURE_RATE_BELOW = 60;
constexpr uint32_t RARE_UNCAUGHT_WEIGHT = 3;

bool isStarter(const uint16_t speciesId) {
  return speciesId == 1 || speciesId == 4 || speciesId == 7 || speciesId == 25;
}

uint32_t rareWeight(const PokemonState& state, const SpeciesData& species) {
  return isSpeciesMarked(state.caughtSpecies, species.speciesId) ? 1U : RARE_UNCAUGHT_WEIGHT;
}

// A streak's first day. Requires todayIndex + 1 >= streakDays (checked by callers).
uint32_t streakAnchor(const StreakInput& input) { return input.todayIndex + 1U - input.streakDays; }

bool streakUsable(const ExtrasState& state, const StreakInput& input) {
  return isFeatureEnabled(state, Feature::Streaks) && input.clockValid && input.streakDays > 0 &&
         static_cast<uint64_t>(input.todayIndex) + 1U >= input.streakDays;
}

}  // namespace

// --- 2. Streaks ------------------------------------------------------------

uint16_t streakBonusPermille(const ExtrasState& state, const StreakInput& input) {
  if (!streakUsable(state, input)) return 0;
  const uint16_t days = std::min<uint16_t>(input.streakDays, STREAK_BONUS_CAP_DAYS);
  return static_cast<uint16_t>(days * STREAK_BONUS_PERMILLE_PER_DAY);
}

uint8_t streakBonusPercent(const ExtrasState& state, const StreakInput& input) {
  return static_cast<uint8_t>(streakBonusPermille(state, input) / 10U);
}

uint32_t applyStreakBonus(ExtrasState& state, const StreakInput& input, const uint32_t baseXp) {
  const uint16_t permille = streakBonusPermille(state, input);
  if (permille == 0 || baseXp == 0) return 0;
  // Cap the input: one credit is at most a few hundred minutes, and
  // MAXIMUM_TOTAL_XP bounds what can ever be granted anyway.
  const uint64_t base = std::min<uint32_t>(baseXp, MAXIMUM_TOTAL_XP);
  const uint64_t total = static_cast<uint64_t>(state.streakXpRemainderPermille) + base * permille;
  state.streakXpRemainderPermille = static_cast<uint16_t>(total % PERMILLE);
  return static_cast<uint32_t>(total / PERMILLE);
}

uint16_t dueStreakMilestone(const ExtrasState& state, const StreakInput& input) {
  if (!streakUsable(state, input)) return 0;
  const uint16_t milestone =
      static_cast<uint16_t>((input.streakDays / STREAK_MILESTONE_INTERVAL) * STREAK_MILESTONE_INTERVAL);
  if (milestone == 0) return 0;
  if (state.lastRewardedStreakMilestone != 0) {
    // Same streak if the anchors are within a day: the firmware's streak can
    // end yesterday (today not recorded yet), which shifts the computed anchor
    // by one. Two different rewarded streaks are always further apart: the
    // older one lasted >= 7 days and a gap day separates them.
    const uint32_t anchor = streakAnchor(input);
    const uint32_t last = state.lastRewardedStreakDay;
    const uint32_t distance = anchor > last ? anchor - last : last - anchor;
    if (distance <= 1U && state.lastRewardedStreakMilestone >= milestone) return 0;
  }
  return milestone;
}

void markStreakMilestoneRewarded(ExtrasState& state, const StreakInput& input, const uint16_t milestone) {
  if (milestone == 0 || !streakUsable(state, input)) return;
  state.lastRewardedStreakMilestone = milestone;
  state.lastRewardedStreakDay = streakAnchor(input);
}

// --- Rare encounter --------------------------------------------------------

bool hasEncounterRoom(const PokemonState& state) {
  return pendingEventCount(state) < PENDING_EVENT_CAPACITY - PENDING_EVENT_PROMPT_RESERVE;
}

bool isRarePoolSpecies(const SpeciesData& species, const uint8_t bookProgressPercent) {
  if (species.acquisition != Acquisition::Wild) return false;
  if (species.stage == EvolutionStage::Middle && bookProgressPercent < 50) return false;
  if (species.stage == EvolutionStage::Final && bookProgressPercent < 75) return false;
  return isStarter(species.speciesId) || species.captureRate < RARE_CAPTURE_RATE_BELOW;
}

bool chooseRareSpecies(const PokemonState& state, const uint8_t bookProgressPercent, const RandomSource& random,
                       uint16_t& speciesId) {
  if (random.below == nullptr) return false;
  uint32_t totalWeight = 0;
  for (uint16_t candidateId = 1; candidateId <= KANTO_SPECIES_COUNT; ++candidateId) {
    const SpeciesData* candidate = speciesData(candidateId);
    if (candidate != nullptr && isRarePoolSpecies(*candidate, bookProgressPercent)) {
      totalWeight += rareWeight(state, *candidate);
    }
  }
  if (totalWeight == 0) return false;

  uint32_t selected = random.below(random.context, totalWeight);
  if (selected >= totalWeight) return false;
  for (uint16_t candidateId = 1; candidateId <= KANTO_SPECIES_COUNT; ++candidateId) {
    const SpeciesData* candidate = speciesData(candidateId);
    if (candidate == nullptr || !isRarePoolSpecies(*candidate, bookProgressPercent)) continue;
    const uint32_t weight = rareWeight(state, *candidate);
    if (selected < weight) {
      speciesId = candidateId;
      return true;
    }
    selected -= weight;
  }
  return false;
}

bool queueRareEncounter(PokemonState& state, const uint8_t level, const uint8_t bookProgressPercent,
                        const RandomSource& random) {
  if (!hasEncounterRoom(state)) return false;
  uint16_t speciesId = 0;
  if (!chooseRareSpecies(state, bookProgressPercent, random, speciesId)) return false;
  Gender gender = Gender::Unknown;
  if (!chooseGenderForSpecies(speciesId, random, gender)) return false;

  PokemonState updated = state;  // all or nothing
  const uint8_t encounterLevel = std::clamp<uint8_t>(level, 2, 100);
  const PendingEvent event{0, speciesId, encounterLevel, gender, EvolutionItem::None, PendingEventKind::Encounter};
  if (!enqueuePendingEvent(updated, event) || !markSpecies(updated.seenSpecies, speciesId)) return false;
  syncDashboardNotice(updated);
  state = updated;
  return true;
}

// --- 7. Day Care -----------------------------------------------------------

bool isInParty(const PokemonState& state, const uint32_t recordId) {
  if (recordId == 0) return false;
  return std::find(state.partyRecordIds.begin(), state.partyRecordIds.end(), recordId) != state.partyRecordIds.end();
}

DayCareSendResult sendToDayCare(ExtrasState& state, const PokemonState& mainState, const uint32_t recordId) {
  if (recordId == 0) return DayCareSendResult::InvalidRecord;
  if (!isFeatureEnabled(state, Feature::DayCare)) return DayCareSendResult::Disabled;
  if (isInParty(mainState, recordId)) return DayCareSendResult::InParty;
  if (state.dayCareRecordId == recordId) return DayCareSendResult::AlreadyThere;
  if (state.dayCareRecordId != 0) return DayCareSendResult::Occupied;
  state.dayCareRecordId = recordId;
  state.dayCareMinuteRemainder = 0;
  return DayCareSendResult::Sent;
}

bool clearDayCareIfHolds(ExtrasState& state, const uint32_t recordId) {
  if (recordId == 0 || state.dayCareRecordId != recordId) return false;
  clearDayCare(state);
  return true;
}

void clearDayCare(ExtrasState& state) {
  state.dayCareRecordId = 0;
  state.dayCareMinuteRemainder = 0;
}

uint32_t dayCareXpCap(const uint8_t leaderLevel) {
  if (leaderLevel >= 100) return MAXIMUM_TOTAL_XP;
  const uint8_t level = std::max<uint8_t>(leaderLevel, 1);
  return xpRequired(static_cast<uint8_t>(level + 1U)) - 1U;
}

uint32_t applyDayCareMinutes(ExtrasState& state, const uint16_t minutes, const uint32_t dayCareTotalXp,
                             const uint8_t leaderLevel) {
  if (state.dayCareRecordId == 0 || minutes == 0 || !isFeatureEnabled(state, Feature::DayCare)) return 0;
  const uint32_t totalMinutes = static_cast<uint32_t>(state.dayCareMinuteRemainder) + minutes;
  state.dayCareMinuteRemainder = static_cast<uint8_t>(totalMinutes % DAY_CARE_MINUTES_PER_XP);
  const uint32_t earned = totalMinutes / DAY_CARE_MINUTES_PER_XP;
  const uint32_t cap = dayCareXpCap(leaderLevel);
  if (dayCareTotalXp >= cap) return 0;
  return std::min<uint32_t>(earned, cap - dayCareTotalXp);
}

}  // namespace pokemon::extras

#endif
