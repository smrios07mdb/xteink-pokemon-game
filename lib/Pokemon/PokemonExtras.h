#pragma once

// Extras (docs/extras/PLAN.md): everything this fork adds on top of upstream.
// This header holds the extras state and the pure, storage-free rules. It is
// host-testable like PokemonGame. All extras state lives in its own side file
// (PokemonExtrasCodec.h); nothing here touches the main save, the reading
// stats, or any persisted upstream enum (docs/extras/SAVE-SAFETY.md R1-R4).
//
// The .cpp compiles to nothing unless CROSSINK_POKEMON_EXTRAS is defined
// (SAVE-SAFETY R11).

#include <array>
#include <cstddef>
#include <cstdint>

#include "PokemonGame.h"

namespace pokemon::extras {

// ---------------------------------------------------------------------------
// Runtime toggles (Pokemon > Settings > Extras). One bit per feature, stored
// in the extras file, never in settings.json. The bit positions are part of
// the extras file format: append new features at the end, never renumber.
// ---------------------------------------------------------------------------
enum class Feature : uint8_t {
  Eggs = 0,
  Streaks = 1,
  BookBoss = 2,
  KindleCredit = 3,
  PostGame = 4,
  DayCare = 5,
  DailyQuests = 6,
  PartySleepScreen = 7,
};
constexpr uint8_t FEATURE_COUNT = 8;
constexpr uint8_t featureBit(const Feature feature) { return static_cast<uint8_t>(1U << static_cast<uint8_t>(feature)); }
// All on, except the Party sleep screen (it replaces the configured sleep screen).
constexpr uint8_t DEFAULT_FEATURE_MASK = static_cast<uint8_t>(0xFFU & ~featureBit(Feature::PartySleepScreen));

// ---------------------------------------------------------------------------
// Capacities and ranges. Changing any of these changes the file layout, so
// they are frozen for codec version 1.
// ---------------------------------------------------------------------------
constexpr size_t REWARDED_BOOK_CAPACITY = 128;  // feature 3: FIFO, oldest dropped when full
constexpr size_t KINDLE_MARK_CAPACITY = 64;      // feature 5: LRU, least recently used dropped when full
constexpr size_t GYM_REMATCH_COUNT = 13;         // feature 6: 8 gyms + Elite Four + Champion
constexpr size_t DAILY_QUEST_SLOTS = 3;          // feature 8
constexpr uint16_t EGG_MAX_PAGES = 1200;         // feature 1: rarest egg
constexpr uint8_t EGG_MAX_MISSES = 3;            // feature 1: guaranteed find after 3 misses
constexpr uint8_t BATTLE_PASS_CAP = 3;           // feature 6
constexpr uint16_t PERMILLE = 1000;

// Egg flag bits (eggFlags).
constexpr uint8_t EGG_FLAG_SHINY = 0x01U;
constexpr uint8_t EGG_FLAG_MASK = EGG_FLAG_SHINY;

// Daily quest kinds (feature 8). Persisted in questIds: append only.
enum class QuestKind : uint8_t {
  None = 0,
  Read20 = 1,
  Read40 = 2,
  Read60 = 3,
  WinWild = 4,
  WinTrainer = 5,
  CatchAny = 6,
  CatchNew = 7,
  EggPages100 = 8,
  LevelUp = 9,
};
constexpr uint8_t QUEST_KIND_MAX = static_cast<uint8_t>(QuestKind::LevelUp);
// Bits 0-2: one per quest slot. Bit 3 (rewarded mask only): the all-three bonus.
constexpr uint8_t QUEST_SLOT_MASK = 0x07U;
constexpr uint8_t QUEST_BONUS_BIT = 0x08U;

struct KindleMark {
  uint32_t documentHash = 0;  // 0 = unused
  uint16_t highWaterPermille = 0;  // 0-1000

  bool operator==(const KindleMark&) const = default;
};

// The whole extras state, about 1 KB. Field groups follow the feature specs
// in docs/extras/features/. Zero means "nothing yet" everywhere, so a
// value-initialised state (plus the default toggles) is the empty state.
struct ExtrasState {
  uint8_t featureMask = DEFAULT_FEATURE_MASK;
  uint32_t rngState = 0;  // xorshift32; 0 = not seeded yet (see seedRandom)

  // 1. Eggs. An egg is held when eggSpeciesId != 0.
  uint16_t eggSpeciesId = 0;
  uint16_t eggPagesRemaining = 0;  // 0 with an egg held = ready to hatch
  uint8_t eggFlags = 0;
  uint8_t eggMisses = 0;
  uint32_t pendingHatchRecordId = 0;  // crash safety, see 01-eggs.md

  // 2. Streaks.
  uint16_t streakXpRemainderPermille = 0;  // < 1000
  uint16_t lastRewardedStreakMilestone = 0;
  uint32_t lastRewardedStreakDay = 0;

  // 3. Book-finish boss. rewardedBookHashes[0..rewardedBookCount) oldest first, rest 0.
  uint16_t rewardedBookCount = 0;
  std::array<uint32_t, REWARDED_BOOK_CAPACITY> rewardedBookHashes{};
  uint16_t heldBossSpeciesId = 0;
  uint16_t ribbonCount = 0;

  // 5. Kindle credit. kindleMarks[0..kindleMarkCount) least recently used first, rest 0.
  uint8_t kindleMarkCount = 0;
  std::array<KindleMark, KINDLE_MARK_CAPACITY> kindleMarks{};
  uint16_t kindlePendingNoticeMinutes = 0;

  // 6. Post-game. A Battle Tower run is active when towerRunSeed != 0.
  uint8_t battlePasses = 0;
  uint32_t passMinuteAnchor = 0;
  std::array<uint8_t, GYM_REMATCH_COUNT> rematchWins{};
  uint16_t towerBestStreak = 0;
  uint32_t towerRunSeed = 0;
  uint16_t towerRunWins = 0;

  // 7. Day Care. Occupied when dayCareRecordId != 0.
  uint32_t dayCareRecordId = 0;
  uint8_t dayCareMinuteRemainder = 0;  // 0 or 1

  // 8. Daily quests. No set chosen yet when questIds are all None.
  uint32_t questDayAnchor = 0;
  std::array<uint8_t, DAILY_QUEST_SLOTS> questIds{};
  std::array<uint16_t, DAILY_QUEST_SLOTS> questProgress{};
  uint8_t questDoneMask = 0;
  uint8_t questRewardedMask = 0;

  bool operator==(const ExtrasState&) const = default;
};

// Every invariant the codec relies on. encode refuses an invalid state and
// decode rejects one, so a bad state is never written and a bad file reads as
// "empty extras" (SAVE-SAFETY R5).
bool validateExtrasState(const ExtrasState& state);

// --- Toggles ---------------------------------------------------------------
bool isFeatureEnabled(const ExtrasState& state, Feature feature);
void setFeatureEnabled(ExtrasState& state, Feature feature, bool enabled);

// --- Reset -----------------------------------------------------------------
// Game reset (PokemonService::reset()): clears all progress. The toggles and
// the RNG stream are kept: they are settings / entropy, not game progress.
void resetExtrasProgress(ExtrasState& state);

// --- Extras RNG ------------------------------------------------------------
// The extras RNG is separate from the draws inside applyCreditedMinutes()
// (its tests script exact draw sequences). It lives in the extras file so a
// reload continues the same stream. Seed once from hardware entropy
// (esp_random() on device) when rngState == 0; a zero seed is remapped.
void seedRandom(ExtrasState& state, uint32_t seed);
// Uniform in [0, upperExclusive). Returns 0 when upperExclusive <= 1. Seeds
// itself with a fixed constant if seedRandom was never called.
uint32_t randomBelow(ExtrasState& state, uint32_t upperExclusive);
// Adapter for upstream helpers that take a RandomSource. The source points at
// `state`, so it must not outlive it.
RandomSource randomSource(ExtrasState& state);

// --- Stable ids ------------------------------------------------------------
// FNV-1a 32 of a book's or document's stable id (path or KOReader document
// hash). Never returns 0, so 0 can keep meaning "unused slot".
uint32_t stableIdHash(const char* text);
uint32_t stableIdHash(const uint8_t* data, size_t size);

}  // namespace pokemon::extras
