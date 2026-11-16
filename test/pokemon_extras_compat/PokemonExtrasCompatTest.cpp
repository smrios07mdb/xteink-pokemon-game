// docs/extras/SAVE-SAFETY.md R10: the main save stays upstream-readable.
//
// Starts from a v11 save, runs every extras operation that exists so far
// through the host-test service and the extras store, then decodes the main
// save with the upstream codec (PokemonStoreCodec, PokemonTypes - unchanged
// in this fork, R1) and asserts that the version is still 11, the state is
// still 437 bytes, and validateState()/validateRecord() pass for every record.
//
// The fixture save is built here through the unmodified PokemonStore. To run
// the same checks on a real save (the PLAN M1 exit gate), point
// CROSSINK_EXTRAS_COMPAT_SAVE_DIR at a COPY of a `.crosspoint` folder: the
// test reads that folder's pokemon-*.bin files into the in-memory storage stub
// and never writes to disk. CROSSINK_EXTRAS_COMPAT_CHECK_ONLY=1 only decodes
// the folder's main save (no operations), for checking a save after a
// simulator session.
//
// Extend this test with every extras operation each milestone adds.

#include <HalStorage.h>

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <map>
#include <string>
#include <vector>

#include "PokemonExtras.h"
#include "PokemonExtrasBoss.h"
#include "PokemonExtrasEggs.h"
#include "PokemonExtrasKindle.h"
#include "PokemonExtrasQuests.h"
#include "PokemonExtrasRules.h"
#include "PokemonStoreCodec.h"
#include "pokemon/PokemonBattleStore.h"
#include "pokemon/PokemonExtrasHooks.h"
#include "pokemon/PokemonExtrasStore.h"
#include "pokemon/PokemonHallOfFameStore.h"
#include "pokemon/PokemonIvEvStore.h"
#include "pokemon/PokemonService.h"
#include "pokemon/PokemonStore.h"

namespace {

int failures = 0;

#define CHECK(condition)                                                                \
  do {                                                                                  \
    if (!(condition)) {                                                                 \
      std::fprintf(stderr, "%s:%d check failed: %s\n", __FILE__, __LINE__, #condition); \
      ++failures;                                                                       \
    }                                                                                   \
  } while (false)

constexpr const char* MAIN_SAVE_PATHS[] = {"/.crosspoint/pokemon-a.bin", "/.crosspoint/pokemon-b.bin"};
// Every upstream Pokemon side file (SAVE-SAFETY "What must stay untouched").
constexpr const char* UPSTREAM_SAVE_FILES[] = {
    "pokemon-a.bin",        "pokemon-b.bin",        "pokemon-moves-a.bin", "pokemon-moves-b.bin",
    "pokemon-battle-a.bin", "pokemon-battle-b.bin", "pokemon-ivev-a.bin",  "pokemon-ivev-b.bin",
    "pokemon-hof-a.bin",    "pokemon-hof-b.bin",
};
constexpr const char* EXTRAS_PATHS[] = {"/.crosspoint/pokemon-extras-a.bin", "/.crosspoint/pokemon-extras-b.bin"};

uint32_t zeroRandom(void*, uint32_t) { return 0; }
uint32_t fixedEntropy() { return 0x2468ACE1U; }

std::string devicePath(const char* name) { return std::string("/.crosspoint/") + name; }

std::vector<uint8_t> readStub(const std::string& path) {
  FsFile file = Storage.open(path.c_str(), O_RDONLY);
  if (!file) return {};
  std::vector<uint8_t> bytes(static_cast<size_t>(file.fileSize64()));
  if (!bytes.empty()) file.read(bytes.data(), bytes.size());
  file.close();
  return bytes;
}

void writeStub(const std::string& path, const std::vector<uint8_t>& bytes) {
  FsFile file = Storage.open(path.c_str(), O_WRONLY | O_CREAT | O_TRUNC);
  if (!bytes.empty()) file.write(bytes.data(), bytes.size());
  file.close();
}

std::map<std::string, std::vector<uint8_t>> snapshotUpstreamFiles() {
  std::map<std::string, std::vector<uint8_t>> files;
  for (const char* name : UPSTREAM_SAVE_FILES) {
    const std::string path = devicePath(name);
    if (Storage.exists(path.c_str())) files[path] = readStub(path);
  }
  return files;
}

uint32_t read32(const uint8_t* bytes) {
  return static_cast<uint32_t>(bytes[0]) | (static_cast<uint32_t>(bytes[1]) << 8U) |
         (static_cast<uint32_t>(bytes[2]) << 16U) | (static_cast<uint32_t>(bytes[3]) << 24U);
}

// Decodes one main-save slot with the upstream codec only (no PokemonStore).
// Returns false if the slot is absent.
bool checkMainSaveSlot(const char* path) {
  if (!Storage.exists(path)) return false;
  const std::vector<uint8_t> file = readStub(path);
  CHECK(file.size() >= pokemon::POKEMON_SNAPSHOT_HEADER_BYTES + pokemon::POKEMON_SNAPSHOT_CRC_BYTES);
  if (file.size() < pokemon::POKEMON_SNAPSHOT_HEADER_BYTES + pokemon::POKEMON_SNAPSHOT_CRC_BYTES) return true;

  pokemon::HeaderBytes headerBytes{};
  std::copy_n(file.begin(), headerBytes.size(), headerBytes.begin());
  pokemon::SnapshotHeader header{};
  CHECK(pokemon::decodeSnapshotHeader(headerBytes, header) == pokemon::HeaderDecodeResult::Ready);
  CHECK(header.version == 11);
  CHECK(pokemon::POKEMON_SNAPSHOT_VERSION == 11);
  CHECK(pokemon::snapshotStateBytes(header.version) == 437);
  CHECK(pokemon::POKEMON_STATE_BYTES == 437);
  CHECK(file.size() == pokemon::snapshotFileBytes(header));
  if (header.version != 11 || file.size() != pokemon::snapshotFileBytes(header)) return true;

  size_t offset = pokemon::POKEMON_SNAPSHOT_HEADER_BYTES;
  pokemon::PokemonState state{};
  CHECK(pokemon::decodeState(file.data() + offset, 437, header.version, state));
  CHECK(pokemon::validateState(state));
  CHECK(state.sequence == header.sequence);
  offset += 437;
  for (uint32_t index = 0; index < header.recordCount; ++index) {
    pokemon::RecordBytes recordBytes{};
    std::copy_n(file.begin() + static_cast<std::ptrdiff_t>(offset), recordBytes.size(), recordBytes.begin());
    pokemon::PokemonRecord record{};
    CHECK(pokemon::decodeRecord(recordBytes, record));
    CHECK(pokemon::validateRecord(record));
    offset += pokemon::POKEMON_RECORD_BYTES;
  }
  const uint32_t crc = pokemon::finishSnapshotCrc32(
      pokemon::updateSnapshotCrc32(pokemon::POKEMON_SNAPSHOT_CRC32_INITIAL, file.data(), offset));
  CHECK(read32(file.data() + offset) == crc);
  return true;
}

void checkMainSave() {
  int slots = 0;
  for (const char* path : MAIN_SAVE_PATHS) slots += checkMainSaveSlot(path) ? 1 : 0;
  CHECK(slots > 0);
}

// The v11 fixture: a starter plus two catches, written by the unmodified store.
void buildFixtureSave() {
  pokemon::PokemonStore store;
  CHECK(store.begin() == pokemon::StoreBeginResult::Empty);
  pokemon::PokemonState state{};
  const auto append = [&](const uint32_t recordId, const uint16_t speciesId, const pokemon::Origin origin) {
    pokemon::PokemonRecord record{};
    record.recordId = recordId;
    record.totalXp = pokemon::xpRequired(5);
    record.speciesId = speciesId;
    record.caughtLevel = 5;
    record.gender = pokemon::Gender::Female;
    record.origin = origin;
    CHECK(pokemon::markSpecies(state.seenSpecies, speciesId));
    CHECK(pokemon::markSpecies(state.caughtSpecies, speciesId));
    state.partyRecordIds[recordId - 1] = recordId;
    CHECK(store.commit(state, pokemon::RecordMutation{recordId, record, pokemon::RecordMutationKind::Append}));
  };
  append(1, 25, pokemon::Origin::Starter);
  append(2, 16, pokemon::Origin::Caught);
  append(3, 19, pokemon::Origin::Caught);
}

bool loadSaveDirectory(const char* directory) {
  bool foundMain = false;
  for (const char* name : UPSTREAM_SAVE_FILES) {
    std::ifstream input(std::string(directory) + "/" + name, std::ios::binary);
    if (!input) continue;
    const std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
    writeStub(devicePath(name), bytes);
    foundMain = foundMain || std::string(name).rfind("pokemon-a", 0) == 0 || std::string(name).rfind("pokemon-b", 0) == 0;
  }
  return foundMain;
}

// What the streak rules see in place of the firmware's reading statistics.
pokemon::extras::StreakInput testStreak{};
pokemon::extras::StreakInput testStreakSource() { return testStreak; }

size_t countEncounters(const pokemon::PokemonState& state) {
  size_t count = 0;
  for (const pokemon::PendingEvent& event : state.pendingEvents) {
    count += event.kind == pokemon::PendingEventKind::Encounter ? 1 : 0;
  }
  return count;
}

// M2 (docs/extras/M2-HOOKS.md): the streak bonus, the streak milestone
// encounter and Day Care XP, all through PokemonService::creditMinutes(), plus
// the Day Care slot being cleared on withdraw and release. `fixture` is false
// for a real save, where the party, the queue and the levels are whatever the
// player has: the checks then adapt, and nothing is released.
void runM2Operations(const bool fixture) {
  using pokemon::ServiceStatus;
  using pokemon::extras::DayCareSendResult;
  using pokemon::extras::Feature;

  pokemon::PokemonExtrasStore& extrasStore = pokemon::devicePokemonExtrasStore();
  extrasStore.load();  // the M1 block wrote the files through its own store object
  CHECK(extrasStore.setFeatureEnabled(Feature::Streaks, true));
  CHECK(extrasStore.setFeatureEnabled(Feature::DayCare, true));
  pokemon::setExtrasStreakInputSource(testStreakSource);

  pokemon::PokemonStore store;
  pokemon::PokemonBattleStore battleStore;
  pokemon::PokemonIvEvStore ivEvStore;
  pokemon::PokemonHallOfFameStore hallOfFameStore;
  pokemon::PokemonService service(store, battleStore, ivEvStore, hallOfFameStore, {nullptr, zeroRandom});
  pokemon::PokemonSnapshot snapshot{};
  CHECK(service.loadSnapshot(snapshot) == ServiceStatus::Ok);
  if (snapshot.partyCount == 0) return;
  const uint32_t leaderId = snapshot.party[0].recordId;
  const auto recordOf = [&](const uint32_t recordId) {
    pokemon::PokemonRecord record{};
    CHECK(service.readRecord(recordId, record) == ServiceStatus::Ok);
    return record;
  };
  const auto stateNow = [&] {
    pokemon::PokemonSnapshot current{};
    CHECK(service.loadSnapshot(current) == ServiceStatus::Ok);
    return current.state;
  };

  // --- Streak bonus: a 6-day streak is +30%, so 10 minutes grant 10 + 3 XP.
  {
    testStreak = {true, 6, 20000};
    const uint32_t xpBefore = recordOf(leaderId).totalXp;
    CHECK(service.creditMinutes(10, 40));
    if (xpBefore + 13U <= pokemon::MAXIMUM_TOTAL_XP) CHECK(recordOf(leaderId).totalXp == xpBefore + 13U);
    std::printf("  streak bonus: 10 minutes at a 6-day streak gave the lead %lu XP\n",
                static_cast<unsigned long>(recordOf(leaderId).totalXp - xpBefore));
    CHECK(extrasStore.state().streakXpRemainderPermille == 0);
    // 1 minute at +30% is 0.3 XP: nothing granted yet, the fraction is kept.
    const uint32_t xpMid = recordOf(leaderId).totalXp;
    CHECK(service.creditMinutes(1, 40));
    if (xpMid + 1U <= pokemon::MAXIMUM_TOTAL_XP) CHECK(recordOf(leaderId).totalXp == xpMid + 1U);
    CHECK(extrasStore.state().streakXpRemainderPermille == 300);
    checkMainSave();
  }

  // --- No clock: no bonus, and the remainder is left alone.
  {
    testStreak = {};
    const uint32_t xpBefore = recordOf(leaderId).totalXp;
    CHECK(service.creditMinutes(1, 40));
    if (xpBefore + 1U <= pokemon::MAXIMUM_TOTAL_XP) CHECK(recordOf(leaderId).totalXp == xpBefore + 1U);
    CHECK(extrasStore.state().streakXpRemainderPermille == 300);
    CHECK(extrasStore.state().lastRewardedStreakMilestone == 0);
  }

  // --- Milestone: day 7 queues one ordinary wild Encounter, once.
  {
    testStreak = {true, 7, 20001};
    const pokemon::PokemonState before = stateNow();
    const bool room = pokemon::extras::hasEncounterRoom(before);
    CHECK(service.creditMinutes(1, 40));
    const pokemon::PokemonState after = stateNow();
    std::printf("  streak milestone: room in the queue %s, encounters queued %lu -> %lu, rewarded milestone %u\n",
                room ? "yes" : "no", static_cast<unsigned long>(countEncounters(before)),
                static_cast<unsigned long>(countEncounters(after)),
                static_cast<unsigned>(extrasStore.state().lastRewardedStreakMilestone));
    if (room) {
      CHECK(extrasStore.state().lastRewardedStreakMilestone == 7);
      CHECK(countEncounters(after) >= countEncounters(before) + 1);
      if (fixture) CHECK(pokemon::pendingEventCount(after) == pokemon::pendingEventCount(before) + 1);
    } else {
      CHECK(extrasStore.state().lastRewardedStreakMilestone == 0);  // retried at the next checkpoint
    }
    checkMainSave();
    // The same streak day again: no second encounter.
    const size_t pendingMid = pokemon::pendingEventCount(after);
    CHECK(service.creditMinutes(1, 40));
    if (room) {
      CHECK(pokemon::extras::dueStreakMilestone(extrasStore.state(), testStreak) == 0);
      if (fixture) CHECK(pokemon::pendingEventCount(stateNow()) == pendingMid);
    }
    checkMainSave();
  }
  testStreak = {};  // the Day Care checks below count XP without a bonus

  // --- Day Care. Needs a boxed Pokemon: the last party member is deposited,
  // or the first boxed one is used when the party has a single member.
  CHECK(service.loadSnapshot(snapshot) == ServiceStatus::Ok);
  uint32_t boxedId = 0;
  bool deposited = false;
  if (snapshot.partyCount > 1) {
    const uint32_t candidate = snapshot.party[snapshot.partyCount - 1U].recordId;
    if (service.depositPokemon(candidate) == ServiceStatus::Ok) {
      boxedId = candidate;
      deposited = true;
    }
  }
  if (boxedId == 0) {
    std::array<pokemon::PokemonRecord, 1> page{};
    size_t count = 0;
    if (service.readPcPage(pokemon::PcOrder::CatchDate, 0, page, count) == ServiceStatus::Ok && count > 0) {
      boxedId = page[0].recordId;
    }
  }
  if (boxedId == 0) {
    std::printf("PokemonExtrasCompatTest: no boxed Pokemon available, Day Care checks skipped\n");
  } else {
    DayCareSendResult result = DayCareSendResult::InvalidRecord;
    CHECK(service.sendToDayCare(leaderId, result) == ServiceStatus::Ok);
    CHECK(result == DayCareSendResult::InParty);
    CHECK(extrasStore.state().dayCareRecordId == 0);
    CHECK(service.sendToDayCare(boxedId, result) == ServiceStatus::Ok);
    CHECK(result == DayCareSendResult::Sent);
    CHECK(extrasStore.state().dayCareRecordId == boxedId);
    pokemon::PokemonRecord resident{};
    CHECK(service.readDayCareRecord(resident));
    CHECK(resident.recordId == boxedId);

    // 11 minutes: 5 XP for the Day Care Pokemon (never past the lead's level)
    // and the odd minute kept.
    const uint32_t residentXp = resident.totalXp;
    CHECK(service.creditMinutes(11, 40));
    const uint32_t cap = pokemon::extras::dayCareXpCap(pokemon::levelForXp(recordOf(leaderId).totalXp));
    const uint32_t expected = residentXp >= cap ? 0U : std::min<uint32_t>(5U, cap - residentXp);
    CHECK(recordOf(boxedId).totalXp == residentXp + expected);
    std::printf("  Day Care: 11 minutes gave record %lu %lu XP (cap %lu, had %lu)\n",
                static_cast<unsigned long>(boxedId), static_cast<unsigned long>(recordOf(boxedId).totalXp - residentXp),
                static_cast<unsigned long>(cap), static_cast<unsigned long>(residentXp));
    CHECK(recordOf(boxedId).totalXp <= std::max(cap, residentXp));
    CHECK(extrasStore.state().dayCareMinuteRemainder == 1);
    checkMainSave();

    // Feature off: the Pokemon stays, and earns nothing.
    CHECK(extrasStore.setFeatureEnabled(Feature::DayCare, false));
    const uint32_t pausedXp = recordOf(boxedId).totalXp;
    CHECK(service.creditMinutes(4, 40));
    CHECK(recordOf(boxedId).totalXp == pausedXp);
    CHECK(extrasStore.state().dayCareRecordId == boxedId);
    CHECK(extrasStore.setFeatureEnabled(Feature::DayCare, true));

    // Back to the Party (or taken back): the slot is cleared.
    if (deposited) {
      CHECK(service.withdrawPokemon(boxedId) == ServiceStatus::Ok);
    } else {
      CHECK(service.takeFromDayCare() == ServiceStatus::Ok);
    }
    CHECK(extrasStore.state().dayCareRecordId == 0);
    CHECK(extrasStore.state().dayCareMinuteRemainder == 0);
    CHECK(service.takeFromDayCare() == ServiceStatus::NotApplicable);
    checkMainSave();

    if (fixture && deposited) {
      // Released while in Day Care: cleared too.
      CHECK(service.depositPokemon(boxedId) == ServiceStatus::Ok);
      CHECK(service.sendToDayCare(boxedId, result) == ServiceStatus::Ok);
      CHECK(result == DayCareSendResult::Sent);
      CHECK(service.releasePokemon(boxedId) == ServiceStatus::Ok);
      CHECK(extrasStore.state().dayCareRecordId == 0);
      // A slot left holding a record that no longer exists is cleared
      // silently by the next reading credit.
      pokemon::extras::ExtrasState stale = extrasStore.state();
      stale.dayCareRecordId = boxedId;
      CHECK(extrasStore.commit(stale));
      CHECK(service.creditMinutes(2, 40));
      CHECK(extrasStore.state().dayCareRecordId == 0);
      checkMainSave();
    }
  }

  pokemon::setExtrasStreakInputSource(nullptr);
  // What was saved reads back the same from disk.
  pokemon::PokemonExtrasStore reader(fixedEntropy);
  CHECK(reader.state() == extrasStore.state());
}

// What the daily quests see in place of the firmware's clock.
pokemon::extras::QuestClock testClock{};
pokemon::extras::QuestClock testClockSource() { return testClock; }

std::vector<uint8_t> extrasFileBytes() {
  std::vector<uint8_t> bytes;
  for (const char* path : EXTRAS_PATHS) {
    const std::vector<uint8_t> file = readStub(path);
    bytes.insert(bytes.end(), file.begin(), file.end());
    bytes.push_back(0xFF);  // separator: a file that moved between slots differs
  }
  return bytes;
}

// The first day whose set the service can finish without a trainer, a catch
// or an egg: one reading quest, a wild win and a level-up.
uint32_t questDayWithoutCatches() {
  using pokemon::extras::QuestKind;
  for (uint32_t day = 20000; day < 60000; ++day) {
    const auto set = pokemon::extras::chooseQuestSet(pokemon::extras::questSeedForDay(day), false);
    bool ok = true;
    for (const QuestKind kind : set) {
      ok = ok && (pokemon::extras::isReadingQuest(kind) || kind == QuestKind::WinWild || kind == QuestKind::LevelUp);
    }
    if (ok) return day;
  }
  return 0;
}

uint32_t bagTotal(const pokemon::PokemonState& state) {
  uint32_t total = 0;
  for (const uint8_t count : state.bagCounts) total += count;
  return total;
}

// M3 (docs/extras/M3-HOOKS.md section 8): find an egg, count it down, hatch
// it through the service, a crash between the hatch's commits 1-2 and 2-3
// (one record per egg either way), a finished quest set and its rewards, a
// rotation that grants a ready reward, and every toggle off writing nothing.
void runM3Operations(const bool fixture) {
  using pokemon::ServiceStatus;
  using pokemon::extras::Feature;
  namespace extras = pokemon::extras;

  pokemon::PokemonExtrasStore& extrasStore = pokemon::devicePokemonExtrasStore();
  extrasStore.load();
  for (uint8_t index = 0; index < extras::FEATURE_COUNT; ++index) {
    CHECK(extrasStore.setFeatureEnabled(static_cast<Feature>(index), false));
  }
  CHECK(extrasStore.setFeatureEnabled(Feature::Eggs, true));
  pokemon::setExtrasQuestClockSource(testClockSource);
  testClock = {};

  pokemon::PokemonStore store;
  pokemon::PokemonBattleStore battleStore;
  pokemon::PokemonIvEvStore ivEvStore;
  pokemon::PokemonHallOfFameStore hallOfFameStore;
  pokemon::PokemonService service(store, battleStore, ivEvStore, hallOfFameStore, {nullptr, zeroRandom});
  pokemon::PokemonSnapshot snapshot{};
  CHECK(service.loadSnapshot(snapshot) == ServiceStatus::Ok);
  if (snapshot.partyCount == 0) return;
  const auto stateNow = [&] {
    pokemon::PokemonSnapshot current{};
    CHECK(service.loadSnapshot(current) == ServiceStatus::Ok);
    return current.state;
  };
  const auto countSpecies = [&](const uint16_t speciesId) {
    // Every record of a species, Party and PC Box.
    size_t count = 0;
    std::array<pokemon::PokemonRecord, 16> page{};
    for (size_t offset = 0;; offset += page.size()) {
      size_t got = 0;
      if (service.readPcPage(pokemon::PcOrder::CatchDate, offset, page, got) != ServiceStatus::Ok) break;
      for (size_t index = 0; index < got; ++index) count += page[index].speciesId == speciesId ? 1 : 0;
      if (got < page.size()) break;
    }
    const pokemon::PokemonState state = stateNow();
    for (const uint32_t recordId : state.partyRecordIds) {
      pokemon::PokemonRecord record{};
      if (recordId != 0 && service.readRecord(recordId, record) == ServiceStatus::Ok && record.speciesId == speciesId) {
        ++count;
      }
    }
    return count;
  };

  // --- Egg find: one roll per hour of reading, at most 4 hours to a find.
  const size_t recordsBefore = store.recordCount();
  for (int hour = 0; hour < 4 && !extras::hasEgg(extrasStore.state()); ++hour) CHECK(service.creditMinutes(60, 40));
  CHECK(extras::hasEgg(extrasStore.state()));
  CHECK(store.recordCount() == recordsBefore);  // a find touches extras only
  const uint16_t eggSpecies = extrasStore.state().eggSpeciesId;
  CHECK(extras::isEggPoolSpecies(eggSpecies));
  CHECK(extrasStore.state().eggPagesRemaining == extras::eggTotalPages(eggSpecies));
  std::printf("  egg: found species %u, %u pages\n", static_cast<unsigned>(eggSpecies),
              static_cast<unsigned>(extrasStore.state().eggPagesRemaining));
  checkMainSave();

  // --- Pages: RAM only until the next credit, then counted.
  CHECK(service.beginReadingSession());
  const std::vector<uint8_t> extrasBeforePages = extrasFileBytes();
  for (int page = 0; page < 50; ++page) service.countExtrasEggPage();
  CHECK(extrasFileBytes() == extrasBeforePages);  // no SD write per page
  const uint16_t pagesBefore = extrasStore.state().eggPagesRemaining;
  CHECK(service.creditMinutes(1, 40));
  CHECK(extrasStore.state().eggPagesRemaining == pagesBefore - 50);
  const uint16_t left = extrasStore.state().eggPagesRemaining;
  for (uint16_t page = 0; page < left + 5U; ++page) service.countExtrasEggPage();
  CHECK(service.creditMinutes(1, 40));
  CHECK(extras::isEggReadyToHatch(extrasStore.state()));
  service.flushOnExit(0);

  // --- Crash between commit 1 (intent) and 2 (main save): the next extras use
  // drops the intent and the egg hatches once.
  const auto speciesBefore = countSpecies(eggSpecies);
  {
    extras::ExtrasState intent = extrasStore.state();
    CHECK(extras::beginHatch(intent, store.nextRecordId()));
    CHECK(extrasStore.commit(intent));
  }
  extrasStore.load();  // reboot
  uint32_t hatchedId = 0;
  uint16_t hatchedSpecies = 0;
  const ServiceStatus hatch = service.hatchReadyEgg(hatchedId, hatchedSpecies);
  if (!fixture && hatch == ServiceStatus::BoxFull) {
    std::printf("  hatch: Party and PC Box full, the egg waits (as designed)\n");
    CHECK(extras::isEggReadyToHatch(extrasStore.state()));
  } else {
    CHECK(hatch == ServiceStatus::Ok);
    CHECK(hatchedSpecies == eggSpecies && hatchedId != 0);
    CHECK(countSpecies(eggSpecies) == speciesBefore + 1);
    CHECK(!extras::hasEgg(extrasStore.state()) && extrasStore.state().pendingHatchRecordId == 0);
    pokemon::PokemonRecord hatched{};
    CHECK(service.readRecord(hatchedId, hatched) == ServiceStatus::Ok);
    CHECK(hatched.origin == pokemon::Origin::Caught);
    CHECK(pokemon::levelForXp(hatched.totalXp) == extras::EGG_HATCH_LEVEL);
    CHECK(pokemon::isSpeciesMarked(stateNow().caughtSpecies, eggSpecies));
    const pokemon::IvEvEntry ivEv = service.peekIvEv(hatchedId);
    CHECK(ivEv.recordId == hatchedId);
    for (const uint8_t iv : ivEv.iv) CHECK(iv >= extras::EGG_IV_FLOOR && iv <= extras::EGG_IV_MAX);
    std::printf("  hatch: record %lu, species %u, after a crash between commits 1 and 2\n",
                static_cast<unsigned long>(hatchedId), static_cast<unsigned>(hatchedSpecies));
    checkMainSave();

    // --- Crash between commit 2 (main save) and 3 (egg cleared): the next
    // extras use finds the record and clears the egg; no second record.
    extras::ExtrasState egg = extrasStore.state();
    egg.eggSpeciesId = eggSpecies;
    egg.eggPagesRemaining = 0;
    egg.eggFlags = 0;
    CHECK(extrasStore.commit(egg));
    const auto speciesMid = countSpecies(eggSpecies);
    const uint32_t newId = store.nextRecordId();
    CHECK(extras::beginHatch(egg, newId));
    CHECK(extrasStore.commit(egg));
    pokemon::PokemonState state = stateNow();
    pokemon::RecordMutation mutation{};
    CHECK(extras::buildHatchedRecord(egg, state, newId, pokemon::Gender::Female, "", mutation));
    CHECK(store.commit(state, mutation));
    extrasStore.load();  // reboot
    CHECK(service.hatchReadyEgg(hatchedId, hatchedSpecies) == ServiceStatus::NotApplicable);
    CHECK(!extras::hasEgg(extrasStore.state()) && extrasStore.state().pendingHatchRecordId == 0);
    CHECK(countSpecies(eggSpecies) == speciesMid + 1);
    std::printf("  hatch: a crash between commits 2 and 3 left one record (%lu)\n", static_cast<unsigned long>(newId));
    checkMainSave();
  }

  // --- Daily quests: a set the service can finish, finished and claimed.
  const uint32_t day = questDayWithoutCatches();
  CHECK(day != 0);
  CHECK(extrasStore.setFeatureEnabled(Feature::DailyQuests, true));
  testClock = {true, day};
  CHECK(service.refreshDailyQuests() == ServiceStatus::Ok);
  CHECK(extrasStore.state().questDayAnchor == day);
  uint16_t readTarget = 0;
  for (uint8_t slot = 0; slot < extras::DAILY_QUEST_SLOTS; ++slot) {
    const extras::QuestKind kind = extras::questKindAt(extrasStore.state(), slot);
    if (extras::isReadingQuest(kind)) readTarget = extras::questTarget(kind);
  }
  CHECK(readTarget > 0);
  CHECK(service.creditMinutes(readTarget, 40));
  service.noteWildBattleWon();
  // A battle-style level-up for a Party member below 100.
  pokemon::PokemonState state = stateNow();
  for (const uint32_t recordId : state.partyRecordIds) {
    pokemon::PokemonRecord record{};
    if (recordId == 0 || service.readRecord(recordId, record) != ServiceStatus::Ok) continue;
    const uint8_t level = pokemon::levelForXp(record.totalXp);
    if (level >= 100) continue;
    CHECK(service.awardFixedXp(recordId, pokemon::xpRequired(level + 1U) - record.totalXp) == ServiceStatus::Ok);
    break;
  }
  CHECK(extras::questRewardsReadyMask(extrasStore.state()) == (extras::QUEST_SLOT_MASK | extras::QUEST_BONUS_BIT));
  const uint32_t bagBefore = bagTotal(stateNow());
  extras::QuestRewards granted{};
  CHECK(service.claimQuestRewards(granted) == ServiceStatus::Ok);
  CHECK(granted.count == 4);
  CHECK(granted.itemIds[3] == extras::QUEST_BONUS_ITEM);
  if (fixture) CHECK(bagTotal(stateNow()) == bagBefore + 4U);
  std::printf("  quests: day %lu finished, claimed %u items (bag %lu -> %lu)\n", static_cast<unsigned long>(day),
              static_cast<unsigned>(granted.count), static_cast<unsigned long>(bagBefore),
              static_cast<unsigned long>(bagTotal(stateNow())));
  CHECK(extras::questRewardsReadyMask(extrasStore.state()) == 0);
  CHECK(service.claimQuestRewards(granted) == ServiceStatus::NotApplicable);  // never twice
  checkMainSave();

  // --- Rotation: tomorrow's set; a ready reward of today's set is granted.
  testClock = {true, day + 1};
  CHECK(service.refreshDailyQuests() == ServiceStatus::Ok);
  CHECK(extrasStore.state().questDayAnchor == day + 1);
  // A reward taken in extras whose bag write never happened (a crash between
  // the two commits) is lost, never given twice.
  {
    extras::ExtrasState taken = extrasStore.state();
    taken.questDoneMask = 0x01;
    CHECK(extrasStore.commit(taken));
    const uint32_t bagMid = bagTotal(stateNow());
    extras::ExtrasState afterTake = extrasStore.state();
    CHECK(extras::takeQuestRewards(afterTake).count == 1);
    CHECK(extrasStore.commit(afterTake));
    extrasStore.load();  // reboot before the bag write
    CHECK(service.claimQuestRewards(granted) == ServiceStatus::NotApplicable);
    CHECK(bagTotal(stateNow()) == bagMid);
  }
  checkMainSave();

  // --- Every toggle off: reading, page turns and battle events write no extras.
  for (uint8_t index = 0; index < extras::FEATURE_COUNT; ++index) {
    CHECK(extrasStore.setFeatureEnabled(static_cast<Feature>(index), false));
  }
  const std::vector<uint8_t> extrasOff = extrasFileBytes();
  CHECK(service.beginReadingSession());
  for (int page = 0; page < 20; ++page) service.countExtrasEggPage();
  CHECK(service.creditMinutes(61, 40));
  service.noteWildBattleWon();
  CHECK(service.refreshDailyQuests() == ServiceStatus::NotApplicable);
  CHECK(service.hatchReadyEgg(hatchedId, hatchedSpecies) == ServiceStatus::NotApplicable);
  service.flushOnExit(0);
  CHECK(extrasFileBytes() == extrasOff);
  checkMainSave();

  pokemon::setExtrasQuestClockSource(nullptr);
  pokemon::PokemonExtrasStore reader(fixedEntropy);
  CHECK(reader.state() == extrasStore.state());
}

// M4 (docs/extras/M4-HOOKS.md section 5): the book boss and Kindle credit
// through the service, the way the reader hooks call it. Reading stats are not
// part of the host service (the hooks only read them, R4); the simulator gate
// covers those files.
void runM4Operations(const bool fixture) {
  using pokemon::ServiceStatus;
  using pokemon::extras::BookFinishResult;
  using pokemon::extras::Feature;
  namespace extras = pokemon::extras;

  pokemon::PokemonExtrasStore& extrasStore = pokemon::devicePokemonExtrasStore();
  extrasStore.load();
  for (uint8_t index = 0; index < extras::FEATURE_COUNT; ++index) {
    CHECK(extrasStore.setFeatureEnabled(static_cast<Feature>(index), false));
  }

  pokemon::PokemonStore store;
  pokemon::PokemonBattleStore battleStore;
  pokemon::PokemonIvEvStore ivEvStore;
  pokemon::PokemonHallOfFameStore hallOfFameStore;
  pokemon::PokemonService service(store, battleStore, ivEvStore, hallOfFameStore, {nullptr, zeroRandom});
  pokemon::PokemonSnapshot snapshot{};
  CHECK(service.loadSnapshot(snapshot) == ServiceStatus::Ok);
  if (snapshot.partyCount == 0) return;
  const uint32_t leaderId = snapshot.party[0].recordId;
  const auto stateNow = [&] {
    pokemon::PokemonSnapshot current{};
    CHECK(service.loadSnapshot(current) == ServiceStatus::Ok);
    return current.state;
  };
  const auto leaderXp = [&] {
    pokemon::PokemonRecord record{};
    CHECK(service.readRecord(leaderId, record) == ServiceStatus::Ok);
    return record.totalXp;
  };
  // The queue is the player's on a real save: the test empties or fills it in
  // the in-memory copy only.
  const auto clearQueue = [&] {
    pokemon::PokemonState state = stateNow();
    while (pokemon::discardFrontPendingEvent(state)) {
    }
    CHECK(store.commit(state));
  };
  const auto fillQueue = [&] {
    pokemon::PokemonState state = stateNow();
    while (extras::hasEncounterRoom(state)) {
      pokemon::PendingEvent event{};
      event.kind = pokemon::PendingEventKind::Encounter;
      event.speciesId = 16;
      event.level = 5;
      event.gender = pokemon::Gender::Male;
      if (!pokemon::enqueuePendingEvent(state, event)) break;
    }
    CHECK(store.commit(state));
  };
  constexpr size_t masterBallSlot = extras::BOSS_ITEM_MASTER_BALL - pokemon::EVOLUTION_ITEM_COUNT - 1U;
  constexpr uint32_t BOOK_A = 0xB00C000AU;
  constexpr uint32_t BOOK_B = 0xB00C000BU;
  constexpr uint32_t BOOK_C = 0xB00C000CU;
  constexpr uint32_t BOOK_SHORT = 0xB00C0005U;
  constexpr uint32_t HOUR = 60U * 60U;

  // --- Toggles off: neither feature reads or writes anything.
  {
    const auto filesBefore = snapshotUpstreamFiles();
    const std::vector<uint8_t> extrasBefore = extrasFileBytes();
    extras::KindleSync sync{};
    sync.documentHash = BOOK_A;
    sync.localPermille = 100;
    sync.remotePermille = 900;
    sync.bookPages = 1000;
    CHECK(service.creditRemoteReading(sync) == 0);
    CHECK(service.onExtrasBookFinished({BOOK_A, 10 * HOUR}) == BookFinishResult::Ignored);
    CHECK(!service.deliverHeldExtrasBoss());
    CHECK(service.takeExtrasKindleNotice() == 0);
    CHECK(!service.takeExtrasBossNotice());
    CHECK(snapshotUpstreamFiles() == filesBefore);
    CHECK(extrasFileBytes() == extrasBefore);
    // Reading with a book open and every toggle off writes no extras either.
    service.setExtrasOpenBook(BOOK_A, 700);
    CHECK(service.creditMinutes(1, 40));
    CHECK(extrasFileBytes() == extrasBefore);
    CHECK(extras::kindleHighWaterPermille(extrasStore.state(), BOOK_A) == 0);
    service.setExtrasOpenBook(0, 0);
  }

  // --- Book boss on: a finished book with an hour of reading.
  CHECK(extrasStore.setFeatureEnabled(Feature::BookBoss, true));
  clearQueue();
  {
    const pokemon::PokemonState before = stateNow();
    const uint8_t ballsBefore = before.bagCounts[masterBallSlot];
    CHECK(service.onExtrasBookFinished({BOOK_A, HOUR}) == BookFinishResult::BossHeld);
    CHECK(service.takeExtrasBossNotice());
    CHECK(!service.takeExtrasBossNotice());  // once
    const pokemon::PokemonState after = stateNow();
    CHECK(countEncounters(after) == countEncounters(before) + 1);
    CHECK(pokemon::pendingEventCount(after) == pokemon::pendingEventCount(before) + 1);
    const pokemon::PendingEvent& boss = after.pendingEvents[pokemon::pendingEventCount(after) - 1];
    CHECK(boss.kind == pokemon::PendingEventKind::Encounter);
    CHECK(boss.speciesId != 151 && boss.speciesId != 0);
    if (ballsBefore < 255) CHECK(after.bagCounts[masterBallSlot] == ballsBefore + 1);
    CHECK(!extras::hasHeldBoss(extrasStore.state()));
    CHECK(extras::booksFinishedRibbons(extrasStore.state()) == 1);
    std::printf("  boss: species %u at level %u, Master Balls %u -> %u, ribbons 1\n",
                static_cast<unsigned>(boss.speciesId), static_cast<unsigned>(boss.level),
                static_cast<unsigned>(ballsBefore), static_cast<unsigned>(after.bagCounts[masterBallSlot]));
    checkMainSave();
    // The same book again (un-marked and marked again): nothing.
    const auto filesMid = snapshotUpstreamFiles();
    const std::vector<uint8_t> extrasMid = extrasFileBytes();
    CHECK(service.onExtrasBookFinished({BOOK_A, HOUR}) == BookFinishResult::AlreadyCounted);
    CHECK(snapshotUpstreamFiles() == filesMid);
    CHECK(extrasFileBytes() == extrasMid);
    // Under an hour, Kindle credit off: ribbon only.
    CHECK(service.onExtrasBookFinished({BOOK_SHORT, HOUR - 1}) == BookFinishResult::RibbonOnly);
    CHECK(snapshotUpstreamFiles() == filesMid);
    CHECK(extras::booksFinishedRibbons(extrasStore.state()) == 2);
  }

  // --- A full queue: the boss is held, then delivered once an event is cleared.
  {
    fillQueue();
    const pokemon::PokemonState full = stateNow();
    CHECK(service.onExtrasBookFinished({BOOK_B, 2 * HOUR}) == BookFinishResult::BossHeld);
    CHECK(extras::hasHeldBoss(extrasStore.state()));
    CHECK(pokemon::pendingEventCount(stateNow()) == pokemon::pendingEventCount(full));
    // A second qualifying book while one is held: counted, no second boss.
    CHECK(service.onExtrasBookFinished({BOOK_C, 2 * HOUR}) == BookFinishResult::BossAlreadyWaiting);
    CHECK(!service.deliverHeldExtrasBoss());  // still no room
    pokemon::PokemonState cleared = stateNow();
    CHECK(pokemon::discardFrontPendingEvent(cleared));
    CHECK(store.commit(cleared));
    const size_t encountersBefore = countEncounters(cleared);
    // The next credit checkpoint delivers it.
    CHECK(service.creditMinutes(1, 40));
    CHECK(!extras::hasHeldBoss(extrasStore.state()));
    CHECK(countEncounters(stateNow()) >= encountersBefore + 1);
    CHECK(extras::booksFinishedRibbons(extrasStore.state()) == 4);
    std::printf("  boss: held with a full queue, delivered at the next checkpoint\n");
    checkMainSave();
  }

  // --- A crash between the extras save (boss taken) and the main commit: no
  // boss, and no second boss later.
  {
    clearQueue();
    extras::ExtrasState held = extrasStore.state();
    CHECK(extras::holdBoss(held, 149));
    CHECK(extrasStore.commit(held));
    extras::ExtrasState taken = extrasStore.state();
    CHECK(extras::takeHeldBoss(taken) == 149);
    CHECK(extrasStore.commit(taken));  // step 1 done, then the power goes
    extrasStore.load();                // reboot
    const auto dragonites = [&] {
      size_t count = 0;
      const pokemon::PokemonState state = stateNow();
      for (size_t index = 0; index < pokemon::pendingEventCount(state); ++index) {
        count += state.pendingEvents[index].speciesId == 149 ? 1 : 0;
      }
      return count;
    };
    const size_t before = dragonites();
    CHECK(!service.deliverHeldExtrasBoss());
    CHECK(service.creditMinutes(1, 40));
    CHECK(dragonites() == before);
    std::printf("  boss: a crash between taking it and the main commit lost it, no second boss\n");
    CHECK(!extras::hasHeldBoss(extrasStore.state()));
    checkMainSave();
  }

  // --- Kindle credit on.
  CHECK(extrasStore.setFeatureEnabled(Feature::KindleCredit, true));
  CHECK(extrasStore.setFeatureEnabled(Feature::BookBoss, false));
  clearQueue();
  extras::KindleSync sync{};
  sync.documentHash = BOOK_A;
  sync.bookPages = 1000;
  sync.pace.bookSecondsPerPage = 60;  // a minute a page
  {
    // 42 pages at a minute a page.
    sync.localPermille = 100;
    sync.remotePermille = 142;
    const uint32_t lifetimeBefore = stateNow().lifetimeMinutes;
    CHECK(service.creditRemoteReading(sync) == 42);
    CHECK(stateNow().lifetimeMinutes == lifetimeBefore + 42U);
    CHECK(extras::kindleHighWaterPermille(extrasStore.state(), BOOK_A) == 142);
    // The same sync again: nothing, nothing written.
    const auto filesMid = snapshotUpstreamFiles();
    const std::vector<uint8_t> extrasMid = extrasFileBytes();
    CHECK(service.creditRemoteReading(sync) == 0);
    CHECK(snapshotUpstreamFiles() == filesMid);
    CHECK(extrasFileBytes() == extrasMid);
    std::printf("  kindle: a 42-minute sync credited 42 minutes; the same sync again, nothing\n");
    checkMainSave();
  }
  {
    // The mis-sync guard: 858 pages, 600 minutes in 10 chunks, the rest dropped.
    sync.localPermille = 142;
    sync.remotePermille = 1000;
    const uint32_t lifetimeBefore = stateNow().lifetimeMinutes;
    const uint32_t xpBefore = leaderXp();
    const uint16_t credited = service.creditRemoteReading(sync);
    CHECK(credited == extras::KINDLE_GUARD_MINUTES);
    const uint32_t lifetimeGained = stateNow().lifetimeMinutes - lifetimeBefore;
    CHECK(lifetimeGained <= extras::KINDLE_GUARD_MINUTES);
    CHECK(lifetimeGained % extras::KINDLE_CHUNK_MINUTES == 0);  // whole chunks, then XP only if the queue filled
    CHECK(extras::kindleHighWaterPermille(extrasStore.state(), BOOK_A) == 1000);
    std::printf("  kindle: guarded sync, %u minutes credited (%lu through the reading path), lead +%lu XP\n",
                static_cast<unsigned>(credited), static_cast<unsigned long>(lifetimeGained),
                static_cast<unsigned long>(leaderXp() - xpBefore));
    checkMainSave();
  }
  {
    // A full queue: XP only for the lead.
    fillQueue();
    sync.documentHash = BOOK_B;
    sync.localPermille = 0;
    sync.remotePermille = 20;
    const pokemon::PokemonState before = stateNow();
    const uint32_t xpBefore = leaderXp();
    CHECK(service.creditRemoteReading(sync) == 20);
    const pokemon::PokemonState after = stateNow();
    CHECK(after.lifetimeMinutes == before.lifetimeMinutes);
    CHECK(pokemon::pendingEventCount(after) == pokemon::pendingEventCount(before));
    if (pokemon::levelForXp(xpBefore) < 100) CHECK(leaderXp() > xpBefore);
    checkMainSave();
    clearQueue();
  }
  {
    // Local reading keeps the mark current: only pages past it are credited.
    service.setExtrasOpenBook(BOOK_C, 500);
    CHECK(service.creditMinutes(1, 50));
    CHECK(extras::kindleHighWaterPermille(extrasStore.state(), BOOK_C) == 500);
    service.setExtrasOpenBook(0, 0);
    sync.documentHash = BOOK_C;
    sync.localPermille = 0;
    sync.remotePermille = 520;
    CHECK(service.creditRemoteReading(sync) == 20);
    // A jump back and forward again earns nothing.
    sync.localPermille = 520;
    sync.remotePermille = 300;
    CHECK(service.creditRemoteReading(sync) == 0);
    sync.localPermille = 300;
    sync.remotePermille = 520;
    CHECK(service.creditRemoteReading(sync) == 0);
  }
  {
    const uint16_t notice = service.takeExtrasKindleNotice();
    CHECK(notice == 42 + extras::KINDLE_GUARD_MINUTES + 20 + 20);
    CHECK(service.takeExtrasKindleNotice() == 0);
    std::printf("  kindle: notice \"%u minutes credited\", shown once\n", static_cast<unsigned>(notice));
  }
  checkMainSave();
  (void)fixture;
}

// Every extras operation there is so far, with the main save checked after each group.
void runExtrasOperations(const bool fixture) {
  checkMainSave();
  const auto before = snapshotUpstreamFiles();

  {
    pokemon::PokemonExtrasStore extrasStore(fixedEntropy);
    // Load (defaults on a missing file), every toggle off, then on/off again.
    CHECK(extrasStore.state().featureMask == pokemon::extras::DEFAULT_FEATURE_MASK);
    for (uint8_t index = 0; index < pokemon::extras::FEATURE_COUNT; ++index) {
      const auto feature = static_cast<pokemon::extras::Feature>(index);
      CHECK(extrasStore.setFeatureEnabled(feature, true));
      CHECK(extrasStore.setFeatureEnabled(feature, false));
    }
    CHECK(extrasStore.state().featureMask == 0);
    // Progress, RNG draws, and a progress reset.
    pokemon::extras::ExtrasState progress = extrasStore.state();
    (void)pokemon::extras::randomBelow(progress, 100);
    progress.ribbonCount = 3;
    progress.eggSpeciesId = 1;
    progress.eggPagesRemaining = 100;
    CHECK(extrasStore.commit(progress));
    CHECK(extrasStore.resetProgress());
    CHECK(extrasStore.state().featureMask == 0);
  }
  CHECK(Storage.exists(EXTRAS_PATHS[0]));
  CHECK(snapshotUpstreamFiles() == before);  // extras ops wrote nothing else
  checkMainSave();

  // A reading session with every toggle off goes through the main save as upstream.
  {
    pokemon::PokemonStore store;
    pokemon::PokemonBattleStore battleStore;
    pokemon::PokemonIvEvStore ivEvStore;
    pokemon::PokemonHallOfFameStore hallOfFameStore;
    pokemon::PokemonService service(store, battleStore, ivEvStore, hallOfFameStore, {nullptr, zeroRandom});
    (void)service.creditMinutes(10, 40);
  }
  checkMainSave();

  runM2Operations(fixture);
  runM3Operations(fixture);
  runM4Operations(fixture);

  // Deleting the extras files changes nothing else.
  const auto withExtras = snapshotUpstreamFiles();
  for (const char* path : EXTRAS_PATHS) {
    if (Storage.exists(path)) CHECK(Storage.remove(path));
  }
  CHECK(snapshotUpstreamFiles() == withExtras);
  checkMainSave();
  pokemon::devicePokemonExtrasStore().load();  // the files it had loaded are gone

  // Game reset: main save first, then the extras store (R6). The fresh main
  // save is still upstream's v11, and extras progress is cleared.
  {
    pokemon::PokemonExtrasStore& extrasStore = pokemon::devicePokemonExtrasStore();
    CHECK(extrasStore.setFeatureEnabled(pokemon::extras::Feature::Eggs, false));
    pokemon::extras::ExtrasState progress = extrasStore.state();
    progress.ribbonCount = 4;
    CHECK(extrasStore.commit(progress));

    pokemon::PokemonStore store;
    pokemon::PokemonBattleStore battleStore;
    pokemon::PokemonIvEvStore ivEvStore;
    pokemon::PokemonHallOfFameStore hallOfFameStore;
    CHECK(store.begin() == pokemon::StoreBeginResult::Ready);  // the Pokemon screen has it open
    pokemon::PokemonService service(store, battleStore, ivEvStore, hallOfFameStore, {nullptr, zeroRandom});
    CHECK(service.reset() == pokemon::ServiceStatus::Ok);
    CHECK(extrasStore.state().ribbonCount == 0);
    CHECK(!extrasStore.isFeatureEnabled(pokemon::extras::Feature::Eggs));  // toggles survive a reset

    pokemon::PokemonExtrasStore reader(fixedEntropy);
    CHECK(reader.state().ribbonCount == 0);
  }
  // A reset leaves either no main save or an empty v11 one; either way any
  // slot present must still decode with the upstream codec.
  for (const char* path : MAIN_SAVE_PATHS) (void)checkMainSaveSlot(path);
}

}  // namespace

int main() {
  Storage.clear();
  const char* saveDirectory = std::getenv("CROSSINK_EXTRAS_COMPAT_SAVE_DIR");
  const char* checkOnly = std::getenv("CROSSINK_EXTRAS_COMPAT_CHECK_ONLY");
  const bool fixture = saveDirectory == nullptr || *saveDirectory == '\0';
  if (!fixture) {
    CHECK(loadSaveDirectory(saveDirectory));
    std::printf("PokemonExtrasCompatTest: using the save in %s\n", saveDirectory);
  } else {
    buildFixtureSave();
  }
  if (checkOnly != nullptr && std::string(checkOnly) == "1") {
    checkMainSave();
  } else {
    runExtrasOperations(fixture);
  }
  if (failures == 0) std::printf("PokemonExtrasCompatTest: all checks passed\n");
  return failures == 0 ? 0 : 1;
}
