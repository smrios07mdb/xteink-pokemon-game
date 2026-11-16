#if defined(CROSSINK_ENABLE_POKEMON) && defined(CROSSINK_POKEMON_EXTRAS)

#include "PokemonExtrasHooks.h"

#if !defined(POKEMON_SERVICE_HOST_TEST)
#include <Epub.h>
#include <Logging.h>
#include <PokemonExtrasBoss.h>
#include <PokemonExtrasKindle.h>

#include <cmath>

#include "FeatureToggles.h"
#include "PokemonService.h"
#include "activities/reader/BookReadingStats.h"
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

uint32_t extrasBookHash(const std::string& bookPath) {
  return bookPath.empty() ? 0 : extras::stableIdHash(bookPath.c_str());
}

#if !defined(POKEMON_SERVICE_HOST_TEST)
namespace {
uint16_t permilleOf(const float fraction) { return extras::fractionToPermille(fraction); }
}  // namespace

void extrasNoteOpenBook(const std::string& bookPath, const float fraction) {
  if (!features::pokemonGame()) return;
  devicePokemonService().setExtrasOpenBook(extrasBookHash(bookPath), permilleOf(fraction));
}

void extrasOnRemoteProgressApplied(const std::string& bookPath, const std::string& cachePath,
                                   const float localFraction, const float remoteFraction, const uint32_t bookPages) {
  if (!features::pokemonGame() || bookPath.empty()) return;
  extras::KindleSync sync{};
  sync.documentHash = extrasBookHash(bookPath);
  sync.localPermille = permilleOf(localFraction);
  sync.remotePermille = permilleOf(remoteFraction);
  sync.bookPages = bookPages;
  // Pace from the firmware's own stats, read-only (R4).
  if (!cachePath.empty()) sync.pace.bookSecondsPerPage = BookReadingStats::load(cachePath).avgSecondsPerForwardPage;
  const GlobalReadingStats global = GlobalReadingStats::load();
  sync.pace.globalReadingSeconds = global.totalReadingSeconds;
  sync.pace.globalPagesTurned = global.totalPagesTurned;
  const uint16_t credited = devicePokemonService().creditRemoteReading(sync);
  LOG_INF("PokemonExtras", "Sync applied: %u -> %u permille, %lu pages, %u minutes credited",
          static_cast<unsigned>(sync.localPermille), static_cast<unsigned>(sync.remotePermille),
          static_cast<unsigned long>(bookPages), static_cast<unsigned>(credited));
}

void extrasOnBookCompleted(const std::string& bookPath, const std::string& cachePath) {
  if (!features::pokemonGame() || bookPath.empty()) return;
  extras::BookFinish book{};
  book.bookHash = extrasBookHash(bookPath);
  if (!cachePath.empty()) book.deviceReadingSeconds = BookReadingStats::load(cachePath).totalReadingSeconds;
  const extras::BookFinishResult result = devicePokemonService().onExtrasBookFinished(book);
  LOG_INF("PokemonExtras", "Book finished: result %u", static_cast<unsigned>(result));
}

uint32_t extrasEpubBookPages(const Epub& epub, const int spineIndex, const int spinePages) {
  if (spineIndex < 0 || spinePages <= 0) return 0;
  const size_t before = spineIndex > 0 ? epub.getCumulativeSpineItemSize(spineIndex - 1) : 0;
  const size_t through = epub.getCumulativeSpineItemSize(spineIndex);
  const size_t bookSize = epub.getBookSize();
  if (through <= before || bookSize == 0) return 0;
  const double pages = static_cast<double>(spinePages) * static_cast<double>(bookSize) / static_cast<double>(through - before);
  return pages >= 1.0e6 ? 0U : static_cast<uint32_t>(std::lround(pages));
}
#endif

}  // namespace pokemon

#endif
