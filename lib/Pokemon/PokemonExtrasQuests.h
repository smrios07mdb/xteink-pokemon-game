#pragma once

// Extras M3 rules, feature 8: daily quests (docs/extras/features/08-daily-quests.md).
// Pure, storage-free, host-testable. The service hooks call these and do the
// writes (docs/extras/M3-HOOKS.md):
//   - rewards go into the bag the way upstream's own item drops do (R3);
//   - the day comes from the caller, read-only from the reading-stats clock (R4);
//   - every quest field lives in the extras file; no codec change (R1, R2).
//
// Compiles to nothing without CROSSINK_POKEMON_EXTRAS (R11).

#include <array>
#include <cstdint>

#include "PokemonExtras.h"
#include "PokemonTypes.h"

namespace pokemon::extras {

// ---------------------------------------------------------------------------
// Tunables
// ---------------------------------------------------------------------------
constexpr uint16_t QUEST_CLOCKLESS_ROTATION_MINUTES = 120;  // no valid clock: new set every 120 credited minutes

// Reward item ids (PokemonItems.generated.h): one per quest, Rare Candy for all three.
constexpr uint8_t QUEST_ITEM_POKE_BALL = 7;
constexpr uint8_t QUEST_ITEM_GREAT_BALL = 8;
constexpr uint8_t QUEST_ITEM_POTION = 11;
constexpr uint8_t QUEST_ITEM_SUPER_POTION = 12;
constexpr uint8_t QUEST_ITEM_HYPER_POTION = 13;
constexpr uint8_t QUEST_ITEM_RARE_CANDY = 24;
constexpr uint8_t QUEST_BONUS_ITEM = QUEST_ITEM_RARE_CANDY;

// ---------------------------------------------------------------------------
// questDayAnchor encoding (existing u32 field, no codec change)
// ---------------------------------------------------------------------------
// Bit 31 clear: the set belongs to that reading-stats day index
//   (readingStatsDayIndex(), days since 2000-01-01, far below 2^31).
// Bit 31 set: a clockless set; bits 0-30 are the credited minutes since it
//   was chosen (0..QUEST_CLOCKLESS_ROTATION_MINUTES-1).
constexpr uint32_t QUEST_CLOCKLESS_ANCHOR_BIT = 0x80000000U;

// What the caller read from the firmware's clock (read-only, R4).
// clockValid: getCurrentLocalReadingStatsDateTime() succeeded.
// todayIndex: readingStatsDayIndex(today).
struct QuestClock {
  bool clockValid = false;
  uint32_t todayIndex = 0;
};

// ---------------------------------------------------------------------------
// Quest data
// ---------------------------------------------------------------------------
// The goal for a kind: 20/40/60 minutes, 100 pages, else 1. 0 for None.
uint16_t questTarget(QuestKind kind);
// The item a finished quest gives (balls for battles and catches, medicine
// for reading, eggs and levels). 0 for None.
uint8_t questRewardItem(QuestKind kind);
bool isReadingQuest(QuestKind kind);
QuestKind questKindAt(const ExtrasState& state, uint8_t slot);
bool hasQuestSet(const ExtrasState& state);

// ---------------------------------------------------------------------------
// Choosing the day's three
// ---------------------------------------------------------------------------
// Three different kinds from the pool, at most one reading quest (Read20/40/60
// overlap), EggPages100 only when `eggHeld`. Deterministic for a seed.
std::array<QuestKind, DAILY_QUEST_SLOTS> chooseQuestSet(uint32_t seed, bool eggHeld);
// The seed for a day: the same day always gives the same set (for the same
// eggHeld). Clockless sets use the extras RNG instead.
uint32_t questSeedForDay(uint32_t dayIndex);

// ---------------------------------------------------------------------------
// Rewards
// ---------------------------------------------------------------------------
struct QuestRewards {
  std::array<uint8_t, DAILY_QUEST_SLOTS + 1> itemIds{};  // up to three quest items + the bonus
  uint8_t count = 0;
};

// Rewards ready to claim: bits 0-2 a finished quest not yet rewarded, bit 3
// (QUEST_BONUS_BIT) all three finished and the bonus not yet given. 0 with the
// toggle off. Drives the Home `!` (with isEggReadyToHatch).
uint8_t questRewardsReadyMask(const ExtrasState& state);

// Marks every ready reward as rewarded and returns the items to grant.
// Crash safety (R6, loss over duplication, like the streak milestone): commit
// EXTRAS first, then grant the items with grantQuestItem() and commit the
// MAIN save. A crash in between loses those items; it can never give them
// twice. If the main commit fails, restore the extras copy and commit again.
QuestRewards takeQuestRewards(ExtrasState& state);

// Puts one item in the bag exactly as upstream's own drops do (PokemonGame.cpp
// creditItem(), file-local there): count +1, listed on the "items received"
// summary, dashboard notice refreshed. Returns false (state untouched) when the
// id is out of range or that count is already at its maximum; the reward is
// then simply lost, never retried.
bool grantQuestItem(PokemonState& mainState, uint8_t itemId);

// ---------------------------------------------------------------------------
// Rotation and progress
// ---------------------------------------------------------------------------
// Call at every credit with the credited minutes (Kindle minutes included) and
// on Pokemon menu open with minutes = 0. In order:
//   1. no set yet: choose one;
//   2. valid clock and the set is not today's: rotate;
//   3. reading quests advance by `minutes`;
//   4. no valid clock: the clockless counter advances by `minutes`; at
//      QUEST_CLOCKLESS_ROTATION_MINUTES the set rotates (a day set seen
//      without a clock becomes a clockless set first, progress kept).
// Rotation: unfinished quests expire, and ready rewards of the old set
// (questRewardsReadyMask) are returned in `expiredRewards` to grant at once,
// same order as takeQuestRewards (extras first, then main).
// Returns true when the extras state changed. Toggle off: nothing happens.
bool updateDailyQuests(ExtrasState& state, const QuestClock& clock, bool eggHeld, uint16_t minutes,
                       QuestRewards& expiredRewards);

// Game events, each a no-op (returns 0) with the toggle off or no set. Return
// the slot bits that became done with this event (0 if none).
uint8_t recordQuestBattleWin(ExtrasState& state, bool trainer);  // wild win, or road/gym trainer win
uint8_t recordQuestCatch(ExtrasState& state, bool newToPokedex);  // CatchAny, plus CatchNew when new
uint8_t recordQuestEggPages(ExtrasState& state, uint16_t pages);  // pages counted by applyEggPages()
uint8_t recordQuestLevelUp(ExtrasState& state);

}  // namespace pokemon::extras
