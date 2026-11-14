#pragma once

#if defined(CROSSINK_ENABLE_POKEMON) && defined(CROSSINK_POKEMON_EXTRAS)

#include <PokemonExtrasRules.h>

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

}  // namespace pokemon

#endif
