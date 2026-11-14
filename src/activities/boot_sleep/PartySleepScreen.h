#pragma once

#if defined(CROSSINK_ENABLE_POKEMON) && defined(CROSSINK_POKEMON_EXTRAS)

class GfxRenderer;

namespace pokemon {

// Extras feature 10 (docs/extras/features/10-party-sleep-screen.md).
//
// Extras > Party sleep screen. Read-only, and loads the extras file on first
// use like every other reader of it.
bool partySleepScreenEnabled();

// Draws the Party sleep screen into the frame buffer: the lead Pokemon's art,
// name, level and XP bar, today's reading time, the streak, and the egg line
// when an egg is held. The caller shows the buffer.
//
// Returns false, and the caller draws the configured sleep screen instead,
// when the toggle is off or anything is missing: no save, no lead, no art, a
// screen too small for the layout. The frame buffer may then hold a partial
// drawing, which every sleep screen already clears or covers.
//
// Writes nothing: the save is opened with PokemonStore::beginReadOnly() on a
// local store, the extras file is only read, and the reading statistics are
// only read (SAVE-SAFETY R4).
bool drawPartySleepScreen(GfxRenderer& renderer);

}  // namespace pokemon

#endif
