#pragma once

#if defined(CROSSINK_ENABLE_POKEMON) && defined(CROSSINK_POKEMON_EXTRAS)

#include <PokemonExtrasQuests.h>
#include <PokemonExtrasRules.h>

#include <cstdint>
#include <string>

#if !defined(POKEMON_SERVICE_HOST_TEST)
class Epub;
#endif

namespace pokemon {

// Extras M2 (docs/extras/M2-HOOKS.md): where the streak rules get their input.
//
// The device source reads the firmware's own reading statistics and never
// writes them (docs/extras/SAVE-SAFETY.md R4):
// getCurrentLocalReadingStatsDateTime(), GlobalReadingStats::load() and
// currentReadingStreak(). Without a valid clock it returns clockValid = false,
// which turns the bonus and the milestone off.
//
// Host tests have no reading statistics: their default source reports no
// clock, and a test installs its own.
using ExtrasStreakInputSource = extras::StreakInput (*)();

// nullptr puts the default source back.
void setExtrasStreakInputSource(ExtrasStreakInputSource source);

extras::StreakInput readExtrasStreakInput();

// Extras M3: the day the daily quests belong to. Same clock as the streak
// (getCurrentLocalReadingStatsDateTime(), readingStatsDayIndex()), read-only,
// without reading global_stats.bin. Host default: no clock.
using ExtrasQuestClockSource = extras::QuestClock (*)();
void setExtrasQuestClockSource(ExtrasQuestClockSource source);
extras::QuestClock readExtrasQuestClock();

// Extras M4 (docs/extras/M4-HOOKS.md). Book boss and Kindle credit key on the
// same id for the same book, from every hook: stableIdHash() of the book's
// file path, the same string the firmware derives the book's cache folder
// from (Epub/Xtc/Txt::getPath()). A renamed or moved file is a new book.
// Call nothing else to make a book id.
uint32_t extrasBookHash(const std::string& bookPath);

#if !defined(POKEMON_SERVICE_HOST_TEST)
// The reader hooks. Each is one line at its call site, behind
// CROSSINK_POKEMON_EXTRAS, after the firmware's own work is done. They only
// read the firmware's reading stats (R4), do nothing while the Pokemon game is
// switched off, and swallow every failure: the caller's flow never changes.

// A page turn, next to setBookProgressPercent(): the open book and its
// whole-book position (0.0..1.0). RAM only.
void extrasNoteOpenBook(const std::string& bookPath, float fraction);

// A synced position was applied and saved (feature 5).
//   localFraction:  this reader's whole-book position before the sync;
//   remoteFraction: the applied position;
//   bookPages:      the whole book's page count on this reader, 0 if unknown
//                   (the sync then only moves the mark);
//   cachePath:      the book's cache folder, for its pace (read-only).
void extrasOnRemoteProgressApplied(const std::string& bookPath, const std::string& cachePath, float localFraction,
                                   float remoteFraction, uint32_t bookPages);

// A book went from not-completed to completed, after the firmware saved that
// (feature 3). Not for a completed flag that arrived from another device.
void extrasOnBookCompleted(const std::string& bookPath, const std::string& cachePath);

// The whole EPUB's page count on this reader, projected from one spine item
// whose page count is known (pages per byte x book size). 0 if unknown.
uint32_t extrasEpubBookPages(const Epub& epub, int spineIndex, int spinePages);
#endif

}  // namespace pokemon

#endif
