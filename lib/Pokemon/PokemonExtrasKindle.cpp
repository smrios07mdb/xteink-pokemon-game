#if defined(CROSSINK_ENABLE_POKEMON) && defined(CROSSINK_POKEMON_EXTRAS)

#include "PokemonExtrasKindle.h"

#include <algorithm>

namespace pokemon::extras {
namespace {

constexpr uint32_t SECONDS_PER_MINUTE = 60;
// Bounds the arithmetic; no book on this reader comes near it.
constexpr uint32_t BOOK_PAGES_MAX = 200000;

// Index of the document's mark, or kindleMarkCount when it has none.
size_t findMark(const ExtrasState& state, const uint32_t documentHash) {
  for (size_t index = 0; index < state.kindleMarkCount; ++index) {
    if (state.kindleMarks[index].documentHash == documentHash) return index;
  }
  return state.kindleMarkCount;
}

uint16_t clampPermille(const uint16_t permille) { return std::min<uint16_t>(permille, PERMILLE); }

// Raw (unguarded) result of one sync.
struct RawCredit {
  uint32_t pages = 0;
  uint32_t minutes = 0;
};

RawCredit rawCredit(const ExtrasState& state, const KindleSync& sync) {
  RawCredit raw{};
  if (!isFeatureEnabled(state, Feature::KindleCredit) || sync.documentHash == 0 || sync.bookPages == 0) return raw;
  const uint16_t remote = clampPermille(sync.remotePermille);
  const uint16_t base =
      std::max(clampPermille(sync.localPermille), kindleHighWaterPermille(state, sync.documentHash));
  if (remote < base || remote - base < KINDLE_MIN_DELTA_PERMILLE) return raw;

  const uint64_t delta = static_cast<uint64_t>(remote - base);
  const uint64_t bookPages = std::min<uint32_t>(sync.bookPages, BOOK_PAGES_MAX);
  // Rounded to the nearest page and the nearest minute.
  const uint64_t pages = (delta * bookPages + PERMILLE / 2U) / PERMILLE;
  const uint64_t seconds = pages * kindleSecondsPerPage(sync.pace);
  raw.pages = static_cast<uint32_t>(pages);
  raw.minutes = static_cast<uint32_t>((seconds + SECONDS_PER_MINUTE / 2U) / SECONDS_PER_MINUTE);
  return raw;
}

}  // namespace

// --- Positions -------------------------------------------------------------

uint16_t fractionToPermille(const float fraction) {
  if (!(fraction > 0.0F)) return 0;  // also catches NaN
  if (fraction >= 1.0F) return PERMILLE;
  return static_cast<uint16_t>(fraction * static_cast<float>(PERMILLE));
}

// --- High-water marks ------------------------------------------------------

uint16_t kindleHighWaterPermille(const ExtrasState& state, const uint32_t documentHash) {
  if (documentHash == 0) return 0;
  const size_t index = findMark(state, documentHash);
  return index < state.kindleMarkCount ? state.kindleMarks[index].highWaterPermille : 0;
}

bool raiseKindleMark(ExtrasState& state, const uint32_t documentHash, const uint16_t permille) {
  if (documentHash == 0 || state.kindleMarkCount > KINDLE_MARK_CAPACITY) return false;
  const ExtrasState before = state;
  size_t index = findMark(state, documentHash);
  KindleMark mark{documentHash, clampPermille(permille)};
  if (index < state.kindleMarkCount) {
    mark.highWaterPermille = std::max(mark.highWaterPermille, state.kindleMarks[index].highWaterPermille);
  } else if (state.kindleMarkCount == KINDLE_MARK_CAPACITY) {
    index = 0;  // full: the least recently used mark goes
  } else {
    index = state.kindleMarkCount++;
  }
  // Close the gap at `index` and put the mark at the most-recently-used end.
  const size_t last = static_cast<size_t>(state.kindleMarkCount) - 1U;
  for (size_t slot = index; slot < last; ++slot) state.kindleMarks[slot] = state.kindleMarks[slot + 1U];
  state.kindleMarks[last] = mark;
  return !(state == before);
}

bool noteLocalProgress(ExtrasState& state, const uint32_t documentHash, const uint16_t permille) {
  if (!isFeatureEnabled(state, Feature::KindleCredit)) return false;
  return raiseKindleMark(state, documentHash, permille);
}

// --- Pace ------------------------------------------------------------------

uint16_t kindleSecondsPerPage(const KindlePace& pace) {
  uint32_t seconds = pace.bookSecondsPerPage;
  if (seconds == 0 && pace.globalPagesTurned != 0 && pace.globalReadingSeconds != 0) {
    seconds = (pace.globalReadingSeconds + pace.globalPagesTurned / 2U) / pace.globalPagesTurned;
    if (seconds == 0) seconds = 1;  // measured, just very fast: clamp below
  }
  if (seconds == 0) return KINDLE_DEFAULT_SECONDS_PER_PAGE;
  return static_cast<uint16_t>(std::clamp<uint32_t>(seconds, KINDLE_PACE_MIN_SECONDS, KINDLE_PACE_MAX_SECONDS));
}

// --- A sync was applied ----------------------------------------------------

KindleCredit planKindleCredit(const ExtrasState& state, const KindleSync& sync) {
  KindleCredit credit{};
  if (isFeatureEnabled(state, Feature::KindleCredit) && sync.documentHash != 0) {
    credit.highWaterPermille =
        std::max({kindleHighWaterPermille(state, sync.documentHash), clampPermille(sync.localPermille),
                  clampPermille(sync.remotePermille)});
  } else {
    credit.highWaterPermille = kindleHighWaterPermille(state, sync.documentHash);
  }

  const RawCredit raw = rawCredit(state, sync);
  if (raw.minutes == 0) return credit;  // under half a minute: nothing to credit, no egg pages either

  uint32_t minutes = raw.minutes;
  if (KINDLE_GUARD_MINUTES != 0 && minutes > KINDLE_GUARD_MINUTES) minutes = KINDLE_GUARD_MINUTES;
  minutes = std::min<uint32_t>(minutes, UINT16_MAX);
  credit.minutes = static_cast<uint16_t>(minutes);
  credit.droppedMinutes = static_cast<uint16_t>(std::min<uint32_t>(raw.minutes - minutes, UINT16_MAX));
  // Egg pages shrink with the guard, so a mis-sync cannot hatch an egg either.
  credit.pages = static_cast<uint32_t>(static_cast<uint64_t>(raw.pages) * minutes / raw.minutes);
  return credit;
}

KindleCredit applyKindleSync(ExtrasState& state, const KindleSync& sync) {
  const KindleCredit credit = planKindleCredit(state, sync);
  if (isFeatureEnabled(state, Feature::KindleCredit) && sync.documentHash != 0) {
    raiseKindleMark(state, sync.documentHash, credit.highWaterPermille);
  }
  return credit;
}

// --- Crediting in chunks ---------------------------------------------------

KindleStep nextKindleStep(const uint16_t remainingMinutes, const bool queueHasRoom) {
  KindleStep step{};
  if (remainingMinutes == 0) return step;
  if (queueHasRoom) {
    step.creditMinutes = std::min<uint16_t>(remainingMinutes, KINDLE_CHUNK_MINUTES);
  } else {
    step.xpOnlyMinutes = remainingMinutes;
  }
  return step;
}

// --- Notice ----------------------------------------------------------------

void addKindleNotice(ExtrasState& state, const uint16_t minutes) {
  const uint32_t total = static_cast<uint32_t>(state.kindlePendingNoticeMinutes) + minutes;
  state.kindlePendingNoticeMinutes = static_cast<uint16_t>(std::min<uint32_t>(total, UINT16_MAX));
}

uint16_t takeKindleNotice(ExtrasState& state) {
  const uint16_t minutes = state.kindlePendingNoticeMinutes;
  state.kindlePendingNoticeMinutes = 0;
  return minutes;
}

}  // namespace pokemon::extras

#endif
