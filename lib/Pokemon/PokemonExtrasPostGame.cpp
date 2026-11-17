#if defined(CROSSINK_ENABLE_POKEMON) && defined(CROSSINK_POKEMON_EXTRAS)

#include "PokemonExtrasPostGame.h"

#include <algorithm>
#include <span>

namespace pokemon::extras {
namespace {

constexpr uint8_t MAX_LEVEL = 100;
constexpr uint8_t TEAM_SEED_ATTEMPTS = 8;

bool validGymIndex(const uint8_t gymIndex) { return gymIndex >= 1 && gymIndex <= GYM_COUNT; }

// murmur3 finaliser: spreads (seed, wins) into well-mixed 32-bit values.
uint32_t mix(uint32_t value) {
  value ^= value >> 16U;
  value *= 0x85EBCA6BU;
  value ^= value >> 13U;
  value *= 0xC2B2AE35U;
  value ^= value >> 16U;
  return value;
}

}  // namespace

// --- Unlock and passes -------------------------------------------------------

bool postGameUnlocked(const PokemonState& mainState) {
  return gymProgressFor(mainState.battleProgress, CHAMPION_GYM_INDEX) == GymProgress::Defeated;
}

bool postGameAvailable(const ExtrasState& state, const PokemonState& mainState) {
  return isFeatureEnabled(state, Feature::PostGame) && postGameUnlocked(mainState);
}

uint8_t accrueBattlePasses(ExtrasState& state, const PokemonState& mainState) {
  if (!postGameAvailable(state, mainState)) return 0;
  const uint32_t lifetime = mainState.lifetimeMinutes;
  // First call after unlock (anchor 0), or a lifetime that went backwards
  // (save restored from a backup): restart counting from now.
  if (state.passMinuteAnchor == 0 || state.passMinuteAnchor > lifetime) {
    state.passMinuteAnchor = lifetime;
    return 0;
  }
  const uint32_t earned = (lifetime - state.passMinuteAnchor) / MINUTES_PER_BATTLE_PASS;
  if (earned == 0) return 0;
  const uint8_t room = static_cast<uint8_t>(BATTLE_PASS_CAP - std::min(state.battlePasses, BATTLE_PASS_CAP));
  if (earned >= room) {
    // Reaching (or sitting at) the cap: minutes beyond it are not banked.
    state.battlePasses = BATTLE_PASS_CAP;
    state.passMinuteAnchor = lifetime;
    return room;
  }
  state.battlePasses = static_cast<uint8_t>(state.battlePasses + earned);
  state.passMinuteAnchor += earned * MINUTES_PER_BATTLE_PASS;  // keep the partial hour
  return static_cast<uint8_t>(earned);
}

bool spendBattlePass(ExtrasState& state) {
  if (state.battlePasses == 0) return false;
  --state.battlePasses;
  return true;
}

// --- Rematches ---------------------------------------------------------------

bool canRematch(const ExtrasState& state, const PokemonState& mainState, const uint8_t gymIndex) {
  if (!validGymIndex(gymIndex) || !postGameAvailable(state, mainState)) return false;
  if (gymProgressFor(mainState.battleProgress, gymIndex) != GymProgress::Defeated) return false;
  return state.battlePasses > 0;
}

uint8_t rematchLevel(const uint8_t originalLevel, const uint8_t partyTopLevel, const uint8_t gymIndex) {
  const int offset = static_cast<int>(GYM_COUNT) - static_cast<int>(std::clamp<uint8_t>(gymIndex, 1, GYM_COUNT));
  const int scaled = static_cast<int>(partyTopLevel) - offset;
  const int level = std::max(static_cast<int>(originalLevel), scaled);
  return static_cast<uint8_t>(std::clamp(level, 1, static_cast<int>(MAX_LEVEL)));
}

bool buildRematchTeam(const uint8_t gymIndex, const uint8_t partyTopLevel, const uint16_t starterSpeciesId,
                      RematchTeam& team) {
  team = RematchTeam{};
  if (!validGymIndex(gymIndex) || partyTopLevel == 0) return false;
  const std::span<const GymTeamMember> roster = gymTeamFor(gymIndex);
  if (roster.empty() || roster.size() > team.members.size()) return false;
  for (size_t index = 0; index < roster.size(); ++index) {
    GymTeamMember member = roster[index];
    const bool championFinalSlot = gymIndex == CHAMPION_GYM_INDEX && index + 1 == roster.size();
    if (championFinalSlot && starterSpeciesId != 0) member = championFinalSlotFor(starterSpeciesId);
    member.level = rematchLevel(member.level, partyTopLevel, gymIndex);
    team.members[index] = member;
  }
  team.count = roster.size();
  return true;
}

bool recordRematchWin(ExtrasState& state, const uint8_t gymIndex) {
  if (!validGymIndex(gymIndex)) return false;
  uint8_t& wins = state.rematchWins[gymIndex - 1U];
  if (wins < REMATCH_WINS_CAP) ++wins;
  return true;
}

uint8_t rematchWinsFor(const ExtrasState& state, const uint8_t gymIndex) {
  return validGymIndex(gymIndex) ? state.rematchWins[gymIndex - 1U] : 0;
}

// --- Battle Tower ------------------------------------------------------------

bool towerRunActive(const ExtrasState& state) { return state.towerRunSeed != 0; }

bool startTowerRun(ExtrasState& state, const PokemonState& mainState) {
  if (!postGameAvailable(state, mainState) || towerRunActive(state) || state.battlePasses == 0) return false;
  --state.battlePasses;
  state.towerRunSeed = randomBelow(state, UINT32_MAX) + 1U;  // 1..UINT32_MAX, never 0
  state.towerRunWins = 0;
  return true;
}

uint8_t towerLevel(const uint16_t wins) {
  const uint32_t level = TOWER_BASE_LEVEL + static_cast<uint32_t>(TOWER_LEVEL_PER_WIN) * wins;
  return static_cast<uint8_t>(std::min<uint32_t>(level, MAX_LEVEL));
}

bool towerOpponent(const ExtrasState& state, TowerOpponent& opponent) {
  opponent = TowerOpponent{};
  if (!towerRunActive(state)) return false;
  const uint32_t battleHash = mix(state.towerRunSeed ^ mix(static_cast<uint32_t>(state.towerRunWins) + 1U));
  opponent.classId = static_cast<uint8_t>(1U + battleHash % TRAINER_CLASS_COUNT);
  opponent.level = towerLevel(state.towerRunWins);

  TrainerTeam fallback{};
  uint32_t fallbackSeed = 0;
  for (uint8_t attempt = 0; attempt < TEAM_SEED_ATTEMPTS; ++attempt) {
    const uint32_t seed = mix(battleHash + attempt + 1U);
    TrainerTeam team{};
    if (!buildTrainerTeam(opponent.classId, opponent.level, seed, team)) continue;
    if (team.count == TOWER_TEAM_SIZE) {
      opponent.seed = seed;
      opponent.team = team;
      return true;
    }
    if (fallback.count == 0) {
      fallback = team;
      fallbackSeed = seed;
    }
  }
  if (fallback.count == 0) {
    opponent = TowerOpponent{};
    return false;
  }
  opponent.seed = fallbackSeed;
  opponent.team = fallback;
  return true;
}

bool recordTowerWin(ExtrasState& state, TowerWin& win) {
  win = TowerWin{};
  if (!towerRunActive(state)) return false;
  const uint8_t beatenLevel = towerLevel(state.towerRunWins);
  if (state.towerRunWins < UINT16_MAX) ++state.towerRunWins;
  state.towerBestStreak = std::max(state.towerBestStreak, state.towerRunWins);
  win.wins = state.towerRunWins;
  win.rewardDue = state.towerRunWins % TOWER_REWARD_INTERVAL == 0;
  win.rewardLevel = beatenLevel;
  return true;
}

void endTowerRun(ExtrasState& state) {
  if (!towerRunActive(state)) return;
  state.towerBestStreak = std::max(state.towerBestStreak, state.towerRunWins);
  state.towerRunSeed = 0;
  state.towerRunWins = 0;
}

uint16_t towerBestStreak(const ExtrasState& state) { return state.towerBestStreak; }

// --- Day Care marker -----------------------------------------------------------

bool isInDayCare(const ExtrasState& state, const uint32_t recordId) {
  return recordId != 0 && state.dayCareRecordId == recordId;
}

}  // namespace pokemon::extras

#endif
