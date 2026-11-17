// Extras M5 rules, feature 6: post-game rematches and Battle Tower
// (06-post-game.md), plus the Day Care marker. Pure functions only; the
// PokemonActivity hooks are covered by the compat test (R10) and the
// simulator gate (M5-HOOKS.md).

#include <algorithm>
#include <cstdio>
#include <set>

#include "PokemonExtrasPostGame.h"

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

constexpr uint16_t ALL_BEATEN = POKEMON_GYM_PROGRESS_MASK;            // bits 0-12
constexpr uint16_t BEFORE_CHAMPION = ALL_BEATEN & ~(1U << 12U);       // Elite Four done, Champion not

PokemonState mainWith(const uint16_t battleProgress, const uint32_t lifetimeMinutes) {
  PokemonState state{};
  state.battleProgress = battleProgress;
  state.lifetimeMinutes = lifetimeMinutes;
  return state;
}

void testUnlock() {
  ExtrasState extras{};
  CHECK(!postGameUnlocked(mainWith(BEFORE_CHAMPION, 5000)));
  CHECK(postGameUnlocked(mainWith(ALL_BEATEN, 5000)));
  CHECK(postGameAvailable(extras, mainWith(ALL_BEATEN, 5000)));
  setFeatureEnabled(extras, Feature::PostGame, false);
  CHECK(!postGameAvailable(extras, mainWith(ALL_BEATEN, 5000)));
}

void testPassAccrual() {
  // Not unlocked: nothing, anchor untouched.
  ExtrasState extras{};
  CHECK(accrueBattlePasses(extras, mainWith(BEFORE_CHAMPION, 9000)) == 0);
  CHECK(extras.passMinuteAnchor == 0 && extras.battlePasses == 0);

  // First call after unlock only sets the anchor: earlier minutes never count.
  CHECK(accrueBattlePasses(extras, mainWith(ALL_BEATEN, 9000)) == 0);
  CHECK(extras.passMinuteAnchor == 9000 && extras.battlePasses == 0);

  // 59 minutes: nothing. 60: one pass. Partial hour kept.
  CHECK(accrueBattlePasses(extras, mainWith(ALL_BEATEN, 9059)) == 0);
  CHECK(extras.battlePasses == 0 && extras.passMinuteAnchor == 9000);
  CHECK(accrueBattlePasses(extras, mainWith(ALL_BEATEN, 9075)) == 1);
  CHECK(extras.battlePasses == 1 && extras.passMinuteAnchor == 9060);
  CHECK(accrueBattlePasses(extras, mainWith(ALL_BEATEN, 9119)) == 0);  // 59 since anchor
  CHECK(accrueBattlePasses(extras, mainWith(ALL_BEATEN, 9120)) == 1);
  CHECK(extras.battlePasses == 2 && extras.passMinuteAnchor == 9120);

  // A long session: capped at 3, minutes beyond the cap not banked.
  CHECK(accrueBattlePasses(extras, mainWith(ALL_BEATEN, 9120 + 600)) == 1);
  CHECK(extras.battlePasses == BATTLE_PASS_CAP && extras.passMinuteAnchor == 9720);
  CHECK(accrueBattlePasses(extras, mainWith(ALL_BEATEN, 9900)) == 0);
  CHECK(extras.battlePasses == BATTLE_PASS_CAP && extras.passMinuteAnchor == 9900);
  CHECK(validateExtrasState(extras));

  // Spend one, then an hour from the cap anchor earns it back.
  CHECK(spendBattlePass(extras));
  CHECK(extras.battlePasses == 2);
  CHECK(accrueBattlePasses(extras, mainWith(ALL_BEATEN, 9959)) == 0);
  CHECK(accrueBattlePasses(extras, mainWith(ALL_BEATEN, 9960)) == 1);
  CHECK(extras.battlePasses == 3);

  // Spending with none held fails and changes nothing.
  ExtrasState empty{};
  CHECK(!spendBattlePass(empty));
  CHECK(empty == ExtrasState{});

  // Toggle off: nothing changes; back on, the gap is credited up to the cap.
  ExtrasState toggled{};
  accrueBattlePasses(toggled, mainWith(ALL_BEATEN, 100));
  setFeatureEnabled(toggled, Feature::PostGame, false);
  const ExtrasState before = toggled;
  CHECK(accrueBattlePasses(toggled, mainWith(ALL_BEATEN, 400)) == 0);
  CHECK(toggled == before);
  setFeatureEnabled(toggled, Feature::PostGame, true);
  CHECK(accrueBattlePasses(toggled, mainWith(ALL_BEATEN, 400)) == 3);

  // Lifetime below the anchor (restored backup): re-anchor, no passes.
  ExtrasState restored{};
  restored.passMinuteAnchor = 5000;
  CHECK(accrueBattlePasses(restored, mainWith(ALL_BEATEN, 4000)) == 0);
  CHECK(restored.passMinuteAnchor == 4000 && restored.battlePasses == 0);
}

void testRematchLevels() {
  // Every gym: max(original, top - (13 - gym)), capped at 100.
  for (uint8_t gym = 1; gym <= GYM_COUNT; ++gym) {
    const int offset = 13 - gym;
    CHECK(rematchLevel(20, 80, gym) == std::max(20, 80 - offset));
    CHECK(rematchLevel(70, 60, gym) == 70);  // never below the original
    CHECK(rematchLevel(50, 100, gym) == 100 - offset);
    CHECK(rematchLevel(100, 100, gym) == 100);
  }
  CHECK(rematchLevel(14, 50, 1) == 38);   // Brock: 12 under the party's best
  CHECK(rematchLevel(65, 72, 13) == 72);  // Champion: level with it
  CHECK(rematchLevel(5, 3, 1) == 5);      // tiny party: original kept
}

void testRematchTeams() {
  for (uint8_t gym = 1; gym <= GYM_COUNT; ++gym) {
    RematchTeam team{};
    CHECK(buildRematchTeam(gym, 90, 0, team));
    const auto roster = gymTeamFor(gym);
    CHECK(team.count == roster.size());
    for (size_t i = 0; i < team.count; ++i) {
      CHECK(team.members[i].speciesId == roster[i].speciesId);
      CHECK(team.members[i].moves == roster[i].moves);
      CHECK(team.members[i].level == rematchLevel(roster[i].level, 90, gym));
      CHECK(team.members[i].level >= roster[i].level);
    }
  }
  // Champion's final slot follows the starter, like the first fight.
  RematchTeam champion{};
  CHECK(buildRematchTeam(CHAMPION_GYM_INDEX, 90, 4, champion));  // Charmander -> Blastoise
  CHECK(champion.count == 6);
  CHECK(champion.members[5].speciesId == 9);
  CHECK(champion.members[5].level == 90);
  CHECK(buildRematchTeam(CHAMPION_GYM_INDEX, 90, 1, champion));  // Bulbasaur -> Charizard
  CHECK(champion.members[5].speciesId == 6);

  RematchTeam bad{};
  CHECK(!buildRematchTeam(0, 50, 0, bad));
  CHECK(!buildRematchTeam(14, 50, 0, bad));
  CHECK(!buildRematchTeam(1, 0, 0, bad));
  CHECK(bad.count == 0);
}

void testCanRematchAndWins() {
  ExtrasState extras{};
  extras.battlePasses = 1;
  const PokemonState beaten = mainWith(ALL_BEATEN, 1000);
  for (uint8_t gym = 1; gym <= GYM_COUNT; ++gym) CHECK(canRematch(extras, beaten, gym));
  CHECK(!canRematch(extras, beaten, 0));
  CHECK(!canRematch(extras, beaten, 14));
  CHECK(!canRematch(extras, mainWith(BEFORE_CHAMPION, 1000), 1));  // not unlocked
  extras.battlePasses = 0;
  CHECK(!canRematch(extras, beaten, 1));

  CHECK(recordRematchWin(extras, 3));
  CHECK(recordRematchWin(extras, 3));
  CHECK(rematchWinsFor(extras, 3) == 2 && rematchWinsFor(extras, 4) == 0);
  extras.rematchWins[12] = 254;
  CHECK(recordRematchWin(extras, 13) && recordRematchWin(extras, 13));
  CHECK(rematchWinsFor(extras, 13) == 255);  // saturates
  CHECK(!recordRematchWin(extras, 0) && !recordRematchWin(extras, 14));
  CHECK(validateExtrasState(extras));
}

void testTowerLevels() {
  CHECK(towerLevel(0) == 50);
  CHECK(towerLevel(1) == 52);
  CHECK(towerLevel(10) == 70);
  CHECK(towerLevel(24) == 98);
  CHECK(towerLevel(25) == 100);
  CHECK(towerLevel(26) == 100);
  CHECK(towerLevel(UINT16_MAX) == 100);
}

void testTowerRun() {
  const PokemonState beaten = mainWith(ALL_BEATEN, 1000);
  ExtrasState extras{};
  seedRandom(extras, 1234);

  // No pass, not unlocked, toggle off: no run, nothing spent.
  CHECK(!startTowerRun(extras, beaten));
  extras.battlePasses = 2;
  CHECK(!startTowerRun(extras, mainWith(BEFORE_CHAMPION, 1000)));
  setFeatureEnabled(extras, Feature::PostGame, false);
  CHECK(!startTowerRun(extras, beaten));
  setFeatureEnabled(extras, Feature::PostGame, true);
  CHECK(extras.battlePasses == 2);

  CHECK(startTowerRun(extras, beaten));
  CHECK(towerRunActive(extras) && extras.towerRunSeed != 0 && extras.towerRunWins == 0);
  CHECK(extras.battlePasses == 1);
  CHECK(!startTowerRun(extras, beaten));  // one run at a time
  CHECK(extras.battlePasses == 1);
  CHECK(validateExtrasState(extras));

  // Wins: reward on every 5th; reward level is the beaten trainer's level.
  for (uint16_t expected = 1; expected <= 12; ++expected) {
    TowerOpponent opponent{};
    CHECK(towerOpponent(extras, opponent));
    CHECK(opponent.level == towerLevel(expected - 1));
    TowerWin win{};
    CHECK(recordTowerWin(extras, win));
    CHECK(win.wins == expected);
    CHECK(win.rewardDue == (expected % 5 == 0));
    CHECK(win.rewardLevel == opponent.level);
    CHECK(extras.towerBestStreak == expected);
  }
  endTowerRun(extras);
  CHECK(!towerRunActive(extras) && extras.towerRunWins == 0 && extras.towerBestStreak == 12);
  CHECK(validateExtrasState(extras));

  // A shorter second run keeps the best streak.
  CHECK(startTowerRun(extras, beaten));
  TowerWin win{};
  CHECK(recordTowerWin(extras, win) && win.wins == 1);
  endTowerRun(extras);
  CHECK(towerBestStreak(extras) == 12);
  CHECK(extras.battlePasses == 0);

  // No active run: win / opponent refused, end is a no-op.
  TowerOpponent none{};
  CHECK(!towerOpponent(extras, none));
  CHECK(!recordTowerWin(extras, win));
  const ExtrasState idle = extras;
  endTowerRun(extras);
  CHECK(extras == idle);
}

void testTowerSeedReproducible() {
  ExtrasState run{};
  run.towerRunSeed = 0xC0FFEE11U;
  std::set<uint32_t> seeds;
  size_t teamsOfThree = 0;
  for (uint16_t wins = 0; wins < 60; ++wins) {
    run.towerRunWins = wins;
    TowerOpponent first{};
    TowerOpponent again{};
    CHECK(towerOpponent(run, first));
    CHECK(towerOpponent(run, again));  // same (seed, wins) -> same trainer
    CHECK(first.classId == again.classId && first.seed == again.seed && first.level == again.level);
    CHECK(first.team.count == again.team.count);
    for (size_t i = 0; i < first.team.count; ++i) {
      CHECK(first.team.members[i].speciesId == again.team.members[i].speciesId);
      CHECK(first.team.members[i].level == again.team.members[i].level);
      CHECK(first.team.members[i].moves == again.team.members[i].moves);
    }
    CHECK(first.classId >= 1 && first.classId <= TRAINER_CLASS_COUNT);
    CHECK(first.team.count >= 2 && first.team.count <= TOWER_TEAM_SIZE);
    CHECK(first.team.members[first.team.count - 1].level == first.level);  // strongest last, at the tower level
    if (first.team.count == TOWER_TEAM_SIZE) ++teamsOfThree;
    seeds.insert(first.seed);
  }
  CHECK(teamsOfThree == 60);  // 8 seed tries make a 2-member team practically impossible
  CHECK(seeds.size() == 60);  // every battle differs

  // A different run seed gives a different sequence.
  ExtrasState other = run;
  other.towerRunSeed = 0x0BADF00DU;
  other.towerRunWins = 0;
  run.towerRunWins = 0;
  TowerOpponent a{}, b{};
  CHECK(towerOpponent(run, a) && towerOpponent(other, b));
  CHECK(a.seed != b.seed);
}

void testTowerSeedFromRng() {
  // The run seed comes from the extras RNG: same RNG state, same run.
  const PokemonState beaten = mainWith(ALL_BEATEN, 1000);
  ExtrasState one{};
  ExtrasState two{};
  seedRandom(one, 77);
  seedRandom(two, 77);
  one.battlePasses = two.battlePasses = 1;
  CHECK(startTowerRun(one, beaten) && startTowerRun(two, beaten));
  CHECK(one.towerRunSeed == two.towerRunSeed && one.towerRunSeed != 0);
}

void testResetClearsPostGame() {
  ExtrasState extras{};
  extras.battlePasses = 3;
  extras.passMinuteAnchor = 500;
  extras.rematchWins[0] = 4;
  extras.towerBestStreak = 9;
  extras.towerRunSeed = 1;
  extras.towerRunWins = 2;
  resetExtrasProgress(extras);
  CHECK(extras.battlePasses == 0 && extras.passMinuteAnchor == 0 && extras.rematchWins[0] == 0);
  CHECK(extras.towerBestStreak == 0 && extras.towerRunSeed == 0 && extras.towerRunWins == 0);
}

void testDayCareMarker() {
  ExtrasState extras{};
  CHECK(!isInDayCare(extras, 0));
  CHECK(!isInDayCare(extras, 42));
  extras.dayCareRecordId = 42;
  CHECK(isInDayCare(extras, 42));
  CHECK(!isInDayCare(extras, 43));
  CHECK(!isInDayCare(extras, 0));
  setFeatureEnabled(extras, Feature::DayCare, false);
  CHECK(isInDayCare(extras, 42));  // the marker shows where it is, toggle or not
}

}  // namespace

int main() {
  testUnlock();
  testPassAccrual();
  testRematchLevels();
  testRematchTeams();
  testCanRematchAndWins();
  testTowerLevels();
  testTowerRun();
  testTowerSeedReproducible();
  testTowerSeedFromRng();
  testResetClearsPostGame();
  testDayCareMarker();
  if (failures != 0) {
    std::fprintf(stderr, "PokemonExtrasPostGameTest: %d failure(s)\n", failures);
    return 1;
  }
  std::printf("PokemonExtrasPostGameTest: all checks passed\n");
  return 0;
}
