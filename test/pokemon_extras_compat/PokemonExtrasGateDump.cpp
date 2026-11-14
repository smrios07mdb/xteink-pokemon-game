// Milestone gate helper (docs/extras/SAVE-SAFETY.md, test ladder step 3). Not a
// test: it has no checks of its own and CI does not run it.
//
//   PokemonExtrasGateDump <save-dir> <out-dir> [minutes]
//
// Reads the pokemon-*.bin files of <save-dir> (a COPY of a `.crosspoint`
// folder) into the in-memory storage stub, turns every extras toggle off,
// credits `minutes` (default 10) of reading through
// PokemonService::creditMinutes() with a fixed random sequence, and writes the
// resulting pokemon-*.bin files to <out-dir>. <save-dir> is never written.
//
// Build it from two trees (the previous milestone and this one), run both on
// the same save, and compare the two output folders: with every toggle off
// they must be byte for byte the same. It only uses what M1 already had, so
// it compiles against either tree.

#include <HalStorage.h>

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "PokemonExtras.h"
#include "pokemon/PokemonBattleStore.h"
#include "pokemon/PokemonExtrasStore.h"
#include "pokemon/PokemonHallOfFameStore.h"
#include "pokemon/PokemonIvEvStore.h"
#include "pokemon/PokemonService.h"
#include "pokemon/PokemonStore.h"

namespace {

constexpr const char* SAVE_FILES[] = {
    "pokemon-a.bin",        "pokemon-b.bin",        "pokemon-moves-a.bin",  "pokemon-moves-b.bin",
    "pokemon-battle-a.bin", "pokemon-battle-b.bin", "pokemon-ivev-a.bin",   "pokemon-ivev-b.bin",
    "pokemon-hof-a.bin",    "pokemon-hof-b.bin",    "pokemon-extras-a.bin", "pokemon-extras-b.bin",
};

// A fixed sequence, the same in every build of this tool.
uint32_t sequenceRandom(void* context, const uint32_t upperExclusive) {
  auto& state = *static_cast<uint32_t*>(context);
  state = state * 1664525U + 1013904223U;
  return upperExclusive == 0 ? 0 : (state >> 8U) % upperExclusive;
}

std::string devicePath(const char* name) { return std::string("/.crosspoint/") + name; }

}  // namespace

int main(const int argc, char** argv) {
  if (argc < 3) {
    std::fprintf(stderr, "usage: %s <save-dir> <out-dir> [minutes]\n", argv[0]);
    return 2;
  }
  const std::string saveDirectory = argv[1];
  const std::string outDirectory = argv[2];
  const int minutes = argc > 3 ? std::atoi(argv[3]) : 10;
  if (minutes <= 0 || minutes > 600) return 2;

  Storage.clear();
  for (const char* name : SAVE_FILES) {
    std::ifstream input(saveDirectory + "/" + name, std::ios::binary);
    if (!input) continue;
    const std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
    FsFile file = Storage.open(devicePath(name).c_str(), O_WRONLY | O_CREAT | O_TRUNC);
    if (!bytes.empty()) file.write(bytes.data(), bytes.size());
    file.close();
  }

  pokemon::PokemonExtrasStore& extrasStore = pokemon::devicePokemonExtrasStore();
  for (uint8_t index = 0; index < pokemon::extras::FEATURE_COUNT; ++index) {
    if (!extrasStore.setFeatureEnabled(static_cast<pokemon::extras::Feature>(index), false)) {
      std::fprintf(stderr, "could not turn an extras toggle off\n");
      return 1;
    }
  }

  uint32_t randomState = 0x13572468U;
  bool credited = false;
  {
    pokemon::PokemonStore store;
    pokemon::PokemonBattleStore battleStore;
    pokemon::PokemonIvEvStore ivEvStore;
    pokemon::PokemonHallOfFameStore hallOfFameStore;
    pokemon::PokemonService service(store, battleStore, ivEvStore, hallOfFameStore, {&randomState, sequenceRandom});
    if (store.begin() != pokemon::StoreBeginResult::Ready) {
      std::fprintf(stderr, "no readable Pokemon save in %s\n", saveDirectory.c_str());
      return 1;
    }
    credited = service.creditMinutes(static_cast<uint16_t>(minutes), 40);
  }

  for (const char* name : SAVE_FILES) {
    const std::string path = devicePath(name);
    if (!Storage.exists(path.c_str())) continue;
    FsFile file = Storage.open(path.c_str(), O_RDONLY);
    std::vector<uint8_t> bytes(static_cast<size_t>(file.fileSize64()));
    if (!bytes.empty()) file.read(bytes.data(), bytes.size());
    file.close();
    std::ofstream output(outDirectory + "/" + name, std::ios::binary | std::ios::trunc);
    output.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    if (!output) {
      std::fprintf(stderr, "could not write %s/%s\n", outDirectory.c_str(), name);
      return 1;
    }
  }
  std::printf("PokemonExtrasGateDump: %d minutes %s, files written to %s\n", minutes,
              credited ? "credited" : "NOT credited", outDirectory.c_str());
  return credited ? 0 : 1;
}
