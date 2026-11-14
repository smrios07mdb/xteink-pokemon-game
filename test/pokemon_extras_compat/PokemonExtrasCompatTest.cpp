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

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <map>
#include <string>
#include <vector>

#include "PokemonExtras.h"
#include "PokemonStoreCodec.h"
#include "pokemon/PokemonBattleStore.h"
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

// Every extras operation M1 has, with the main save checked after each group.
void runExtrasOperations() {
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

  // Deleting the extras files changes nothing else.
  const auto withExtras = snapshotUpstreamFiles();
  for (const char* path : EXTRAS_PATHS) {
    if (Storage.exists(path)) CHECK(Storage.remove(path));
  }
  CHECK(snapshotUpstreamFiles() == withExtras);
  checkMainSave();

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
  if (saveDirectory != nullptr && *saveDirectory != '\0') {
    CHECK(loadSaveDirectory(saveDirectory));
    std::printf("PokemonExtrasCompatTest: using the save in %s\n", saveDirectory);
  } else {
    buildFixtureSave();
  }
  if (checkOnly != nullptr && std::string(checkOnly) == "1") {
    checkMainSave();
  } else {
    runExtrasOperations();
  }
  if (failures == 0) std::printf("PokemonExtrasCompatTest: all checks passed\n");
  return failures == 0 ? 0 : 1;
}
