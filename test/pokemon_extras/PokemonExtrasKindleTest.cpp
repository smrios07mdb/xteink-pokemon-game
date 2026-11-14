// Extras M4 rules, feature 5: Kindle credit (05-kindle-credit.md). Pure
// functions only; the reader/sync hooks and their writes are covered by the
// compat test (R10) and the simulator gate (M4-HOOKS.md).

#include <cmath>
#include <cstdio>
#include <limits>

#include "PokemonExtrasKindle.h"

namespace {

using namespace pokemon;
using namespace pokemon::extras;

int failures = 0;

#define CHECK(condition)                                                                \
  do {                                                                                  \
    if (!(condition)) {                                                                 \
      std::fprintf(stderr, "%s:%d check failed: %s\n", __FILE__, __LINE__, #condition); \
      ++failures;                                                                       \
    }                                                                                   \
  } while (false)

constexpr uint32_t BOOK = 0xB00C0001U;
constexpr uint32_t OTHER = 0xB00C0002U;

// A 300-page book read at 60 s a page: 1 per mille = 0.3 page = 0.3 minute.
KindleSync sync(const uint16_t local, const uint16_t remote, const uint32_t document = BOOK) {
  KindleSync input{};
  input.documentHash = document;
  input.localPermille = local;
  input.remotePermille = remote;
  input.bookPages = 300;
  input.pace.bookSecondsPerPage = 60;
  return input;
}

// --- Positions -------------------------------------------------------------

void fractionsFloorToPermille() {
  CHECK(fractionToPermille(0.0F) == 0);
  CHECK(fractionToPermille(0.0009F) == 0);
  CHECK(fractionToPermille(0.25F) == 250);
  CHECK(fractionToPermille(0.9999F) == 999);
  CHECK(fractionToPermille(1.0F) == 1000);
  CHECK(fractionToPermille(7.5F) == 1000);
  CHECK(fractionToPermille(-0.5F) == 0);
  CHECK(fractionToPermille(std::numeric_limits<float>::quiet_NaN()) == 0);
  CHECK(fractionToPermille(std::numeric_limits<float>::infinity()) == 1000);
}

// --- Delta to minutes ------------------------------------------------------

void deltaBecomesPagesThenMinutesAtFullRate() {
  const ExtrasState state{};
  // 14% of 300 pages = 42 pages at one minute each.
  const KindleCredit credit = planKindleCredit(state, sync(100, 240));
  CHECK(credit.pages == 42);
  CHECK(credit.minutes == 42);
  CHECK(credit.droppedMinutes == 0);
  CHECK(credit.highWaterPermille == 240);

  // The same pages at 90 s a page: 63 minutes. No cap below the guard.
  KindleSync slow = sync(100, 240);
  slow.pace.bookSecondsPerPage = 90;
  CHECK(planKindleCredit(state, slow).minutes == 63);

  // A whole 500-page book at 60 s a page stays under the guard: all of it.
  KindleSync whole = sync(0, 1000);
  whole.bookPages = 500;
  const KindleCredit all = planKindleCredit(state, whole);
  CHECK(all.pages == 500 && all.minutes == 500 && all.droppedMinutes == 0);
}

void roundingIsToTheNearestPageAndMinute() {
  const ExtrasState state{};
  // 5 per mille of 300 pages = 1.5 pages -> 2 pages; at 40 s = 80 s -> 1 minute.
  KindleSync input = sync(0, 5);
  input.pace.bookSecondsPerPage = 40;
  const KindleCredit credit = planKindleCredit(state, input);
  CHECK(credit.pages == 2 && credit.minutes == 1);

  // 1 page at 20 s is under half a minute: nothing to credit, no egg page.
  KindleSync tiny = sync(0, 3);
  tiny.pace.bookSecondsPerPage = 20;
  const KindleCredit none = planKindleCredit(state, tiny);
  CHECK(none.minutes == 0 && none.pages == 0 && none.highWaterPermille == 3);
}

void smallOrBackwardMovesEarnNothing() {
  const ExtrasState state{};
  CHECK(planKindleCredit(state, sync(500, 500)).minutes == 0);
  CHECK(planKindleCredit(state, sync(500, 501)).minutes == 0);  // under the 0.1 point threshold
  CHECK(planKindleCredit(state, sync(500, 502)).minutes == 1);  // 0.6 page -> 1 page -> 1 minute
  CHECK(planKindleCredit(state, sync(500, 200)).minutes == 0);
  CHECK(planKindleCredit(state, sync(500, 200)).highWaterPermille == 500);
}

void unknownInputsEarnNothing() {
  const ExtrasState state{};
  KindleSync noPages = sync(0, 400);
  noPages.bookPages = 0;
  CHECK(planKindleCredit(state, noPages).minutes == 0);
  CHECK(planKindleCredit(state, noPages).highWaterPermille == 400);  // the mark still moves
  CHECK(planKindleCredit(state, sync(0, 400, 0)).minutes == 0);      // no document id
  CHECK(planKindleCredit(state, sync(0, 400, 0)).highWaterPermille == 0);
  // Positions past the end clamp to the end.
  CHECK(planKindleCredit(state, sync(900, 5000)).pages == 30);
}

// --- Pace fallbacks --------------------------------------------------------

void paceFallsBackFromBookToGlobalToDefault() {
  KindlePace pace{};
  CHECK(kindleSecondsPerPage(pace) == KINDLE_DEFAULT_SECONDS_PER_PAGE);

  pace.globalReadingSeconds = 90000;
  pace.globalPagesTurned = 2000;
  CHECK(kindleSecondsPerPage(pace) == 45);

  pace.bookSecondsPerPage = 72;
  CHECK(kindleSecondsPerPage(pace) == 72);

  // Global seconds without pages (or the reverse) is not a pace.
  CHECK(kindleSecondsPerPage(KindlePace{0, 90000, 0}) == KINDLE_DEFAULT_SECONDS_PER_PAGE);
  CHECK(kindleSecondsPerPage(KindlePace{0, 0, 2000}) == KINDLE_DEFAULT_SECONDS_PER_PAGE);

  // Junk stats are clamped, not trusted.
  CHECK(kindleSecondsPerPage(KindlePace{2, 0, 0}) == KINDLE_PACE_MIN_SECONDS);
  CHECK(kindleSecondsPerPage(KindlePace{3600, 0, 0}) == KINDLE_PACE_MAX_SECONDS);
  CHECK(kindleSecondsPerPage(KindlePace{0, 10, 2000}) == KINDLE_PACE_MIN_SECONDS);
  CHECK(kindleSecondsPerPage(KindlePace{0, UINT32_MAX, 1}) == KINDLE_PACE_MAX_SECONDS);

  // The plan uses it.
  ExtrasState state{};
  KindleSync input = sync(0, 100);  // 30 pages
  input.pace = KindlePace{0, 90000, 2000};
  CHECK(planKindleCredit(state, input).minutes == 23);  // 30 x 45 s = 22.5 min
  input.pace = KindlePace{};
  CHECK(planKindleCredit(state, input).minutes == 30);
}

// --- High-water mark -------------------------------------------------------

void repeatSyncEarnsNothing() {
  ExtrasState state{};
  CHECK(applyKindleSync(state, sync(100, 240)).minutes == 42);
  CHECK(kindleHighWaterPermille(state, BOOK) == 240);
  CHECK(validateExtrasState(state));

  const ExtrasState after = state;
  // The same sync again, also if the local position was somehow behind.
  CHECK(applyKindleSync(state, sync(100, 240)).minutes == 0);
  CHECK(applyKindleSync(state, sync(240, 240)).minutes == 0);
  CHECK(state == after);
}

void jumpingBackAndForwardEarnsNothing() {
  ExtrasState state{};
  CHECK(applyKindleSync(state, sync(0, 600)).minutes == 180);
  // Jump back to 20% on the X4 Pro, then a sync brings 60% again, then 65%.
  CHECK(applyKindleSync(state, sync(200, 600)).minutes == 0);
  const KindleCredit onward = applyKindleSync(state, sync(200, 650));
  CHECK(onward.pages == 15 && onward.minutes == 15);  // only 60% -> 65%
  CHECK(kindleHighWaterPermille(state, BOOK) == 650);
}

void localReadingThenSyncCreditsOnlyTheRest() {
  ExtrasState state{};
  // Read to 30% on the X4 Pro (checkpoints raise the mark), then reopen at 10%.
  CHECK(noteLocalProgress(state, BOOK, 150));
  CHECK(noteLocalProgress(state, BOOK, 300));
  CHECK(!noteLocalProgress(state, BOOK, 300));  // unchanged: no save needed
  CHECK(!noteLocalProgress(state, BOOK, 100));  // never lowers
  CHECK(kindleHighWaterPermille(state, BOOK) == 300);
  // The Kindle is at 50%: only 30% -> 50% is new.
  const KindleCredit credit = applyKindleSync(state, sync(100, 500));
  CHECK(credit.pages == 60 && credit.minutes == 60);

  // Without a mark the local position alone protects what was read here.
  ExtrasState fresh{};
  CHECK(applyKindleSync(fresh, sync(300, 500)).minutes == 60);
  // A sync that moves backwards still records where this reader was.
  ExtrasState behind{};
  CHECK(applyKindleSync(behind, sync(700, 400)).minutes == 0);
  CHECK(kindleHighWaterPermille(behind, BOOK) == 700);
}

void marksArePerDocument() {
  ExtrasState state{};
  CHECK(applyKindleSync(state, sync(0, 500, BOOK)).minutes == 150);
  CHECK(applyKindleSync(state, sync(0, 500, OTHER)).minutes == 150);
  CHECK(kindleHighWaterPermille(state, BOOK) == 500);
  CHECK(kindleHighWaterPermille(state, OTHER) == 500);
  CHECK(kindleHighWaterPermille(state, 0xDEADU) == 0);
  CHECK(kindleHighWaterPermille(state, 0) == 0);
  CHECK(state.kindleMarkCount == 2);
}

void leastRecentlyUsedMarkIsDroppedWhenFull() {
  ExtrasState state{};
  for (uint32_t index = 0; index < KINDLE_MARK_CAPACITY; ++index) {
    CHECK(raiseKindleMark(state, 1000U + index, static_cast<uint16_t>(index + 1U)));
  }
  CHECK(state.kindleMarkCount == KINDLE_MARK_CAPACITY);
  CHECK(validateExtrasState(state));

  // Touch the oldest: it becomes the most recently used, value kept.
  CHECK(raiseKindleMark(state, 1000U, 0));
  CHECK(kindleHighWaterPermille(state, 1000U) == 1);
  CHECK(state.kindleMarks[KINDLE_MARK_CAPACITY - 1U].documentHash == 1000U);
  CHECK(!raiseKindleMark(state, 1000U, 1));  // already last, same value

  // A new document drops the least recently used one, which is now 1001.
  CHECK(raiseKindleMark(state, 5000U, 900));
  CHECK(state.kindleMarkCount == KINDLE_MARK_CAPACITY);
  CHECK(kindleHighWaterPermille(state, 1001U) == 0);
  CHECK(kindleHighWaterPermille(state, 1000U) == 1);
  CHECK(kindleHighWaterPermille(state, 1002U) == 3);
  CHECK(kindleHighWaterPermille(state, 5000U) == 900);
  CHECK(validateExtrasState(state));

  CHECK(!raiseKindleMark(state, 0, 500));
  CHECK(raiseKindleMark(state, 5000U, 4000));  // clamps to the end of the book
  CHECK(kindleHighWaterPermille(state, 5000U) == 1000);
  CHECK(validateExtrasState(state));
}

// --- Mis-sync guard --------------------------------------------------------

void oneSyncCreditsAtMostTheGuard() {
  ExtrasState state{};
  // A wrong match jumps a 2000-page book from 0 to 100% at two minutes a page.
  KindleSync wrong = sync(0, 1000);
  wrong.bookPages = 2000;
  wrong.pace.bookSecondsPerPage = 120;
  const KindleCredit credit = applyKindleSync(state, wrong);
  CHECK(credit.minutes == KINDLE_GUARD_MINUTES);
  CHECK(credit.droppedMinutes == 4000 - KINDLE_GUARD_MINUTES);
  CHECK(credit.pages == 300);  // egg pages shrink in step: 2000 x 600 / 4000
  // What was dropped is gone: the mark is at the end, a repeat earns nothing.
  CHECK(kindleHighWaterPermille(state, BOOK) == 1000);
  CHECK(applyKindleSync(state, wrong).minutes == 0);

  // Exactly at the guard nothing is dropped.
  ExtrasState exact{};
  KindleSync atGuard = sync(0, 1000);
  atGuard.bookPages = 600;
  const KindleCredit full = applyKindleSync(exact, atGuard);
  CHECK(full.minutes == 600 && full.droppedMinutes == 0 && full.pages == 600);

  // Absurd inputs do not overflow.
  ExtrasState huge{};
  KindleSync absurd = sync(0, 1000);
  absurd.bookPages = UINT32_MAX;
  absurd.pace.bookSecondsPerPage = UINT16_MAX;
  const KindleCredit capped = applyKindleSync(huge, absurd);
  CHECK(capped.minutes == KINDLE_GUARD_MINUTES && capped.droppedMinutes == UINT16_MAX);
}

// --- Chunking and a full queue ---------------------------------------------

void minutesAreCreditedInChunks() {
  uint16_t remaining = 150;
  uint16_t credited = 0;
  int calls = 0;
  while (remaining != 0) {
    const KindleStep step = nextKindleStep(remaining, true);
    CHECK(step.xpOnlyMinutes == 0);
    CHECK(step.creditMinutes >= 1 && step.creditMinutes <= KINDLE_CHUNK_MINUTES);
    credited = static_cast<uint16_t>(credited + step.creditMinutes);
    remaining = static_cast<uint16_t>(remaining - step.creditMinutes);
    ++calls;
  }
  CHECK(credited == 150 && calls == 3);  // 60 + 60 + 30

  const KindleStep nothing = nextKindleStep(0, true);
  CHECK(nothing.creditMinutes == 0 && nothing.xpOnlyMinutes == 0);
  CHECK(nextKindleStep(KINDLE_GUARD_MINUTES, true).creditMinutes == KINDLE_CHUNK_MINUTES);
}

void fullQueueTurnsTheRestIntoXpOnly() {
  // Two chunks roll normally, then the queue fills: the rest is XP only, in one step.
  uint16_t remaining = 200;
  remaining = static_cast<uint16_t>(remaining - nextKindleStep(remaining, true).creditMinutes);
  remaining = static_cast<uint16_t>(remaining - nextKindleStep(remaining, true).creditMinutes);
  const KindleStep step = nextKindleStep(remaining, false);
  CHECK(step.creditMinutes == 0 && step.xpOnlyMinutes == 80);
  const KindleStep done = nextKindleStep(0, false);
  CHECK(done.creditMinutes == 0 && done.xpOnlyMinutes == 0);
}

// --- Notice ----------------------------------------------------------------

void noticeAddsUpAndIsShownOnce() {
  ExtrasState state{};
  CHECK(takeKindleNotice(state) == 0);
  addKindleNotice(state, 42);
  addKindleNotice(state, 18);
  CHECK(state.kindlePendingNoticeMinutes == 60);
  CHECK(takeKindleNotice(state) == 60);
  CHECK(takeKindleNotice(state) == 0);
  addKindleNotice(state, 65000);
  addKindleNotice(state, 65000);
  CHECK(takeKindleNotice(state) == UINT16_MAX);
}

// --- Toggle and reset ------------------------------------------------------

void toggleOffChangesNothing() {
  ExtrasState state{};
  setFeatureEnabled(state, Feature::KindleCredit, false);
  const ExtrasState before = state;
  const KindleCredit credit = applyKindleSync(state, sync(0, 800));
  CHECK(credit == KindleCredit{});
  CHECK(!noteLocalProgress(state, BOOK, 500));
  CHECK(state == before);
}

void resetClearsMarksAndNotice() {
  ExtrasState state{};
  applyKindleSync(state, sync(0, 800));
  addKindleNotice(state, 240);
  resetExtrasProgress(state);
  CHECK(state.kindleMarkCount == 0 && kindleHighWaterPermille(state, BOOK) == 0);
  CHECK(state.kindlePendingNoticeMinutes == 0);
  CHECK(validateExtrasState(state));
}

}  // namespace

int main() {
  fractionsFloorToPermille();
  deltaBecomesPagesThenMinutesAtFullRate();
  roundingIsToTheNearestPageAndMinute();
  smallOrBackwardMovesEarnNothing();
  unknownInputsEarnNothing();
  paceFallsBackFromBookToGlobalToDefault();
  repeatSyncEarnsNothing();
  jumpingBackAndForwardEarnsNothing();
  localReadingThenSyncCreditsOnlyTheRest();
  marksArePerDocument();
  leastRecentlyUsedMarkIsDroppedWhenFull();
  oneSyncCreditsAtMostTheGuard();
  minutesAreCreditedInChunks();
  fullQueueTurnsTheRestIntoXpOnly();
  noticeAddsUpAndIsShownOnce();
  toggleOffChangesNothing();
  resetClearsMarksAndNotice();
  if (failures != 0) {
    std::fprintf(stderr, "%d check(s) failed\n", failures);
    return 1;
  }
  std::puts("PokemonExtrasKindleTest: all checks passed");
  return 0;
}
