#if defined(CROSSINK_ENABLE_POKEMON) && defined(CROSSINK_POKEMON_EXTRAS)

#include "PokemonExtrasQuests.h"

#include <algorithm>

#include "PokemonGame.h"

namespace pokemon::extras {
namespace {

constexpr uint8_t BAG_ITEM_ID_FIRST = static_cast<uint8_t>(EVOLUTION_ITEM_COUNT + 1U);  // 7
constexpr uint8_t BAG_ITEM_ID_LAST = static_cast<uint8_t>(EVOLUTION_ITEM_COUNT + POKEMON_BAG_SLOT_COUNT);  // 83

bool questsOn(const ExtrasState& state) { return isFeatureEnabled(state, Feature::DailyQuests); }

bool isClocklessAnchor(const uint32_t anchor) { return (anchor & QUEST_CLOCKLESS_ANCHOR_BIT) != 0; }
uint32_t clocklessMinutes(const uint32_t anchor) { return anchor & ~QUEST_CLOCKLESS_ANCHOR_BIT; }

// Small local generator for chooseQuestSet: the day's set must not depend on,
// or advance, the extras RNG stream.
uint32_t mix(uint32_t x) {
  x ^= x >> 16U;
  x *= 0x7FEB352DU;
  x ^= x >> 15U;
  x *= 0x846CA68BU;
  x ^= x >> 16U;
  return x;
}

void clearSet(ExtrasState& state) {
  state.questIds = {};
  state.questProgress = {};
  state.questDoneMask = 0;
  state.questRewardedMask = 0;
}

void installSet(ExtrasState& state, const std::array<QuestKind, DAILY_QUEST_SLOTS>& set, const uint32_t anchor) {
  clearSet(state);
  for (size_t slot = 0; slot < DAILY_QUEST_SLOTS; ++slot) state.questIds[slot] = static_cast<uint8_t>(set[slot]);
  state.questDayAnchor = anchor;
}

QuestRewards collectReady(const ExtrasState& state, const uint8_t mask) {
  QuestRewards rewards{};
  for (uint8_t slot = 0; slot < DAILY_QUEST_SLOTS; ++slot) {
    if ((mask & (1U << slot)) == 0) continue;
    const uint8_t item = questRewardItem(questKindAt(state, slot));
    if (item != 0) rewards.itemIds[rewards.count++] = item;
  }
  if ((mask & QUEST_BONUS_BIT) != 0) rewards.itemIds[rewards.count++] = QUEST_BONUS_ITEM;
  return rewards;
}

// Rotates to a new set, returning the old set's ready rewards.
void rotate(ExtrasState& state, const std::array<QuestKind, DAILY_QUEST_SLOTS>& set, const uint32_t anchor,
            QuestRewards& expiredRewards) {
  const uint8_t ready = questRewardsReadyMask(state);
  const QuestRewards old = collectReady(state, ready);
  for (uint8_t index = 0; index < old.count && expiredRewards.count < expiredRewards.itemIds.size(); ++index) {
    expiredRewards.itemIds[expiredRewards.count++] = old.itemIds[index];
  }
  installSet(state, set, anchor);
}

std::array<QuestKind, DAILY_QUEST_SLOTS> clocklessSet(ExtrasState& state, const bool eggHeld) {
  return chooseQuestSet(randomBelow(state, UINT32_MAX), eggHeld);
}

// Adds `amount` to every quest of `kind` (at most one, no duplicates) and
// returns the slot bits that just became done.
uint8_t advance(ExtrasState& state, const QuestKind kind, const uint32_t amount) {
  if (!questsOn(state) || !hasQuestSet(state) || amount == 0) return 0;
  uint8_t newlyDone = 0;
  for (uint8_t slot = 0; slot < DAILY_QUEST_SLOTS; ++slot) {
    if (questKindAt(state, slot) != kind) continue;
    const uint8_t bit = static_cast<uint8_t>(1U << slot);
    if ((state.questDoneMask & bit) != 0) continue;
    const uint16_t target = questTarget(kind);
    const uint32_t progress = std::min<uint32_t>(static_cast<uint32_t>(state.questProgress[slot]) + amount, target);
    state.questProgress[slot] = static_cast<uint16_t>(progress);
    if (progress >= target) {
      state.questDoneMask = static_cast<uint8_t>(state.questDoneMask | bit);
      newlyDone = static_cast<uint8_t>(newlyDone | bit);
    }
  }
  return newlyDone;
}

}  // namespace

// --- Quest data ------------------------------------------------------------

uint16_t questTarget(const QuestKind kind) {
  switch (kind) {
    case QuestKind::Read20:
      return 20;
    case QuestKind::Read40:
      return 40;
    case QuestKind::Read60:
      return 60;
    case QuestKind::EggPages100:
      return 100;
    case QuestKind::WinWild:
    case QuestKind::WinTrainer:
    case QuestKind::CatchAny:
    case QuestKind::CatchNew:
    case QuestKind::LevelUp:
      return 1;
    case QuestKind::None:
      break;
  }
  return 0;
}

uint8_t questRewardItem(const QuestKind kind) {
  switch (kind) {
    case QuestKind::Read20:
      return QUEST_ITEM_POTION;
    case QuestKind::Read40:
      return QUEST_ITEM_SUPER_POTION;
    case QuestKind::Read60:
      return QUEST_ITEM_HYPER_POTION;
    case QuestKind::WinWild:
    case QuestKind::CatchAny:
      return QUEST_ITEM_POKE_BALL;
    case QuestKind::WinTrainer:
    case QuestKind::CatchNew:
      return QUEST_ITEM_GREAT_BALL;
    case QuestKind::EggPages100:
    case QuestKind::LevelUp:
      return QUEST_ITEM_SUPER_POTION;
    case QuestKind::None:
      break;
  }
  return 0;
}

bool isReadingQuest(const QuestKind kind) {
  return kind == QuestKind::Read20 || kind == QuestKind::Read40 || kind == QuestKind::Read60;
}

QuestKind questKindAt(const ExtrasState& state, const uint8_t slot) {
  if (slot >= DAILY_QUEST_SLOTS || state.questIds[slot] > QUEST_KIND_MAX) return QuestKind::None;
  return static_cast<QuestKind>(state.questIds[slot]);
}

bool hasQuestSet(const ExtrasState& state) {
  return std::any_of(state.questIds.begin(), state.questIds.end(), [](const uint8_t id) { return id != 0; });
}

// --- Choosing --------------------------------------------------------------

uint32_t questSeedForDay(const uint32_t dayIndex) { return mix(dayIndex ^ 0x51ED270BU); }

std::array<QuestKind, DAILY_QUEST_SLOTS> chooseQuestSet(const uint32_t seed, const bool eggHeld) {
  std::array<QuestKind, QUEST_KIND_MAX> pool{};
  size_t poolSize = 0;
  for (uint8_t id = 1; id <= QUEST_KIND_MAX; ++id) {
    const auto kind = static_cast<QuestKind>(id);
    if (kind == QuestKind::EggPages100 && !eggHeld) continue;
    pool[poolSize++] = kind;
  }
  std::array<QuestKind, DAILY_QUEST_SLOTS> set{};
  uint32_t state = seed;
  for (size_t slot = 0; slot < DAILY_QUEST_SLOTS && poolSize > 0; ++slot) {
    state = mix(state + 0x9E3779B9U);
    const size_t pick = state % poolSize;
    const QuestKind chosen = pool[pick];
    set[slot] = chosen;
    // Drop the pick (no duplicates) and, after a reading quest, every reading quest.
    size_t kept = 0;
    for (size_t index = 0; index < poolSize; ++index) {
      if (pool[index] == chosen) continue;
      if (isReadingQuest(chosen) && isReadingQuest(pool[index])) continue;
      pool[kept++] = pool[index];
    }
    poolSize = kept;
  }
  return set;
}

// --- Rewards ---------------------------------------------------------------

uint8_t questRewardsReadyMask(const ExtrasState& state) {
  if (!questsOn(state) || !hasQuestSet(state)) return 0;
  uint8_t ready = static_cast<uint8_t>(state.questDoneMask & QUEST_SLOT_MASK & ~state.questRewardedMask);
  if (state.questDoneMask == QUEST_SLOT_MASK && (state.questRewardedMask & QUEST_BONUS_BIT) == 0) {
    ready = static_cast<uint8_t>(ready | QUEST_BONUS_BIT);
  }
  return ready;
}

QuestRewards takeQuestRewards(ExtrasState& state) {
  const uint8_t ready = questRewardsReadyMask(state);
  const QuestRewards rewards = collectReady(state, ready);
  state.questRewardedMask = static_cast<uint8_t>(state.questRewardedMask | ready);
  return rewards;
}

bool grantQuestItem(PokemonState& mainState, const uint8_t itemId) {
  if (itemId < BAG_ITEM_ID_FIRST || itemId > BAG_ITEM_ID_LAST) return false;
  uint8_t& count = mainState.bagCounts[itemId - BAG_ITEM_ID_FIRST];
  if (count == UINT8_MAX) return false;
  ++count;
  uint8_t& received = mainState.receivedItems[itemId - 1U];
  if (received < UINT8_MAX) ++received;
  syncDashboardNotice(mainState);
  return true;
}

// --- Rotation and progress -------------------------------------------------

bool updateDailyQuests(ExtrasState& state, const QuestClock& clock, const bool eggHeld, const uint16_t minutes,
                       QuestRewards& expiredRewards) {
  if (!questsOn(state)) return false;
  const ExtrasState before = state;

  if (clock.clockValid) {
    const uint32_t today = clock.todayIndex & ~QUEST_CLOCKLESS_ANCHOR_BIT;
    if (!hasQuestSet(state) || state.questDayAnchor != today) {
      rotate(state, chooseQuestSet(questSeedForDay(today), eggHeld), today, expiredRewards);
    }
    advance(state, QuestKind::Read20, minutes);
    advance(state, QuestKind::Read40, minutes);
    advance(state, QuestKind::Read60, minutes);
    return !(state == before);
  }

  if (!hasQuestSet(state)) {
    rotate(state, clocklessSet(state, eggHeld), QUEST_CLOCKLESS_ANCHOR_BIT, expiredRewards);
  } else if (!isClocklessAnchor(state.questDayAnchor)) {
    state.questDayAnchor = QUEST_CLOCKLESS_ANCHOR_BIT;  // keep the day's set and its progress
  }
  advance(state, QuestKind::Read20, minutes);
  advance(state, QuestKind::Read40, minutes);
  advance(state, QuestKind::Read60, minutes);
  const uint32_t counted = clocklessMinutes(state.questDayAnchor) + minutes;
  if (counted >= QUEST_CLOCKLESS_ROTATION_MINUTES) {
    const uint32_t carry = counted % QUEST_CLOCKLESS_ROTATION_MINUTES;
    rotate(state, clocklessSet(state, eggHeld), QUEST_CLOCKLESS_ANCHOR_BIT | carry, expiredRewards);
  } else {
    state.questDayAnchor = QUEST_CLOCKLESS_ANCHOR_BIT | counted;
  }
  return !(state == before);
}

uint8_t recordQuestBattleWin(ExtrasState& state, const bool trainer) {
  return advance(state, trainer ? QuestKind::WinTrainer : QuestKind::WinWild, 1);
}

uint8_t recordQuestCatch(ExtrasState& state, const bool newToPokedex) {
  uint8_t done = advance(state, QuestKind::CatchAny, 1);
  if (newToPokedex) done = static_cast<uint8_t>(done | advance(state, QuestKind::CatchNew, 1));
  return done;
}

uint8_t recordQuestEggPages(ExtrasState& state, const uint16_t pages) {
  return advance(state, QuestKind::EggPages100, pages);
}

uint8_t recordQuestLevelUp(ExtrasState& state) { return advance(state, QuestKind::LevelUp, 1); }

}  // namespace pokemon::extras

#endif
