#include "playback/loading/stem_loader.hpp"

#include <AudioToolbox/AudioToolbox.h>
#include <CoreFoundation/CoreFoundation.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <stdexcept>
#include <string>
#include <utility>

namespace playset {
namespace {
constexpr std::size_t stereo_channels = 2;
constexpr double minimum_sample_rate = 8000;
constexpr double maximum_sample_rate = 192000;
}

struct StemReader::Impl {
    ExtAudioFileRef file = nullptr;
    StemInfo info{};
    std::array<float, decode_block_frames * stereo_channels> samples{};
    std::size_t buffered_frames = 0;
    std::size_t buffered_position = 0;
    bool reached_end = false;

    ~Impl() { if (file) ExtAudioFileDispose(file); }

    void check(OSStatus status, const char* operation) const {
        if (status != noErr) {
            throw std::runtime_error(std::string(operation) + ": " + info.path.string()
                                     + " (AudioToolbox " + std::to_string(status) + ")");
        }
    }

    void decode() {
        if (reached_end) return;
        UInt32 frames = static_cast<UInt32>(decode_block_frames);
        AudioBufferList buffers{};
        buffers.mNumberBuffers = 1;
        buffers.mBuffers[0] = {info.channels,
            static_cast<UInt32>(decode_block_frames * info.channels * sizeof(float)), samples.data()};
        check(ExtAudioFileRead(file, &frames, &buffers), "Could not decode stem");
        if (frames > decode_block_frames) throw std::runtime_error("Decoder returned too many frames.");
        const auto count = static_cast<std::size_t>(frames) * info.channels;
        for (std::size_t i = 0; i < count; ++i) {
            if (!std::isfinite(samples[i])) {
                throw std::runtime_error("Stem contains non-finite audio: " + info.path.string());
            }
        }
        reached_end = frames == 0;
        buffered_frames = frames;
        buffered_position = 0;
    }
};

StemReader::StemReader(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}
StemReader::~StemReader() = default;
StemReader::StemReader(StemReader&&) noexcept = default;
StemReader& StemReader::operator=(StemReader&&) noexcept = default;

StemReader StemReader::open(const std::filesystem::path& path) {
    auto impl = std::make_unique<Impl>();
    impl->info.path = std::filesystem::canonical(path);
    const auto native_path = impl->info.path.string();
    const auto url = CFURLCreateFromFileSystemRepresentation(kCFAllocatorDefault,
        reinterpret_cast<const UInt8*>(native_path.data()), static_cast<CFIndex>(native_path.size()), false);
    if (!url) throw std::runtime_error("Could not create stem URL: " + path.string());
    const auto status = ExtAudioFileOpenURL(url, &impl->file);
    CFRelease(url);
    impl->check(status, "Could not open stem");

    AudioStreamBasicDescription source{};
    UInt32 size = sizeof(source);
    impl->check(ExtAudioFileGetProperty(impl->file, kExtAudioFileProperty_FileDataFormat,
                                      &size, &source), "Could not inspect stem format");
    SInt64 length = 0;
    size = sizeof(length);
    impl->check(ExtAudioFileGetProperty(impl->file, kExtAudioFileProperty_FileLengthFrames,
                                      &size, &length), "Could not inspect stem duration");
    AudioFileID audio_file = nullptr;
    size = sizeof(AudioFileID);
    impl->check(ExtAudioFileGetProperty(impl->file, kExtAudioFileProperty_AudioFile,
                                      &size, &audio_file), "Could not inspect stem container");
    AudioFileTypeID container = 0;
    size = sizeof(container);
    impl->check(AudioFileGetProperty(audio_file, kAudioFilePropertyFileFormat,
                                    &size, &container), "Could not inspect stem container type");
    const bool is_wav = container == kAudioFileWAVEType && source.mFormatID == kAudioFormatLinearPCM;
    const bool is_m4a = container == kAudioFileM4AType
        && (source.mFormatID == kAudioFormatMPEG4AAC || source.mFormatID == kAudioFormatAppleLossless);
    if (!is_wav && !is_m4a) {
        throw std::runtime_error("Use PCM WAV or AAC/ALAC M4A audio: " + path.string());
    }
    if ((source.mChannelsPerFrame != 1 && source.mChannelsPerFrame != stereo_channels)
        || !std::isfinite(source.mSampleRate)
        || source.mSampleRate < minimum_sample_rate || source.mSampleRate > maximum_sample_rate
        || length <= 0) {
        throw std::runtime_error("Use nonempty mono or stereo audio at 8–192 kHz: " + path.string());
    }
    impl->info.format = is_wav ? StemFormat::wav : StemFormat::m4a;
    impl->info.sample_rate = source.mSampleRate;
    impl->info.channels = source.mChannelsPerFrame;
    impl->info.frames = static_cast<std::uint64_t>(length);

    AudioStreamBasicDescription client{};
    client.mSampleRate = source.mSampleRate;
    client.mFormatID = kAudioFormatLinearPCM;
    client.mFormatFlags = kAudioFormatFlagsNativeFloatPacked;
    client.mChannelsPerFrame = source.mChannelsPerFrame;
    client.mBitsPerChannel = 32;
    client.mFramesPerPacket = 1;
    client.mBytesPerFrame = client.mChannelsPerFrame * sizeof(float);
    client.mBytesPerPacket = client.mBytesPerFrame;
    impl->check(ExtAudioFileSetProperty(impl->file, kExtAudioFileProperty_ClientDataFormat,
                                      sizeof(client), &client), "Could not prepare stem conversion");
    // Keep the primed block so the first read starts at frame zero.
    impl->decode();
    if (impl->buffered_frames == 0) throw std::runtime_error("Stem contains no decoded audio: " + path.string());
    return StemReader(std::move(impl));
}

const StemInfo& StemReader::info() const noexcept { return impl_->info; }

void StemReader::seek(std::uint64_t frame) {
    frame = std::min(frame, impl_->info.frames);
    impl_->check(ExtAudioFileSeek(impl_->file, static_cast<SInt64>(frame)),
                 "Could not seek stem");
    impl_->buffered_frames = 0;
    impl_->buffered_position = 0;
    impl_->reached_end = frame == impl_->info.frames;
}

std::size_t StemReader::read(std::span<float> output) {
    if (output.size() % stereo_channels != 0 || output.size() > decode_block_frames * stereo_channels) {
        throw std::invalid_argument("Read buffer must contain at most 4096 complete stereo frames.");
    }
    std::fill(output.begin(), output.end(), 0.0F);
    const auto requested_frames = output.size() / stereo_channels;
    const auto right_channel = impl_->info.channels == stereo_channels ? 1U : 0U;
    std::size_t frames_written = 0;
    while (frames_written < requested_frames) {
        if (impl_->buffered_position == impl_->buffered_frames) {
            impl_->decode();
            if (impl_->buffered_frames == 0) break;
        }
        const auto count = std::min(requested_frames - frames_written,
                                   impl_->buffered_frames - impl_->buffered_position);
        for (std::size_t i = 0; i < count; ++i) {
            const auto source_offset = (impl_->buffered_position + i) * impl_->info.channels;
            output[(frames_written + i) * stereo_channels] = impl_->samples[source_offset];
            output[(frames_written + i) * stereo_channels + 1] = impl_->samples[source_offset + right_channel];
        }
        impl_->buffered_position += count;
        frames_written += count;
    }
    return frames_written;
}

} // namespace playset
