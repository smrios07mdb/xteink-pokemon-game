#pragma once

// Extras M5 rules, feature 6: post-game gym rematches and Battle Tower
// (docs/extras/features/06-post-game.md). Pure, storage-free, host-testable.
// The hooks call these and do the writes (docs/extras/M5-HOOKS.md):
//   - battleProgress is read, never written; markGymDefeated() is never called
//     for a rematch, so badges and the Hall of Fame stay as they are (R3);
//   - battles, XP and item rewards go through the existing paths only
//     (the gym battle flow, battleVictoryXp, grantTrainerReward) (R3);
//   - every field lives in the extras file; codec version 1 already holds
//     them, so no codec change (R1).
//
// Links PokemonGymData (gym teams, unlock rule) and PokemonTrainers (the
// road-trainer team generator). Compiles to nothing without
// CROSSINK_POKEMON_EXTRAS (R11).

#include <array>
#include <cstddef>
#include <cstdint>

#include "PokemonBattleTypes.h"
#include "PokemonExtras.h"
#include "PokemonTrainers.h"
#include "PokemonTypes.h"

namespace pokemon::extras {

// ---------------------------------------------------------------------------
// Tunables
// ---------------------------------------------------------------------------
constexpr uint32_t MINUTES_PER_BATTLE_PASS = 60;  // credited reading minutes per pass, after unlock
// BATTLE_PASS_CAP (PokemonExtras.h): passes held at most; minutes read while
// at the cap earn nothing (no banking).
constexpr uint8_t TOWER_BASE_LEVEL = 50;
constexpr uint8_t TOWER_LEVEL_PER_WIN = 2;
constexpr uint16_t TOWER_REWARD_INTERVAL = 5;  // every 5th win of a run grants an item
constexpr size_t TOWER_TEAM_SIZE = 3;
constexpr uint8_t REMATCH_WINS_CAP = 255;

static_assert(GYM_REMATCH_COUNT == GYM_COUNT, "rematchWins holds one entry per gym index 1..GYM_COUNT");

// ---------------------------------------------------------------------------
// Unlock and Battle Passes
// ---------------------------------------------------------------------------
// The Champion is beaten (bit 12 of battleProgress).
bool postGameUnlocked(const PokemonState& mainState);
// Unlocked and the Post-game toggle is on: show Rematch / Battle Tower.
bool postGameAvailable(const ExtrasState& state, const PokemonState& mainState);

// Call after each credit commit (PokemonService::creditMinutes()) with the
// committed main state. Earns 1 pass per MINUTES_PER_BATTLE_PASS of
// lifetimeMinutes since passMinuteAnchor, up to BATTLE_PASS_CAP. The first
// call after unlock only sets the anchor, so minutes read before the
// Champion fell never count. Clock-independent: only lifetimeMinutes is read.
// Toggle off or not unlocked: nothing changes (minutes read while the toggle
// is off are credited when it is turned back on, up to the cap). Returns the
// passes earned; save extras when the state changed (anchor or passes).
uint8_t accrueBattlePasses(ExtrasState& state, const PokemonState& mainState);

// Spends one pass. False (state untouched) when none is held. Spend before
// the battle starts and save extras first: a crash mid-battle loses the pass,
// it never gives a free battle (R6).
bool spendBattlePass(ExtrasState& state);

// ---------------------------------------------------------------------------
// Gym rematches (gymIndex 1..GYM_COUNT, same numbering as gymData())
// ---------------------------------------------------------------------------
// That gym is beaten, post-game is available and a pass is held.
bool canRematch(const ExtrasState& state, const PokemonState& mainState, uint8_t gymIndex);

// max(original, partyTopLevel - (13 - gymIndex)), capped at 100. Gym 1 sits 12
// levels under the party's best, the Champion level with it.
uint8_t rematchLevel(uint8_t originalLevel, uint8_t partyTopLevel, uint8_t gymIndex);

struct RematchTeam {
  std::array<GymTeamMember, MAX_GYM_TEAM_SIZE> members{};
  size_t count = 0;
};

// The leader's real team (gymTeamFor), each member at rematchLevel(), fixed
// movesets unchanged. The Champion's final slot is swapped for
// championFinalSlotFor(starterSpeciesId) as in the first fight (when the
// starter is known). False for an invalid gym index or party level 0.
bool buildRematchTeam(uint8_t gymIndex, uint8_t partyTopLevel, uint16_t starterSpeciesId, RematchTeam& team);

// After a rematch win: rematchWins[gymIndex - 1] + 1 (saturating). Extras-only
// write. XP for the win goes through the battle flow's battleVictoryXp path.
// False for an invalid index.
bool recordRematchWin(ExtrasState& state, uint8_t gymIndex);
uint8_t rematchWinsFor(const ExtrasState& state, uint8_t gymIndex);

// ---------------------------------------------------------------------------
// Battle Tower
// ---------------------------------------------------------------------------
bool towerRunActive(const ExtrasState& state);

// Spends a pass and starts a run with a fresh non-zero seed from the extras
// RNG. False (state untouched) when post-game is not available, a run is
// already active or no pass is held. Save extras before the first battle.
bool startTowerRun(ExtrasState& state, const PokemonState& mainState);

// min(100, 50 + 2 x wins).
uint8_t towerLevel(uint16_t wins);

struct TowerOpponent {
  uint8_t classId = 0;  // TrainerClass
  uint8_t level = 0;
  uint32_t seed = 0;    // for buildTrainerTeam
  TrainerTeam team{};
};

// The next opponent of the active run, a pure function of (towerRunSeed,
// towerRunWins): reloading mid-run meets the same trainer. A team of 3 from
// the road-trainer generator (if no seed variant gives 3 within a few tries,
// the generator's 2-member team is used). False when no run is active.
bool towerOpponent(const ExtrasState& state, TowerOpponent& opponent);

struct TowerWin {
  uint16_t wins = 0;         // wins in this run after the win
  bool rewardDue = false;    // every TOWER_REWARD_INTERVAL-th win
  uint8_t rewardLevel = 0;   // trainer level to pass to grantTrainerReward()
};

// After a tower win: wins + 1, best streak raised. Save extras FIRST, then
// (rewardDue) grantTrainerReward(main copy, rewardLevel, ...) and heal the
// party in the main commit: a crash in between loses that item, never doubles
// it (R6). False when no run is active.
bool recordTowerWin(ExtrasState& state, TowerWin& win);

// On a loss or when the player leaves: the run ends, best streak kept.
// No-op when no run is active. Extras-only write.
void endTowerRun(ExtrasState& state);

uint16_t towerBestStreak(const ExtrasState& state);

// ---------------------------------------------------------------------------
// Day Care marker (play-test backlog 2026-11-16): display only
// ---------------------------------------------------------------------------
// True when this record is the one in Day Care: the PC Box list tags it "DC"
// and its detail screen says "In Day Care". Reads state only.
bool isInDayCare(const ExtrasState& state, uint32_t recordId);

}  // namespace pokemon::extras
