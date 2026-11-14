#include <cstdio>
#include <cstring>
#include <vector>

#include "PokemonCrc32.h"
#include "PokemonExtrasCodec.h"

namespace {

using namespace pokemon::extras;

int failures = 0;

#define CHECK(condition)                                                                \
  do {                                                                                  \
    if (!(condition)) {                                                                 \
      std::fprintf(stderr, "%s:%d check failed: %s\n", __FILE__, __LINE__, #condition); \
      ++failures;                                                                       \
    }                                                                                   \
  } while (false)

uint16_t read16(const uint8_t* bytes, const size_t offset) {
  return static_cast<uint16_t>(bytes[offset] | (bytes[offset + 1] << 8));
}

uint32_t read32(const uint8_t* bytes, const size_t offset) {
  return static_cast<uint32_t>(bytes[offset]) | (static_cast<uint32_t>(bytes[offset + 1]) << 8U) |
         (static_cast<uint32_t>(bytes[offset + 2]) << 16U) | (static_cast<uint32_t>(bytes[offset + 3]) << 24U);
}

void write16(uint8_t* bytes, const size_t offset, const uint16_t value) {
  bytes[offset] = static_cast<uint8_t>(value);
  bytes[offset + 1] = static_cast<uint8_t>(value >> 8U);
}

void write32(uint8_t* bytes, const size_t offset, const uint32_t value) {
  for (size_t index = 0; index < 4; ++index) bytes[offset + index] = static_cast<uint8_t>(value >> (8U * index));
}

// Recomputes the trailing CRC so a test can craft a file that passes the CRC
// check and exercises the checks behind it.
void resign(std::vector<uint8_t>& file) {
  const size_t crcOffset = file.size() - EXTRAS_CRC_BYTES;
  write32(file.data(), crcOffset, pokemon::updatePokemonCrc32(0xFFFFFFFFU, file.data(), crcOffset) ^ 0xFFFFFFFFU);
}

std::vector<uint8_t> toVector(const ExtrasFileBytes& bytes) { return {bytes.begin(), bytes.end()}; }

// Every field set to a distinct, valid, non-default value.
ExtrasState populatedState() {
  ExtrasState s{};
  s.featureMask = 0xA5;
  s.rngState = 0x12345678U;
  s.eggSpeciesId = 147;  // Dratini
  s.eggPagesRemaining = 1199;
  s.eggFlags = EGG_FLAG_SHINY;
  s.eggMisses = 2;
  s.pendingHatchRecordId = 0x01020304U;
  s.streakXpRemainderPermille = 750;
  s.lastRewardedStreakMilestone = 14;
  s.lastRewardedStreakDay = 20406;
  s.rewardedBookCount = REWARDED_BOOK_CAPACITY;
  for (size_t index = 0; index < REWARDED_BOOK_CAPACITY; ++index) {
    s.rewardedBookHashes[index] = 0xB0000000U + static_cast<uint32_t>(index);
  }
  s.heldBossSpeciesId = 144;
  s.ribbonCount = 31;
  s.kindleMarkCount = KINDLE_MARK_CAPACITY;
  for (size_t index = 0; index < KINDLE_MARK_CAPACITY; ++index) {
    s.kindleMarks[index] = {0xC0000000U + static_cast<uint32_t>(index), static_cast<uint16_t>(index * 15)};
  }
  s.kindlePendingNoticeMinutes = 42;
  s.battlePasses = 3;
  s.passMinuteAnchor = 8000;
  for (size_t index = 0; index < GYM_REMATCH_COUNT; ++index) s.rematchWins[index] = static_cast<uint8_t>(index + 1);
  s.towerBestStreak = 27;
  s.towerRunSeed = 0xDEADBEEFU;
  s.towerRunWins = 4;
  s.dayCareRecordId = 77;
  s.dayCareMinuteRemainder = 1;
  s.questDayAnchor = 20406;
  s.questIds = {static_cast<uint8_t>(QuestKind::Read60), static_cast<uint8_t>(QuestKind::EggPages100),
                static_cast<uint8_t>(QuestKind::LevelUp)};
  s.questProgress = {60, 100, 0};
  s.questDoneMask = 0x03;
  s.questRewardedMask = 0x01;
  return s;
}

void layoutConstantsAddUp() {
  CHECK(EXTRAS_PAYLOAD_V1_BYTES == 974);
  CHECK(EXTRAS_FILE_V1_BYTES == 989);
  CHECK(EXTRAS_FILE_V1_BYTES <= EXTRAS_FILE_READ_MAX_BYTES);
}

void populatedStateRoundTrips() {
  const ExtrasState original = populatedState();
  CHECK(validateExtrasState(original));
  ExtrasFileBytes bytes{};
  CHECK(encodeExtrasFile(original, 7, bytes));

  ExtrasState decoded{};
  uint32_t sequence = 0;
  CHECK(decodeExtrasFile(bytes.data(), bytes.size(), decoded, sequence) == DecodeStatus::Ok);
  CHECK(decoded == original);
  CHECK(sequence == 7);
}

void defaultStateRoundTrips() {
  const ExtrasState original{};
  ExtrasFileBytes bytes{};
  CHECK(encodeExtrasFile(original, 1, bytes));
  ExtrasState decoded = populatedState();  // prove decode overwrites everything
  uint32_t sequence = 0;
  CHECK(decodeExtrasFile(bytes.data(), bytes.size(), decoded, sequence) == DecodeStatus::Ok);
  CHECK(decoded == original);
  CHECK(sequence == 1);
}

// Pins the on-disk layout. If this fails, the format changed: that needs a
// version bump and an append-only change, never an edit to version 1.
void headerAndFieldOffsetsArePinned() {
  const ExtrasState state = populatedState();
  ExtrasFileBytes bytes{};
  CHECK(encodeExtrasFile(state, 0x0A0B0C0DU, bytes));
  const uint8_t* b = bytes.data();
  CHECK(std::memcmp(b, "PKEX", 4) == 0);
  CHECK(b[4] == 1);
  CHECK(read16(b, 5) == 974);
  CHECK(read32(b, 7) == 0x0A0B0C0DU);

  const size_t p = EXTRAS_HEADER_BYTES;
  CHECK(b[p + 0] == 0xA5);                       // featureMask
  CHECK(read32(b, p + 1) == 0x12345678U);        // rngState
  CHECK(read16(b, p + 5) == 147);                // eggSpeciesId
  CHECK(read16(b, p + 7) == 1199);               // eggPagesRemaining
  CHECK(b[p + 9] == EGG_FLAG_SHINY);             // eggFlags
  CHECK(b[p + 10] == 2);                         // eggMisses
  CHECK(read32(b, p + 11) == 0x01020304U);       // pendingHatchRecordId
  CHECK(read16(b, p + 15) == 750);               // streakXpRemainderPermille
  CHECK(read16(b, p + 17) == 14);                // lastRewardedStreakMilestone
  CHECK(read32(b, p + 19) == 20406);             // lastRewardedStreakDay
  CHECK(read16(b, p + 23) == 128);               // rewardedBookCount
  CHECK(read32(b, p + 25) == 0xB0000000U);       // rewardedBookHashes[0]
  CHECK(read32(b, p + 25 + 127 * 4) == 0xB000007FU);  // rewardedBookHashes[127]
  CHECK(read16(b, p + 537) == 144);              // heldBossSpeciesId
  CHECK(read16(b, p + 539) == 31);               // ribbonCount
  CHECK(b[p + 541] == 64);                       // kindleMarkCount
  CHECK(read32(b, p + 542) == 0xC0000000U);      // kindleMarks[0].documentHash
  CHECK(read16(b, p + 542 + 63 * 6 + 4) == 945); // kindleMarks[63].highWaterPermille
  CHECK(read16(b, p + 926) == 42);               // kindlePendingNoticeMinutes
  CHECK(b[p + 928] == 3);                        // battlePasses
  CHECK(read32(b, p + 929) == 8000);             // passMinuteAnchor
  CHECK(b[p + 933] == 1 && b[p + 945] == 13);    // rematchWins[0], [12]
  CHECK(read16(b, p + 946) == 27);               // towerBestStreak
  CHECK(read32(b, p + 948) == 0xDEADBEEFU);      // towerRunSeed
  CHECK(read16(b, p + 952) == 4);                // towerRunWins
  CHECK(read32(b, p + 954) == 77);               // dayCareRecordId
  CHECK(b[p + 958] == 1);                        // dayCareMinuteRemainder
  CHECK(read32(b, p + 959) == 20406);            // questDayAnchor
  CHECK(b[p + 963] == 3 && b[p + 964] == 8 && b[p + 965] == 9);  // questIds
  CHECK(read16(b, p + 966) == 60 && read16(b, p + 968) == 100 && read16(b, p + 970) == 0);  // questProgress
  CHECK(b[p + 972] == 0x03);                     // questDoneMask
  CHECK(b[p + 973] == 0x01);                     // questRewardedMask
}

void encodeRejectsZeroSequenceAndInvalidState() {
  const ExtrasFileBytes sentinel = [] {
    ExtrasFileBytes bytes{};
    bytes.fill(0xEE);
    return bytes;
  }();
  ExtrasFileBytes bytes = sentinel;
  CHECK(!encodeExtrasFile(ExtrasState{}, 0, bytes));
  CHECK(bytes == sentinel);

  ExtrasState invalid{};
  invalid.battlePasses = 9;
  CHECK(!encodeExtrasFile(invalid, 1, bytes));
  CHECK(bytes == sentinel);
}

void everySingleBitFlipIsRejected() {
  ExtrasFileBytes bytes{};
  CHECK(encodeExtrasFile(populatedState(), 3, bytes));
  int accepted = 0;
  for (size_t byte = 0; byte < bytes.size(); ++byte) {
    for (int bit = 0; bit < 8; ++bit) {
      ExtrasFileBytes corrupt = bytes;
      corrupt[byte] = static_cast<uint8_t>(corrupt[byte] ^ (1U << bit));
      ExtrasState decoded{};
      uint32_t sequence = 0;
      if (decodeExtrasFile(corrupt.data(), corrupt.size(), decoded, sequence) == DecodeStatus::Ok) ++accepted;
    }
  }
  CHECK(accepted == 0);
}

void truncatedOrPaddedFilesAreRejected() {
  ExtrasFileBytes bytes{};
  CHECK(encodeExtrasFile(populatedState(), 3, bytes));
  ExtrasState decoded{};
  uint32_t sequence = 0;
  CHECK(decodeExtrasFile(bytes.data(), bytes.size() - 1, decoded, sequence) == DecodeStatus::Invalid);
  CHECK(decodeExtrasFile(bytes.data(), 0, decoded, sequence) == DecodeStatus::Invalid);
  CHECK(decodeExtrasFile(nullptr, bytes.size(), decoded, sequence) == DecodeStatus::Invalid);

  std::vector<uint8_t> padded = toVector(bytes);
  padded.push_back(0);
  CHECK(decodeExtrasFile(padded.data(), padded.size(), decoded, sequence) == DecodeStatus::Invalid);
  CHECK(sequence == 0);
}

void wrongMagicOrZeroVersionOrZeroSequenceIsRejectedEvenWithAValidCrc() {
  ExtrasFileBytes bytes{};
  CHECK(encodeExtrasFile(ExtrasState{}, 5, bytes));

  std::vector<uint8_t> magic = toVector(bytes);
  magic[0] = 'X';
  resign(magic);
  std::vector<uint8_t> version = toVector(bytes);
  version[4] = 0;
  resign(version);
  std::vector<uint8_t> sequenceZero = toVector(bytes);
  write32(sequenceZero.data(), 7, 0);
  resign(sequenceZero);

  for (const auto* file : {&magic, &version, &sequenceZero}) {
    ExtrasState decoded{};
    uint32_t sequence = 0;
    CHECK(decodeExtrasFile(file->data(), file->size(), decoded, sequence) == DecodeStatus::Invalid);
  }
}

void aCrcValidButInvalidStateLeavesOutputUntouched() {
  ExtrasFileBytes bytes{};
  CHECK(encodeExtrasFile(ExtrasState{}, 5, bytes));
  std::vector<uint8_t> file = toVector(bytes);
  file[EXTRAS_HEADER_BYTES + 928] = 4;  // battlePasses above the cap of 3
  resign(file);

  const ExtrasState before = populatedState();
  ExtrasState decoded = before;
  uint32_t sequence = 99;
  CHECK(decodeExtrasFile(file.data(), file.size(), decoded, sequence) == DecodeStatus::Invalid);
  CHECK(decoded == before);
  CHECK(sequence == 99);
}

void aNewerVersionIsReportedNotDecoded() {
  // A future version 2 with 8 more payload bytes appended.
  ExtrasFileBytes bytes{};
  CHECK(encodeExtrasFile(populatedState(), 41, bytes));
  std::vector<uint8_t> file(bytes.begin(), bytes.end() - EXTRAS_CRC_BYTES);
  file[4] = 2;
  write16(file.data(), 5, static_cast<uint16_t>(EXTRAS_PAYLOAD_V1_BYTES + 8));
  for (int extra = 0; extra < 8; ++extra) file.push_back(0x5A);
  file.resize(file.size() + EXTRAS_CRC_BYTES);
  resign(file);

  const ExtrasState before{};
  ExtrasState decoded = before;
  uint32_t sequence = 0;
  CHECK(decodeExtrasFile(file.data(), file.size(), decoded, sequence) == DecodeStatus::NewerVersion);
  CHECK(decoded == before);
  CHECK(sequence == 41);

  // A newer version with a bad CRC is just corrupt.
  file[20] ^= 0x01;
  sequence = 0;
  CHECK(decodeExtrasFile(file.data(), file.size(), decoded, sequence) == DecodeStatus::Invalid);
  CHECK(sequence == 0);
}

void version1WithTheWrongPayloadLengthIsRejected() {
  ExtrasFileBytes bytes{};
  CHECK(encodeExtrasFile(ExtrasState{}, 5, bytes));
  std::vector<uint8_t> file(bytes.begin(), bytes.end() - EXTRAS_CRC_BYTES);
  write16(file.data(), 5, static_cast<uint16_t>(EXTRAS_PAYLOAD_V1_BYTES + 2));
  file.push_back(0);
  file.push_back(0);
  file.resize(file.size() + EXTRAS_CRC_BYTES);
  resign(file);
  ExtrasState decoded{};
  uint32_t sequence = 0;
  CHECK(decodeExtrasFile(file.data(), file.size(), decoded, sequence) == DecodeStatus::Invalid);
}

void encodingIsDeterministic() {
  ExtrasFileBytes first{};
  ExtrasFileBytes second{};
  CHECK(encodeExtrasFile(populatedState(), 12, first));
  CHECK(encodeExtrasFile(populatedState(), 12, second));
  CHECK(first == second);
  ExtrasFileBytes nextSequence{};
  CHECK(encodeExtrasFile(populatedState(), 13, nextSequence));
  CHECK(first != nextSequence);
}

}  // namespace

int main() {
  layoutConstantsAddUp();
  populatedStateRoundTrips();
  defaultStateRoundTrips();
  headerAndFieldOffsetsArePinned();
  encodeRejectsZeroSequenceAndInvalidState();
  everySingleBitFlipIsRejected();
  truncatedOrPaddedFilesAreRejected();
  wrongMagicOrZeroVersionOrZeroSequenceIsRejectedEvenWithAValidCrc();
  aCrcValidButInvalidStateLeavesOutputUntouched();
  aNewerVersionIsReportedNotDecoded();
  version1WithTheWrongPayloadLengthIsRejected();
  encodingIsDeterministic();
  if (failures == 0) std::printf("PokemonExtrasCodecTest: all checks passed\n");
  return failures == 0 ? 0 : 1;
}
