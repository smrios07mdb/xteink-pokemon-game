// Extras M3 rules, feature 1: eggs (01-eggs.md). Pure functions only; the
// service hooks and their writes are covered by the compat test (R10) and the
// simulator gate (M3-HOOKS.md).

#include <array>
#include <cstdio>
#include <map>
#include <set>
#include <string_view>

#include "PokemonExtrasEggs.h"

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

uint32_t chooseFirst(void*, uint32_t) { return 0; }
uint32_t chooseLast(void*, const uint32_t upper) { return upper == 0 ? 0 : upper - 1; }
RandomSource fixed(uint32_t (*below)(void*, uint32_t)) { return RandomSource{nullptr, below}; }

PokemonState partyOf(const uint32_t a, const uint32_t b = 0) {
  PokemonState state{};
  state.partyRecordIds[0] = a;
  state.partyRecordIds[1] = b;
  return state;
}

ExtrasState withEgg(const uint16_t speciesId, const uint16_t pages, const uint8_t flags = 0) {
  ExtrasState state{};
  state.eggSpeciesId = speciesId;
  state.eggPagesRemaining = pages;
  state.eggFlags = flags;
  return state;
}

// Finds a seed whose first roll misses or hits, so tests do not depend on
// the exact xorshift stream.
uint32_t seedWhoseFirstRoll(const bool hits) {
  for (uint32_t seed = 1; seed < 10000; ++seed) {
    ExtrasState probe{};
    seedRandom(probe, seed);
    if ((randomBelow(probe, EGG_FIND_CHANCE_DENOMINATOR) == 0) == hits) return seed;
  }
  return 1;
}

// --- Pool and rarity -------------------------------------------------------

void poolIsBaseStageWildOnly() {
  std::set<uint16_t> pool;
  for (uint16_t id = 1; id <= KANTO_SPECIES_COUNT; ++id) {
    if (isEggPoolSpecies(id)) pool.insert(id);
  }
  CHECK(pool.count(1) == 1 && pool.count(4) == 1 && pool.count(7) == 1);  // starters
  CHECK(pool.count(25) == 1 && pool.count(133) == 1 && pool.count(147) == 1);
  CHECK(pool.count(131) == 1 && pool.count(143) == 1 && pool.count(113) == 1);  // single-stage
  CHECK(pool.count(138) == 1 && pool.count(140) == 1 && pool.count(142) == 1);  // fossils
  CHECK(pool.count(2) == 0 && pool.count(3) == 0 && pool.count(26) == 0);        // evolved
  CHECK(pool.count(144) == 0 && pool.count(145) == 0 && pool.count(146) == 0 && pool.count(150) == 0);  // legendary
  CHECK(pool.count(151) == 0);  // Mew
  CHECK(pool.count(132) == 0);  // Ditto
  CHECK(!isEggPoolSpecies(static_cast<uint16_t>(0)) && !isEggPoolSpecies(static_cast<uint16_t>(152)));
  for (const uint16_t id : pool) {
    const SpeciesData* species = speciesData(id);
    CHECK(species != nullptr && species->stage == EvolutionStage::Base && species->acquisition == Acquisition::Wild);
  }
}

void rarityTableAndPages() {
  for (const uint16_t id : {1, 4, 7, 133, 147, 138, 140, 142, 131, 143, 113}) {
    CHECK(eggRarity(id) == EggRarity::Rare);
    CHECK(eggTotalPages(id) == 1200);
  }
  CHECK(eggRarity(16) == EggRarity::Common && eggTotalPages(16) == 300);        // Pidgey, 255
  CHECK(eggRarity(129) == EggRarity::Common && eggTotalPages(129) == 300);      // Magikarp, 255
  CHECK(eggRarity(35) == EggRarity::Uncommon && eggTotalPages(35) == 600);      // Clefairy, 150
  CHECK(eggRarity(123) == EggRarity::Uncommon && eggTotalPages(123) == 600);    // Scyther, 45
  CHECK(eggRarity(25) == EggRarity::Common && eggTotalPages(25) == 300);        // Pikachu, 190
  CHECK(eggTotalPages(2) == 0 && eggTotalPages(150) == 0 && eggTotalPages(132) == 0 && eggTotalPages(0) == 0);
  for (uint16_t id = 1; id <= KANTO_SPECIES_COUNT; ++id) CHECK(eggTotalPages(id) <= EGG_MAX_PAGES);
}

// --- Finding ---------------------------------------------------------------

void boundariesAreWholeHoursOfLifetimeMinutes() {
  CHECK(eggFindBoundariesCrossed(0, 59) == 0);
  CHECK(eggFindBoundariesCrossed(0, 60) == 1);
  CHECK(eggFindBoundariesCrossed(55, 65) == 1);
  CHECK(eggFindBoundariesCrossed(60, 65) == 0);
  CHECK(eggFindBoundariesCrossed(59, 180) == 3);
  CHECK(eggFindBoundariesCrossed(100, 100) == 0);
  CHECK(eggFindBoundariesCrossed(100, 90) == 0);
}

void findSetsSpeciesPagesAndShinyFlag() {
  ExtrasState state{};
  seedRandom(state, seedWhoseFirstRoll(true));
  CHECK(rollEggFind(state, PokemonState{}, 1));
  CHECK(hasEgg(state) && isEggPoolSpecies(state.eggSpeciesId));
  CHECK(state.eggPagesRemaining == eggTotalPages(state.eggSpeciesId));
  CHECK((state.eggFlags & ~EGG_FLAG_SHINY) == 0);
  CHECK(state.eggMisses == 0 && state.pendingHatchRecordId == 0);
  CHECK(validateExtrasState(state));
  // One egg at a time.
  const ExtrasState held = state;
  CHECK(!rollEggFind(state, PokemonState{}, 5));
  CHECK(state == held);
}

void threeMissesGuaranteeTheNext() {
  ExtrasState state{};
  state.eggMisses = EGG_MAX_MISSES;
  seedRandom(state, seedWhoseFirstRoll(false));  // would miss without the pity
  CHECK(rollEggFind(state, PokemonState{}, 1));
  CHECK(hasEgg(state) && state.eggMisses == 0);

  // From zero, a miss counts up and never passes the guarantee.
  for (uint32_t seed = 1; seed < 200; ++seed) {
    ExtrasState run{};
    seedRandom(run, seed);
    uint32_t rolls = 0;
    while (!hasEgg(run) && rolls < 10) {
      rollEggFind(run, PokemonState{}, 1);
      ++rolls;
      CHECK(run.eggMisses <= EGG_MAX_MISSES);
    }
    CHECK(hasEgg(run) && rolls <= static_cast<uint32_t>(EGG_MAX_MISSES) + 1U);
  }
}

void missStaysWithoutEgg() {
  ExtrasState state{};
  seedRandom(state, seedWhoseFirstRoll(false));
  CHECK(!rollEggFind(state, PokemonState{}, 1));
  CHECK(!hasEgg(state) && state.eggMisses == 1);
  CHECK(validateExtrasState(state));
}

void severalBoundariesStopAtFirstFind() {
  ExtrasState state{};
  seedRandom(state, seedWhoseFirstRoll(false));
  CHECK(rollEggFind(state, PokemonState{}, 10));  // the 4th roll at the latest
  CHECK(hasEgg(state) && state.eggMisses == 0);
}

void toggleOffOrNoBoundaryDoesNothing() {
  ExtrasState state{};
  setFeatureEnabled(state, Feature::Eggs, false);
  state.eggMisses = 3;
  const ExtrasState before = state;
  CHECK(!rollEggFind(state, PokemonState{}, 3));
  CHECK(state == before);

  ExtrasState on{};
  const ExtrasState onBefore = on;
  CHECK(!rollEggFind(on, PokemonState{}, 0));
  CHECK(on == onBefore);
}

void uncaughtSpeciesWeighThreeTimes() {
  PokemonState main{};
  uint16_t first = 0;
  CHECK(chooseEggSpecies(main, fixed(chooseFirst), first));
  CHECK(first == 1);  // lowest pool id
  // Weight of Bulbasaur uncaught = 3: draws 0..2 pick it, 3 picks the next.
  struct Fixed {
    uint32_t value;
  } three{3};
  RandomSource source{&three, [](void* context, uint32_t) { return static_cast<Fixed*>(context)->value; }};
  uint16_t next = 0;
  CHECK(chooseEggSpecies(main, source, next) && next == 4);
  markSpecies(main.seenSpecies, 1);
  markSpecies(main.caughtSpecies, 1);
  three.value = 1;  // caught Bulbasaur weighs 1: draw 1 is already the next species
  CHECK(chooseEggSpecies(main, source, next) && next == 4);

  uint16_t last = 0;
  CHECK(chooseEggSpecies(main, fixed(chooseLast), last) && last == 147);  // Dratini is the highest pool id
  CHECK(!chooseEggSpecies(main, RandomSource{}, last));
}

void manyFindsStayInThePool() {
  std::map<uint16_t, int> seen;
  for (uint32_t seed = 1; seed <= 3000; ++seed) {
    ExtrasState state{};
    state.eggMisses = EGG_MAX_MISSES;
    seedRandom(state, seed);
    CHECK(rollEggFind(state, PokemonState{}, 1));
    CHECK(isEggPoolSpecies(state.eggSpeciesId));
    ++seen[state.eggSpeciesId];
  }
  CHECK(seen.size() > 50);
}

void shinyIsAboutOneInSixteen() {
  int shiny = 0;
  constexpr int RUNS = 16000;
  for (int seed = 1; seed <= RUNS; ++seed) {
    ExtrasState state{};
    state.eggMisses = EGG_MAX_MISSES;
    seedRandom(state, static_cast<uint32_t>(seed) * 2654435761U);
    rollEggFind(state, PokemonState{}, 1);
    if ((state.eggFlags & EGG_FLAG_SHINY) != 0) ++shiny;
  }
  CHECK(shiny > 800 && shiny < 1200);  // expected 1000
}

// --- Pages -----------------------------------------------------------------

void pagesCountDownToZeroOnly() {
  ExtrasState state = withEgg(16, 300);
  CHECK(applyEggPages(state, 1) == 1 && state.eggPagesRemaining == 299);
  CHECK(applyEggPages(state, 250) == 250 && state.eggPagesRemaining == 49);
  CHECK(!isEggReadyToHatch(state));
  CHECK(applyEggPages(state, 100) == 49 && state.eggPagesRemaining == 0);
  CHECK(isEggReadyToHatch(state));
  CHECK(applyEggPages(state, 5) == 0 && state.eggPagesRemaining == 0);
  CHECK(applyEggPages(state, 0) == 0);

  ExtrasState none{};
  CHECK(applyEggPages(none, 10) == 0 && none == ExtrasState{});

  ExtrasState off = withEgg(16, 300);
  setFeatureEnabled(off, Feature::Eggs, false);
  CHECK(applyEggPages(off, 10) == 0 && off.eggPagesRemaining == 300);

  ExtrasState big = withEgg(1, 1200);
  CHECK(applyEggPages(big, 100000) == 1200 && big.eggPagesRemaining == 0);
}

void progressForTheSleepScreen() {
  CHECK(!eggProgress(ExtrasState{}).held);
  const EggProgress rare = eggProgress(withEgg(133, 412));
  CHECK(rare.held && !rare.ready && rare.pagesRemaining == 412 && rare.totalPages == 1200);
  const EggProgress ready = eggProgress(withEgg(16, 0));
  CHECK(ready.held && ready.ready && ready.totalPages == 300);
  // A held egg with more pages than today's table (tunables changed) never overflows.
  const EggProgress stale = eggProgress(withEgg(16, 500));
  CHECK(stale.totalPages == 500);
}

// --- Hatching --------------------------------------------------------------

void hatchBuildsAnOrdinaryLevelFiveCatch() {
  ExtrasState state = withEgg(133, 0, EGG_FLAG_SHINY);
  PokemonState main = partyOf(1);
  RecordMutation early{};
  CHECK(!buildHatchedRecord(state, main, 7, Gender::Female, "", early));  // beginHatch first
  CHECK(beginHatch(state, 7));
  CHECK(state.pendingHatchRecordId == 7 && validateExtrasState(state));

  RecordMutation mutation{};
  CHECK(buildHatchedRecord(state, main, 7, Gender::Female, "Vee", mutation));
  CHECK(mutation.kind == RecordMutationKind::Append && mutation.requestedRecordId == 7);
  const PokemonRecord& record = mutation.record;
  CHECK(record.recordId == 7 && record.speciesId == 133);
  CHECK(record.origin == Origin::Caught && record.caughtLevel == 5);
  CHECK(record.totalXp == xpRequired(5) && levelForXp(record.totalXp) == 5);
  CHECK(isRecordShiny(record) && record.gender == Gender::Female);
  CHECK(std::string_view(record.nickname.data()) == "Vee");
  CHECK(validateRecord(record));
  CHECK(main.partyRecordIds[1] == 7);
  CHECK(isSpeciesMarked(main.seenSpecies, 133) && isSpeciesMarked(main.caughtSpecies, 133));
  CHECK(validateState(main));

  finishHatch(state);
  CHECK(!hasEgg(state) && state.pendingHatchRecordId == 0 && state.eggFlags == 0);
  CHECK(validateExtrasState(state));
}

void fullPartyHatchesIntoTheBox() {
  ExtrasState state = withEgg(16, 0);
  PokemonState main{};
  for (uint32_t slot = 0; slot < PARTY_SIZE; ++slot) main.partyRecordIds[slot] = slot + 1;
  CHECK(beginHatch(state, 40));
  RecordMutation mutation{};
  CHECK(buildHatchedRecord(state, main, 40, Gender::Male, nullptr, mutation));
  CHECK(!isRecordShiny(mutation.record));
  for (uint32_t slot = 0; slot < PARTY_SIZE; ++slot) CHECK(main.partyRecordIds[slot] == slot + 1);
}

void hatchIsRefusedWhenNotReady() {
  ExtrasState notReady = withEgg(16, 3);
  CHECK(!beginHatch(notReady, 5));
  ExtrasState none{};
  CHECK(!beginHatch(none, 5));
  ExtrasState zeroId = withEgg(16, 0);
  CHECK(!beginHatch(zeroId, 0));
  ExtrasState off = withEgg(16, 0);
  setFeatureEnabled(off, Feature::Eggs, false);
  CHECK(!beginHatch(off, 5));
  ExtrasState twice = withEgg(16, 0);
  CHECK(beginHatch(twice, 5) && !beginHatch(twice, 6) && twice.pendingHatchRecordId == 5);

  // Wrong id, a used mutation or a party id clash: nothing changes.
  PokemonState main = partyOf(1);
  const PokemonState mainBefore = main;
  RecordMutation mutation{};
  CHECK(!buildHatchedRecord(twice, main, 6, Gender::Male, "", mutation));
  RecordMutation used{};
  used.kind = RecordMutationKind::Replace;
  CHECK(!buildHatchedRecord(twice, main, 5, Gender::Male, "", used));
  PokemonState clash = partyOf(5);
  CHECK(!buildHatchedRecord(twice, clash, 5, Gender::Male, "", mutation));
  CHECK(main == mainBefore && mutation.kind == RecordMutationKind::None);
}

void pendingHatchNeverMakesTwoRecords() {
  // Crash after the main commit: the record is there, the egg goes.
  ExtrasState committed = withEgg(133, 0);
  beginHatch(committed, 9);
  PokemonRecord record{};
  record.recordId = 9;
  record.speciesId = 133;
  CHECK(resolvePendingHatch(committed, &record) == PendingHatch::Finished);
  CHECK(!hasEgg(committed) && committed.pendingHatchRecordId == 0);
  CHECK(!beginHatch(committed, 10));  // nothing left to hatch

  // Crash before the main commit: the record is missing, the hatch runs again.
  ExtrasState lost = withEgg(133, 0);
  beginHatch(lost, 9);
  CHECK(resolvePendingHatch(lost, nullptr) == PendingHatch::Retry);
  CHECK(hasEgg(lost) && isEggReadyToHatch(lost) && lost.pendingHatchRecordId == 0);
  CHECK(beginHatch(lost, 9));

  // Another species under that id is not the hatch.
  ExtrasState other = withEgg(133, 0);
  beginHatch(other, 9);
  record.speciesId = 16;
  CHECK(resolvePendingHatch(other, &record) == PendingHatch::Retry && hasEgg(other));

  ExtrasState idle = withEgg(133, 0);
  const ExtrasState idleBefore = idle;
  CHECK(resolvePendingHatch(idle, &record) == PendingHatch::None && idle == idleBefore);
}

void eggIvsHaveAFloor() {
  std::array<uint8_t, STAT_COUNT> iv{};
  rollEggIvs(fixed(chooseFirst), false, iv);
  for (const uint8_t value : iv) CHECK(value == EGG_IV_FLOOR);
  rollEggIvs(fixed(chooseLast), false, iv);
  for (const uint8_t value : iv) CHECK(value == 15);
  rollEggIvs(fixed(chooseFirst), true, iv);
  for (const uint8_t value : iv) CHECK(value == 12);
  rollEggIvs(RandomSource{}, false, iv);
  for (const uint8_t value : iv) CHECK(value == EGG_IV_FLOOR);
}

void resetClearsTheEgg() {
  ExtrasState state = withEgg(16, 10, EGG_FLAG_SHINY);
  state.eggMisses = 2;
  resetExtrasProgress(state);
  CHECK(!hasEgg(state) && state.eggMisses == 0 && state.eggFlags == 0);
}

}  // namespace

int main() {
  poolIsBaseStageWildOnly();
  rarityTableAndPages();
  boundariesAreWholeHoursOfLifetimeMinutes();
  findSetsSpeciesPagesAndShinyFlag();
  threeMissesGuaranteeTheNext();
  missStaysWithoutEgg();
  severalBoundariesStopAtFirstFind();
  toggleOffOrNoBoundaryDoesNothing();
  uncaughtSpeciesWeighThreeTimes();
  manyFindsStayInThePool();
  shinyIsAboutOneInSixteen();
  pagesCountDownToZeroOnly();
  progressForTheSleepScreen();
  hatchBuildsAnOrdinaryLevelFiveCatch();
  fullPartyHatchesIntoTheBox();
  hatchIsRefusedWhenNotReady();
  pendingHatchNeverMakesTwoRecords();
  eggIvsHaveAFloor();
  resetClearsTheEgg();
  if (failures != 0) {
    std::fprintf(stderr, "%d check(s) failed\n", failures);
    return 1;
  }
  std::puts("PokemonExtrasEggsTest: all checks passed");
  return 0;
}
