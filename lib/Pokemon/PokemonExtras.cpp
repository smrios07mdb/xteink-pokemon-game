#if defined(CROSSINK_ENABLE_POKEMON) && defined(CROSSINK_POKEMON_EXTRAS)

#include "PokemonExtras.h"

namespace pokemon::extras {
namespace {

constexpr uint32_t DEFAULT_RNG_SEED = 0x9E3779B9U;  // used if nothing seeded the stream

uint32_t nextRandom(ExtrasState& state) {
  if (state.rngState == 0) state.rngState = DEFAULT_RNG_SEED;
  uint32_t x = state.rngState;
  x ^= x << 13U;
  x ^= x >> 17U;
  x ^= x << 5U;
  state.rngState = x;  // xorshift32 never reaches 0 from a non-zero state
  return x;
}

uint32_t randomBelowThunk(void* context, const uint32_t upperExclusive) {
  return randomBelow(*static_cast<ExtrasState*>(context), upperExclusive);
}

bool questsValid(const ExtrasState& state) {
  size_t chosen = 0;
  for (size_t slot = 0; slot < DAILY_QUEST_SLOTS; ++slot) {
    const uint8_t id = state.questIds[slot];
    if (id > QUEST_KIND_MAX) return false;
    if (id == 0) continue;
    ++chosen;
    for (size_t other = slot + 1; other < DAILY_QUEST_SLOTS; ++other) {
      if (state.questIds[other] == id) return false;  // no duplicates
    }
  }
  if (chosen == 0) {
    // No set chosen yet: nothing can be in progress, done or rewarded.
    for (const uint16_t progress : state.questProgress) {
      if (progress != 0) return false;
    }
    return state.questDoneMask == 0 && state.questRewardedMask == 0;
  }
  if (chosen != DAILY_QUEST_SLOTS) return false;  // a set is always three quests
  if ((state.questDoneMask & ~QUEST_SLOT_MASK) != 0) return false;
  if ((state.questRewardedMask & ~(QUEST_SLOT_MASK | QUEST_BONUS_BIT)) != 0) return false;
  // A reward is only ever granted for a finished quest.
  if ((state.questRewardedMask & QUEST_SLOT_MASK & ~state.questDoneMask) != 0) return false;
  if ((state.questRewardedMask & QUEST_BONUS_BIT) != 0 && state.questDoneMask != QUEST_SLOT_MASK) return false;
  return true;
}

}  // namespace

bool validateExtrasState(const ExtrasState& state) {
  // 1. Eggs
  if (state.eggSpeciesId > KANTO_SPECIES_COUNT) return false;
  if (state.eggSpeciesId == 0) {
    if (state.eggPagesRemaining != 0 || state.eggFlags != 0 || state.pendingHatchRecordId != 0) return false;
  }
  if (state.eggPagesRemaining > EGG_MAX_PAGES) return false;
  if ((state.eggFlags & ~EGG_FLAG_MASK) != 0) return false;
  if (state.eggMisses > EGG_MAX_MISSES) return false;

  // 2. Streaks
  if (state.streakXpRemainderPermille >= PERMILLE) return false;

  // 3. Book boss
  if (state.rewardedBookCount > REWARDED_BOOK_CAPACITY) return false;
  for (size_t index = 0; index < REWARDED_BOOK_CAPACITY; ++index) {
    const bool used = index < state.rewardedBookCount;
    if (used != (state.rewardedBookHashes[index] != 0)) return false;
  }
  if (state.heldBossSpeciesId > KANTO_SPECIES_COUNT) return false;
  if (state.heldBossSpeciesId == KANTO_SPECIES_COUNT) return false;  // Mew (151) is never a boss

  // 5. Kindle credit
  if (state.kindleMarkCount > KINDLE_MARK_CAPACITY) return false;
  for (size_t index = 0; index < KINDLE_MARK_CAPACITY; ++index) {
    const KindleMark& mark = state.kindleMarks[index];
    if (index < state.kindleMarkCount) {
      if (mark.documentHash == 0 || mark.highWaterPermille > PERMILLE) return false;
    } else if (mark.documentHash != 0 || mark.highWaterPermille != 0) {
      return false;
    }
  }

  // 6. Post-game
  if (state.battlePasses > BATTLE_PASS_CAP) return false;
  if (state.towerRunSeed == 0 && state.towerRunWins != 0) return false;

  // 7. Day Care
  if (state.dayCareMinuteRemainder > 1) return false;
  if (state.dayCareRecordId == 0 && state.dayCareMinuteRemainder != 0) return false;

  // 8. Daily quests
  return questsValid(state);
}

bool isFeatureEnabled(const ExtrasState& state, const Feature feature) {
  return (state.featureMask & featureBit(feature)) != 0;
}

void setFeatureEnabled(ExtrasState& state, const Feature feature, const bool enabled) {
  if (enabled) {
    state.featureMask = static_cast<uint8_t>(state.featureMask | featureBit(feature));
  } else {
    state.featureMask = static_cast<uint8_t>(state.featureMask & ~featureBit(feature));
  }
}

void resetExtrasProgress(ExtrasState& state) {
  ExtrasState fresh{};
  fresh.featureMask = state.featureMask;
  fresh.rngState = state.rngState;
  state = fresh;
}

void seedRandom(ExtrasState& state, const uint32_t seed) { state.rngState = seed != 0 ? seed : DEFAULT_RNG_SEED; }

uint32_t randomBelow(ExtrasState& state, const uint32_t upperExclusive) {
  if (upperExclusive <= 1) return 0;
  // xorshift32 yields 1..2^32-1; shift to 0..2^32-2 (UINT32_MAX values), then
  // reject the top partial bucket so every result is exactly equally likely.
  const uint32_t limit = UINT32_MAX - (UINT32_MAX % upperExclusive);
  uint32_t value = nextRandom(state) - 1U;
  while (value >= limit) value = nextRandom(state) - 1U;
  return value % upperExclusive;
}

RandomSource randomSource(ExtrasState& state) {
  RandomSource source{};
  source.context = &state;
  source.below = &randomBelowThunk;
  return source;
}

uint32_t stableIdHash(const uint8_t* data, const size_t size) {
  uint32_t hash = 0x811C9DC5U;  // FNV-1a 32 offset basis
  for (size_t index = 0; index < size; ++index) {
    hash ^= data[index];
    hash *= 0x01000193U;  // FNV prime
  }
  return hash != 0 ? hash : 1U;
}

uint32_t stableIdHash(const char* text) {
  if (text == nullptr) return stableIdHash(static_cast<const uint8_t*>(nullptr), 0);
  size_t size = 0;
  while (text[size] != '\0') ++size;
  return stableIdHash(reinterpret_cast<const uint8_t*>(text), size);
}

}  // namespace pokemon::extras

#endif
