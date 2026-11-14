#if defined(CROSSINK_ENABLE_POKEMON) && defined(CROSSINK_POKEMON_EXTRAS)

#include "PokemonExtrasBoss.h"

#include <algorithm>

#include "PokemonExtrasKindle.h"
#include "PokemonExtrasQuests.h"
#include "PokemonExtrasRules.h"
#include "PokemonSpecies.h"

namespace pokemon::extras {
namespace {

constexpr uint16_t MEW = 151;

bool isValidBoss(const uint16_t speciesId) {
  return speciesId >= 1 && speciesId <= KANTO_SPECIES_COUNT && speciesId != MEW;
}

bool caught(const PokemonState& state, const uint16_t speciesId) {
  return isSpeciesMarked(state.caughtSpecies, speciesId);
}

uint32_t rarePoolWeight(const PokemonState& state, const uint16_t speciesId) {
  return caught(state, speciesId) ? 1U : BOSS_UNCAUGHT_WEIGHT;
}

// Appends the book to the FIFO list, dropping the oldest when it is full.
void countBook(ExtrasState& state, const uint32_t bookHash) {
  if (state.rewardedBookCount == REWARDED_BOOK_CAPACITY) {
    for (size_t index = 1; index < REWARDED_BOOK_CAPACITY; ++index) {
      state.rewardedBookHashes[index - 1U] = state.rewardedBookHashes[index];
    }
    --state.rewardedBookCount;
  }
  state.rewardedBookHashes[state.rewardedBookCount++] = bookHash;
  if (state.ribbonCount < UINT16_MAX) ++state.ribbonCount;
}

}  // namespace

// --- A book was marked finished --------------------------------------------

bool isBookCounted(const ExtrasState& state, const uint32_t bookHash) {
  if (bookHash == 0) return false;
  const size_t count = std::min<size_t>(state.rewardedBookCount, REWARDED_BOOK_CAPACITY);
  return std::find(state.rewardedBookHashes.begin(), state.rewardedBookHashes.begin() + count, bookHash) !=
         state.rewardedBookHashes.begin() + count;
}

bool bookEarnsBoss(const ExtrasState& state, const BookFinish& book) {
  if (book.deviceReadingSeconds >= BOSS_MIN_READING_SECONDS) return true;
  if (BOSS_MIN_SYNCED_PERMILLE == 0 || !isFeatureEnabled(state, Feature::KindleCredit)) return false;
  return kindleHighWaterPermille(state, book.bookHash) >= BOSS_MIN_SYNCED_PERMILLE;
}

bool chooseBossSpecies(const PokemonState& mainState, const RandomSource& random, uint16_t& speciesId) {
  if (random.below == nullptr) return false;

  // 1. An uncaught legendary bird.
  std::array<uint16_t, BOSS_LEGENDARY_BIRDS.size()> birds{};
  uint32_t birdCount = 0;
  for (const uint16_t bird : BOSS_LEGENDARY_BIRDS) {
    if (!caught(mainState, bird)) birds[birdCount++] = bird;
  }
  if (birdCount != 0) {
    const uint32_t pick = birdCount == 1 ? 0 : random.below(random.context, birdCount);
    if (pick >= birdCount) return false;
    speciesId = birds[pick];
    return true;
  }

  // 2. Mewtwo.
  if (!caught(mainState, BOSS_MEWTWO)) {
    speciesId = BOSS_MEWTWO;
    return true;
  }

  // 3. The rare pool, uncaught first by weight.
  uint32_t totalWeight = 0;
  for (const uint16_t candidate : BOSS_RARE_POOL) totalWeight += rarePoolWeight(mainState, candidate);
  uint32_t selected = random.below(random.context, totalWeight);
  if (selected >= totalWeight) return false;
  for (const uint16_t candidate : BOSS_RARE_POOL) {
    const uint32_t weight = rarePoolWeight(mainState, candidate);
    if (selected < weight) {
      speciesId = candidate;
      return true;
    }
    selected -= weight;
  }
  return false;
}

BookFinishResult onBookFinished(ExtrasState& state, const PokemonState& mainState, const BookFinish& book) {
  if (book.bookHash == 0 || !isFeatureEnabled(state, Feature::BookBoss)) return BookFinishResult::Ignored;
  if (isBookCounted(state, book.bookHash)) return BookFinishResult::AlreadyCounted;

  countBook(state, book.bookHash);
  if (!bookEarnsBoss(state, book)) return BookFinishResult::RibbonOnly;
  if (hasHeldBoss(state)) return BookFinishResult::BossAlreadyWaiting;

  uint16_t speciesId = 0;
  if (!chooseBossSpecies(mainState, randomSource(state), speciesId) || !holdBoss(state, speciesId)) {
    return BookFinishResult::RibbonOnly;
  }
  return BookFinishResult::BossHeld;
}

// --- Delivering the held boss ----------------------------------------------

bool hasHeldBoss(const ExtrasState& state) { return state.heldBossSpeciesId != 0; }

uint16_t takeHeldBoss(ExtrasState& state) {
  const uint16_t speciesId = state.heldBossSpeciesId;
  state.heldBossSpeciesId = 0;
  return speciesId;
}

bool holdBoss(ExtrasState& state, const uint16_t speciesId) {
  if (!isValidBoss(speciesId) || hasHeldBoss(state)) return false;
  state.heldBossSpeciesId = speciesId;
  return true;
}

uint8_t bossLevel(const uint8_t leaderLevel) {
  const uint32_t level = static_cast<uint32_t>(leaderLevel) + BOSS_LEVEL_OFFSET;
  return static_cast<uint8_t>(std::clamp<uint32_t>(level, 2, 100));
}

bool deliverBoss(PokemonState& mainState, const uint16_t speciesId, const uint8_t leaderLevel,
                 const RandomSource& random, BossReward& reward) {
  if (!isValidBoss(speciesId) || !hasEncounterRoom(mainState)) return false;
  Gender gender = Gender::Unknown;
  if (!chooseGenderForSpecies(speciesId, random, gender)) return false;

  PokemonState updated = mainState;  // all or nothing
  const PendingEvent event{0, speciesId, bossLevel(leaderLevel), gender, EvolutionItem::None,
                           PendingEventKind::Encounter};
  if (!enqueuePendingEvent(updated, event) || !markSpecies(updated.seenSpecies, speciesId)) return false;

  BossReward granted{};
  if (grantQuestItem(updated, BOSS_ITEM_MASTER_BALL)) {
    granted = BossReward{BOSS_ITEM_MASTER_BALL, 1};
  } else {
    for (uint8_t ball = 0; ball < BOSS_ULTRA_BALL_COUNT; ++ball) {
      if (!grantQuestItem(updated, BOSS_ITEM_ULTRA_BALL)) break;
      granted = BossReward{BOSS_ITEM_ULTRA_BALL, static_cast<uint8_t>(granted.count + 1U)};
    }
  }
  syncDashboardNotice(updated);
  mainState = updated;
  reward = granted;
  return true;
}

uint16_t booksFinishedRibbons(const ExtrasState& state) { return state.ribbonCount; }

}  // namespace pokemon::extras

#endif
