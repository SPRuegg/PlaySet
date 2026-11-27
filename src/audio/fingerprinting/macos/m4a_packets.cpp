#include "m4a_packets.hpp"

#include "audio/fingerprinting/fingerprint_encoding.hpp"

#include <CoreFoundation/CoreFoundation.h>
#include <limits>
#include <stdexcept>

namespace playset::detail {
namespace {
constexpr UInt32 maximum_packet_bytes = 1024 * 1024;

void check(OSStatus status) {
  if (status != noErr)
    throw std::runtime_error("Could not read encoded audio packets (AudioToolbox " +
                             std::to_string(status) + ").");
}

template <class T> T property(AudioFileID file, AudioFilePropertyID id) {
  T result{};
  UInt32 size = sizeof(result);
  check(AudioFileGetProperty(file, id, &size, &result));
  if (size != sizeof(result)) throw std::runtime_error("Invalid encoded audio property size.");
  return result;
}
}

M4aPackets::M4aPackets(const StemInfo &info) {
  const auto path = info.path.string();
  const auto url = CFURLCreateFromFileSystemRepresentation(kCFAllocatorDefault,
      reinterpret_cast<const UInt8 *>(path.data()), static_cast<CFIndex>(path.size()), false);
  if (!url) throw std::runtime_error("Could not create encoded audio URL.");
  const auto status = AudioFileOpenURL(url, kAudioFileReadPermission, 0, &file_);
  CFRelease(url);
  try {
    check(status);
    const auto format = property<AudioStreamBasicDescription>(file_, kAudioFilePropertyDataFormat);
    codec_ = format.mFormatID;
    frames_per_packet_ = format.mFramesPerPacket;
    packet_count_ = property<UInt64>(file_, kAudioFilePropertyAudioDataPacketCount);
    const auto maximum = property<UInt32>(file_, kAudioFilePropertyPacketSizeUpperBound);
    if ((codec_ != kAudioFormatMPEG4AAC && codec_ != kAudioFormatAppleLossless) ||
        frames_per_packet_ == 0 || packet_count_ == 0 ||
        packet_count_ > static_cast<UInt64>(std::numeric_limits<SInt64>::max()) ||
        maximum == 0 || maximum > maximum_packet_bytes)
      throw std::runtime_error("Unsupported encoded audio packet layout.");
    packet_.resize(maximum);
    AudioFilePacketTableInfo table{};
    UInt32 size = sizeof(table);
    const auto table_status = AudioFileGetProperty(file_, kAudioFilePropertyPacketTableInfo, &size, &table);
    if (table_status == noErr) {
      if (size != sizeof(table) || table.mPrimingFrames < 0)
        throw std::runtime_error("Invalid encoded audio priming length.");
      priming_frames_ = static_cast<std::uint64_t>(table.mPrimingFrames);
    } else if (table_status != kAudioFileUnsupportedPropertyError) {
      check(table_status);
    }
    UInt32 cookie_size = 0;
    const auto cookie_status = AudioFileGetPropertyInfo(file_, kAudioFilePropertyMagicCookieData,
                                                       &cookie_size, nullptr);
    if (cookie_status == noErr && cookie_size != 0) {
      if (cookie_size > maximum_packet_bytes) throw std::runtime_error("Encoded audio cookie is too large.");
      cookie_.resize(cookie_size);
      check(AudioFileGetProperty(file_, kAudioFilePropertyMagicCookieData, &cookie_size, cookie_.data()));
      if (cookie_size > cookie_.size()) throw std::runtime_error("Encoded audio cookie changed size.");
      cookie_.resize(cookie_size);
    } else if (cookie_status != noErr && cookie_status != kAudioFileUnsupportedPropertyError) {
      check(cookie_status);
    }
  } catch (...) {
    if (file_) AudioFileClose(file_);
    file_ = nullptr;
    throw;
  }
}

M4aPackets::~M4aPackets() { if (file_) AudioFileClose(file_); }

void M4aPackets::hash_format(Sha256 &hash) const {
  hash_integer(hash, codec_);
  hash_integer(hash, frames_per_packet_);
  hash_integer(hash, priming_frames_);
  hash_integer(hash, static_cast<std::uint32_t>(cookie_.size()));
  hash.update(cookie_);
}

void M4aPackets::hash_window(Sha256 &hash, std::int64_t start, std::int64_t frames) {
  const auto first_frame = static_cast<std::uint64_t>(start) + priming_frames_;
  const auto end_frame = first_frame + static_cast<std::uint64_t>(frames);
  const auto first_packet = first_frame / frames_per_packet_;
  const auto end_packet = end_frame / frames_per_packet_ + (end_frame % frames_per_packet_ != 0);
  if (end_packet > packet_count_ || first_packet >= end_packet)
    throw std::runtime_error("Encoded audio ended during fingerprinting.");
  hash_integer(hash, static_cast<std::uint64_t>(start));
  hash_integer(hash, first_packet);
  hash_integer(hash, end_packet - first_packet);
  for (auto index = first_packet; index < end_packet; ++index) {
    UInt32 bytes = static_cast<UInt32>(packet_.size());
    UInt32 packets = 1;
    AudioStreamPacketDescription description{};
    check(AudioFileReadPacketData(file_, false, &bytes, &description,
                                 static_cast<SInt64>(index), &packets, packet_.data()));
    if (packets != 1 || bytes == 0 || bytes > packet_.size())
      throw std::runtime_error("Incomplete encoded audio packet.");
    hash_integer(hash, bytes);
    hash.update(std::span(packet_).first(bytes));
  }
}

} // namespace playset::detail
