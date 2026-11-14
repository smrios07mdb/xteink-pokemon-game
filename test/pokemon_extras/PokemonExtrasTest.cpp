#include <cstdio>
#include <cstring>

#include "PokemonExtras.h"

namespace {

using namespace pokemon::extras;

int failures = 0;

#define CHECK(condition)                                                                \
  do {                                                                                  \
    if (!(condition)) {                                                                 \
      std::fprintf(stderr, "%s:%d check failed: %s\n", __FILE__, __LINE__, #condition); \
      ++failures;                                                                       \
    }                                                                                   \
  } while (false)

ExtrasState withQuests() {
  ExtrasState state{};
  state.questDayAnchor = 20406;
  state.questIds = {static_cast<uint8_t>(QuestKind::Read20), static_cast<uint8_t>(QuestKind::WinWild),
                    static_cast<uint8_t>(QuestKind::CatchAny)};
  return state;
}

void defaultStateIsValidWithEveryFeatureOnExceptSleepScreen() {
  const ExtrasState state{};
  CHECK(validateExtrasState(state));
  CHECK(isFeatureEnabled(state, Feature::Eggs));
  CHECK(isFeatureEnabled(state, Feature::Streaks));
  CHECK(isFeatureEnabled(state, Feature::BookBoss));
  CHECK(isFeatureEnabled(state, Feature::KindleCredit));
  CHECK(isFeatureEnabled(state, Feature::PostGame));
  CHECK(isFeatureEnabled(state, Feature::DayCare));
  CHECK(isFeatureEnabled(state, Feature::DailyQuests));
  CHECK(!isFeatureEnabled(state, Feature::PartySleepScreen));
  CHECK(state.featureMask == 0x7F);
}

void togglesFlipOneBitOnly() {
  ExtrasState state{};
  setFeatureEnabled(state, Feature::DayCare, false);
  CHECK(!isFeatureEnabled(state, Feature::DayCare));
  CHECK(state.featureMask == (0x7F & ~0x20));
  setFeatureEnabled(state, Feature::PartySleepScreen, true);
  CHECK(isFeatureEnabled(state, Feature::PartySleepScreen));
  setFeatureEnabled(state, Feature::DayCare, true);
  setFeatureEnabled(state, Feature::DayCare, true);  // idempotent
  CHECK(state.featureMask == 0xFF);
  for (uint8_t bit = 0; bit < FEATURE_COUNT; ++bit) setFeatureEnabled(state, static_cast<Feature>(bit), false);
  CHECK(state.featureMask == 0);
  CHECK(validateExtrasState(state));  // all-off is a valid state
}

void eggInvariants() {
  ExtrasState state{};
  state.eggSpeciesId = 133;  // Eevee
  state.eggPagesRemaining = 1200;
  state.eggFlags = EGG_FLAG_SHINY;
  state.eggMisses = 3;
  state.pendingHatchRecordId = 42;
  CHECK(validateExtrasState(state));

  ExtrasState bad = state;
  bad.eggPagesRemaining = 1201;
  CHECK(!validateExtrasState(bad));
  bad = state;
  bad.eggSpeciesId = 152;
  CHECK(!validateExtrasState(bad));
  bad = state;
  bad.eggFlags = 0x02;
  CHECK(!validateExtrasState(bad));
  bad = state;
  bad.eggMisses = 4;
  CHECK(!validateExtrasState(bad));

  // No egg held: nothing egg-shaped may linger.
  ExtrasState none{};
  none.eggMisses = 2;  // misses count while no egg is held
  CHECK(validateExtrasState(none));
  none.eggPagesRemaining = 5;
  CHECK(!validateExtrasState(none));
  none = ExtrasState{};
  none.pendingHatchRecordId = 9;
  CHECK(!validateExtrasState(none));
  none = ExtrasState{};
  none.eggFlags = EGG_FLAG_SHINY;
  CHECK(!validateExtrasState(none));
}

void streakRemainderMustStayBelowOneThousand() {
  ExtrasState state{};
  state.streakXpRemainderPermille = 999;
  CHECK(validateExtrasState(state));
  state.streakXpRemainderPermille = 1000;
  CHECK(!validateExtrasState(state));
}

void bookHashesArePackedAtTheFront() {
  ExtrasState state{};
  state.rewardedBookCount = 2;
  state.rewardedBookHashes[0] = 11;
  state.rewardedBookHashes[1] = 22;
  CHECK(validateExtrasState(state));

  ExtrasState gap = state;
  gap.rewardedBookHashes[1] = 0;  // count says 2, only 1 used
  CHECK(!validateExtrasState(gap));
  ExtrasState trailing = state;
  trailing.rewardedBookHashes[5] = 33;  // past count
  CHECK(!validateExtrasState(trailing));
  ExtrasState over = state;
  over.rewardedBookCount = REWARDED_BOOK_CAPACITY + 1;
  CHECK(!validateExtrasState(over));

  ExtrasState full{};
  full.rewardedBookCount = REWARDED_BOOK_CAPACITY;
  for (size_t index = 0; index < REWARDED_BOOK_CAPACITY; ++index) full.rewardedBookHashes[index] = 100 + index;
  CHECK(validateExtrasState(full));
}

void mewIsNeverAHeldBoss() {
  ExtrasState state{};
  state.heldBossSpeciesId = 150;  // Mewtwo is fine
  CHECK(validateExtrasState(state));
  state.heldBossSpeciesId = 151;
  CHECK(!validateExtrasState(state));
  state.heldBossSpeciesId = 152;
  CHECK(!validateExtrasState(state));
}

void kindleMarksArePackedAndInRange() {
  ExtrasState state{};
  state.kindleMarkCount = 1;
  state.kindleMarks[0] = {0xABCDEF01U, 1000};
  CHECK(validateExtrasState(state));

  ExtrasState bad = state;
  bad.kindleMarks[0].highWaterPermille = 1001;
  CHECK(!validateExtrasState(bad));
  bad = state;
  bad.kindleMarks[0].documentHash = 0;
  CHECK(!validateExtrasState(bad));
  bad = state;
  bad.kindleMarks[3].highWaterPermille = 7;  // unused slot must be all zero
  CHECK(!validateExtrasState(bad));
  bad = state;
  bad.kindleMarkCount = KINDLE_MARK_CAPACITY + 1;
  CHECK(!validateExtrasState(bad));
}

void postGameAndDayCareInvariants() {
  ExtrasState state{};
  state.battlePasses = 3;
  state.towerRunSeed = 77;
  state.towerRunWins = 12;
  state.dayCareRecordId = 5;
  state.dayCareMinuteRemainder = 1;
  CHECK(validateExtrasState(state));

  ExtrasState bad = state;
  bad.battlePasses = 4;
  CHECK(!validateExtrasState(bad));
  bad = state;
  bad.towerRunSeed = 0;  // wins without an active run
  CHECK(!validateExtrasState(bad));
  bad = state;
  bad.dayCareMinuteRemainder = 2;
  CHECK(!validateExtrasState(bad));
  bad = state;
  bad.dayCareRecordId = 0;  // remainder with an empty Day Care
  CHECK(!validateExtrasState(bad));
}

void questInvariants() {
  ExtrasState state = withQuests();
  CHECK(validateExtrasState(state));
  state.questProgress = {18, 0, 1};
  state.questDoneMask = 0x04;
  state.questRewardedMask = 0x04;
  CHECK(validateExtrasState(state));
  state.questDoneMask = 0x07;
  state.questRewardedMask = 0x0F;  // all three plus the bonus
  CHECK(validateExtrasState(state));

  ExtrasState bad = withQuests();
  bad.questIds[2] = bad.questIds[0];  // duplicate
  CHECK(!validateExtrasState(bad));
  bad = withQuests();
  bad.questIds[1] = 0;  // a set is always three
  CHECK(!validateExtrasState(bad));
  bad = withQuests();
  bad.questIds[0] = QUEST_KIND_MAX + 1;
  CHECK(!validateExtrasState(bad));
  bad = withQuests();
  bad.questRewardedMask = 0x01;  // rewarded but not done
  CHECK(!validateExtrasState(bad));
  bad = withQuests();
  bad.questDoneMask = 0x03;
  bad.questRewardedMask = 0x0B;  // bonus before all three are done
  CHECK(!validateExtrasState(bad));
  bad = withQuests();
  bad.questDoneMask = 0x08;
  CHECK(!validateExtrasState(bad));

  ExtrasState noSet{};
  noSet.questProgress[0] = 3;  // progress without a quest set
  CHECK(!validateExtrasState(noSet));
}

void resetClearsProgressButKeepsTogglesAndRng() {
  ExtrasState state = withQuests();
  setFeatureEnabled(state, Feature::Eggs, false);
  seedRandom(state, 1234);
  state.eggSpeciesId = 1;
  state.eggPagesRemaining = 300;
  state.ribbonCount = 9;
  state.battlePasses = 2;
  state.dayCareRecordId = 8;
  resetExtrasProgress(state);

  ExtrasState expected{};
  expected.featureMask = state.featureMask;
  expected.rngState = 1234;
  CHECK(state == expected);
  CHECK(!isFeatureEnabled(state, Feature::Eggs));
  CHECK(validateExtrasState(state));
}

void randomIsDeterministicPerSeedAndInRange() {
  ExtrasState a{};
  ExtrasState b{};
  seedRandom(a, 42);
  seedRandom(b, 42);
  for (int draw = 0; draw < 1000; ++draw) {
    const uint32_t value = randomBelow(a, 7);
    CHECK(value < 7);
    CHECK(value == randomBelow(b, 7));
  }
  CHECK(randomBelow(a, 1) == 0);
  CHECK(randomBelow(a, 0) == 0);

  ExtrasState zero{};
  seedRandom(zero, 0);  // remapped, never a stuck stream
  CHECK(zero.rngState != 0);

  ExtrasState unseeded{};
  (void)randomBelow(unseeded, 10);  // seeds itself
  CHECK(unseeded.rngState != 0);
}

void randomCoversEveryValueRoughlyEvenly() {
  ExtrasState state{};
  seedRandom(state, 0xC0FFEEU);
  int counts[3] = {0, 0, 0};
  constexpr int draws = 30000;
  for (int draw = 0; draw < draws; ++draw) ++counts[randomBelow(state, 3)];
  for (const int count : counts) CHECK(count > 9500 && count < 10500);  // 1 in 3, generous bounds
}

void randomSourceAdapterDrawsFromTheExtrasStream() {
  ExtrasState viaAdapter{};
  ExtrasState direct{};
  seedRandom(viaAdapter, 99);
  seedRandom(direct, 99);
  const pokemon::RandomSource source = randomSource(viaAdapter);
  CHECK(source.context == &viaAdapter);
  for (int draw = 0; draw < 50; ++draw) CHECK(source.below(source.context, 64) == randomBelow(direct, 64));
  CHECK(viaAdapter.rngState == direct.rngState);
}

void stableIdHashIsFnv1aAndNeverZero() {
  // Published FNV-1a 32 vectors.
  CHECK(stableIdHash("") == 0x811C9DC5U);
  CHECK(stableIdHash("a") == 0xE40C292CU);
  CHECK(stableIdHash("foobar") == 0xBF9CF968U);
  CHECK(stableIdHash(static_cast<const char*>(nullptr)) == 0x811C9DC5U);
  const char* path = "/Books/The Hobbit.epub";
  CHECK(stableIdHash(path) == stableIdHash(reinterpret_cast<const uint8_t*>(path), std::strlen(path)));
  CHECK(stableIdHash("/Books/A.epub") != stableIdHash("/Books/B.epub"));
}

}  // namespace

int main() {
  defaultStateIsValidWithEveryFeatureOnExceptSleepScreen();
  togglesFlipOneBitOnly();
  eggInvariants();
  streakRemainderMustStayBelowOneThousand();
  bookHashesArePackedAtTheFront();
  mewIsNeverAHeldBoss();
  kindleMarksArePackedAndInRange();
  postGameAndDayCareInvariants();
  questInvariants();
  resetClearsProgressButKeepsTogglesAndRng();
  randomIsDeterministicPerSeedAndInRange();
  randomCoversEveryValueRoughlyEvenly();
  randomSourceAdapterDrawsFromTheExtrasStream();
  stableIdHashIsFnv1aAndNeverZero();
  if (failures == 0) std::printf("PokemonExtrasTest: all checks passed\n");
  return failures == 0 ? 0 : 1;
}
