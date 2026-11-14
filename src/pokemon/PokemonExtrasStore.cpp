#if defined(CROSSINK_ENABLE_POKEMON) && defined(CROSSINK_POKEMON_EXTRAS)

#include "PokemonExtrasStore.h"

#include <HalStorage.h>
#include <Logging.h>

#include <array>
#include <cstring>
#include <memory>
#include <new>

#if defined(SIMULATOR) || defined(POKEMON_SERVICE_HOST_TEST)
#include <random>
#else
#include <esp_random.h>
#endif

namespace pokemon {
namespace {

constexpr const char* STORE_DIRECTORY = "/.crosspoint";
constexpr const char* STORE_PATH_A = "/.crosspoint/pokemon-extras-a.bin";
constexpr const char* STORE_PATH_B = "/.crosspoint/pokemon-extras-b.bin";

using ReadBuffer = std::array<uint8_t, extras::EXTRAS_FILE_READ_MAX_BYTES>;

uint32_t hardwareEntropy() {
#if defined(SIMULATOR) || defined(POKEMON_SERVICE_HOST_TEST)
  return std::random_device{}();
#else
  return esp_random();
#endif
}

bool readExact(FsFile& file, void* output, const size_t size) {
  return file.read(output, size) == static_cast<int>(size);
}

bool writeExact(FsFile& file, const void* input, const size_t size) { return file.write(input, size) == size; }

// Same wraparound-safe comparison as PokemonIvEvStore.
bool sequenceIsNewer(const uint32_t candidate, const uint32_t current) {
  return candidate != current && candidate - current < 0x80000000U;
}

// Deletes both slot files. Used only by resetProgress() when the files could
// not be read: a deliberate wipe must not be skipped, but it must not write
// over files it cannot inspect either (same as PokemonIvEvStore::reset()).
bool discardSlotFiles() {
  for (const char* path : {STORE_PATH_A, STORE_PATH_B}) {
    if (Storage.exists(path) && !Storage.remove(path)) return false;
  }
  return true;
}

enum class SlotResult : uint8_t { Missing, Invalid, Ok, NewerVersion, ReadFailed };

// Reads and decodes one slot. Missing and Invalid both mean "not a candidate"
// (SAVE-SAFETY R5). ReadFailed says nothing about the contents (open failed,
// out of memory, short read), so load() must not treat it as an empty slot.
SlotResult inspectSlot(const char* path, extras::ExtrasState& output, uint32_t& sequence) {
  if (!Storage.exists(path)) return SlotResult::Missing;
  FsFile file = Storage.open(path, O_RDONLY);
  if (!file) {
    LOG_ERR("PokemonExtrasStore", "Failed to open %s", path);
    return SlotResult::ReadFailed;
  }
  const uint64_t fileSize = file.fileSize64();
  if (fileSize == 0 || fileSize > extras::EXTRAS_FILE_READ_MAX_BYTES) {
    LOG_ERR("PokemonExtrasStore", "Extras slot has an impossible size, ignoring %s", path);
    file.close();
    return SlotResult::Invalid;
  }
  std::unique_ptr<ReadBuffer> bytes(new (std::nothrow) ReadBuffer());
  if (!bytes) {
    LOG_ERR("PokemonExtrasStore", "Out of memory reading %s", path);
    file.close();
    return SlotResult::ReadFailed;
  }
  const size_t size = static_cast<size_t>(fileSize);
  const bool readOk = readExact(file, bytes->data(), size);
  file.close();
  if (!readOk) {
    LOG_ERR("PokemonExtrasStore", "Short read on %s", path);
    return SlotResult::ReadFailed;
  }
  switch (extras::decodeExtrasFile(bytes->data(), size, output, sequence)) {
    case extras::DecodeStatus::Ok:
      return SlotResult::Ok;
    case extras::DecodeStatus::NewerVersion:
      return SlotResult::NewerVersion;
    case extras::DecodeStatus::Invalid:
    default:
      LOG_ERR("PokemonExtrasStore", "Extras slot is not valid, ignoring %s", path);
      return SlotResult::Invalid;
  }
}

bool hasSequence(const SlotResult result) {
  return result == SlotResult::Ok || result == SlotResult::NewerVersion;
}

}  // namespace

uint32_t PokemonExtrasStore::nextEntropy() const { return entropy_ != nullptr ? entropy_() : hardwareEntropy(); }

void PokemonExtrasStore::load() {
  state_ = extras::ExtrasState{};
  ready_ = false;
  activeIsA_ = false;
  readOnly_ = false;
  sequence_ = 0;
  loaded_ = true;

  // Heap, not stack: two ~1 KB states plus the 2 KB read buffer would be a
  // lot for the task stacks this can run on (see PokemonIvEvStore::load()).
  std::unique_ptr<extras::ExtrasState> stateA(new (std::nothrow) extras::ExtrasState());
  std::unique_ptr<extras::ExtrasState> stateB(new (std::nothrow) extras::ExtrasState());
  if (!stateA || !stateB) {
    LOG_ERR("PokemonExtrasStore", "Out of memory loading extras, will retry");
    loaded_ = false;
    return;
  }
  uint32_t sequenceA = 0;
  uint32_t sequenceB = 0;
  const SlotResult resultA = inspectSlot(STORE_PATH_A, *stateA, sequenceA);
  const SlotResult resultB = inspectSlot(STORE_PATH_B, *stateB, sequenceB);
  if (resultA == SlotResult::ReadFailed || resultB == SlotResult::ReadFailed) {
    loaded_ = false;  // reads see defaults for now, writes are refused, the next call retries
    return;
  }

  // Never overwrite a newer build's file, even the older of the two slots.
  readOnly_ = resultA == SlotResult::NewerVersion || resultB == SlotResult::NewerVersion;

  if (hasSequence(resultA) || hasSequence(resultB)) {
    const bool newestIsA = hasSequence(resultA) && (!hasSequence(resultB) || !sequenceIsNewer(sequenceB, sequenceA));
    const SlotResult newest = newestIsA ? resultA : resultB;
    if (newest == SlotResult::Ok) {
      state_ = newestIsA ? *stateA : *stateB;
      sequence_ = newestIsA ? sequenceA : sequenceB;
      activeIsA_ = newestIsA;
      ready_ = true;
    } else {
      LOG_INF("PokemonExtrasStore", "Extras file is from a newer build: using defaults, not writing");
    }
  }

  if (state_.rngState == 0) extras::seedRandom(state_, nextEntropy());
}

bool PokemonExtrasStore::ensureLoaded() {
  if (!loaded_) load();
  return loaded_;
}

const extras::ExtrasState& PokemonExtrasStore::state() {
  ensureLoaded();
  return state_;
}

bool PokemonExtrasStore::isFeatureEnabled(const extras::Feature feature) {
  ensureLoaded();
  return extras::isFeatureEnabled(state_, feature);
}

bool PokemonExtrasStore::readOnly() {
  ensureLoaded();
  return readOnly_;
}

bool PokemonExtrasStore::commit(const extras::ExtrasState& state) {
  if (!ensureLoaded()) return false;  // couldn't read the existing files: don't write over them
  if (readOnly_) {
    LOG_ERR("PokemonExtrasStore", "Extras file is from a newer build, not writing");
    return false;
  }
  if (!Storage.ensureDirectoryExists(STORE_DIRECTORY)) {
    LOG_ERR("PokemonExtrasStore", "Failed to prepare data directory");
    return false;
  }
  const uint32_t nextSequence = !ready_ ? 1U : (sequence_ == UINT32_MAX ? 1U : sequence_ + 1U);
  std::unique_ptr<extras::ExtrasFileBytes> bytes(new (std::nothrow) extras::ExtrasFileBytes());
  if (!bytes) {
    LOG_ERR("PokemonExtrasStore", "Out of memory encoding extras");
    return false;
  }
  if (!extras::encodeExtrasFile(state, nextSequence, *bytes)) {
    LOG_ERR("PokemonExtrasStore", "Refusing to write an invalid extras state");
    return false;
  }
  const size_t size = bytes->size();

  const bool destinationIsA = !ready_ || !activeIsA_;
  const char* destinationPath = destinationIsA ? STORE_PATH_A : STORE_PATH_B;
  FsFile file = Storage.open(destinationPath, O_WRONLY | O_CREAT | O_TRUNC);
  if (!file) {
    LOG_ERR("PokemonExtrasStore", "Failed to open %s for write", destinationPath);
    return false;
  }
  const bool writeOk = writeExact(file, bytes->data(), size) && file.sync();
  const bool closeOk = file.close();
  if (!writeOk || !closeOk) {
    LOG_ERR("PokemonExtrasStore", "Failed to write %s", destinationPath);
    return false;
  }

  // Byte-for-byte read-back, like PokemonIvEvStore::writeState().
  FsFile verifyFile = Storage.open(destinationPath, O_RDONLY);
  bool verifiedOk = false;
  if (verifyFile && verifyFile.fileSize64() == size) {
    std::unique_ptr<extras::ExtrasFileBytes> readBack(new (std::nothrow) extras::ExtrasFileBytes());
    verifiedOk = readBack != nullptr && readExact(verifyFile, readBack->data(), size) &&
                 std::memcmp(readBack->data(), bytes->data(), size) == 0;
  }
  const bool verifyCloseOk = !verifyFile || verifyFile.close();
  if (!verifiedOk || !verifyCloseOk) {
    LOG_ERR("PokemonExtrasStore", "Inactive extras slot verification failed");
    return false;
  }

  state_ = state;
  sequence_ = nextSequence;
  activeIsA_ = destinationIsA;
  ready_ = true;
  return true;
}

bool PokemonExtrasStore::setFeatureEnabled(const extras::Feature feature, const bool enabled) {
  if (!ensureLoaded()) return false;
  std::unique_ptr<extras::ExtrasState> candidate(new (std::nothrow) extras::ExtrasState(state_));
  if (!candidate) {
    LOG_ERR("PokemonExtrasStore", "Out of memory changing an extras toggle");
    return false;
  }
  extras::setFeatureEnabled(*candidate, feature, enabled);
  return commit(*candidate);
}

bool PokemonExtrasStore::resetProgress() {
  if (!ensureLoaded()) {
    // Unreadable files: a game reset still wipes them (deleted, never written
    // over blind), exactly like PokemonIvEvStore::reset(). Toggles return to
    // their defaults.
    if (!discardSlotFiles()) return false;
    load();
    return loaded_;
  }
  if (readOnly_) {
    LOG_ERR("PokemonExtrasStore", "Extras file is from a newer build, not resetting it");
    return false;
  }
  std::unique_ptr<extras::ExtrasState> candidate(new (std::nothrow) extras::ExtrasState(state_));
  if (!candidate) {
    LOG_ERR("PokemonExtrasStore", "Out of memory resetting extras");
    return false;
  }
  extras::resetExtrasProgress(*candidate);
  return commit(*candidate);
}

PokemonExtrasStore& devicePokemonExtrasStore() {
  // Heap, not .bss (same reason as the IV/EV store in devicePokemonService()),
  // and only on first use.
  static auto& store = *new PokemonExtrasStore();
  return store;
}

}  // namespace pokemon

#endif
