// Extras M2 rules: streak bonus (02-streaks.md), rare milestone encounter and
// Day Care (07-day-care.md). Pure functions only; the service hooks and their
// writes are covered by the compat test (R10) and the simulator gate.

#include <array>
#include <cstdio>
#include <set>

#include "PokemonExtrasRules.h"

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

StreakInput streak(const uint16_t days, const uint32_t today = TODAY) { return StreakInput{true, days, today}; }

uint32_t chooseFirst(void*, uint32_t) { return 0; }
uint32_t chooseLast(void*, const uint32_t upper) { return upper == 0 ? 0 : upper - 1; }

struct Counter {
  uint32_t value = 0;
};
uint32_t cycle(void* context, const uint32_t upper) {
  auto* counter = static_cast<Counter*>(context);
  return upper == 0 ? 0 : (counter->value++ * 7919U) % upper;
}

void fillQueue(PokemonState& state, const size_t count) {
  for (size_t index = 0; index < count; ++index) {
    const PendingEvent event{0, 16, 5, Gender::Male, EvolutionItem::None, PendingEventKind::Encounter};
    enqueuePendingEvent(state, event);
  }
}

// --- 2. Streaks ------------------------------------------------------------

void bonusIsFivePercentPerDayCappedAtTenDays() {
  const ExtrasState state{};
  CHECK(streakBonusPermille(state, streak(0)) == 0);
  CHECK(streakBonusPermille(state, streak(1)) == 50);
  CHECK(streakBonusPermille(state, streak(6)) == 300);
  CHECK(streakBonusPercent(state, streak(6)) == 30);
  CHECK(streakBonusPermille(state, streak(10)) == 500);
  CHECK(streakBonusPermille(state, streak(11)) == 500);
  CHECK(streakBonusPermille(state, streak(365)) == 500);
  CHECK(streakBonusPercent(state, streak(365)) == 50);
}

void noClockOrFeatureOffMeansNoBonusAndNoChange() {
  ExtrasState state{};
  state.streakXpRemainderPermille = 700;
  const ExtrasState before = state;
  CHECK(streakBonusPermille(state, StreakInput{false, 6, TODAY}) == 0);
  CHECK(applyStreakBonus(state, StreakInput{false, 6, TODAY}, 100) == 0);
  CHECK(state == before);

  setFeatureEnabled(state, Feature::Streaks, false);
  const ExtrasState off = state;
  CHECK(streakBonusPermille(state, streak(6)) == 0);
  CHECK(applyStreakBonus(state, streak(6), 100) == 0);
  CHECK(dueStreakMilestone(state, streak(7)) == 0);
  CHECK(state == off);
}

void remainderAddsUpOverManySmallCredits() {
  // 5-minute checkpoints at +30%: 1.5 XP each. 100 checkpoints = 500 minutes
  // = exactly 150 bonus XP, nothing lost to rounding.
  ExtrasState state{};
  uint32_t granted = 0;
  for (int checkpoint = 0; checkpoint < 100; ++checkpoint) {
    granted += applyStreakBonus(state, streak(6), 5);
    CHECK(state.streakXpRemainderPermille < PERMILLE);
    CHECK(validateExtrasState(state));
  }
  CHECK(granted == 150);
  CHECK(state.streakXpRemainderPermille == 0);

  // 1-minute credits at +5%: 0.05 each; the first whole XP comes on the 20th.
  ExtrasState slow{};
  for (int minute = 1; minute <= 19; ++minute) CHECK(applyStreakBonus(slow, streak(1), 1) == 0);
  CHECK(slow.streakXpRemainderPermille == 950);
  CHECK(applyStreakBonus(slow, streak(1), 1) == 1);
  CHECK(slow.streakXpRemainderPermille == 0);

  // One big credit gives the same total as many small ones.
  ExtrasState big{};
  CHECK(applyStreakBonus(big, streak(6), 500) == 150);
}

void milestoneIsGrantedOncePerStreak() {
  ExtrasState state{};
  CHECK(dueStreakMilestone(state, streak(6)) == 0);
  CHECK(dueStreakMilestone(state, streak(7)) == 7);
  markStreakMilestoneRewarded(state, streak(7), 7);
  CHECK(validateExtrasState(state));
  // Later checkpoints the same day, and the following days: nothing until 14.
  CHECK(dueStreakMilestone(state, streak(7)) == 0);
  for (uint16_t day = 8; day <= 13; ++day) CHECK(dueStreakMilestone(state, streak(day, TODAY + day - 7)) == 0);
  CHECK(dueStreakMilestone(state, streak(14, TODAY + 7)) == 14);
  markStreakMilestoneRewarded(state, streak(14, TODAY + 7), 14);
  CHECK(dueStreakMilestone(state, streak(14, TODAY + 7)) == 0);
  CHECK(dueStreakMilestone(state, streak(20, TODAY + 13)) == 0);
  CHECK(dueStreakMilestone(state, streak(21, TODAY + 14)) == 21);
}

void milestoneToleratesStreakEndingYesterday() {
  // Morning of day 8: today not recorded yet, so the firmware still says 7
  // (ending yesterday). Rewarded then. After today is recorded it says 8 with
  // an anchor one day earlier: still the same streak, no second encounter.
  ExtrasState state{};
  CHECK(dueStreakMilestone(state, streak(7, TODAY + 1)) == 7);
  markStreakMilestoneRewarded(state, streak(7, TODAY + 1), 7);
  CHECK(dueStreakMilestone(state, streak(8, TODAY + 1)) == 0);
  CHECK(dueStreakMilestone(state, streak(13, TODAY + 6)) == 0);
  CHECK(dueStreakMilestone(state, streak(14, TODAY + 7)) == 14);
}

void brokenStreakEarnsMilestonesAgain() {
  ExtrasState state{};
  markStreakMilestoneRewarded(state, streak(7), 7);
  // Missed a day; a new streak starts two days later and reaches 7 again.
  const uint32_t restartToday = TODAY + 2 + 6;
  CHECK(dueStreakMilestone(state, streak(7, restartToday)) == 7);
}

void missedCheckpointsGrantOnlyTheHighestMilestone() {
  ExtrasState state{};
  CHECK(dueStreakMilestone(state, streak(16)) == 14);
  markStreakMilestoneRewarded(state, streak(16), 14);
  CHECK(dueStreakMilestone(state, streak(17, TODAY + 1)) == 0);
}

void malformedInputIsIgnored() {
  ExtrasState state{};
  // A streak longer than the calendar: no anchor, no reward, no change.
  CHECK(dueStreakMilestone(state, StreakInput{true, 14, 3}) == 0);
  markStreakMilestoneRewarded(state, StreakInput{true, 14, 3}, 14);
  CHECK(state == ExtrasState{});
  markStreakMilestoneRewarded(state, streak(7), 0);
  CHECK(state == ExtrasState{});
}

void resetClearsStreakProgress() {
  ExtrasState state{};
  applyStreakBonus(state, streak(6), 3);
  markStreakMilestoneRewarded(state, streak(7), 7);
  resetExtrasProgress(state);
  CHECK(state.streakXpRemainderPermille == 0);
  CHECK(state.lastRewardedStreakMilestone == 0);
  CHECK(state.lastRewardedStreakDay == 0);
  CHECK(dueStreakMilestone(state, streak(7)) == 7);
}

// --- Rare encounter --------------------------------------------------------

void rarePoolIsWildRareTierOnly() {
  std::set<uint16_t> pool;
  for (uint16_t id = 1; id <= KANTO_SPECIES_COUNT; ++id) {
    const SpeciesData* species = speciesData(id);
    if (species != nullptr && isRarePoolSpecies(*species, 100)) pool.insert(id);
  }
  CHECK(!pool.empty());
  for (const uint16_t id : std::array<uint16_t, 5>{144, 145, 146, 150, 151}) CHECK(pool.count(id) == 0);  // legendaries, Mew
  CHECK(pool.count(1) == 1);    // starters
  CHECK(pool.count(25) == 1);   // Pikachu
  CHECK(pool.count(143) == 1);  // Snorlax
  CHECK(pool.count(19) == 0);   // Rattata: common
  for (const uint16_t id : pool) {
    const SpeciesData* species = speciesData(id);
    CHECK(species->acquisition == Acquisition::Wild);
  }
  // Book-progress gate: no middle/final stages early in a book.
  for (uint16_t id = 1; id <= KANTO_SPECIES_COUNT; ++id) {
    const SpeciesData* species = speciesData(id);
    if (species == nullptr || !isRarePoolSpecies(*species, 0)) continue;
    CHECK(species->stage != EvolutionStage::Middle && species->stage != EvolutionStage::Final);
  }
}

void queuesAnOrdinaryWildEncounterAtTheLeadersLevel() {
  PokemonState state{};
  RandomSource random{nullptr, chooseFirst};
  CHECK(queueRareEncounter(state, 23, 40, random));
  CHECK(pendingEventCount(state) == 1);
  const PendingEvent* event = pendingEventFront(state);
  CHECK(event != nullptr);
  CHECK(event->kind == PendingEventKind::Encounter);
  CHECK(event->recordId == 0);
  CHECK(event->level == 23);
  CHECK(!event->isShiny);
  const SpeciesData* species = speciesData(event->speciesId);
  CHECK(species != nullptr && isRarePoolSpecies(*species, 40));
  CHECK(isSpeciesMarked(state.seenSpecies, event->speciesId));
  CHECK(state.dashboardNotice != DashboardNotice::None);

  PokemonState low{};
  CHECK(queueRareEncounter(low, 1, 0, random));
  CHECK(pendingEventFront(low)->level == 2);
}

void respectsThePromptReserve() {
  PokemonState state{};
  fillQueue(state, PENDING_EVENT_CAPACITY - PENDING_EVENT_PROMPT_RESERVE - 1);
  CHECK(hasEncounterRoom(state));
  RandomSource random{nullptr, chooseLast};
  CHECK(queueRareEncounter(state, 10, 50, random));
  CHECK(!hasEncounterRoom(state));
  const PokemonState full = state;
  CHECK(!queueRareEncounter(state, 10, 50, random));
  CHECK(state == full);  // untouched: retry at the next checkpoint
  CHECK(pendingEventCount(state) == PENDING_EVENT_CAPACITY - PENDING_EVENT_PROMPT_RESERVE);
}

void prefersUncaughtSpecies() {
  // Catch every rare species but one: it should come up 3x as often as any
  // single caught one. Checked by exact weights.
  PokemonState state{};
  uint16_t lastRare = 0;
  uint32_t poolSize = 0;
  for (uint16_t id = 1; id <= KANTO_SPECIES_COUNT; ++id) {
    const SpeciesData* species = speciesData(id);
    if (species == nullptr || !isRarePoolSpecies(*species, 100)) continue;
    markSpecies(state.caughtSpecies, id);
    lastRare = id;
    ++poolSize;
  }
  // Unmark the last one: total weight = (poolSize - 1) + 3; chooseLast picks
  // the final 3 slots, which all belong to it.
  PokemonState partly = state;
  partly.caughtSpecies[(lastRare - 1U) / 8U] = static_cast<uint8_t>(
      partly.caughtSpecies[(lastRare - 1U) / 8U] & ~static_cast<uint8_t>(1U << ((lastRare - 1U) % 8U)));
  uint16_t picked = 0;
  CHECK(chooseRareSpecies(partly, 100, RandomSource{nullptr, chooseLast}, picked));
  CHECK(picked == lastRare);
  // Every draw value once, 50 times over: the uncaught species owns 3 of the
  // poolSize + 2 slots.
  uint32_t hits = 0;
  const uint32_t draws = (poolSize + 2U) * 50U;
  for (uint32_t draw = 0; draw < draws; ++draw) {
    struct Fixed {
      uint32_t value;
    } fixed{draw % (poolSize + 2U)};
    RandomSource exact{&fixed, [](void* context, uint32_t) { return static_cast<Fixed*>(context)->value; }};
    CHECK(chooseRareSpecies(partly, 100, exact, picked));
    if (picked == lastRare) ++hits;
  }
  CHECK(hits == 3U * 50U);
}

void manyRollsStayInThePool() {
  Counter counter{};
  RandomSource random{&counter, cycle};
  for (int roll = 0; roll < 500; ++roll) {
    PokemonState state{};
    const uint8_t progress = static_cast<uint8_t>(roll % 101);
    CHECK(queueRareEncounter(state, 12, progress, random));
    const PendingEvent* event = pendingEventFront(state);
    CHECK(event != nullptr);
    const SpeciesData* species = speciesData(event->speciesId);
    CHECK(species != nullptr && isRarePoolSpecies(*species, progress));
    CHECK(event->speciesId != 151);
  }
}

// --- 7. Day Care -----------------------------------------------------------

PokemonState partyOf(const uint32_t a, const uint32_t b = 0) {
  PokemonState state{};
  state.partyRecordIds[0] = a;
  state.partyRecordIds[1] = b;
  return state;
}

void onlyABoxedPokemonCanBeSent() {
  ExtrasState state{};
  const PokemonState main = partyOf(1, 2);
  CHECK(sendToDayCare(state, main, 0) == DayCareSendResult::InvalidRecord);
  CHECK(sendToDayCare(state, main, 2) == DayCareSendResult::InParty);
  CHECK(state.dayCareRecordId == 0);
  CHECK(sendToDayCare(state, main, 7) == DayCareSendResult::Sent);
  CHECK(state.dayCareRecordId == 7);
  CHECK(sendToDayCare(state, main, 7) == DayCareSendResult::AlreadyThere);
  CHECK(sendToDayCare(state, main, 8) == DayCareSendResult::Occupied);
  CHECK(state.dayCareRecordId == 7);
  CHECK(validateExtrasState(state));

  ExtrasState off{};
  setFeatureEnabled(off, Feature::DayCare, false);
  CHECK(sendToDayCare(off, main, 7) == DayCareSendResult::Disabled);
  CHECK(off.dayCareRecordId == 0);
}

void earnsHalfTheLeadsReadingXpKeepingTheOddMinute() {
  ExtrasState state{};
  sendToDayCare(state, partyOf(1), 9);
  const uint32_t lowXp = xpRequired(5);
  CHECK(applyDayCareMinutes(state, 5, lowXp, 30) == 2);
  CHECK(state.dayCareMinuteRemainder == 1);
  CHECK(applyDayCareMinutes(state, 5, lowXp, 30) == 3);
  CHECK(state.dayCareMinuteRemainder == 0);
  CHECK(applyDayCareMinutes(state, 1, lowXp, 30) == 0);
  CHECK(applyDayCareMinutes(state, 1, lowXp, 30) == 1);
  CHECK(validateExtrasState(state));

  // 1000 one-minute credits: exactly 500 XP.
  ExtrasState steady{};
  sendToDayCare(steady, partyOf(1), 9);
  uint32_t total = 0;
  for (int minute = 0; minute < 1000; ++minute) total += applyDayCareMinutes(steady, 1, lowXp, 100);
  CHECK(total == 500);
}

void neverPassesTheLeadsLevel() {
  CHECK(levelForXp(dayCareXpCap(20)) == 20);
  CHECK(levelForXp(dayCareXpCap(20) + 1) == 21);
  CHECK(dayCareXpCap(100) == MAXIMUM_TOTAL_XP);
  CHECK(levelForXp(dayCareXpCap(1)) == 1);
  CHECK(levelForXp(dayCareXpCap(0)) == 1);

  ExtrasState state{};
  sendToDayCare(state, partyOf(1), 9);
  const uint32_t cap = dayCareXpCap(20);
  // Two XP short of the cap: a 10-minute credit (5 XP) is cut to 2.
  CHECK(applyDayCareMinutes(state, 10, cap - 2, 20) == 2);
  // At the cap: nothing more, but the minute remainder still advances.
  CHECK(applyDayCareMinutes(state, 3, cap, 20) == 0);
  CHECK(state.dayCareMinuteRemainder == 1);
  // Above the cap (the lead was swapped for a weaker one): nothing.
  CHECK(applyDayCareMinutes(state, 10, xpRequired(30), 20) == 0);
  // Lead at 100: up to MAXIMUM_TOTAL_XP.
  CHECK(applyDayCareMinutes(state, 10, MAXIMUM_TOTAL_XP - 1, 100) == 1);
}

void emptySlotOrFeatureOffEarnsNothing() {
  ExtrasState empty{};
  CHECK(applyDayCareMinutes(empty, 10, 0, 50) == 0);
  CHECK(empty == ExtrasState{});

  ExtrasState state{};
  sendToDayCare(state, partyOf(1), 9);
  setFeatureEnabled(state, Feature::DayCare, false);
  const ExtrasState off = state;
  CHECK(applyDayCareMinutes(state, 10, 0, 50) == 0);
  CHECK(state == off);  // the slot is kept while the feature is off
}

void slotClearedOnWithdrawReleaseStaleAndReset() {
  ExtrasState state{};
  sendToDayCare(state, partyOf(1), 9);
  applyDayCareMinutes(state, 1, 0, 50);
  CHECK(!clearDayCareIfHolds(state, 8));
  CHECK(!clearDayCareIfHolds(state, 0));
  CHECK(state.dayCareRecordId == 9);
  CHECK(clearDayCareIfHolds(state, 9));  // withdraw / release / stale id
  CHECK(state.dayCareRecordId == 0 && state.dayCareMinuteRemainder == 0);
  CHECK(validateExtrasState(state));

  sendToDayCare(state, partyOf(1), 9);
  applyDayCareMinutes(state, 1, 0, 50);
  clearDayCare(state);
  CHECK(state.dayCareRecordId == 0 && state.dayCareMinuteRemainder == 0);

  sendToDayCare(state, partyOf(1), 9);
  applyDayCareMinutes(state, 1, 0, 50);
  resetExtrasProgress(state);
  CHECK(state.dayCareRecordId == 0 && state.dayCareMinuteRemainder == 0);
  CHECK(isFeatureEnabled(state, Feature::DayCare));
}

}  // namespace

int main() {
  bonusIsFivePercentPerDayCappedAtTenDays();
  noClockOrFeatureOffMeansNoBonusAndNoChange();
  remainderAddsUpOverManySmallCredits();
  milestoneIsGrantedOncePerStreak();
  milestoneToleratesStreakEndingYesterday();
  brokenStreakEarnsMilestonesAgain();
  missedCheckpointsGrantOnlyTheHighestMilestone();
  malformedInputIsIgnored();
  resetClearsStreakProgress();
  rarePoolIsWildRareTierOnly();
  queuesAnOrdinaryWildEncounterAtTheLeadersLevel();
  respectsThePromptReserve();
  prefersUncaughtSpecies();
  manyRollsStayInThePool();
  onlyABoxedPokemonCanBeSent();
  earnsHalfTheLeadsReadingXpKeepingTheOddMinute();
  neverPassesTheLeadsLevel();
  emptySlotOrFeatureOffEarnsNothing();
  slotClearedOnWithdrawReleaseStaleAndReset();
  if (failures != 0) {
    std::fprintf(stderr, "%d check(s) failed\n", failures);
    return 1;
  }
  std::puts("PokemonExtrasRulesTest: all checks passed");
  return 0;
}
