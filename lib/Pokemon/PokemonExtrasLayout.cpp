#if defined(CROSSINK_ENABLE_POKEMON) && defined(CROSSINK_POKEMON_EXTRAS)

#include "PokemonExtrasLayout.h"

#include <algorithm>

namespace pokemon::extras {

PartySleepLayout partySleepLayout(const int width, const int height, const int lineHeight, const int preferredArt,
                                  const PartySleepContent& content) {
  PartySleepLayout layout{};
  const int contentWidth = width - 2 * kPartySleepMargin;
  const int contentHeight = height - 2 * kPartySleepMargin;
  if (contentWidth <= 0 || contentHeight <= 0 || lineHeight <= 0) return layout;

  layout.showToday = content.clockValid;
  layout.showStreak = content.clockValid;
  layout.showEgg = content.hasEgg;
  const int readingLines = (layout.showToday ? 1 : 0) + (layout.showStreak ? 1 : 0) + (layout.showEgg ? 1 : 0);

  const int leadText = 2 * lineHeight + kPartySleepXpBarGap + kPartySleepXpBarHeight;
  const int readingText = readingLines > 0 ? kPartySleepSectionGap + readingLines * lineHeight : 0;
  const int textHeight = leadText + readingText;
  if (textHeight > contentHeight) return layout;

  int art = std::min({std::max(preferredArt, 0), contentWidth, contentHeight - textHeight - kPartySleepArtGap});
  if (art < kPartySleepMinArt) art = 0;
  const int blockHeight = (art > 0 ? art + kPartySleepArtGap : 0) + textHeight;

  int y = kPartySleepMargin + (contentHeight - blockHeight) / 2;
  const auto line = [&](SleepBox& box, const int boxHeight) {
    box = SleepBox{kPartySleepMargin, y, contentWidth, boxHeight};
    y += boxHeight;
  };

  if (art > 0) {
    layout.art = SleepBox{(width - art) / 2, y, art, art};
    y += art + kPartySleepArtGap;
  }
  line(layout.name, lineHeight);
  line(layout.level, lineHeight);
  y += kPartySleepXpBarGap;
  line(layout.xpBar, kPartySleepXpBarHeight);
  if (readingLines > 0) y += kPartySleepSectionGap;
  if (layout.showToday) line(layout.today, lineHeight);
  if (layout.showStreak) line(layout.streak, lineHeight);
  if (layout.showEgg) line(layout.egg, lineHeight);

  layout.valid = true;
  return layout;
}

int xpBarFillWidth(const int barWidth, const uint32_t earned, const uint32_t required) {
  if (barWidth <= 0) return 0;
  if (required == 0) return barWidth;
  const uint64_t clamped = std::min<uint64_t>(earned, required);
  return static_cast<int>(clamped * static_cast<uint64_t>(barWidth) / required);
}

uint8_t eggProgressPercent(const uint16_t pagesRemaining, const uint16_t totalPages) {
  if (totalPages == 0) return 0;
  const uint32_t remaining = std::min(pagesRemaining, totalPages);
  return static_cast<uint8_t>((static_cast<uint32_t>(totalPages - remaining) * 100U) / totalPages);
}

}  // namespace pokemon::extras

#endif
