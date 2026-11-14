// Extras M3 rules, feature 8: daily quests (08-daily-quests.md). Pure
// functions only; the service hooks are covered by the compat test (R10) and
// the simulator gate (M3-HOOKS.md).

#include <array>
#include <cstdio>
#include <set>

#include "PokemonBattleTypes.h"
#include "PokemonExtrasQuests.h"
#include "PokemonGame.h"

namespace {

using namespace pokemon;
using namespace pokemon::extras;

int failures = 0;

#define CHECK(condition)                                                                \
  do {                                                                                  \
    if (!(condition)) {                                                                 \
      std::fprintf(stderr, "%s:%d check failed: %s\n", __FILE__, __LINE__, #condition); \
      ++failures;                                                                       \
    }                                                                                   \
  } while (false)

constexpr uint32_t TODAY = 9800;  // readingStatsDayIndex, days since 2000-01-01
constexpr QuestClock clockOn(const uint32_t day = TODAY) { return QuestClock{true, day}; }
constexpr QuestClock clockOff() { return QuestClock{false, 0}; }

bool setIsWellFormed(const std::array<QuestKind, DAILY_QUEST_SLOTS>& set, const bool eggHeld) {
  std::set<QuestKind> kinds(set.begin(), set.end());
  int reading = 0;
  for (const QuestKind kind : set) {
    if (kind == QuestKind::None) return false;
    if (kind == QuestKind::EggPages100 && !eggHeld) return false;
    if (isReadingQuest(kind)) ++reading;
  }
  return kinds.size() == DAILY_QUEST_SLOTS && reading <= 1;
}

int slotOf(const ExtrasState& state, const QuestKind kind) {
  for (uint8_t slot = 0; slot < DAILY_QUEST_SLOTS; ++slot) {
    if (questKindAt(state, slot) == kind) return slot;
  }
  return -1;
}

// A state holding exactly this set for today.
ExtrasState stateWith(const QuestKind a, const QuestKind b, const QuestKind c, const uint32_t anchor = TODAY) {
  ExtrasState state{};
  state.questIds = {static_cast<uint8_t>(a), static_cast<uint8_t>(b), static_cast<uint8_t>(c)};
  state.questDayAnchor = anchor;
  return state;
}

// --- Data ------------------------------------------------------------------

void targetsAndRewards() {
  CHECK(questTarget(QuestKind::Read20) == 20 && questTarget(QuestKind::Read40) == 40 &&
        questTarget(QuestKind::Read60) == 60);
  CHECK(questTarget(QuestKind::EggPages100) == 100 && questTarget(QuestKind::WinWild) == 1 &&
        questTarget(QuestKind::LevelUp) == 1 && questTarget(QuestKind::None) == 0);
  for (uint8_t id = 1; id <= QUEST_KIND_MAX; ++id) {
    const uint8_t item = questRewardItem(static_cast<QuestKind>(id));
    const ItemData* data = itemData(item);
    CHECK(data != nullptr);
    if (data != nullptr) CHECK(data->category == ItemCategory::Ball || data->category == ItemCategory::Medicine);
  }
  CHECK(questRewardItem(QuestKind::None) == 0);
  const ItemData* candy = itemData(QUEST_BONUS_ITEM);
  CHECK(candy != nullptr && candy->category == ItemCategory::Candy);
}

// --- Choosing --------------------------------------------------------------

void selectionIsDeterministicPerDay() {
  for (uint32_t day = 9000; day < 9400; ++day) {
    const auto first = chooseQuestSet(questSeedForDay(day), false);
    CHECK(first == chooseQuestSet(questSeedForDay(day), false));
    CHECK(setIsWellFormed(first, false));
    CHECK(setIsWellFormed(chooseQuestSet(questSeedForDay(day), true), true));
  }
  // Different days do not all give the same set.
  std::set<std::array<QuestKind, DAILY_QUEST_SLOTS>> sets;
  for (uint32_t day = 0; day < 60; ++day) sets.insert(chooseQuestSet(questSeedForDay(day), false));
  CHECK(sets.size() > 20);
}

void eggQuestOnlyWithAnEgg() {
  bool offered = false;
  for (uint32_t seed = 0; seed < 2000; ++seed) {
    for (const QuestKind kind : chooseQuestSet(seed, false)) CHECK(kind != QuestKind::EggPages100);
    for (const QuestKind kind : chooseQuestSet(seed, true)) offered = offered || kind == QuestKind::EggPages100;
  }
  CHECK(offered);
}

void everyKindCanBeChosen() {
  std::set<QuestKind> seen;
  for (uint32_t seed = 0; seed < 2000; ++seed) {
    for (const QuestKind kind : chooseQuestSet(seed, true)) seen.insert(kind);
  }
  CHECK(seen.size() == QUEST_KIND_MAX);
}

// --- Rotation --------------------------------------------------------------

void firstCallChoosesTodaysSet() {
  ExtrasState state{};
  QuestRewards expired{};
  CHECK(updateDailyQuests(state, clockOn(), false, 0, expired));
  CHECK(hasQuestSet(state) && state.questDayAnchor == TODAY && expired.count == 0);
  const auto expected = chooseQuestSet(questSeedForDay(TODAY), false);
  for (uint8_t slot = 0; slot < DAILY_QUEST_SLOTS; ++slot) CHECK(questKindAt(state, slot) == expected[slot]);
  CHECK(validateExtrasState(state));
  // Same day again: nothing changes.
  const ExtrasState before = state;
  CHECK(!updateDailyQuests(state, clockOn(), true, 0, expired));
  CHECK(state == before);
}

void readingMinutesAdvanceOnlyTheReadingQuest() {
  ExtrasState state = stateWith(QuestKind::Read40, QuestKind::WinWild, QuestKind::CatchAny);
  QuestRewards expired{};
  CHECK(updateDailyQuests(state, clockOn(), false, 18, expired));
  CHECK(state.questProgress[0] == 18 && state.questProgress[1] == 0 && state.questDoneMask == 0);
  updateDailyQuests(state, clockOn(), false, 30, expired);
  CHECK(state.questProgress[0] == 40 && state.questDoneMask == 0x01);  // capped at the target
  updateDailyQuests(state, clockOn(), false, 30, expired);
  CHECK(state.questProgress[0] == 40);
  CHECK(validateExtrasState(state));
}

void newDayExpiresAndAutoGrantsUnclaimed() {
  ExtrasState state = stateWith(QuestKind::Read20, QuestKind::WinWild, QuestKind::LevelUp);
  QuestRewards expired{};
  updateDailyQuests(state, clockOn(), false, 20, expired);
  recordQuestBattleWin(state, false);
  CHECK(state.questDoneMask == 0x03);
  // One claimed, one not; LevelUp unfinished.
  state.questRewardedMask = 0x01;
  CHECK(updateDailyQuests(state, clockOn(TODAY + 1), false, 5, expired));
  CHECK(expired.count == 1 && expired.itemIds[0] == questRewardItem(QuestKind::WinWild));
  CHECK(state.questDayAnchor == TODAY + 1 && state.questDoneMask == 0 && state.questRewardedMask == 0);
  // Minutes of the new day go to the new set.
  const int reading = [&] {
    for (uint8_t slot = 0; slot < DAILY_QUEST_SLOTS; ++slot) {
      if (isReadingQuest(questKindAt(state, slot))) return static_cast<int>(slot);
    }
    return -1;
  }();
  if (reading >= 0) CHECK(state.questProgress[reading] == 5);
  CHECK(validateExtrasState(state));
}

void allThreeDoneAutoGrantsTheBonusAtRotation() {
  ExtrasState state = stateWith(QuestKind::WinWild, QuestKind::WinTrainer, QuestKind::LevelUp);
  recordQuestBattleWin(state, false);
  recordQuestBattleWin(state, true);
  recordQuestLevelUp(state);
  QuestRewards expired{};
  updateDailyQuests(state, clockOn(TODAY + 2), false, 0, expired);
  CHECK(expired.count == 4 && expired.itemIds[3] == QUEST_BONUS_ITEM);
}

void noClockRotatesEvery120Minutes() {
  ExtrasState state{};
  seedRandom(state, 42);
  QuestRewards expired{};
  CHECK(updateDailyQuests(state, clockOff(), false, 0, expired));
  CHECK(hasQuestSet(state) && state.questDayAnchor == QUEST_CLOCKLESS_ANCHOR_BIT);
  CHECK(validateExtrasState(state));
  const auto firstIds = state.questIds;
  updateDailyQuests(state, clockOff(), false, 100, expired);
  CHECK(state.questIds == firstIds && state.questDayAnchor == (QUEST_CLOCKLESS_ANCHOR_BIT | 100U));
  updateDailyQuests(state, clockOff(), false, 25, expired);  // 125: rotates, 5 carried
  CHECK(state.questDayAnchor == (QUEST_CLOCKLESS_ANCHOR_BIT | 5U));
  CHECK(state.questDoneMask == 0 && validateExtrasState(state));
  // A very long credit (a Kindle sync) rotates once and keeps the counter in range.
  updateDailyQuests(state, clockOff(), false, 600, expired);
  CHECK((state.questDayAnchor & ~QUEST_CLOCKLESS_ANCHOR_BIT) < QUEST_CLOCKLESS_ROTATION_MINUTES);
}

void clockLostKeepsTheDaySetThenClockBackRotates() {
  ExtrasState state = stateWith(QuestKind::Read60, QuestKind::CatchAny, QuestKind::LevelUp);
  QuestRewards expired{};
  updateDailyQuests(state, clockOn(), false, 30, expired);
  updateDailyQuests(state, clockOff(), false, 10, expired);
  CHECK(state.questProgress[0] == 40 && state.questDayAnchor == (QUEST_CLOCKLESS_ANCHOR_BIT | 10U));
  CHECK(questKindAt(state, 0) == QuestKind::Read60);
  updateDailyQuests(state, clockOn(), false, 0, expired);
  CHECK(state.questDayAnchor == TODAY && state.questProgress[0] == 0);
}

void toggleOffDoesNothing() {
  ExtrasState state = stateWith(QuestKind::WinWild, QuestKind::CatchAny, QuestKind::LevelUp);
  setFeatureEnabled(state, Feature::DailyQuests, false);
  const ExtrasState before = state;
  QuestRewards expired{};
  CHECK(!updateDailyQuests(state, clockOn(TODAY + 5), false, 30, expired));
  CHECK(recordQuestBattleWin(state, false) == 0 && recordQuestCatch(state, true) == 0);
  CHECK(recordQuestLevelUp(state) == 0 && recordQuestEggPages(state, 100) == 0);
  CHECK(questRewardsReadyMask(state) == 0);
  CHECK(state == before && expired.count == 0);

  ExtrasState empty{};
  CHECK(recordQuestBattleWin(empty, false) == 0 && empty == ExtrasState{});
}

// --- Events ----------------------------------------------------------------

void eachEventAdvancesTheRightQuest() {
  ExtrasState state = stateWith(QuestKind::WinTrainer, QuestKind::CatchNew, QuestKind::EggPages100);
  CHECK(recordQuestBattleWin(state, false) == 0);
  CHECK(recordQuestBattleWin(state, true) == 0x01);
  CHECK(recordQuestBattleWin(state, true) == 0);  // already done
  CHECK(recordQuestCatch(state, false) == 0);
  CHECK(recordQuestCatch(state, true) == 0x02);
  CHECK(recordQuestEggPages(state, 60) == 0 && state.questProgress[2] == 60);
  CHECK(recordQuestEggPages(state, 60) == 0x04 && state.questProgress[2] == 100);
  CHECK(state.questDoneMask == QUEST_SLOT_MASK);
  CHECK(recordQuestLevelUp(state) == 0);

  ExtrasState both = stateWith(QuestKind::CatchAny, QuestKind::CatchNew, QuestKind::LevelUp);
  CHECK(recordQuestCatch(both, true) == 0x03);
  CHECK(recordQuestLevelUp(both) == 0x04);
  CHECK(slotOf(both, QuestKind::LevelUp) == 2);
}

// --- Rewards ---------------------------------------------------------------

void rewardsAreGrantedOnce() {
  ExtrasState state = stateWith(QuestKind::WinWild, QuestKind::CatchAny, QuestKind::LevelUp);
  CHECK(questRewardsReadyMask(state) == 0);
  recordQuestBattleWin(state, false);
  CHECK(questRewardsReadyMask(state) == 0x01);
  QuestRewards first = takeQuestRewards(state);
  CHECK(first.count == 1 && first.itemIds[0] == QUEST_ITEM_POKE_BALL);
  CHECK(questRewardsReadyMask(state) == 0 && takeQuestRewards(state).count == 0);
  CHECK(validateExtrasState(state));

  recordQuestCatch(state, false);
  recordQuestLevelUp(state);
  CHECK(questRewardsReadyMask(state) == (0x06 | QUEST_BONUS_BIT));
  const QuestRewards rest = takeQuestRewards(state);
  CHECK(rest.count == 3 && rest.itemIds[0] == QUEST_ITEM_POKE_BALL && rest.itemIds[1] == QUEST_ITEM_SUPER_POTION &&
        rest.itemIds[2] == QUEST_BONUS_ITEM);
  CHECK(state.questRewardedMask == (QUEST_SLOT_MASK | QUEST_BONUS_BIT));
  CHECK(questRewardsReadyMask(state) == 0 && validateExtrasState(state));

  // Already rewarded: nothing comes back at rotation either.
  QuestRewards expired{};
  updateDailyQuests(state, clockOn(TODAY + 1), false, 0, expired);
  CHECK(expired.count == 0);
}

void grantGoesIntoTheBagLikeAnUpstreamDrop() {
  PokemonState main{};
  CHECK(grantQuestItem(main, QUEST_ITEM_POKE_BALL));
  CHECK(main.bagCounts[QUEST_ITEM_POKE_BALL - EVOLUTION_ITEM_COUNT - 1U] == 1);
  CHECK(main.receivedItems[QUEST_ITEM_POKE_BALL - 1U] == 1);
  CHECK(main.dashboardNotice == DashboardNotice::ItemFound);
  CHECK(grantQuestItem(main, QUEST_ITEM_RARE_CANDY));
  CHECK(main.bagCounts[QUEST_ITEM_RARE_CANDY - EVOLUTION_ITEM_COUNT - 1U] == 1);
  CHECK(validateState(main));

  main.bagCounts[QUEST_ITEM_POTION - EVOLUTION_ITEM_COUNT - 1U] = UINT8_MAX;
  const PokemonState full = main;
  CHECK(!grantQuestItem(main, QUEST_ITEM_POTION) && main == full);
  CHECK(!grantQuestItem(main, 0) && !grantQuestItem(main, 3) && !grantQuestItem(main, 84) && main == full);
}

void resetClearsQuests() {
  ExtrasState state = stateWith(QuestKind::WinWild, QuestKind::CatchAny, QuestKind::LevelUp);
  recordQuestBattleWin(state, false);
  resetExtrasProgress(state);
  CHECK(!hasQuestSet(state) && state.questDoneMask == 0 && state.questDayAnchor == 0);
  CHECK(validateExtrasState(state));
}

}  // namespace

int main() {
  targetsAndRewards();
  selectionIsDeterministicPerDay();
  eggQuestOnlyWithAnEgg();
  everyKindCanBeChosen();
  firstCallChoosesTodaysSet();
  readingMinutesAdvanceOnlyTheReadingQuest();
  newDayExpiresAndAutoGrantsUnclaimed();
  allThreeDoneAutoGrantsTheBonusAtRotation();
  noClockRotatesEvery120Minutes();
  clockLostKeepsTheDaySetThenClockBackRotates();
  toggleOffDoesNothing();
  eachEventAdvancesTheRightQuest();
  rewardsAreGrantedOnce();
  grantGoesIntoTheBagLikeAnUpstreamDrop();
  resetClearsQuests();
  if (failures != 0) {
    std::fprintf(stderr, "%d check(s) failed\n", failures);
    return 1;
  }
  std::puts("PokemonExtrasQuestsTest: all checks passed");
  return 0;
}
