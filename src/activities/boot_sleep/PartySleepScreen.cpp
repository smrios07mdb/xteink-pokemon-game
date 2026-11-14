#if defined(CROSSINK_ENABLE_POKEMON) && defined(CROSSINK_POKEMON_EXTRAS)

#include "PartySleepScreen.h"

#include <GfxRenderer.h>
#include <I18n.h>
#include <Logging.h>
#include <PokemonExtrasEggs.h>
#include <PokemonExtrasLayout.h>
#include <PokemonSpecies.h>
#include <PokemonTypes.h>

#include <cstdio>
#include <memory>
#include <new>
#include <string>

#include "FeatureToggles.h"
#include "activities/reader/DailyReadingStats.h"
#include "activities/reader/GlobalReadingStats.h"
#include "activities/reader/ReadingStatsUtils.h"
#include "components/pokemon/PokemonArt.h"
#include "fontIds.h"
#include "pokemon/PokemonExtrasStore.h"
#include "pokemon/PokemonStore.h"

namespace pokemon {
namespace {

// Hero art is 120x90 and is never scaled up; the layout hands out a square
// box, so the art takes its full width and sits in the middle of its height.
constexpr int PREFERRED_ART_SIZE = 120;

// The lead Pokemon, read without creating or changing any file. A local
// store: the device's shared one may not be open yet, and opening it with
// begin() can migrate a legacy file name.
bool readLeader(PokemonRecord& leader) {
  PokemonStore store;
  if (store.beginReadOnly() != StoreBeginResult::Ready) return false;
  const std::unique_ptr<PokemonState> state(new (std::nothrow) PokemonState());
  if (!state || !store.loadState(*state) || state->partyRecordIds[0] == 0) return false;
  return store.readRecord(state->partyRecordIds[0], leader);
}

void drawCenteredLine(const GfxRenderer& renderer, const int font, const extras::SleepBox& box, const char* text,
                      const EpdFontFamily::Style style = EpdFontFamily::REGULAR) {
  const std::string fitted = renderer.truncatedText(font, text, box.width, style);
  const int width = renderer.getTextWidth(font, fitted.c_str(), style);
  renderer.drawText(font, box.x + (box.width - width) / 2, box.y, fitted.c_str(), true, style);
}

}  // namespace

bool partySleepScreenEnabled() {
  return features::pokemonGame() && devicePokemonExtrasStore().isFeatureEnabled(extras::Feature::PartySleepScreen);
}

bool drawPartySleepScreen(GfxRenderer& renderer) {
  if (!partySleepScreenEnabled()) return false;
  const extras::ExtrasState& extrasState = devicePokemonExtrasStore().state();

  PokemonRecord leader{};
  if (!readLeader(leader)) {
    LOG_INF("SLP", "Party sleep screen: no lead Pokemon to show");
    return false;
  }

  ReadingStatsDateTime now{};
  extras::PartySleepContent content{};
  content.clockValid = getCurrentLocalReadingStatsDateTime(now);
  // Eggs off hides the egg, as on the Pokemon menu.
  const extras::EggProgress egg = devicePokemonExtrasStore().isFeatureEnabled(extras::Feature::Eggs)
                                      ? extras::eggProgress(extrasState)
                                      : extras::EggProgress{};
  content.hasEgg = egg.held;

  constexpr int font = UI_12_FONT_ID;
  const extras::PartySleepLayout layout = extras::partySleepLayout(
      renderer.getScreenWidth(), renderer.getScreenHeight(), renderer.getLineHeight(font), PREFERRED_ART_SIZE, content);
  if (!layout.valid) return false;

  renderer.clearScreen();
  if (layout.art.width > 0) {
    const int artHeight = layout.art.width * 3 / 4;
    const Rect artBounds{layout.art.x, layout.art.y + (layout.art.height - artHeight) / 2, layout.art.width, artHeight};
    if (!drawPokemonSpeciesArt(renderer, leader.speciesId, true, artBounds, false)) return false;
  }

  char line[96];
  const SpeciesData* species = speciesData(leader.speciesId);
  drawCenteredLine(renderer, font, layout.name,
                   leader.nickname[0] != '\0' ? leader.nickname.data() : (species == nullptr ? "???" : species->name),
                   EpdFontFamily::BOLD);

  const LevelXpProgress progress = levelXpProgress(leader.totalXp);
  snprintf(line, sizeof(line), "%s %u", tr(STR_POKEMON_LEVEL), static_cast<unsigned>(progress.level));
  drawCenteredLine(renderer, font, layout.level, line);

  // The bar is narrower than its box, like the game's own bars.
  const int barWidth = layout.xpBar.width * 3 / 5;
  const int barX = layout.xpBar.x + (layout.xpBar.width - barWidth) / 2;
  renderer.drawRect(barX, layout.xpBar.y, barWidth, layout.xpBar.height, true);
  const int filled = extras::xpBarFillWidth(barWidth - 2, progress.earned, progress.required);
  if (filled > 0) renderer.fillRect(barX + 1, layout.xpBar.y + 1, filled, layout.xpBar.height - 2, true);

  if (layout.showToday) {
    const DailyReadingStats::Summary today = DailyReadingStats::summarize(readingStatsDayIndex(now.date));
    char duration[24];
    formatCompactReadingDuration(today.hasToday ? today.todaySeconds : 0, duration, sizeof(duration));
    snprintf(line, sizeof(line), tr(STR_POKEMON_SLEEP_TODAY),
             today.hasToday && today.todaySeconds > 0 ? duration : "0m");
    drawCenteredLine(renderer, font, layout.today, line);
  }
  if (layout.showStreak) {
    const uint16_t streakDays = GlobalReadingStats::load().currentReadingStreak(&now.date);
    if (streakDays == 1) {
      snprintf(line, sizeof(line), "%s", tr(STR_POKEMON_SLEEP_STREAK_ONE));
    } else {
      snprintf(line, sizeof(line), tr(STR_POKEMON_SLEEP_STREAK), static_cast<unsigned>(streakDays));
    }
    drawCenteredLine(renderer, font, layout.streak, line);
  }
  if (layout.showEgg) {
    snprintf(line, sizeof(line), tr(STR_POKEMON_SLEEP_EGG), static_cast<unsigned>(egg.pagesRemaining));
    drawCenteredLine(renderer, font, layout.egg, line);
    const int eggBarX = layout.eggBar.x + (layout.eggBar.width - barWidth) / 2;
    renderer.drawRect(eggBarX, layout.eggBar.y, barWidth, layout.eggBar.height, true);
    const int eggFilled = (barWidth - 2) * extras::eggProgressPercent(egg.pagesRemaining, egg.totalPages) / 100;
    if (eggFilled > 0) renderer.fillRect(eggBarX + 1, layout.eggBar.y + 1, eggFilled, layout.eggBar.height - 2, true);
  }
  return true;
}

}  // namespace pokemon

#endif
