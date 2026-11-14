#if defined(CROSSINK_ENABLE_POKEMON) && defined(CROSSINK_POKEMON_EXTRAS)

#include "PokemonExtrasHooks.h"

#if !defined(POKEMON_SERVICE_HOST_TEST)
#include "activities/reader/GlobalReadingStats.h"
#include "activities/reader/ReadingStatsUtils.h"
#endif

namespace pokemon {
namespace {

ExtrasStreakInputSource streakInputSource = nullptr;
ExtrasQuestClockSource questClockSource = nullptr;

extras::QuestClock defaultQuestClock() {
  extras::QuestClock clock{};
#if !defined(POKEMON_SERVICE_HOST_TEST)
  ReadingStatsDateTime now{};
  if (!getCurrentLocalReadingStatsDateTime(now)) return clock;
  clock.clockValid = true;
  clock.todayIndex = readingStatsDayIndex(now.date);
#endif
  return clock;
}

extras::StreakInput defaultStreakInput() {
  extras::StreakInput input{};
#if !defined(POKEMON_SERVICE_HOST_TEST)
  ReadingStatsDateTime now{};
  if (!getCurrentLocalReadingStatsDateTime(now)) return input;
  // One read of global_stats.bin (159 bytes). Not cached: the streak grows
  // during the day, the moment today's reading is first recorded.
  const GlobalReadingStats stats = GlobalReadingStats::load();
  input.clockValid = true;
  input.streakDays = stats.currentReadingStreak(&now.date);
  input.todayIndex = readingStatsDayIndex(now.date);
#endif
  return input;
}

}  // namespace

void setExtrasStreakInputSource(const ExtrasStreakInputSource source) { streakInputSource = source; }

extras::StreakInput readExtrasStreakInput() {
  return streakInputSource != nullptr ? streakInputSource() : defaultStreakInput();
}

void setExtrasQuestClockSource(const ExtrasQuestClockSource source) { questClockSource = source; }

extras::QuestClock readExtrasQuestClock() {
  return questClockSource != nullptr ? questClockSource() : defaultQuestClock();
}

}  // namespace pokemon

#endif
