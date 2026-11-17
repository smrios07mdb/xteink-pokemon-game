#if defined(CROSSINK_ENABLE_POKEMON) && defined(CROSSINK_POKEMON_EXTRAS)

// Extras M2, M3 and M4 (docs/extras/M2-HOOKS.md, M3-HOOKS.md, M4-HOOKS.md):
// the PokemonService members that tie the pure rules in
// lib/Pokemon/PokemonExtras{Rules,Eggs,Quests,Kindle,Boss} to the two stores. Kept out of
// PokemonService.cpp so upstream merges touch as little as possible there.
//
// Hard limits (docs/extras/SAVE-SAFETY.md):
//   R3  the main save changes only through awardFixedXp(), an ordinary
//       wild Encounter (streak milestone, book boss), a hatch appended like a
//       catch, quest items and the boss's balls put in the bag the way
//       upstream's own drops are, and synced reading through creditMinutes();
//   R4  reading statistics are only read (PokemonExtrasHooks);
//   R5  everything here is best effort: a missing, corrupt or unwritable
//       extras file never fails a main-save operation;
//   R6  main save first, then extras. The exceptions record their intent in
//       extras first, so a crash loses and never duplicates: the milestone
//       encounter, the egg hatch, quest rewards, the Kindle mark and the book
//       boss (see each below).

#include <Logging.h>

#include <algorithm>
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
  // M4: the open book's Kindle mark, and a boss still held for delivery.
  const bool markOn = extrasOpenBookHash_ != 0 && extrasStore.isFeatureEnabled(extras::Feature::KindleCredit);
  const bool bossWaiting =
      extrasStore.isFeatureEnabled(extras::Feature::BookBoss) && extras::hasHeldBoss(extrasStore.state());
  // M5: Battle Passes accrue once the Champion is beaten (checked below).
  const bool postGameOn = extrasStore.isFeatureEnabled(extras::Feature::PostGame);
  // Nothing to do: no stats read, no extras write, nothing in the main save.
  if (!streaksOn && !eggsOn && !questsOn && !markOn && !bossWaiting && !postGameOn &&
      extrasStore.state().dayCareRecordId == 0) {
    return;
  }

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

  // M4, Kindle credit: pages read here raise the open book's mark (only
  // upward; noteLocalProgress() does nothing with the toggle off), so a later
  // sync never credits them again. No write unless the mark moved.
  if (markOn) extras::noteLocalProgress(*copy, extrasOpenBookHash_, extrasOpenBookPermille_);

  // M5: Battle Passes from lifetime minutes (read only). Nothing changes
  // before the Champion is beaten, so no write until then.
  if (postGameOn && store_.loadState(mainState)) extras::accrueBattlePasses(*copy, mainState);

  if (*copy != extrasStore.state() && !extrasStore.commit(*copy)) {
    LOG_ERR("PokemonService", "Failed to save extras after reading credit");
  }
  // The RAM page counter is cleared only once its pages are in the file.
  if (eggPagesCounted > 0 && *copy == extrasStore.state()) {
    extrasEggPagesPending_ = extrasEggPagesPending_ > pagesToFlush ? extrasEggPagesPending_ - pagesToFlush : 0;
  }
  // M4, book boss: a boss held because the queue was full goes in now if
  // there is room. After the commit above: it changes the extras file itself.
  if (bossWaiting) deliverHeldExtrasBoss();
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

// --- M4 -------------------------------------------------------------------

void PokemonService::setExtrasOpenBook(const uint32_t bookHash, const uint16_t permille) {
  extrasOpenBookHash_ = bookHash;
  extrasOpenBookPermille_ = permille > 1000 ? 1000 : permille;
}

uint16_t PokemonService::creditRemoteReading(const extras::KindleSync& sync) {
  PokemonExtrasStore& extrasStore = devicePokemonExtrasStore();
  if (sync.documentHash == 0 || extrasStore.readOnly() ||
      !extrasStore.isFeatureEnabled(extras::Feature::KindleCredit)) {
    return 0;
  }
  PokemonState mainState{};
  if (loadReadyState(mainState) != ServiceStatus::Ok || mainState.partyRecordIds[0] == 0) return 0;
  settleExtrasPendingHatch();

  // 1. The mark, in extras, BEFORE any minute reaches the main save: a crash
  // after this loses this sync's minutes and can never credit them twice.
  extras::KindleCredit credit{};
  {
    const StatePtr copy = copyExtrasState(extrasStore);
    if (!copy) return 0;
    credit = extras::applyKindleSync(*copy, sync);
    if (*copy != extrasStore.state() && !extrasStore.commit(*copy)) {
      LOG_ERR("PokemonService", "Failed to save the Kindle mark; nothing credited");
      return 0;
    }
  }
  if (credit.droppedMinutes != 0) {
    LOG_INF("PokemonService", "Kindle credit: guard dropped %u minutes (book %08lx)",
            static_cast<unsigned>(credit.droppedMinutes), static_cast<unsigned long>(sync.documentHash));
  }
  if (credit.minutes == 0) return 0;

  // 2. The minutes, through the normal reading path in chunks of at most an
  // hour, so the hourly rolls fire as they would have while reading. Queue
  // full: the rest is XP only, for the lead.
  const auto remotePercent = static_cast<uint8_t>(std::min<uint32_t>(100U, (sync.remotePermille + 5U) / 10U));
  uint16_t remaining = credit.minutes;
  uint16_t credited = 0;
  while (remaining > 0) {
    if (!store_.loadState(mainState) || mainState.partyRecordIds[0] == 0) break;
    const extras::KindleStep step = extras::nextKindleStep(remaining, extras::hasEncounterRoom(mainState));
    if (step.creditMinutes != 0) {
      if (!creditMinutes(step.creditMinutes, remotePercent)) break;
      remaining = static_cast<uint16_t>(remaining - step.creditMinutes);
      credited = static_cast<uint16_t>(credited + step.creditMinutes);
      continue;
    }
    if (step.xpOnlyMinutes != 0 && awardFixedXp(mainState.partyRecordIds[0], step.xpOnlyMinutes) == ServiceStatus::Ok) {
      credited = static_cast<uint16_t>(credited + step.xpOnlyMinutes);
    }
    break;
  }
  if (credited != credit.minutes) {
    LOG_ERR("PokemonService", "Kindle credit: %u of %u minutes went through", static_cast<unsigned>(credited),
            static_cast<unsigned>(credit.minutes));
  }

  // 3. Egg pages (and the egg-pages quest) and the notice, after the main save.
  const StatePtr after = copyExtrasState(extrasStore);
  if (!after) return credited;
  if (credit.pages > 0 && extrasStore.isFeatureEnabled(extras::Feature::Eggs)) {
    const uint16_t counted = extras::applyEggPages(*after, credit.pages);
    if (extrasStore.isFeatureEnabled(extras::Feature::DailyQuests)) extras::recordQuestEggPages(*after, counted);
  }
  extras::addKindleNotice(*after, credited);
  if (*after != extrasStore.state() && !extrasStore.commit(*after)) {
    LOG_ERR("PokemonService", "Failed to save egg pages and the notice after a Kindle credit");
  }
  return credited;
}

extras::BookFinishResult PokemonService::onExtrasBookFinished(const extras::BookFinish& book) {
  PokemonExtrasStore& extrasStore = devicePokemonExtrasStore();
  if (book.bookHash == 0 || extrasStore.readOnly() || !extrasStore.isFeatureEnabled(extras::Feature::BookBoss)) {
    return extras::BookFinishResult::Ignored;
  }
  PokemonState mainState{};
  if (loadReadyState(mainState) != ServiceStatus::Ok || mainState.partyRecordIds[0] == 0) {
    return extras::BookFinishResult::Ignored;  // no game yet: nothing to count toward
  }
  settleExtrasPendingHatch();
  const StatePtr copy = copyExtrasState(extrasStore);
  if (!copy) return extras::BookFinishResult::Ignored;
  const extras::BookFinishResult result = extras::onBookFinished(*copy, mainState, book);
  if (result == extras::BookFinishResult::Ignored || result == extras::BookFinishResult::AlreadyCounted) return result;
  if (!extrasStore.commit(*copy)) {
    LOG_ERR("PokemonService", "Failed to save a finished book");
    return extras::BookFinishResult::Ignored;
  }
  if (result == extras::BookFinishResult::BossHeld) {
    extrasBossNoticePending_ = true;
    deliverHeldExtrasBoss();
  }
  return result;
}

bool PokemonService::deliverHeldExtrasBoss() {
  PokemonExtrasStore& extrasStore = devicePokemonExtrasStore();
  if (extrasStore.readOnly() || !extrasStore.isFeatureEnabled(extras::Feature::BookBoss) ||
      !extras::hasHeldBoss(extrasStore.state())) {
    return false;
  }
  PokemonState state{};
  if (loadReadyState(state) != ServiceStatus::Ok || state.partyRecordIds[0] == 0 ||
      !extras::hasEncounterRoom(state)) {
    return false;  // the boss stays held for the next checkpoint or menu visit
  }
  PokemonRecord leader{};
  if (!store_.readRecord(state.partyRecordIds[0], leader)) return false;

  // 1. Taken, in extras. A crash after this loses this boss, never queues two.
  const StatePtr copy = copyExtrasState(extrasStore);
  if (!copy) return false;
  const uint16_t speciesId = extras::takeHeldBoss(*copy);
  if (!extrasStore.commit(*copy)) {
    LOG_ERR("PokemonService", "Failed to save the boss delivery");
    return false;
  }

  // 2. An ordinary wild Encounter and its balls, in the main save (R2, R3).
  const size_t pendingBefore = pendingEventCount(state);
  extras::BossReward reward{};
  bool queued = extras::deliverBoss(state, speciesId, levelForXp(leader.totalXp), random_, reward);
  if (queued) {
    // Shininess as creditMinutes() rolls it, on the Encounter just queued.
    const size_t pendingCount = pendingEventCount(state);
    for (size_t index = pendingBefore; index < pendingCount && random_.below != nullptr; ++index) {
      PendingEvent& queuedEvent = state.pendingEvents[index];
      if (queuedEvent.kind == PendingEventKind::Encounter) {
        queuedEvent.isShiny = random_.below(random_.context, SHINY_CHANCE_DENOMINATOR) == 0;
      }
    }
    queued = store_.commit(state);
  }
  if (queued) return true;

  // 3. The main save did not take it: hold the boss again.
  LOG_ERR("PokemonService", "Failed to queue the book boss");
  if (!extras::holdBoss(*copy, speciesId) || !extrasStore.commit(*copy)) {
    LOG_ERR("PokemonService", "Failed to hold the book boss again");
  }
  return false;
}

bool PokemonService::takeExtrasBossNotice() {
  const bool pending = extrasBossNoticePending_;
  extrasBossNoticePending_ = false;
  return pending;
}

uint16_t PokemonService::takeExtrasKindleNotice() {
  PokemonExtrasStore& extrasStore = devicePokemonExtrasStore();
  if (extrasStore.readOnly() || extrasStore.state().kindlePendingNoticeMinutes == 0) return 0;
  const StatePtr copy = copyExtrasState(extrasStore);
  if (!copy) return 0;
  const uint16_t minutes = extras::takeKindleNotice(*copy);
  if (!extrasStore.commit(*copy)) return 0;  // shown on a later visit
  return minutes;
}

bool PokemonService::readDayCareRecord(PokemonRecord& output) {
  const uint32_t recordId = devicePokemonExtrasStore().state().dayCareRecordId;
  return recordId != 0 && readRecord(recordId, output) == ServiceStatus::Ok;
}

// --- M5 -------------------------------------------------------------------

ServiceStatus PokemonService::beginExtrasRematch(const uint8_t gymIndex) {
  PokemonExtrasStore& extrasStore = devicePokemonExtrasStore();
  if (extrasStore.readOnly()) return ServiceStatus::StorageError;
  PokemonState mainState{};
  if (!store_.loadState(mainState)) return ServiceStatus::StorageError;
  if (!extras::canRematch(extrasStore.state(), mainState, gymIndex)) return ServiceStatus::NotApplicable;
  const StatePtr copy = copyExtrasState(extrasStore);
  if (!copy || !extras::spendBattlePass(*copy)) return ServiceStatus::StorageError;
  if (!extrasStore.commit(*copy)) {
    LOG_ERR("PokemonService", "Failed to spend a Battle Pass");
    return ServiceStatus::StorageError;
  }
  return ServiceStatus::Ok;
}

void PokemonService::noteExtrasRematchWin(const uint8_t gymIndex) {
  PokemonExtrasStore& extrasStore = devicePokemonExtrasStore();
  if (extrasStore.readOnly()) return;
  const StatePtr copy = copyExtrasState(extrasStore);
  if (copy && extras::recordRematchWin(*copy, gymIndex) && !extrasStore.commit(*copy)) {
    LOG_ERR("PokemonService", "Failed to save the rematch win");
  }
  applyExtrasQuestEvent(extras::recordQuestBattleWin, /*trainer=*/true);
}

ServiceStatus PokemonService::beginExtrasTowerRun() {
  PokemonExtrasStore& extrasStore = devicePokemonExtrasStore();
  if (extrasStore.readOnly()) return ServiceStatus::StorageError;
  PokemonState mainState{};
  if (!store_.loadState(mainState)) return ServiceStatus::StorageError;
  const StatePtr copy = copyExtrasState(extrasStore);
  if (!copy) return ServiceStatus::StorageError;
  if (!extras::startTowerRun(*copy, mainState)) return ServiceStatus::NotApplicable;
  if (!extrasStore.commit(*copy)) {
    LOG_ERR("PokemonService", "Failed to start a Battle Tower run");
    return ServiceStatus::StorageError;
  }
  return ServiceStatus::Ok;
}

ServiceStatus PokemonService::noteExtrasTowerWin(extras::TowerWin& win, uint8_t& rewardItemId) {
  rewardItemId = 0;
  PokemonExtrasStore& extrasStore = devicePokemonExtrasStore();
  if (extrasStore.readOnly()) return ServiceStatus::StorageError;
  const StatePtr copy = copyExtrasState(extrasStore);
  if (!copy) return ServiceStatus::StorageError;
  if (!extras::recordTowerWin(*copy, win)) return ServiceStatus::NotApplicable;
  if (!extrasStore.commit(*copy)) {
    LOG_ERR("PokemonService", "Failed to save the Battle Tower win");
    return ServiceStatus::StorageError;
  }
  // Extras first, then the item (lose rather than duplicate, R6).
  if (win.rewardDue && random_.below != nullptr) {
    PokemonState state{};
    if (loadReadyState(state) == ServiceStatus::Ok) {
      OwnedEvolutionNeeds needs{};
      if (!store_.loadOwnedEvolutionNeeds(needs)) needs = {};
      if (!grantTrainerReward(state, win.rewardLevel, needs, random_, rewardItemId) || !store_.commit(state)) {
        LOG_ERR("PokemonService", "Failed to grant the Battle Tower reward");
        rewardItemId = 0;
      }
    }
  }
  applyExtrasQuestEvent(extras::recordQuestBattleWin, /*trainer=*/true);
  healExtrasTowerParty();
  return ServiceStatus::Ok;
}

ServiceStatus PokemonService::endExtrasTowerRun(uint16_t& wins) {
  PokemonExtrasStore& extrasStore = devicePokemonExtrasStore();
  wins = extrasStore.state().towerRunWins;
  if (!extras::towerRunActive(extrasStore.state())) return ServiceStatus::NotApplicable;
  if (extrasStore.readOnly()) return ServiceStatus::StorageError;
  const StatePtr copy = copyExtrasState(extrasStore);
  if (!copy) return ServiceStatus::StorageError;
  extras::endTowerRun(*copy);
  if (!extrasStore.commit(*copy)) {
    LOG_ERR("PokemonService", "Failed to end the Battle Tower run");
    return ServiceStatus::StorageError;
  }
  return ServiceStatus::Ok;
}

void PokemonService::healExtrasTowerParty() {
  PokemonState state{};
  if (!store_.loadState(state)) return;
  std::array<BattleRecordEntry, PARTY_SIZE> healedEntries{};
  size_t healedCount = 0;
  std::array<PokemonRecord, PARTY_SIZE> partyRecords{};
  store_.readRecords(std::span<const uint32_t>(state.partyRecordIds.data(), PARTY_SIZE),
                     std::span<PokemonRecord>(partyRecords.data(), PARTY_SIZE));
  for (size_t slot = 0; slot < PARTY_SIZE; ++slot) {
    const uint32_t recordId = state.partyRecordIds[slot];
    if (recordId == 0) continue;
    const BattleRecordEntry* existing = battleStore_.findEntry(recordId);
    if (existing == nullptr) continue;  // never fought: synthesizes at full HP/PP
    const PokemonRecord& record = partyRecords[slot];
    if (record.recordId != recordId) continue;
    const BaseStats* stats = baseStatsFor(record.speciesId);
    if (stats == nullptr) continue;
    const IvEvEntry ivEv = ensureIvEv(recordId);
    constexpr size_t hpIndex = static_cast<size_t>(StatIndex::Hp);
    BattleRecordEntry healed = *existing;
    healed.currentHp = battleMaxHp(stats->hp, levelForXp(record.totalXp), ivEv.iv[hpIndex], ivEv.ev[hpIndex]);
    for (size_t moveSlot = 0; moveSlot < BATTLE_MOVE_SLOTS; ++moveSlot) {
      if (healed.moves[moveSlot] == 0) continue;
      const MoveData* move = moveData(healed.moves[moveSlot]);
      if (move != nullptr) healed.pp[moveSlot] = maxPpFor(move->pp, healed.ppUp[moveSlot]);
    }
    healed.status = Ailment::None;
    healed.statusTurns = 0;
    healed.toxicCounter = 0;
    if (healed == *existing) continue;
    healedEntries[healedCount++] = healed;
  }
  if (healedCount > 0) {
    battleStore_.upsertEntries(std::span<const BattleRecordEntry>(healedEntries.data(), healedCount));
  }
}

}  // namespace pokemon

#endif
