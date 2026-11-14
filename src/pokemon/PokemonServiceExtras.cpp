#if defined(CROSSINK_ENABLE_POKEMON) && defined(CROSSINK_POKEMON_EXTRAS)

// Extras M2 and M3 (docs/extras/M2-HOOKS.md, M3-HOOKS.md): the PokemonService
// members that tie the pure rules in lib/Pokemon/PokemonExtras{Rules,Eggs,
// Quests} to the two stores. Kept out of
// PokemonService.cpp so upstream merges touch as little as possible there.
//
// Hard limits (docs/extras/SAVE-SAFETY.md):
//   R3  the main save changes only through awardFixedXp(), an ordinary
//       wild Encounter, a hatch appended like a catch and quest items put in
//       the bag the way upstream's own drops are;
//   R4  reading statistics are only read (PokemonExtrasHooks);
//   R5  everything here is best effort: a missing, corrupt or unwritable
//       extras file never fails a main-save operation;
//   R6  main save first, then extras. The exceptions record their intent in
//       extras first, so a crash loses and never duplicates: the milestone
//       encounter, the egg hatch and quest rewards (see each below).

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

void PokemonService::applyExtrasAfterCredit(const uint16_t minutes, const uint8_t bookProgressPercent,
                                            const uint32_t lifetimeMinutesBefore, const uint8_t leaderLevelBefore) {
  PokemonExtrasStore& extrasStore = devicePokemonExtrasStore();
  if (extrasStore.readOnly()) return;
  settleExtrasPendingHatch();
  const bool streaksOn = extrasStore.isFeatureEnabled(extras::Feature::Streaks);
  const bool eggsOn = extrasStore.isFeatureEnabled(extras::Feature::Eggs);
  const bool questsOn = extrasStore.isFeatureEnabled(extras::Feature::DailyQuests);
  // Page turns with Eggs off never count toward a later egg.
  if (!eggsOn) extrasEggPagesPending_ = 0;
  // Nothing to do: no stats read, no extras write, nothing in the main save.
  if (!streaksOn && !eggsOn && !questsOn && extrasStore.state().dayCareRecordId == 0) return;

  const StatePtr copy = copyExtrasState(extrasStore);
  if (!copy) {
    LOG_ERR("PokemonService", "Out of memory applying extras");
    return;
  }

  PokemonState mainState{};
  if (!store_.loadState(mainState) || mainState.partyRecordIds[0] == 0) return;
  const uint32_t leaderId = mainState.partyRecordIds[0];
  const extras::StreakInput streak = streaksOn ? readExtrasStreakInput() : extras::StreakInput{};
  // The XP granted below must not write extras on its own (grantXp()).
  extrasCreditActive_ = true;
  struct CreditFlagReset {
    bool& flag;
    ~CreditFlagReset() { flag = false; }
  } creditFlagReset{extrasCreditActive_};

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

  // M3. Egg pages (M3-HOOKS section 3): the forward turns since the last
  // checkpoint, counted toward the held egg. Pages past 0 are dropped.
  const uint32_t pagesToFlush = extrasEggPagesPending_;
  uint16_t eggPagesCounted = 0;
  if (pagesToFlush > 0) {
    eggPagesCounted = extras::applyEggPages(*copy, pagesToFlush);
    if (eggPagesCounted == 0) extrasEggPagesPending_ -= pagesToFlush;  // no egg to count them toward
  }

  // Egg find: one roll per hour of lifetime reading crossed. Extras only.
  if (eggsOn && store_.loadState(mainState)) {
    extras::rollEggFind(*copy, mainState,
                        extras::eggFindBoundariesCrossed(lifetimeMinutesBefore, mainState.lifetimeMinutes));
  }

  // Daily quests: rotation, reading minutes, egg pages, the lead's level-up.
  if (questsOn) {
    const StatePtr beforeQuests(new (std::nothrow) extras::ExtrasState(*copy));
    if (beforeQuests) {
      PokemonRecord leaderAfter{};
      const bool leveledUp =
          store_.readRecord(leaderId, leaderAfter) && levelForXp(leaderAfter.totalXp) > leaderLevelBefore;
      extras::QuestRewards expired{};
      extras::updateDailyQuests(*copy, readExtrasQuestClock(), extras::hasEgg(*copy), minutes, expired);
      extras::recordQuestEggPages(*copy, eggPagesCounted);
      if (leveledUp) extras::recordQuestLevelUp(*copy);
      // An expired set's ready rewards: extras first, then the bag.
      if (expired.count > 0 && !commitExtrasWithQuestItems(*beforeQuests, *copy, expired)) {
        LOG_ERR("PokemonService", "Failed to grant the rewards of the expired daily quests");
        *copy = *beforeQuests;  // rotation retried at the next checkpoint
      }
    }
  }

  if (*copy != extrasStore.state() && !extrasStore.commit(*copy)) {
    LOG_ERR("PokemonService", "Failed to save extras after reading credit");
  }
  // The RAM page counter is cleared only once its pages are in the file.
  if (eggPagesCounted > 0 && *copy == extrasStore.state()) {
    extrasEggPagesPending_ = extrasEggPagesPending_ > pagesToFlush ? extrasEggPagesPending_ - pagesToFlush : 0;
  }
}

void PokemonService::settleExtrasPendingHatch() {
  PokemonExtrasStore& extrasStore = devicePokemonExtrasStore();
  const uint32_t pendingId = extrasStore.state().pendingHatchRecordId;
  if (pendingId == 0 || extrasStore.readOnly() || !store_.isReady()) return;
  PokemonRecord record{};
  const RecordLookup lookup = lookupRecord(store_, pendingId, record);
  if (lookup == RecordLookup::ReadFailed) return;  // unsure: try again next time, never guess
  const StatePtr copy = copyExtrasState(extrasStore);
  if (!copy) return;
  const extras::PendingHatch outcome =
      extras::resolvePendingHatch(*copy, lookup == RecordLookup::Found ? &record : nullptr);
  if (outcome != extras::PendingHatch::None && !extrasStore.commit(*copy)) {
    LOG_ERR("PokemonService", "Failed to settle a pending egg hatch");
  }
}

void PokemonService::applyExtrasQuestEvent(uint8_t (*event)(extras::ExtrasState&, bool), const bool argument) {
  PokemonExtrasStore& extrasStore = devicePokemonExtrasStore();
  if (extrasStore.readOnly() || !extrasStore.isFeatureEnabled(extras::Feature::DailyQuests)) return;
  settleExtrasPendingHatch();
  if (!extras::hasQuestSet(extrasStore.state())) return;
  const StatePtr copy = copyExtrasState(extrasStore);
  if (!copy) return;
  if (event(*copy, argument) != 0 && !extrasStore.commit(*copy)) {
    LOG_ERR("PokemonService", "Failed to save daily quest progress");
  }
}

bool PokemonService::commitExtrasWithQuestItems(const extras::ExtrasState& before, const extras::ExtrasState& after,
                                                const extras::QuestRewards& items) {
  PokemonExtrasStore& extrasStore = devicePokemonExtrasStore();
  if (!extrasStore.commit(after)) return false;
  if (items.count == 0) return true;
  PokemonState state{};
  bool granted = false;
  bool ok = store_.loadState(state);
  if (ok) {
    for (uint8_t index = 0; index < items.count; ++index) {
      // A full count drops that one item (grantQuestItem() leaves it out).
      granted = extras::grantQuestItem(state, items.itemIds[index]) || granted;
    }
    ok = !granted || store_.commit(state);
  }
  if (ok) return true;
  // The bag write failed: put the rewards back. A crash before this line
  // loses them; nothing can give them twice.
  if (!extrasStore.commit(before)) LOG_ERR("PokemonService", "Failed to restore daily quest rewards");
  return false;
}

void PokemonService::countExtrasEggPage() {
  if (!readingSessionActive_ || extrasEggPagesPending_ >= extras::EGG_MAX_PAGES) return;
  ++extrasEggPagesPending_;
}

void PokemonService::noteWildBattleWon() { applyExtrasQuestEvent(extras::recordQuestBattleWin, /*trainer=*/false); }

ServiceStatus PokemonService::refreshDailyQuests() {
  PokemonExtrasStore& extrasStore = devicePokemonExtrasStore();
  if (extrasStore.readOnly() || !extrasStore.isFeatureEnabled(extras::Feature::DailyQuests)) {
    return ServiceStatus::NotApplicable;
  }
  settleExtrasPendingHatch();
  const StatePtr before = copyExtrasState(extrasStore);
  const StatePtr copy = copyExtrasState(extrasStore);
  if (!before || !copy) return ServiceStatus::StorageError;
  extras::QuestRewards expired{};
  if (!extras::updateDailyQuests(*copy, readExtrasQuestClock(), extras::hasEgg(*copy), 0, expired)) {
    return ServiceStatus::Ok;
  }
  return commitExtrasWithQuestItems(*before, *copy, expired) ? ServiceStatus::Ok : ServiceStatus::StorageError;
}

ServiceStatus PokemonService::claimQuestRewards(extras::QuestRewards& granted) {
  granted = {};
  PokemonExtrasStore& extrasStore = devicePokemonExtrasStore();
  if (extrasStore.readOnly()) return ServiceStatus::NotApplicable;
  settleExtrasPendingHatch();
  if (extras::questRewardsReadyMask(extrasStore.state()) == 0) return ServiceStatus::NotApplicable;
  PokemonState state{};
  const ServiceStatus stateStatus = loadReadyState(state);
  if (stateStatus != ServiceStatus::Ok) return stateStatus;
  const StatePtr before = copyExtrasState(extrasStore);
  const StatePtr copy = copyExtrasState(extrasStore);
  if (!before || !copy) return ServiceStatus::StorageError;
  const extras::QuestRewards rewards = extras::takeQuestRewards(*copy);
  if (rewards.count == 0) return ServiceStatus::NotApplicable;
  if (!commitExtrasWithQuestItems(*before, *copy, rewards)) {
    LOG_ERR("PokemonService", "Failed to claim daily quest rewards");
    return ServiceStatus::StorageError;
  }
  granted = rewards;
  return ServiceStatus::Ok;
}

ServiceStatus PokemonService::hatchReadyEgg(uint32_t& recordId, uint16_t& speciesId) {
  recordId = 0;
  speciesId = 0;
  PokemonState state{};
  const ServiceStatus stateStatus = loadReadyState(state);
  if (stateStatus != ServiceStatus::Ok) return stateStatus;
  PokemonExtrasStore& extrasStore = devicePokemonExtrasStore();
  if (extrasStore.readOnly()) return ServiceStatus::NotApplicable;
  settleExtrasPendingHatch();
  if (!extrasStore.isFeatureEnabled(extras::Feature::Eggs) || !extras::isEggReadyToHatch(extrasStore.state())) {
    return ServiceStatus::NotApplicable;
  }
  // Same room rule as a catch: the egg waits at 0 pages while both are full.
  uint8_t partyCount = 0;
  for (const uint32_t partyRecordId : state.partyRecordIds) {
    if (partyRecordId != 0) ++partyCount;
  }
  if (store_.recordCount() >= 1024U || catchBlockedByFullBox(partyCount, store_.recordCount())) {
    return ServiceStatus::BoxFull;
  }

  const StatePtr copy = copyExtrasState(extrasStore);
  if (!copy) return ServiceStatus::StorageError;
  const uint16_t eggSpecies = copy->eggSpeciesId;
  const bool shiny = (copy->eggFlags & extras::EGG_FLAG_SHINY) != 0;
  const bool newToPokedex = !isSpeciesMarked(state.caughtSpecies, eggSpecies);

  // 1. Intent, in extras.
  const uint32_t newId = store_.nextRecordId();
  if (!extras::beginHatch(*copy, newId)) return ServiceStatus::NotApplicable;
  if (!extrasStore.commit(*copy)) {
    LOG_ERR("PokemonService", "Failed to save the egg hatch intent");
    return ServiceStatus::StorageError;
  }

  // 2. The record, appended to the main save exactly like a catch (R3).
  Gender gender = Gender::Unknown;
  chooseGenderForSpecies(eggSpecies, random_, gender);
  RecordMutation mutation{};
  if (!extras::buildHatchedRecord(*copy, state, newId, gender, "", mutation) || !store_.commit(state, mutation)) {
    LOG_ERR("PokemonService", "Failed to append the hatched Pokemon");
    extras::resolvePendingHatch(*copy, nullptr);  // the egg stays ready
    if (!extrasStore.commit(*copy)) LOG_ERR("PokemonService", "Failed to clear the egg hatch intent");
    return ServiceStatus::StorageError;
  }
  // Stale side-store entries under a reused id, as resolveEncounter() drops them.
  if (battleStore_.findEntry(newId) != nullptr) battleStore_.removeEntry(newId);
  if (movesetStore_.findEntry(newId) != nullptr) movesetStore_.removeEntry(newId);
  if (ivEvStore_.findEntry(newId) != nullptr) ivEvStore_.removeEntry(newId);
  clearPendingIvEvRoll(newId);
  // Egg IVs before anything calls ensureIvEv() for this id, so the floor sticks.
  IvEvEntry ivEv{};
  ivEv.recordId = newId;
  extras::rollEggIvs(random_, shiny, ivEv.iv);
  if (!ivEvStore_.upsertEntry(ivEv)) {
    LOG_ERR("PokemonService", "Failed to persist hatched IVs for record %u", newId);
    cachePendingIvEvRoll(ivEv);  // ensureIvEv() retries with these, not a new roll
  }

  // 3. Done, in extras. A failure here is settled on the next extras use.
  extras::finishHatch(*copy);
  extras::recordQuestCatch(*copy, newToPokedex);
  if (!extrasStore.commit(*copy)) LOG_ERR("PokemonService", "Failed to clear the hatched egg");
  recordId = newId;
  speciesId = eggSpecies;
  return ServiceStatus::Ok;
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
