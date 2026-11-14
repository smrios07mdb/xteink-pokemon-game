#if defined(CROSSINK_ENABLE_POKEMON) && defined(CROSSINK_POKEMON_EXTRAS)

#include "PokemonExtrasCodec.h"

#include "PokemonCrc32.h"

namespace pokemon::extras {
namespace {

constexpr uint8_t MAGIC[4] = {'P', 'K', 'E', 'X'};
constexpr uint32_t CRC_INITIAL = 0xFFFFFFFFU;

void write16(uint8_t* bytes, const size_t offset, const uint16_t value) {
  bytes[offset] = static_cast<uint8_t>(value);
  bytes[offset + 1] = static_cast<uint8_t>(value >> 8U);
}

void write32(uint8_t* bytes, const size_t offset, const uint32_t value) {
  bytes[offset] = static_cast<uint8_t>(value);
  bytes[offset + 1] = static_cast<uint8_t>(value >> 8U);
  bytes[offset + 2] = static_cast<uint8_t>(value >> 16U);
  bytes[offset + 3] = static_cast<uint8_t>(value >> 24U);
}

uint16_t read16(const uint8_t* bytes, const size_t offset) {
  return static_cast<uint16_t>(bytes[offset] | (static_cast<uint16_t>(bytes[offset + 1]) << 8U));
}

uint32_t read32(const uint8_t* bytes, const size_t offset) {
  return static_cast<uint32_t>(bytes[offset]) | (static_cast<uint32_t>(bytes[offset + 1]) << 8U) |
         (static_cast<uint32_t>(bytes[offset + 2]) << 16U) | (static_cast<uint32_t>(bytes[offset + 3]) << 24U);
}

uint32_t fileCrc(const uint8_t* data, const size_t size) {
  return updatePokemonCrc32(CRC_INITIAL, data, size) ^ 0xFFFFFFFFU;
}

// One field list drives both directions, so the encoder and decoder cannot
// drift apart. Append new fields at the end only (and only in a new version).
class Writer {
 public:
  explicit Writer(uint8_t* bytes) : bytes_(bytes) {}
  void u8(const uint8_t& value) { bytes_[offset_++] = value; }
  void u16(const uint16_t& value) {
    write16(bytes_, offset_, value);
    offset_ += 2;
  }
  void u32(const uint32_t& value) {
    write32(bytes_, offset_, value);
    offset_ += 4;
  }
  size_t offset() const { return offset_; }

 private:
  uint8_t* bytes_;
  size_t offset_ = 0;
};

class Reader {
 public:
  explicit Reader(const uint8_t* bytes) : bytes_(bytes) {}
  void u8(uint8_t& value) { value = bytes_[offset_++]; }
  void u16(uint16_t& value) {
    value = read16(bytes_, offset_);
    offset_ += 2;
  }
  void u32(uint32_t& value) {
    value = read32(bytes_, offset_);
    offset_ += 4;
  }
  size_t offset() const { return offset_; }

 private:
  const uint8_t* bytes_;
  size_t offset_ = 0;
};

template <typename IO, typename State>
void visitV1(IO& io, State& s) {
  io.u8(s.featureMask);
  io.u32(s.rngState);

  io.u16(s.eggSpeciesId);
  io.u16(s.eggPagesRemaining);
  io.u8(s.eggFlags);
  io.u8(s.eggMisses);
  io.u32(s.pendingHatchRecordId);

  io.u16(s.streakXpRemainderPermille);
  io.u16(s.lastRewardedStreakMilestone);
  io.u32(s.lastRewardedStreakDay);

  io.u16(s.rewardedBookCount);
  for (auto& hash : s.rewardedBookHashes) io.u32(hash);
  io.u16(s.heldBossSpeciesId);
  io.u16(s.ribbonCount);

  io.u8(s.kindleMarkCount);
  for (auto& mark : s.kindleMarks) {
    io.u32(mark.documentHash);
    io.u16(mark.highWaterPermille);
  }
  io.u16(s.kindlePendingNoticeMinutes);

  io.u8(s.battlePasses);
  io.u32(s.passMinuteAnchor);
  for (auto& wins : s.rematchWins) io.u8(wins);
  io.u16(s.towerBestStreak);
  io.u32(s.towerRunSeed);
  io.u16(s.towerRunWins);

  io.u32(s.dayCareRecordId);
  io.u8(s.dayCareMinuteRemainder);

  io.u32(s.questDayAnchor);
  for (auto& id : s.questIds) io.u8(id);
  for (auto& progress : s.questProgress) io.u16(progress);
  io.u8(s.questDoneMask);
  io.u8(s.questRewardedMask);
}

}  // namespace

bool encodeExtrasFile(const ExtrasState& state, const uint32_t sequence, ExtrasFileBytes& output) {
  if (sequence == 0 || !validateExtrasState(state)) return false;
  ExtrasFileBytes candidate{};
  for (size_t index = 0; index < 4; ++index) candidate[index] = MAGIC[index];
  candidate[4] = EXTRAS_STORE_VERSION;
  write16(candidate.data(), 5, static_cast<uint16_t>(EXTRAS_PAYLOAD_V1_BYTES));
  write32(candidate.data(), 7, sequence);

  Writer writer(candidate.data() + EXTRAS_HEADER_BYTES);
  visitV1(writer, state);
  if (writer.offset() != EXTRAS_PAYLOAD_V1_BYTES) return false;  // layout constant out of date

  const size_t crcOffset = EXTRAS_HEADER_BYTES + EXTRAS_PAYLOAD_V1_BYTES;
  write32(candidate.data(), crcOffset, fileCrc(candidate.data(), crcOffset));
  output = candidate;
  return true;
}

DecodeStatus decodeExtrasFile(const uint8_t* data, const size_t size, ExtrasState& output, uint32_t& sequence) {
  if (data == nullptr || size < EXTRAS_HEADER_BYTES + EXTRAS_CRC_BYTES) return DecodeStatus::Invalid;
  for (size_t index = 0; index < 4; ++index) {
    if (data[index] != MAGIC[index]) return DecodeStatus::Invalid;
  }
  const uint8_t version = data[4];
  const size_t payloadBytes = read16(data, 5);
  const uint32_t fileSequence = read32(data, 7);
  if (version == 0 || fileSequence == 0) return DecodeStatus::Invalid;
  if (size != EXTRAS_HEADER_BYTES + payloadBytes + EXTRAS_CRC_BYTES) return DecodeStatus::Invalid;

  const size_t crcOffset = EXTRAS_HEADER_BYTES + payloadBytes;
  if (read32(data, crcOffset) != fileCrc(data, crcOffset)) return DecodeStatus::Invalid;

  if (version > EXTRAS_STORE_VERSION) {
    sequence = fileSequence;
    return DecodeStatus::NewerVersion;
  }
  if (payloadBytes != EXTRAS_PAYLOAD_V1_BYTES) return DecodeStatus::Invalid;

  ExtrasState candidate{};
  Reader reader(data + EXTRAS_HEADER_BYTES);
  visitV1(reader, candidate);
  if (reader.offset() != EXTRAS_PAYLOAD_V1_BYTES) return DecodeStatus::Invalid;
  if (!validateExtrasState(candidate)) return DecodeStatus::Invalid;

  output = candidate;
  sequence = fileSequence;
  return DecodeStatus::Ok;
}

}  // namespace pokemon::extras

#endif
