// PokemonExtrasStore: the double-buffered /.crosspoint/pokemon-extras-{a,b}.bin
// store (docs/extras/PLAN.md M1). Modeled on test/pokemon_ivev_store, plus the
// extras-only rules from docs/extras/SAVE-SAFETY.md: a missing or invalid file
// is "empty extras" (R5), and a file from a newer extras build is never written
// over.

#include <HalStorage.h>

#include <cstdio>
#include <vector>

#include "PokemonCrc32.h"
#include "PokemonExtras.h"
#include "PokemonExtrasCodec.h"
#include "pokemon/PokemonExtrasStore.h"

namespace {

using pokemon::PokemonExtrasStore;
using pokemon::extras::ExtrasState;
using pokemon::extras::Feature;

int failures = 0;

#define CHECK(condition)                                                                \
  do {                                                                                  \
    if (!(condition)) {                                                                 \
      std::fprintf(stderr, "%s:%d check failed: %s\n", __FILE__, __LINE__, #condition); \
      ++failures;                                                                       \
    }                                                                                   \
  } while (false)

constexpr const char* STORE_PATH_A = "/.crosspoint/pokemon-extras-a.bin";
constexpr const char* STORE_PATH_B = "/.crosspoint/pokemon-extras-b.bin";
constexpr uint32_t TEST_ENTROPY = 0x13572468U;

int entropyCalls = 0;
uint32_t testEntropy() {
  ++entropyCalls;
  return TEST_ENTROPY;
}

std::vector<uint8_t> readFile(const char* path) {
  FsFile file = Storage.open(path, O_RDONLY);
  if (!file) return {};
  std::vector<uint8_t> bytes(static_cast<size_t>(file.fileSize64()));
  if (!bytes.empty()) file.read(bytes.data(), bytes.size());
  file.close();
  return bytes;
}

void writeFile(const char* path, const std::vector<uint8_t>& bytes) {
  FsFile file = Storage.open(path, O_WRONLY | O_CREAT | O_TRUNC);
  if (!bytes.empty()) file.write(bytes.data(), bytes.size());
  file.close();
}

void write32(uint8_t* bytes, const size_t offset, const uint32_t value) {
  for (size_t index = 0; index < 4; ++index) bytes[offset + index] = static_cast<uint8_t>(value >> (8U * index));
}

// A CRC-valid version 2 file (8 payload bytes appended), as a newer extras
// build would write it.
std::vector<uint8_t> newerVersionFile(const uint32_t sequence) {
  ExtrasState state{};
  pokemon::extras::seedRandom(state, 99);
  pokemon::extras::ExtrasFileBytes bytes{};
  CHECK(pokemon::extras::encodeExtrasFile(state, sequence, bytes));
  std::vector<uint8_t> file(bytes.begin(), bytes.end() - pokemon::extras::EXTRAS_CRC_BYTES);
  file[4] = 2;
  const uint16_t payload = static_cast<uint16_t>(pokemon::extras::EXTRAS_PAYLOAD_V1_BYTES + 8);
  file[5] = static_cast<uint8_t>(payload);
  file[6] = static_cast<uint8_t>(payload >> 8U);
  for (int extra = 0; extra < 8; ++extra) file.push_back(0x5A);
  const size_t crcOffset = file.size();
  file.resize(file.size() + pokemon::extras::EXTRAS_CRC_BYTES);
  write32(file.data(), crcOffset, pokemon::updatePokemonCrc32(0xFFFFFFFFU, file.data(), crcOffset) ^ 0xFFFFFFFFU);
  return file;
}

ExtrasState progressState(const ExtrasState& base) {
  ExtrasState state = base;
  state.eggSpeciesId = 7;
  state.eggPagesRemaining = 300;
  state.ribbonCount = 2;
  state.dayCareRecordId = 4;
  return state;
}

void missingFilesLoadDefaultsAndWriteNothing() {
  Storage.clear();
  entropyCalls = 0;
  PokemonExtrasStore store(testEntropy);
  const ExtrasState& state = store.state();
  CHECK(state.featureMask == pokemon::extras::DEFAULT_FEATURE_MASK);
  CHECK(state.rngState != 0);  // seeded from entropy in memory
  CHECK(entropyCalls == 1);
  CHECK(!store.readOnly());
  CHECK(store.isFeatureEnabled(Feature::Eggs));
  CHECK(!store.isFeatureEnabled(Feature::PartySleepScreen));
  CHECK(!Storage.exists(STORE_PATH_A));
  CHECK(!Storage.exists(STORE_PATH_B));
}

void toggleIsPersistedAndAFreshInstanceReadsItBack() {
  Storage.clear();
  uint32_t savedRng = 0;
  {
    PokemonExtrasStore writer(testEntropy);
    CHECK(writer.setFeatureEnabled(Feature::Eggs, false));
    CHECK(writer.setFeatureEnabled(Feature::PartySleepScreen, true));
    CHECK(!writer.isFeatureEnabled(Feature::Eggs));
    savedRng = writer.state().rngState;
  }
  CHECK(Storage.exists(STORE_PATH_A));
  CHECK(Storage.exists(STORE_PATH_B));
  CHECK(readFile(STORE_PATH_A).size() == pokemon::extras::EXTRAS_FILE_V1_BYTES);

  entropyCalls = 0;
  PokemonExtrasStore reader(testEntropy);
  CHECK(!reader.isFeatureEnabled(Feature::Eggs));
  CHECK(reader.isFeatureEnabled(Feature::PartySleepScreen));
  CHECK(reader.isFeatureEnabled(Feature::Streaks));
  CHECK(reader.state().rngState == savedRng);  // the stored stream continues, no re-seed
  CHECK(entropyCalls == 0);
}

void successiveWritesAlternateBetweenTheTwoFiles() {
  Storage.clear();
  PokemonExtrasStore store(testEntropy);
  CHECK(store.setFeatureEnabled(Feature::Eggs, false));  // -> A, sequence 1
  CHECK(Storage.exists(STORE_PATH_A));
  CHECK(!Storage.exists(STORE_PATH_B));
  const std::vector<uint8_t> firstA = readFile(STORE_PATH_A);

  CHECK(store.setFeatureEnabled(Feature::Streaks, false));  // -> B, sequence 2
  CHECK(Storage.exists(STORE_PATH_B));
  CHECK(readFile(STORE_PATH_A) == firstA);  // the active file is never written in place

  CHECK(store.setFeatureEnabled(Feature::DayCare, false));  // -> A again, sequence 3
  PokemonExtrasStore reader(testEntropy);
  CHECK(!reader.isFeatureEnabled(Feature::Eggs));
  CHECK(!reader.isFeatureEnabled(Feature::Streaks));
  CHECK(!reader.isFeatureEnabled(Feature::DayCare));
}

void corruptingTheNewerFileFallsBackToTheOlderOne() {
  Storage.clear();
  PokemonExtrasStore store(testEntropy);
  CHECK(store.setFeatureEnabled(Feature::Eggs, false));     // A, sequence 1
  CHECK(store.setFeatureEnabled(Feature::Streaks, false));  // B, sequence 2 (newest)
  Storage.setByte(STORE_PATH_B, 20, 0xA5U);

  PokemonExtrasStore reader(testEntropy);
  CHECK(!reader.isFeatureEnabled(Feature::Eggs));
  CHECK(reader.isFeatureEnabled(Feature::Streaks));  // A's value, one write behind
  CHECK(!reader.readOnly());
}

void invalidFilesAreEmptyExtrasNotAnError() {
  const std::vector<std::vector<uint8_t>> invalidFiles = {
      {},                                                                // empty
      {'P', 'K', 'E', 'X'},                                              // truncated
      std::vector<uint8_t>(pokemon::extras::EXTRAS_FILE_V1_BYTES, 0x00),  // zeros
      std::vector<uint8_t>(pokemon::extras::EXTRAS_FILE_READ_MAX_BYTES + 1, 0x5A),  // oversized
  };
  for (const auto& bytes : invalidFiles) {
    Storage.clear();
    writeFile(STORE_PATH_A, bytes);
    writeFile(STORE_PATH_B, bytes);
    PokemonExtrasStore store(testEntropy);
    CHECK(store.state().featureMask == pokemon::extras::DEFAULT_FEATURE_MASK);
    CHECK(store.state().rngState != 0);
    CHECK(!store.readOnly());
    // And it is writable again straight away.
    CHECK(store.setFeatureEnabled(Feature::Eggs, false));
    PokemonExtrasStore reader(testEntropy);
    CHECK(!reader.isFeatureEnabled(Feature::Eggs));
  }
}

void aNewerVersionFileLoadsDefaultsAndIsNeverWritten() {
  // Newest slot (B, sequence 9) is from a newer build; A holds an older v1 file.
  Storage.clear();
  {
    PokemonExtrasStore writer(testEntropy);
    CHECK(writer.setFeatureEnabled(Feature::Eggs, false));  // A, sequence 1
  }
  writeFile(STORE_PATH_B, newerVersionFile(9));
  const std::vector<uint8_t> beforeA = readFile(STORE_PATH_A);
  const std::vector<uint8_t> beforeB = readFile(STORE_PATH_B);

  PokemonExtrasStore store(testEntropy);
  CHECK(store.readOnly());
  CHECK(store.state().featureMask == pokemon::extras::DEFAULT_FEATURE_MASK);  // defaults, not A's data
  CHECK(!store.setFeatureEnabled(Feature::DayCare, false));
  CHECK(store.isFeatureEnabled(Feature::DayCare));  // the failed toggle did not stick in memory
  CHECK(!store.commit(ExtrasState{}));
  CHECK(!store.resetProgress());
  CHECK(readFile(STORE_PATH_A) == beforeA);
  CHECK(readFile(STORE_PATH_B) == beforeB);

  // A newer file in the OLDER slot is not written over either.
  Storage.clear();
  writeFile(STORE_PATH_A, newerVersionFile(1));
  {
    ExtrasState state{};
    pokemon::extras::seedRandom(state, 5);
    pokemon::extras::ExtrasFileBytes bytes{};
    CHECK(pokemon::extras::encodeExtrasFile(state, 2, bytes));
    writeFile(STORE_PATH_B, {bytes.begin(), bytes.end()});
  }
  const std::vector<uint8_t> olderNewerA = readFile(STORE_PATH_A);
  PokemonExtrasStore second(testEntropy);
  CHECK(second.readOnly());
  CHECK(!second.setFeatureEnabled(Feature::Eggs, false));
  CHECK(readFile(STORE_PATH_A) == olderNewerA);
}

void aWriteFailureLeavesTheActiveFileAndMemoryUnchanged() {
  Storage.clear();
  PokemonExtrasStore store(testEntropy);
  CHECK(store.setFeatureEnabled(Feature::Eggs, false));  // A, active
  Storage.setFailWritableOpen(true);
  CHECK(!store.setFeatureEnabled(Feature::Eggs, true));
  Storage.setFailWritableOpen(false);
  CHECK(!store.isFeatureEnabled(Feature::Eggs));
  CHECK(!Storage.exists(STORE_PATH_B));
  PokemonExtrasStore reader(testEntropy);
  CHECK(!reader.isFeatureEnabled(Feature::Eggs));
}

void aCorruptedWriteIsDetectedAndDoesNotBecomeActive() {
  Storage.clear();
  PokemonExtrasStore store(testEntropy);
  Storage.setCorruptNextWrite(STORE_PATH_A, 11);
  CHECK(!store.setFeatureEnabled(Feature::Eggs, false));
  CHECK(store.isFeatureEnabled(Feature::Eggs));
  PokemonExtrasStore reader(testEntropy);
  CHECK(reader.isFeatureEnabled(Feature::Eggs));
}

void anInvalidStateIsRefused() {
  Storage.clear();
  PokemonExtrasStore store(testEntropy);
  ExtrasState invalid = store.state();
  invalid.streakXpRemainderPermille = 1000;  // must be < 1000
  CHECK(!store.commit(invalid));
  CHECK(!Storage.exists(STORE_PATH_A));
}

void aTransientReadFailureNeverOverwritesTheRealFile() {
  Storage.clear();
  {
    PokemonExtrasStore writer(testEntropy);
    CHECK(writer.setFeatureEnabled(Feature::Eggs, false));
  }
  const std::vector<uint8_t> before = readFile(STORE_PATH_A);
  PokemonExtrasStore store(testEntropy);
  Storage.setFailRead(true);
  CHECK(store.isFeatureEnabled(Feature::Eggs));  // defaults for now
  CHECK(!store.setFeatureEnabled(Feature::Streaks, false));
  Storage.setFailRead(false);
  CHECK(readFile(STORE_PATH_A) == before);
  CHECK(!Storage.exists(STORE_PATH_B));
  CHECK(!store.isFeatureEnabled(Feature::Eggs));  // recovered on the next call
}

void resetProgressKeepsTogglesAndRng() {
  Storage.clear();
  PokemonExtrasStore store(testEntropy);
  CHECK(store.setFeatureEnabled(Feature::Eggs, false));
  CHECK(store.commit(progressState(store.state())));
  const uint32_t rng = store.state().rngState;
  CHECK(store.resetProgress());

  PokemonExtrasStore reader(testEntropy);
  const ExtrasState& state = reader.state();
  CHECK(!reader.isFeatureEnabled(Feature::Eggs));
  CHECK(state.rngState == rng);
  CHECK(state.eggSpeciesId == 0);
  CHECK(state.ribbonCount == 0);
  CHECK(state.dayCareRecordId == 0);
}

void resetProgressWipesAStoreThatCannotBeRead() {
  Storage.clear();
  {
    PokemonExtrasStore writer(testEntropy);
    CHECK(writer.commit(progressState(writer.state())));
    CHECK(writer.setFeatureEnabled(Feature::Eggs, false));  // both slots exist
  }
  PokemonExtrasStore store(testEntropy);
  Storage.setFailRead(true);
  CHECK(store.resetProgress());
  Storage.setFailRead(false);
  CHECK(!Storage.exists(STORE_PATH_A));
  CHECK(!Storage.exists(STORE_PATH_B));
  PokemonExtrasStore reader(testEntropy);
  CHECK(reader.state().eggSpeciesId == 0);
  CHECK(store.setFeatureEnabled(Feature::Eggs, false));  // usable straight after
}

}  // namespace

int main() {
  missingFilesLoadDefaultsAndWriteNothing();
  toggleIsPersistedAndAFreshInstanceReadsItBack();
  successiveWritesAlternateBetweenTheTwoFiles();
  corruptingTheNewerFileFallsBackToTheOlderOne();
  invalidFilesAreEmptyExtrasNotAnError();
  aNewerVersionFileLoadsDefaultsAndIsNeverWritten();
  aWriteFailureLeavesTheActiveFileAndMemoryUnchanged();
  aCorruptedWriteIsDetectedAndDoesNotBecomeActive();
  anInvalidStateIsRefused();
  aTransientReadFailureNeverOverwritesTheRealFile();
  resetProgressKeepsTogglesAndRng();
  resetProgressWipesAStoreThatCannotBeRead();
  if (failures == 0) std::printf("PokemonExtrasStoreTest: all checks passed\n");
  return failures == 0 ? 0 : 1;
}
