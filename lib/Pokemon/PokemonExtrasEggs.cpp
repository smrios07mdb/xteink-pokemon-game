#if defined(CROSSINK_ENABLE_POKEMON) && defined(CROSSINK_POKEMON_EXTRAS)

#include "PokemonExtrasEggs.h"

#include <algorithm>

namespace pokemon::extras {
namespace {

constexpr uint16_t DITTO_SPECIES_ID = 132;
constexpr std::array<uint16_t, 11> RARE_EGG_SPECIES = {
    1, 4, 7,        // Bulbasaur, Charmander, Squirtle
    133,            // Eevee
    147,            // Dratini
    138, 140, 142,  // Omanyte, Kabuto, Aerodactyl
    131,            // Lapras
    143,            // Snorlax
    113,            // Chansey
};

uint32_t eggWeight(const PokemonState& mainState, const uint16_t speciesId) {
  return isSpeciesMarked(mainState.caughtSpecies, speciesId) ? 1U : EGG_UNCAUGHT_WEIGHT;
}

uint8_t ivInRange(const RandomSource& random, const uint8_t low) {
  const uint32_t span = static_cast<uint32_t>(EGG_IV_MAX - low) + 1U;
  if (random.below == nullptr) return low;
  const uint32_t roll = random.below(random.context, span);
  return static_cast<uint8_t>(low + (roll < span ? roll : 0U));
}

}  // namespace

// --- Pool and rarity -------------------------------------------------------

bool isEggPoolSpecies(const SpeciesData& species) {
  return species.stage == EvolutionStage::Base && species.acquisition == Acquisition::Wild &&
         species.speciesId != DITTO_SPECIES_ID;
}

bool isEggPoolSpecies(const uint16_t speciesId) {
  const SpeciesData* species = speciesData(speciesId);
  return species != nullptr && isEggPoolSpecies(*species);
}

EggRarity eggRarity(const uint16_t speciesId) {
  if (std::find(RARE_EGG_SPECIES.begin(), RARE_EGG_SPECIES.end(), speciesId) != RARE_EGG_SPECIES.end()) {
    return EggRarity::Rare;
  }
  const SpeciesData* species = speciesData(speciesId);
  if (species != nullptr && species->captureRate <= EGG_UNCOMMON_CAPTURE_RATE_MAX) return EggRarity::Uncommon;
  return EggRarity::Common;
}

uint16_t eggTotalPages(const uint16_t speciesId) {
  if (!isEggPoolSpecies(speciesId)) return 0;
  switch (eggRarity(speciesId)) {
    case EggRarity::Rare:
      return EGG_PAGES_RARE;
    case EggRarity::Uncommon:
      return EGG_PAGES_UNCOMMON;
    case EggRarity::Common:
      break;
  }
  return EGG_PAGES_COMMON;
}

// --- State helpers ---------------------------------------------------------

bool hasEgg(const ExtrasState& state) { return state.eggSpeciesId != 0; }

bool isEggReadyToHatch(const ExtrasState& state) { return hasEgg(state) && state.eggPagesRemaining == 0; }

EggProgress eggProgress(const ExtrasState& state) {
  EggProgress progress{};
  if (!hasEgg(state)) return progress;
  progress.held = true;
  progress.pagesRemaining = state.eggPagesRemaining;
  progress.ready = state.eggPagesRemaining == 0;
  progress.totalPages = std::max(eggTotalPages(state.eggSpeciesId), state.eggPagesRemaining);
  return progress;
}

// --- Finding ---------------------------------------------------------------

uint32_t eggFindBoundariesCrossed(const uint32_t lifetimeMinutesBefore, const uint32_t lifetimeMinutesAfter) {
  if (lifetimeMinutesAfter <= lifetimeMinutesBefore) return 0;
  return lifetimeMinutesAfter / EGG_FIND_MINUTES - lifetimeMinutesBefore / EGG_FIND_MINUTES;
}

bool chooseEggSpecies(const PokemonState& mainState, const RandomSource& random, uint16_t& speciesId) {
  if (random.below == nullptr) return false;
  uint32_t totalWeight = 0;
  for (uint16_t candidate = 1; candidate <= KANTO_SPECIES_COUNT; ++candidate) {
    if (isEggPoolSpecies(candidate)) totalWeight += eggWeight(mainState, candidate);
  }
  if (totalWeight == 0) return false;
  uint32_t selected = random.below(random.context, totalWeight);
  if (selected >= totalWeight) return false;
  for (uint16_t candidate = 1; candidate <= KANTO_SPECIES_COUNT; ++candidate) {
    if (!isEggPoolSpecies(candidate)) continue;
    const uint32_t weight = eggWeight(mainState, candidate);
    if (selected < weight) {
      speciesId = candidate;
      return true;
    }
    selected -= weight;
  }
  return false;
}

bool rollEggFind(ExtrasState& state, const PokemonState& mainState, const uint32_t boundariesCrossed) {
  if (!isFeatureEnabled(state, Feature::Eggs) || hasEgg(state)) return false;
  for (uint32_t boundary = 0; boundary < boundariesCrossed; ++boundary) {
    const bool guaranteed = state.eggMisses >= EGG_MAX_MISSES;
    const bool found = guaranteed || randomBelow(state, EGG_FIND_CHANCE_DENOMINATOR) == 0;
    if (!found) {
      ++state.eggMisses;
      continue;
    }
    ExtrasState candidate = state;  // all or nothing
    uint16_t speciesId = 0;
    if (!chooseEggSpecies(mainState, randomSource(candidate), speciesId)) return false;
    candidate.eggSpeciesId = speciesId;
    candidate.eggPagesRemaining = eggTotalPages(speciesId);
    candidate.eggFlags = randomBelow(candidate, EGG_SHINY_DENOMINATOR) == 0 ? EGG_FLAG_SHINY : 0;
    candidate.eggMisses = 0;
    candidate.pendingHatchRecordId = 0;
    state = candidate;
    return true;
  }
  return false;
}

// --- Pages -----------------------------------------------------------------

uint16_t applyEggPages(ExtrasState& state, const uint32_t pages) {
  if (!isFeatureEnabled(state, Feature::Eggs) || !hasEgg(state) || pages == 0) return 0;
  const uint16_t counted = static_cast<uint16_t>(std::min<uint32_t>(pages, state.eggPagesRemaining));
  state.eggPagesRemaining = static_cast<uint16_t>(state.eggPagesRemaining - counted);
  return counted;
}

// --- Hatching --------------------------------------------------------------

bool beginHatch(ExtrasState& state, const uint32_t requestedRecordId) {
  if (requestedRecordId == 0 || !isFeatureEnabled(state, Feature::Eggs) || !isEggReadyToHatch(state) ||
      state.pendingHatchRecordId != 0) {
    return false;
  }
  state.pendingHatchRecordId = requestedRecordId;
  return true;
}

bool buildHatchedRecord(const ExtrasState& state, PokemonState& mainState, const uint32_t requestedRecordId,
                        const Gender gender, const char* nickname, RecordMutation& mutation) {
  if (requestedRecordId == 0 || state.pendingHatchRecordId != requestedRecordId || !isEggReadyToHatch(state) ||
      mutation.kind != RecordMutationKind::None || !validateState(mainState)) {
    return false;
  }
  PokemonRecord hatched{};
  hatched.recordId = requestedRecordId;
  hatched.totalXp = xpRequired(EGG_HATCH_LEVEL);
  hatched.speciesId = state.eggSpeciesId;
  hatched.caughtLevel = EGG_HATCH_LEVEL;
  hatched.gender = gender;
  hatched.origin = Origin::Caught;
  if ((state.eggFlags & EGG_FLAG_SHINY) != 0) hatched.flags |= recordFlag(RecordFlag::Shiny);

  PokemonState candidate = mainState;
  if (!setNickname(hatched, nickname == nullptr ? "" : nickname) || !validateRecord(hatched) ||
      !markSpecies(candidate.seenSpecies, hatched.speciesId) ||
      !markSpecies(candidate.caughtSpecies, hatched.speciesId)) {
    return false;
  }
  for (uint32_t& partyRecordId : candidate.partyRecordIds) {
    if (partyRecordId == requestedRecordId) return false;  // id already in use
  }
  for (uint32_t& partyRecordId : candidate.partyRecordIds) {
    if (partyRecordId == 0) {
      partyRecordId = hatched.recordId;
      break;
    }
  }
  if (!validateState(candidate)) return false;

  RecordMutation result = mutation;
  result.kind = RecordMutationKind::Append;
  result.requestedRecordId = requestedRecordId;
  result.record = hatched;
  mainState = candidate;
  mutation = result;
  return true;
}

void rollEggIvs(const RandomSource& random, const bool shiny, std::array<uint8_t, STAT_COUNT>& iv) {
  const uint8_t low = shiny ? static_cast<uint8_t>(12) : EGG_IV_FLOOR;
  for (uint8_t& value : iv) value = ivInRange(random, low);
}

void finishHatch(ExtrasState& state) {
  state.eggSpeciesId = 0;
  state.eggPagesRemaining = 0;
  state.eggFlags = 0;
  state.pendingHatchRecordId = 0;
}

PendingHatch resolvePendingHatch(ExtrasState& state, const PokemonRecord* recordWithPendingId) {
  if (state.pendingHatchRecordId == 0) return PendingHatch::None;
  if (recordWithPendingId != nullptr && recordWithPendingId->recordId == state.pendingHatchRecordId &&
      recordWithPendingId->speciesId == state.eggSpeciesId) {
    finishHatch(state);
    return PendingHatch::Finished;
  }
  state.pendingHatchRecordId = 0;
  return PendingHatch::Retry;
}

}  // namespace pokemon::extras

#endif
