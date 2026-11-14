// Extras feature 10: Party sleep screen geometry (10-party-sleep-screen.md).

#include <cstdio>
#include <initializer_list>

#include "PokemonExtrasLayout.h"

namespace {

using namespace pokemon::extras;

int failures = 0;

#define CHECK(condition)                                                                \
  do {                                                                                  \
    if (!(condition)) {                                                                 \
      std::fprintf(stderr, "%s:%d check failed: %s\n", __FILE__, __LINE__, #condition); \
      ++failures;                                                                       \
    }                                                                                   \
  } while (false)

constexpr int W = 480;  // X4 Pro portrait
constexpr int H = 800;
constexpr int LINE = 32;
constexpr int ART = 192;

bool inside(const SleepBox& box, const int width, const int height) {
  return box.x >= 0 && box.y >= 0 && box.x + box.width <= width && box.y + box.height <= height;
}

bool empty(const SleepBox& box) { return box.width == 0 && box.height == 0; }

// Visible boxes in order, top to bottom, without overlap.
bool stackedInOrder(const PartySleepLayout& layout) {
  const SleepBox* boxes[] = {&layout.art,   &layout.name,   &layout.level, &layout.xpBar,
                             &layout.today, &layout.streak, &layout.egg};
  int bottom = 0;
  for (const SleepBox* box : boxes) {
    if (empty(*box)) continue;
    if (box->y < bottom) return false;
    bottom = box->y + box->height;
  }
  return true;
}

void fullScreenWithClockAndEgg() {
  const PartySleepLayout layout = partySleepLayout(W, H, LINE, ART, {true, true});
  CHECK(layout.valid);
  CHECK(layout.showToday && layout.showStreak && layout.showEgg);
  CHECK(layout.art.width == ART && layout.art.height == ART);
  CHECK(layout.art.x == (W - ART) / 2);
  for (const SleepBox& box : {layout.art, layout.name, layout.level, layout.xpBar, layout.today, layout.streak,
                              layout.egg}) {
    CHECK(inside(box, W, H));
  }
  CHECK(stackedInOrder(layout));
  CHECK(layout.xpBar.height == kPartySleepXpBarHeight);
  CHECK(layout.name.width == W - 2 * kPartySleepMargin);
  // Centred: equal space above the art and below the egg line (within 1 px).
  const int above = layout.art.y;
  const int below = H - (layout.egg.y + layout.egg.height);
  CHECK(above - below >= -1 && above - below <= 1);
}

void noEggHidesTheEggLine() {
  const PartySleepLayout layout = partySleepLayout(W, H, LINE, ART, {true, false});
  CHECK(layout.valid);
  CHECK(layout.showToday && layout.showStreak && !layout.showEgg);
  CHECK(empty(layout.egg));
  CHECK(stackedInOrder(layout));
}

void noClockHidesTodayAndStreak() {
  const PartySleepLayout layout = partySleepLayout(W, H, LINE, ART, {false, false});
  CHECK(layout.valid);
  CHECK(!layout.showToday && !layout.showStreak && !layout.showEgg);
  CHECK(empty(layout.today) && empty(layout.streak) && empty(layout.egg));
  CHECK(!empty(layout.name) && !empty(layout.level) && !empty(layout.xpBar));
  CHECK(stackedInOrder(layout));

  const PartySleepLayout eggOnly = partySleepLayout(W, H, LINE, ART, {false, true});
  CHECK(eggOnly.valid && eggOnly.showEgg && !eggOnly.showToday);
  CHECK(stackedInOrder(eggOnly));
}

void landscapeAndSmallScreensShrinkThenDropTheArt() {
  // X4 Pro landscape: 800x480. Art shrinks to fit the height.
  const PartySleepLayout landscape = partySleepLayout(H, W, LINE, ART, {true, true});
  CHECK(landscape.valid);
  CHECK(landscape.art.height <= ART);
  CHECK(inside(landscape.egg, H, W));
  CHECK(stackedInOrder(landscape));

  // Text fits but not even a 48 px art: the art is dropped.
  const int textOnly = 2 * kPartySleepMargin + 5 * LINE + kPartySleepXpBarGap + kPartySleepXpBarHeight +
                       kPartySleepSectionGap;
  const PartySleepLayout tight = partySleepLayout(W, textOnly + 20, LINE, ART, {true, true});
  CHECK(tight.valid);
  CHECK(empty(tight.art));
  CHECK(stackedInOrder(tight));
  CHECK(inside(tight.egg, W, textOnly + 20));

  // Not even the text fits: invalid, the caller falls through.
  CHECK(!partySleepLayout(W, textOnly - 1, LINE, ART, {true, true}).valid);
  CHECK(!partySleepLayout(0, H, LINE, ART, {true, true}).valid);
  CHECK(!partySleepLayout(W, H, 0, ART, {true, true}).valid);
  CHECK(!partySleepLayout(-5, -5, LINE, ART, {}).valid);
}

void xpBarAndEggProgress() {
  CHECK(xpBarFillWidth(400, 0, 100) == 0);
  CHECK(xpBarFillWidth(400, 50, 100) == 200);
  CHECK(xpBarFillWidth(400, 99, 100) == 396);
  CHECK(xpBarFillWidth(400, 150, 100) == 400);
  CHECK(xpBarFillWidth(400, 0, 0) == 400);  // level 100
  CHECK(xpBarFillWidth(0, 50, 100) == 0);
  CHECK(xpBarFillWidth(-3, 50, 100) == 0);

  CHECK(eggProgressPercent(600, 600) == 0);
  CHECK(eggProgressPercent(300, 600) == 50);
  CHECK(eggProgressPercent(1, 600) == 99);
  CHECK(eggProgressPercent(0, 600) == 100);
  CHECK(eggProgressPercent(900, 600) == 0);
  CHECK(eggProgressPercent(5, 0) == 0);
}

}  // namespace

int main() {
  fullScreenWithClockAndEgg();
  noEggHidesTheEggLine();
  noClockHidesTodayAndStreak();
  landscapeAndSmallScreensShrinkThenDropTheArt();
  xpBarAndEggProgress();
  if (failures != 0) {
    std::fprintf(stderr, "%d check(s) failed\n", failures);
    return 1;
  }
  std::puts("PokemonExtrasLayoutTest: all checks passed");
  return 0;
}
