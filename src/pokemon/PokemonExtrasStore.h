#pragma once

#if defined(CROSSINK_ENABLE_POKEMON) && defined(CROSSINK_POKEMON_EXTRAS)

#include <PokemonExtras.h>
#include <PokemonExtrasCodec.h>

#include <cstdint>

namespace pokemon {

// Reads/writes /.crosspoint/pokemon-extras-{a,b}.bin (docs/extras/PLAN.md),
// with the same double-buffered write-then-verify-then-flip pattern as
// PokemonIvEvStore: a write always lands on the inactive file, is read back
// and compared byte for byte, and only then becomes the active one.
//
// Extras are optional (docs/extras/SAVE-SAFETY.md R5): a missing or corrupt
// file loads as the default ExtrasState and is not an error. A file written
// by a newer extras build (DecodeStatus::NewerVersion) also loads as the
// defaults, and then the store refuses every write, so neither slot is ever
// overwritten with older data. A failed write never touches the main save:
// callers log it and carry on.
//
// Loaded lazily on first use. The state (~1 KB) lives inside the store
// object; file buffers are heap-allocated per call with non-throwing new.
class PokemonExtrasStore {
 public:
  using EntropySource = uint32_t (*)();

  // `entropy` seeds the extras RNG when the loaded state has rngState == 0
  // (fresh install). nullptr = hardware entropy (esp_random() on device).
  explicit PokemonExtrasStore(EntropySource entropy = nullptr) : entropy_(entropy) {}

  // Reads whichever of the two files is newest. Called automatically by the
  // other methods on first use; call it again to force a re-read.
  void load();

  // The current state (defaults until a valid file is read).
  const extras::ExtrasState& state();
  bool isFeatureEnabled(extras::Feature feature);

  // True when a slot holds a file from a newer extras build: every write is
  // refused for the rest of this boot.
  bool readOnly();

  // Writes `state` to the inactive slot, verifies it, then flips. Returns
  // false (and leaves memory and both files as they were) on a validation,
  // write or verify failure, on a read-only store, or while the existing
  // files could not be read.
  bool commit(const extras::ExtrasState& state);

  // Flips one toggle and commits it. The in-memory toggle is unchanged when
  // the commit fails.
  bool setFeatureEnabled(extras::Feature feature, bool enabled);

  // Game reset (PokemonService::reset(), after the main save): clears all
  // extras progress, keeps the toggles and the RNG stream, and commits.
  bool resetProgress();

 private:
  bool ensureLoaded();
  uint32_t nextEntropy() const;

  EntropySource entropy_ = nullptr;
  extras::ExtrasState state_{};
  bool loaded_ = false;
  bool ready_ = false;      // one of the two files holds the active, decoded state
  bool activeIsA_ = false;  // which file ready_ refers to
  bool readOnly_ = false;   // a slot was written by a newer extras build
  uint32_t sequence_ = 0;   // sequence of the active file; 0 when !ready_
};

PokemonExtrasStore& devicePokemonExtrasStore();

}  // namespace pokemon

#endif
