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
