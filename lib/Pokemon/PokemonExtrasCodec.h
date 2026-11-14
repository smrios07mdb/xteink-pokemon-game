#pragma once

// Byte layout of /.crosspoint/pokemon-extras-{a,b}.bin (docs/extras/PLAN.md).
//
//   header   magic "PKEX" (4) | version u8 | payloadBytes u16 | sequence u32
//   payload  the ExtrasState fields, little-endian, in declaration order,
//            fixed-size arrays written in full (see PokemonExtrasCodec.cpp)
//   trailer  CRC32 (PokemonCrc32) over header + payload
//
// Versioning is append-only: a later version only adds fields at the end of
// the payload and bumps `version`; it must still decode every older version
// (missing tail fields take their ExtrasState defaults). This build writes
// and reads version 1 only. A file with a newer version is reported as
// DecodeStatus::NewerVersion so the store can refuse to overwrite it with
// older data (for example after flashing an older extras build).
//
// The .cpp compiles to nothing unless CROSSINK_POKEMON_EXTRAS is defined.

#include <array>
#include <cstddef>
#include <cstdint>

#include "PokemonExtras.h"

namespace pokemon::extras {

constexpr uint8_t EXTRAS_STORE_VERSION = 1;
constexpr size_t EXTRAS_HEADER_BYTES = 11;
constexpr size_t EXTRAS_CRC_BYTES = 4;

// Version 1 payload, field by field (kept next to the encoder for review):
//   featureMask 1, rngState 4                                        =   5
//   eggs: species 2, pages 2, flags 1, misses 1, pendingHatch 4      =  10
//   streaks: remainder 2, milestone 2, day 4                         =   8
//   book boss: count 2, hashes 128x4, heldBoss 2, ribbons 2          = 518
//   kindle: count 1, marks 64x(4+2), noticeMinutes 2                 = 387
//   post-game: passes 1, anchor 4, rematch 13, best 2, seed 4, wins 2 =  26
//   day care: recordId 4, remainder 1                                =   5
//   quests: anchor 4, ids 3, progress 3x2, done 1, rewarded 1        =  15
constexpr size_t EXTRAS_PAYLOAD_V1_BYTES = 5 + 10 + 8 + 518 + 387 + 26 + 5 + 15;  // 974
constexpr size_t EXTRAS_FILE_V1_BYTES = EXTRAS_HEADER_BYTES + EXTRAS_PAYLOAD_V1_BYTES + EXTRAS_CRC_BYTES;  // 989
// Upper bound the store should read from disk. Leaves room for later
// versions to append without the reader rejecting the file on size alone.
constexpr size_t EXTRAS_FILE_READ_MAX_BYTES = 2048;

using ExtrasFileBytes = std::array<uint8_t, EXTRAS_FILE_V1_BYTES>;

enum class DecodeStatus : uint8_t {
  Ok = 0,
  Invalid = 1,       // wrong magic/size/CRC, or the state fails validateExtrasState: treat as empty extras
  NewerVersion = 2,  // written by a newer extras build: keep the file, do not overwrite it
};

// Fails if `sequence` is 0 (reserved for "no valid slot yet") or `state`
// fails validateExtrasState. `output` is untouched on failure.
bool encodeExtrasFile(const ExtrasState& state, uint32_t sequence, ExtrasFileBytes& output);

// Decodes and verifies a file of exactly `size` bytes. On Ok, `output` and
// `sequence` are set; otherwise both are untouched. On NewerVersion,
// `sequence` is still set (header and CRC were valid) so the store can pick
// the newest slot.
DecodeStatus decodeExtrasFile(const uint8_t* data, size_t size, ExtrasState& output, uint32_t& sequence);

}  // namespace pokemon::extras
