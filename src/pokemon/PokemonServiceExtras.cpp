#if defined(CROSSINK_ENABLE_POKEMON) && defined(CROSSINK_POKEMON_EXTRAS)

// Extras M2 (docs/extras/M2-HOOKS.md): the PokemonService members that tie the
// pure rules in lib/Pokemon/PokemonExtrasRules to the two stores. Kept out of
// PokemonService.cpp so upstream merges touch as little as possible there.
//
// Hard limits (docs/extras/SAVE-SAFETY.md):
//   R3  the main save changes only through awardFixedXp() and an ordinary
//       wild Encounter;
//   R4  reading statistics are only read (PokemonExtrasHooks);
//   R5  everything here is best effort: a missing, corrupt or unwritable
//       extras file never fails a main-save operation;
//   R6  main save first, then extras. The one exception is the milestone
//       encounter, which records its intent in extras first (see below).

#include <Logging.h>

#include <array>
#include <memory>
#include <new>

#include "PokemonExtrasHooks.h"
#include "PokemonExtrasStore.h"
#include "PokemonService.h"

namespace pokemon {
namespace {

// Must stay equal to SHINY_CHANCE_DENOMINATOR in PokemonService.cpp: the
// milestone encounter rolls shininess exactly like creditMinutes() does.
constexpr uint32_t SHINY_CHANCE_DENOMINATOR = 64;

using StatePtr = std::unique_ptr<extras::ExtrasState>;

// ~1 KB: heap, not the reader task's stack (same as PokemonExtrasStore).
StatePtr copyExtrasState(PokemonExtrasStore& extrasStore) {
  return StatePtr(new (std::nothrow) extras::ExtrasState(extrasStore.state()));
}

enum class RecordLookup : uint8_t { Found, Missing, ReadFailed };

// readRecord() cannot tell "no such record" from a failed read, and only the
// first may empty the Day Care slot.
RecordLookup lookupRecord(const PokemonStore& store, const uint32_t recordId, PokemonRecord& output) {
  const std::array<uint32_t, 1> ids{recordId};
  std::array<PokemonRecord, 1> records{};
  if (!store.readRecords(ids, records)) return RecordLookup::ReadFailed;
  if (records[0].recordId != recordId) return RecordLookup::Missing;
  output = records[0];
  return RecordLookup::Found;
}

}  // namespace

void PokemonService::applyExtrasAfterCredit(const uint16_t minutes, const uint8_t bookProgressPercent) {
  PokemonExtrasStore& extrasStore = devicePokemonExtrasStore();
  if (extrasStore.readOnly()) return;
  const bool streaksOn = extrasStore.isFeatureEnabled(extras::Feature::Streaks);
  // Nothing to do: no stats read, no extras write, nothing in the main save.
  if (!streaksOn && extrasStore.state().dayCareRecordId == 0) return;

  const StatePtr copy = copyExtrasState(extrasStore);
  if (!copy) {
    LOG_ERR("PokemonService", "Out of memory applying extras");
    return;
  }

  PokemonState mainState{};
  if (!store_.loadState(mainState) || mainState.partyRecordIds[0] == 0) return;
  const uint32_t leaderId = mainState.partyRecordIds[0];
  const extras::StreakInput streak = streaksOn ? readExtrasStreakInput() : extras::StreakInput{};

  // Streak bonus XP. Granted first; the new remainder is saved afterwards, so
  // a crash in between can lose the fraction but never grant twice.
  const uint16_t previousXpRemainder = copy->streakXpRemainderPermille;
  const uint32_t bonusXp = extras::applyStreakBonus(*copy, streak, minutes);
  if (bonusXp > 0 && awardFixedXp(leaderId, bonusXp) != ServiceStatus::Ok) {
    LOG_ERR("PokemonService", "Failed to grant streak bonus XP");
    copy->streakXpRemainderPermille = previousXpRemainder;
  }

  // Streak milestone: one rare wild encounter. The intent goes into extras
  // BEFORE the main save: a crash in between loses that one encounter, and
  // can never queue two. No room in the queue: nothing is marked and the next
  // checkpoint tries again.
  const uint16_t milestone = extras::dueStreakMilestone(*copy, streak);
  if (milestone != 0 && store_.loadState(mainState) && extras::hasEncounterRoom(mainState)) {
    const uint16_t previousMilestone = copy->lastRewardedStreakMilestone;
    const uint32_t previousMilestoneDay = copy->lastRewardedStreakDay;
    extras::markStreakMilestoneRewarded(*copy, streak, milestone);
    bool queued = false;
    if (extrasStore.commit(*copy)) {
      PokemonState state{};
      PokemonRecord leader{};
      if (store_.loadState(state) && store_.readRecord(leaderId, leader) &&
          extras::queueRareEncounter(state, levelForXp(leader.totalXp), bookProgressPercent, random_)) {
        // queueRareEncounter() appended it, so it is the last event.
        PendingEvent& encounter = state.pendingEvents[pendingEventCount(state) - 1U];
        encounter.isShiny = random_.below(random_.context, SHINY_CHANCE_DENOMINATOR) == 0;
        queued = store_.commit(state);
      }
      if (!queued) LOG_ERR("PokemonService", "Failed to queue the streak milestone encounter");
    }
    if (!queued) {
      copy->lastRewardedStreakMilestone = previousMilestone;
      copy->lastRewardedStreakDay = previousMilestoneDay;
    }
  }

  // Day Care: half the lead's reading XP, never past the lead's level.
  if (copy->dayCareRecordId != 0 && store_.loadState(mainState)) {
    PokemonRecord resident{};
    const RecordLookup lookup = lookupRecord(store_, copy->dayCareRecordId, resident);
    if (lookup == RecordLookup::Missing || extras::isInParty(mainState, copy->dayCareRecordId)) {
      extras::clearDayCare(*copy);  // stale slot, cleared silently
    } else if (lookup == RecordLookup::Found) {
      PokemonRecord leader{};
      if (store_.readRecord(leaderId, leader)) {
        const uint8_t previousMinuteRemainder = copy->dayCareMinuteRemainder;
        const uint32_t dayCareXp =
            extras::applyDayCareMinutes(*copy, minutes, resident.totalXp, levelForXp(leader.totalXp));
        if (dayCareXp > 0 && awardFixedXp(resident.recordId, dayCareXp) != ServiceStatus::Ok) {
          LOG_ERR("PokemonService", "Failed to grant Day Care XP");
          copy->dayCareMinuteRemainder = previousMinuteRemainder;
        }
      }
    }
  }

  if (*copy != extrasStore.state() && !extrasStore.commit(*copy)) {
    LOG_ERR("PokemonService", "Failed to save extras after reading credit");
  }
}

void PokemonService::clearDayCareIfHolds(const uint32_t recordId) {
  PokemonExtrasStore& extrasStore = devicePokemonExtrasStore();
  if (recordId == 0 || extrasStore.state().dayCareRecordId != recordId) return;
  const StatePtr copy = copyExtrasState(extrasStore);
  // A failure leaves a stale slot, which applyExtrasAfterCredit() and
  // sendToDayCare() both empty when they next see it.
  if (!copy || !extras::clearDayCareIfHolds(*copy, recordId) || !extrasStore.commit(*copy)) {
    LOG_ERR("PokemonService", "Failed to clear the Day Care slot");
  }
}

ServiceStatus PokemonService::sendToDayCare(const uint32_t recordId, extras::DayCareSendResult& result) {
  result = extras::DayCareSendResult::InvalidRecord;
  PokemonState state{};
  const ServiceStatus stateStatus = loadReadyState(state);
  if (stateStatus != ServiceStatus::Ok) return stateStatus;
  PokemonRecord record{};
  if (!store_.readRecord(recordId, record)) return ServiceStatus::NotFound;

  PokemonExtrasStore& extrasStore = devicePokemonExtrasStore();
  const StatePtr copy = copyExtrasState(extrasStore);
  if (!copy) return ServiceStatus::StorageError;
  if (copy->dayCareRecordId != 0 && copy->dayCareRecordId != recordId) {
    PokemonRecord resident{};
    if (lookupRecord(store_, copy->dayCareRecordId, resident) == RecordLookup::Missing ||
        extras::isInParty(state, copy->dayCareRecordId)) {
      extras::clearDayCare(*copy);
    }
  }
  result = extras::sendToDayCare(*copy, state, recordId);
  if (result != extras::DayCareSendResult::Sent) return ServiceStatus::Ok;
  if (!extrasStore.commit(*copy)) {
    LOG_ERR("PokemonService", "Failed to send a Pokemon to Day Care");
    return ServiceStatus::StorageError;
  }
  return ServiceStatus::Ok;
}

ServiceStatus PokemonService::takeFromDayCare() {
  PokemonExtrasStore& extrasStore = devicePokemonExtrasStore();
  if (extrasStore.state().dayCareRecordId == 0) return ServiceStatus::NotApplicable;
  const StatePtr copy = copyExtrasState(extrasStore);
  if (!copy) return ServiceStatus::StorageError;
  extras::clearDayCare(*copy);
  if (!extrasStore.commit(*copy)) {
    LOG_ERR("PokemonService", "Failed to take a Pokemon from Day Care");
    return ServiceStatus::StorageError;
  }
  return ServiceStatus::Ok;
}

bool PokemonService::readDayCareRecord(PokemonRecord& output) {
  const uint32_t recordId = devicePokemonExtrasStore().state().dayCareRecordId;
  return recordId != 0 && readRecord(recordId, output) == ServiceStatus::Ok;
}

}  // namespace pokemon

#endif
