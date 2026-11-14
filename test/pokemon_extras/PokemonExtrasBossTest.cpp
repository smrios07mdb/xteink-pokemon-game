// Extras M4 rules, feature 3: book-finish boss (03-book-boss.md). Pure
// functions only; the reader hooks and their writes are covered by the compat
// test (R10) and the simulator gate (M4-HOOKS.md).

#include <algorithm>
#include <cstdio>
#include <map>
#include <set>

#include "PokemonExtrasBoss.h"
#include "PokemonExtrasKindle.h"
#include "PokemonExtrasRules.h"
#include "PokemonSpecies.h"

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

constexpr uint32_t BOOK = 0xB00C0001U;
constexpr uint32_t HOUR = 60U * 60U;
constexpr uint16_t MEW = 151;
constexpr size_t MASTER_BALL_SLOT = BOSS_ITEM_MASTER_BALL - EVOLUTION_ITEM_COUNT - 1U;
constexpr size_t ULTRA_BALL_SLOT = BOSS_ITEM_ULTRA_BALL - EVOLUTION_ITEM_COUNT - 1U;

uint32_t chooseFirst(void*, uint32_t) { return 0; }
uint32_t chooseLast(void*, const uint32_t upper) { return upper == 0 ? 0 : upper - 1; }
RandomSource fixed(uint32_t (*below)(void*, uint32_t)) { return RandomSource{nullptr, below}; }

BookFinish finished(const uint32_t hash, const uint32_t seconds = HOUR) { return BookFinish{hash, seconds}; }

PokemonState caughtAll(std::initializer_list<uint16_t> species) {
  PokemonState state{};
  for (const uint16_t id : species) markSpecies(state.caughtSpecies, id);
  return state;
}

PokemonState birdsCaught() { return caughtAll({144, 145, 146}); }
PokemonState legendariesCaught() { return caughtAll({144, 145, 146, 150}); }

bool inRarePool(const uint16_t speciesId) {
  return std::find(BOSS_RARE_POOL.begin(), BOSS_RARE_POOL.end(), speciesId) != BOSS_RARE_POOL.end();
}

void fillQueue(PokemonState& state, const size_t count) {
  for (size_t index = 0; index < count; ++index) {
    const PendingEvent event{0, 16, 5, Gender::Male, EvolutionItem::None, PendingEventKind::Encounter};
    enqueuePendingEvent(state, event);
  }
}

// --- Once per book ---------------------------------------------------------

void eachBookCountsOnceEver() {
  ExtrasState state{};
  const PokemonState main{};
  CHECK(!isBookCounted(state, BOOK));
  CHECK(onBookFinished(state, main, finished(BOOK)) == BookFinishResult::BossHeld);
  CHECK(isBookCounted(state, BOOK));
  CHECK(booksFinishedRibbons(state) == 1);
  CHECK(hasHeldBoss(state));
  CHECK(validateExtrasState(state));

  // Un-mark and re-mark, with the boss still held or already delivered: nothing.
  ExtrasState again = state;
  CHECK(onBookFinished(again, main, finished(BOOK)) == BookFinishResult::AlreadyCounted);
  CHECK(again == state);
  takeHeldBoss(again);
  const ExtrasState delivered = again;
  CHECK(onBookFinished(again, main, finished(BOOK, 10U * HOUR)) == BookFinishResult::AlreadyCounted);
  CHECK(again == delivered);
}

void toggleOffOrNoIdDoesNothing() {
  ExtrasState state{};
  const ExtrasState fresh = state;
  CHECK(onBookFinished(state, PokemonState{}, finished(0)) == BookFinishResult::Ignored);
  CHECK(state == fresh);

  setFeatureEnabled(state, Feature::BookBoss, false);
  const ExtrasState off = state;
  CHECK(onBookFinished(state, PokemonState{}, finished(BOOK)) == BookFinishResult::Ignored);
  CHECK(state == off);
  // Turned on later, the book has not been counted yet.
  setFeatureEnabled(state, Feature::BookBoss, true);
  CHECK(onBookFinished(state, PokemonState{}, finished(BOOK)) == BookFinishResult::BossHeld);
}

void listKeepsTheLastBooksAndDropsTheOldest() {
  ExtrasState state{};
  const PokemonState main{};
  for (uint32_t index = 0; index < REWARDED_BOOK_CAPACITY; ++index) {
    CHECK(onBookFinished(state, main, finished(100U + index, 0)) == BookFinishResult::RibbonOnly);
  }
  CHECK(state.rewardedBookCount == REWARDED_BOOK_CAPACITY);
  CHECK(isBookCounted(state, 100U));
  CHECK(validateExtrasState(state));

  CHECK(onBookFinished(state, main, finished(9000U, 0)) == BookFinishResult::RibbonOnly);
  CHECK(state.rewardedBookCount == REWARDED_BOOK_CAPACITY);
  CHECK(!isBookCounted(state, 100U));  // the oldest went
  CHECK(isBookCounted(state, 101U));
  CHECK(isBookCounted(state, 9000U));
  CHECK(state.rewardedBookHashes[REWARDED_BOOK_CAPACITY - 1U] == 9000U);
  CHECK(booksFinishedRibbons(state) == REWARDED_BOOK_CAPACITY + 1U);  // ribbons keep counting
  CHECK(validateExtrasState(state));

  state.ribbonCount = UINT16_MAX;
  CHECK(onBookFinished(state, main, finished(9001U, 0)) == BookFinishResult::RibbonOnly);
  CHECK(booksFinishedRibbons(state) == UINT16_MAX);
}

// --- Anti-farm threshold ---------------------------------------------------

void underAnHourOnlyTheRibbonRises() {
  ExtrasState state{};
  CHECK(!bookEarnsBoss(state, finished(BOOK, HOUR - 1U)));
  CHECK(bookEarnsBoss(state, finished(BOOK, HOUR)));
  CHECK(onBookFinished(state, PokemonState{}, finished(BOOK, HOUR - 1U)) == BookFinishResult::RibbonOnly);
  CHECK(booksFinishedRibbons(state) == 1);
  CHECK(!hasHeldBoss(state));
  // It was counted: reading more and re-marking earns nothing.
  CHECK(onBookFinished(state, PokemonState{}, finished(BOOK, 5U * HOUR)) == BookFinishResult::AlreadyCounted);
  CHECK(!hasHeldBoss(state));
}

void aBookReadOnTheKindleStillEarnsItsBoss() {
  // Ten minutes on the X4 Pro, the rest credited from a sync (mark at 95%).
  ExtrasState state{};
  raiseKindleMark(state, BOOK, 950);
  CHECK(bookEarnsBoss(state, finished(BOOK, 600)));
  CHECK(onBookFinished(state, PokemonState{}, finished(BOOK, 600)) == BookFinishResult::BossHeld);

  // Not far enough through the book.
  ExtrasState early{};
  raiseKindleMark(early, BOOK, BOSS_MIN_SYNCED_PERMILLE - 1U);
  CHECK(!bookEarnsBoss(early, finished(BOOK, 600)));
  raiseKindleMark(early, BOOK, BOSS_MIN_SYNCED_PERMILLE);
  CHECK(bookEarnsBoss(early, finished(BOOK, 600)));

  // Another book's mark does not count, and neither does a mark with Kindle credit off.
  ExtrasState other{};
  raiseKindleMark(other, 0xB00C0002U, 1000);
  CHECK(!bookEarnsBoss(other, finished(BOOK, 600)));
  ExtrasState off{};
  raiseKindleMark(off, BOOK, 1000);
  setFeatureEnabled(off, Feature::KindleCredit, false);
  CHECK(!bookEarnsBoss(off, finished(BOOK, 600)));
  CHECK(bookEarnsBoss(off, finished(BOOK, HOUR)));  // device time still does
}

// --- Boss order ------------------------------------------------------------

void uncaughtBirdsComeFirst() {
  uint16_t species = 0;
  CHECK(chooseBossSpecies(PokemonState{}, fixed(chooseFirst), species) && species == 144);
  CHECK(chooseBossSpecies(PokemonState{}, fixed(chooseLast), species) && species == 146);
  // Only the uncaught ones are candidates.
  CHECK(chooseBossSpecies(caughtAll({144}), fixed(chooseFirst), species) && species == 145);
  CHECK(chooseBossSpecies(caughtAll({144, 146}), fixed(chooseLast), species) && species == 145);
  // Mewtwo caught does not skip the birds.
  CHECK(chooseBossSpecies(caughtAll({150, 145}), fixed(chooseLast), species) && species == 146);
}

void thenMewtwoThenTheRarePool() {
  uint16_t species = 0;
  CHECK(chooseBossSpecies(birdsCaught(), fixed(chooseFirst), species) && species == BOSS_MEWTWO);
  CHECK(chooseBossSpecies(birdsCaught(), fixed(chooseLast), species) && species == BOSS_MEWTWO);

  CHECK(chooseBossSpecies(legendariesCaught(), fixed(chooseFirst), species) && species == BOSS_RARE_POOL.front());
  CHECK(chooseBossSpecies(legendariesCaught(), fixed(chooseLast), species) && species == BOSS_RARE_POOL.back());

  CHECK(!chooseBossSpecies(PokemonState{}, RandomSource{}, species));
}

void rarePoolIsRealAndUncaughtWeighsThreeTimes() {
  std::set<uint16_t> unique;
  for (const uint16_t id : BOSS_RARE_POOL) {
    CHECK(speciesData(id) != nullptr);
    CHECK(id != MEW && id != BOSS_MEWTWO);
    CHECK(std::find(BOSS_LEGENDARY_BIRDS.begin(), BOSS_LEGENDARY_BIRDS.end(), id) == BOSS_LEGENDARY_BIRDS.end());
    unique.insert(id);
  }
  CHECK(unique.size() == BOSS_RARE_POOL.size());

  // Everything in the pool caught except Snorlax (143): 3 of 10 weight units.
  PokemonState main = legendariesCaught();
  for (const uint16_t id : BOSS_RARE_POOL) {
    if (id != 143) markSpecies(main.caughtSpecies, id);
  }
  ExtrasState rng{};
  seedRandom(rng, 20261114U);
  std::map<uint16_t, int> counts;
  constexpr int DRAWS = 10000;
  for (int draw = 0; draw < DRAWS; ++draw) {
    uint16_t species = 0;
    CHECK(chooseBossSpecies(main, randomSource(rng), species));
    CHECK(inRarePool(species));
    ++counts[species];
  }
  CHECK(counts.size() == BOSS_RARE_POOL.size());
  CHECK(counts[143] > DRAWS * 26 / 100 && counts[143] < DRAWS * 34 / 100);   // expect 30%
  CHECK(counts[149] > DRAWS * 8 / 100 && counts[149] < DRAWS * 12 / 100);    // expect 10%
}

void mewIsNeverABoss() {
  // Every Pokedex stage, including "the other 150 caught" (upstream's Mew rule).
  PokemonState allButMew{};
  for (uint16_t id = 1; id <= 150; ++id) markSpecies(allButMew.caughtSpecies, id);
  PokemonState everything = allButMew;
  markSpecies(everything.caughtSpecies, MEW);
  const PokemonState stages[] = {PokemonState{}, birdsCaught(), legendariesCaught(), allButMew, everything};

  ExtrasState rng{};
  seedRandom(rng, 151U);
  for (const PokemonState& main : stages) {
    for (int draw = 0; draw < 2000; ++draw) {
      uint16_t species = 0;
      CHECK(chooseBossSpecies(main, randomSource(rng), species));
      CHECK(species != MEW && species >= 1 && species <= 150);
    }
    for (const auto below : {chooseFirst, chooseLast}) {
      uint16_t species = 0;
      CHECK(chooseBossSpecies(main, fixed(below), species) && species != MEW);
    }
  }

  // Mew cannot be held or delivered either.
  ExtrasState state{};
  CHECK(!holdBoss(state, MEW));
  CHECK(!holdBoss(state, 0));
  CHECK(!holdBoss(state, 152));
  PokemonState main{};
  const PokemonState before = main;
  BossReward reward{};
  CHECK(!deliverBoss(main, MEW, 20, fixed(chooseFirst), reward));
  CHECK(!deliverBoss(main, 0, 20, fixed(chooseFirst), reward));
  CHECK(!deliverBoss(main, 152, 20, fixed(chooseFirst), reward));
  CHECK(main == before);
  // And a held Mew is not a valid extras state.
  state.heldBossSpeciesId = MEW;
  CHECK(!validateExtrasState(state));
}

void finishingUsesTheCurrentPokedex() {
  ExtrasState first{};
  CHECK(onBookFinished(first, PokemonState{}, finished(BOOK)) == BookFinishResult::BossHeld);
  CHECK(first.heldBossSpeciesId >= 144 && first.heldBossSpeciesId <= 146);

  ExtrasState second{};
  CHECK(onBookFinished(second, birdsCaught(), finished(BOOK)) == BookFinishResult::BossHeld);
  CHECK(second.heldBossSpeciesId == BOSS_MEWTWO);

  ExtrasState third{};
  CHECK(onBookFinished(third, legendariesCaught(), finished(BOOK)) == BookFinishResult::BossHeld);
  CHECK(inRarePool(third.heldBossSpeciesId));
  CHECK(validateExtrasState(third));
}

// --- Delivery --------------------------------------------------------------

void bossIsAnOrdinaryEncounterWithAMasterBall() {
  PokemonState main{};
  BossReward reward{};
  CHECK(deliverBoss(main, 145, 30, fixed(chooseFirst), reward));
  CHECK(pendingEventCount(main) == 1);
  const PendingEvent& event = main.pendingEvents[0];
  CHECK(event.kind == PendingEventKind::Encounter);
  CHECK(event.speciesId == 145 && event.level == 35);
  CHECK(event.recordId == 0 && event.item == EvolutionItem::None && !event.isShiny);
  CHECK(event.gender == Gender::Genderless);  // the birds have no gender
  CHECK(isSpeciesMarked(main.seenSpecies, 145));
  CHECK(!isSpeciesMarked(main.caughtSpecies, 145));
  CHECK(reward.itemId == BOSS_ITEM_MASTER_BALL && reward.count == 1);
  CHECK(main.bagCounts[MASTER_BALL_SLOT] == 1);
  CHECK(main.receivedItems[BOSS_ITEM_MASTER_BALL - 1U] == 1);
  CHECK(validateState(main));  // upstream's own check accepts it

  // A second boss adds a second Master Ball.
  CHECK(deliverBoss(main, 143, 30, fixed(chooseFirst), reward));
  CHECK(main.bagCounts[MASTER_BALL_SLOT] == 2);
  CHECK(validateState(main));
}

void levelIsLeadPlusFiveWithinBounds() {
  CHECK(bossLevel(11) == 16);
  CHECK(bossLevel(0) == 5);
  CHECK(bossLevel(95) == 100);
  CHECK(bossLevel(100) == 100);
  CHECK(bossLevel(255) == 100);
}

void fullMasterBallSlotGivesUltraBalls() {
  PokemonState main{};
  main.bagCounts[MASTER_BALL_SLOT] = UINT8_MAX;
  BossReward reward{};
  CHECK(deliverBoss(main, 150, 50, fixed(chooseFirst), reward));
  CHECK(reward.itemId == BOSS_ITEM_ULTRA_BALL && reward.count == BOSS_ULTRA_BALL_COUNT);
  CHECK(main.bagCounts[ULTRA_BALL_SLOT] == BOSS_ULTRA_BALL_COUNT);
  CHECK(main.bagCounts[MASTER_BALL_SLOT] == UINT8_MAX);

  // Both slots full: the boss still comes, with no item.
  PokemonState packed{};
  packed.bagCounts[MASTER_BALL_SLOT] = UINT8_MAX;
  packed.bagCounts[ULTRA_BALL_SLOT] = UINT8_MAX;
  CHECK(deliverBoss(packed, 150, 50, fixed(chooseFirst), reward));
  CHECK(reward.itemId == 0 && reward.count == 0);
  CHECK(pendingEventCount(packed) == 1);
}

void fullQueueHoldsTheBossUntilThereIsRoom() {
  ExtrasState state{};
  PokemonState main{};
  fillQueue(main, PENDING_EVENT_CAPACITY - PENDING_EVENT_PROMPT_RESERVE);
  CHECK(!hasEncounterRoom(main));

  // The finish is counted and the boss is held, not queued.
  CHECK(onBookFinished(state, main, finished(BOOK)) == BookFinishResult::BossHeld);
  const uint16_t held = state.heldBossSpeciesId;
  CHECK(held != 0);
  const PokemonState full = main;
  BossReward reward{};
  CHECK(!deliverBoss(main, held, 20, fixed(chooseFirst), reward));
  CHECK(main == full);  // nothing queued, no item
  CHECK(hasHeldBoss(state));

  // A second finished book while one is held: counted, no second boss.
  CHECK(onBookFinished(state, main, finished(0xB00C0002U)) == BookFinishResult::BossAlreadyWaiting);
  CHECK(state.heldBossSpeciesId == held);
  CHECK(booksFinishedRibbons(state) == 2);

  // The player clears one event: the next checkpoint delivers it.
  CHECK(dequeuePendingEvent(main));
  CHECK(hasEncounterRoom(main));
  const uint16_t species = takeHeldBoss(state);
  CHECK(species == held && !hasHeldBoss(state));
  CHECK(deliverBoss(main, species, 20, fixed(chooseFirst), reward));
  CHECK(pendingEventCount(main) == PENDING_EVENT_CAPACITY - PENDING_EVENT_PROMPT_RESERVE);
  CHECK(takeHeldBoss(state) == 0);  // nothing left to deliver twice
}

void failedMainCommitPutsTheBossBack() {
  ExtrasState state{};
  CHECK(onBookFinished(state, birdsCaught(), finished(BOOK)) == BookFinishResult::BossHeld);
  const ExtrasState held = state;
  const uint16_t species = takeHeldBoss(state);
  CHECK(holdBoss(state, species));
  CHECK(state == held);
  CHECK(!holdBoss(state, 149));  // one at a time
  CHECK(state == held);
}

void resetClearsBooksAndBoss() {
  ExtrasState state{};
  onBookFinished(state, PokemonState{}, finished(BOOK));
  resetExtrasProgress(state);
  CHECK(!hasHeldBoss(state) && booksFinishedRibbons(state) == 0 && !isBookCounted(state, BOOK));
  CHECK(validateExtrasState(state));
}

}  // namespace

int main() {
  eachBookCountsOnceEver();
  toggleOffOrNoIdDoesNothing();
  listKeepsTheLastBooksAndDropsTheOldest();
  underAnHourOnlyTheRibbonRises();
  aBookReadOnTheKindleStillEarnsItsBoss();
  uncaughtBirdsComeFirst();
  thenMewtwoThenTheRarePool();
  rarePoolIsRealAndUncaughtWeighsThreeTimes();
  mewIsNeverABoss();
  finishingUsesTheCurrentPokedex();
  bossIsAnOrdinaryEncounterWithAMasterBall();
  levelIsLeadPlusFiveWithinBounds();
  fullMasterBallSlotGivesUltraBalls();
  fullQueueHoldsTheBossUntilThereIsRoom();
  failedMainCommitPutsTheBossBack();
  resetClearsBooksAndBoss();
  if (failures != 0) {
    std::fprintf(stderr, "%d check(s) failed\n", failures);
    return 1;
  }
  std::puts("PokemonExtrasBossTest: all checks passed");
  return 0;
}
