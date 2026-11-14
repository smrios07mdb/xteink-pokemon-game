#pragma once

// Extras feature 10 (docs/extras/features/10-party-sleep-screen.md): the
// Party sleep screen's geometry, pure and host-testable like
// PokemonDashboardLayout. SleepActivity draws into these boxes; nothing here
// reads or writes any file. Compiles to nothing without
// CROSSINK_POKEMON_EXTRAS (SAVE-SAFETY R11).

#include <cstdint>

namespace pokemon::extras {

struct SleepBox {
  int x = 0;
  int y = 0;
  int width = 0;
  int height = 0;
};

// What there is to show. Without a valid clock there is no "today", so the
// reading-time and streak lines are left out (the lead's lines still show).
struct PartySleepContent {
  bool clockValid = false;
  bool hasEgg = false;
};

// Top to bottom: art, nickname, level, XP bar, then today's reading time,
// streak and egg progress. Hidden lines get an empty box and show* = false;
// the block is centred vertically on the screen.
struct PartySleepLayout {
  SleepBox art{};
  SleepBox name{};
  SleepBox level{};
  SleepBox xpBar{};
  SleepBox today{};
  SleepBox streak{};
  SleepBox egg{};
  bool showToday = false;
  bool showStreak = false;
  bool showEgg = false;
  // false: the screen is too small for even the text lines. The caller then
  // falls through to the configured sleep screen.
  bool valid = false;
};

constexpr int kPartySleepMargin = 24;
constexpr int kPartySleepArtGap = 16;       // art to nickname
constexpr int kPartySleepSectionGap = 24;   // XP bar to the reading lines
constexpr int kPartySleepXpBarHeight = 12;
constexpr int kPartySleepXpBarGap = 8;      // level line to XP bar
constexpr int kPartySleepMinArt = 48;       // smaller than this: drop the art

// width/height: the screen (X4 Pro portrait: 480x800). lineHeight: the
// font's line height. preferredArt: the art's native square size; it shrinks
// to fit, and is dropped (art box empty) below kPartySleepMinArt.
PartySleepLayout partySleepLayout(int width, int height, int lineHeight, int preferredArt,
                                  const PartySleepContent& content);

// Filled width of an XP bar: 0..barWidth, rounded down. required == 0 (level
// 100) shows a full bar.
int xpBarFillWidth(int barWidth, uint32_t earned, uint32_t required);

// Egg progress 0..100 from the pages still to read and the egg's total pages.
uint8_t eggProgressPercent(uint16_t pagesRemaining, uint16_t totalPages);

}  // namespace pokemon::extras
