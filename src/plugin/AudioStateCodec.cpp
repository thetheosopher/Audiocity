#include "AudioStateCodec.h"

#include <array>
#include <cmath>
#include <cstring>
#include <limits>

namespace audiocity::plugin
{
namespace
{
constexpr std::array<char, 8> kMagic{ 'A', 'C', 'T', 'Y', 'A', 'U', 'D', '1' };
constexpr int kVersion = 1;
constexpr int kHeaderBytes = 8 + (6 * 4);

void setError(juce::String* const destination, const juce::String& message)
{
    if (destination != nullptr)
        *destination = message;
}

std::uint32_t crc32(const void* const data, const std::size_t size) noexcept
{
    auto crc = std::uint32_t{ 0xffffffffu };
    const auto* bytes = static_cast<const std::uint8_t*>(data);
    for (std::size_t index = 0; index < size; ++index)
    {
        crc ^= bytes[index];
        for (auto bit = 0; bit < 8; ++bit)
            crc = (crc >> 1u) ^ (0xedb88320u & (0u - (crc & 1u)));
    }
    return ~crc;
}

bool computeDecodedByteCount(const int channels,
                             const int samples,
                             std::size_t& decodedBytes) noexcept
{
    if (channels <= 0 || channels > AudioStateCodecLimits::maximumChannels || samples <= 0)
        return false;

    const auto channelCount = static_cast<std::size_t>(channels);
    const auto sampleCount = static_cast<std::size_t>(samples);
    if (sampleCount > std::numeric_limits<std::size_t>::max() / channelCount / sizeof(float))
        return false;

    decodedBytes = channelCount * sampleCount * sizeof(float);
    return decodedBytes <= AudioStateCodecLimits::maximumDecodedBytes;
}
}

juce::MemoryBlock encodeAudioStateAsset(const juce::AudioBuffer<float>& buffer,
                                        juce::String* const errorMessage)
{
    if (errorMessage != nullptr)
        errorMessage->clear();

    const auto channels = buffer.getNumChannels();
    const auto samples = buffer.getNumSamples();
    std::size_t decodedBytes = 0;
    if (!computeDecodedByteCount(channels, samples, decodedBytes))
    {
        setError(errorMessage, "Audio state asset is empty or exceeds the 64 MiB/8-channel limit");
        return {};
    }

    juce::MemoryBlock decoded(decodedBytes);
    auto* decodedFloats = static_cast<float*>(decoded.getData());
    for (auto channel = 0; channel < channels; ++channel)
    {
        const auto* source = buffer.getReadPointer(channel);
        for (auto sample = 0; sample < samples; ++sample)
        {
            if (!std::isfinite(source[sample]))
            {
                setError(errorMessage, "Audio state asset contains a non-finite sample");
                return {};
            }
        }

        std::memcpy(decodedFloats + (static_cast<std::size_t>(channel) * static_cast<std::size_t>(samples)),
                    source,
                    static_cast<std::size_t>(samples) * sizeof(float));
    }

    juce::MemoryOutputStream compressed;
    {
        juce::GZIPCompressorOutputStream zipper(
            compressed,
            6,
            juce::GZIPCompressorOutputStream::windowBitsGZIP);
        if (!zipper.write(decoded.getData(), decoded.getSize()))
        {
            setError(errorMessage, "Audio state asset compression failed");
            return {};
        }
    }

    if (compressed.getDataSize() == 0
        || compressed.getDataSize() > AudioStateCodecLimits::maximumEncodedBytes)
    {
        setError(errorMessage, "Compressed audio state asset exceeds the 64 MiB limit");
        return {};
    }

    juce::MemoryOutputStream output;
    output.write(kMagic.data(), kMagic.size());
    output.writeInt(kVersion);
    output.writeInt(channels);
    output.writeInt(samples);
    output.writeInt(static_cast<int>(decodedBytes));
    output.writeInt(static_cast<int>(crc32(decoded.getData(), decoded.getSize())));
    output.writeInt(static_cast<int>(compressed.getDataSize()));
    output.write(compressed.getData(), compressed.getDataSize());
    return output.getMemoryBlock();
}

juce::MemoryBlock encodeMonoAudioStateAsset(const std::vector<float>& samples,
                                            juce::String* const errorMessage)
{
    if (samples.empty() || samples.size() > static_cast<std::size_t>(std::numeric_limits<int>::max()))
    {
        setError(errorMessage, "Audio state asset is empty or too large");
        return {};
    }

    juce::AudioBuffer<float> buffer(1, static_cast<int>(samples.size()));
    std::memcpy(buffer.getWritePointer(0), samples.data(), samples.size() * sizeof(float));
    return encodeAudioStateAsset(buffer, errorMessage);
}

bool decodeAudioStateAsset(const juce::MemoryBlock& encoded,
                           juce::AudioBuffer<float>& destination,
                           juce::String* const errorMessage)
{
    if (errorMessage != nullptr)
        errorMessage->clear();

    if (encoded.getSize() < static_cast<std::size_t>(kHeaderBytes)
        || encoded.getSize() > AudioStateCodecLimits::maximumEncodedBytes + static_cast<std::size_t>(kHeaderBytes))
    {
        setError(errorMessage, "Audio state asset is truncated or exceeds the encoded-size limit");
        return false;
    }

    juce::MemoryInputStream input(encoded, false);
    std::array<char, kMagic.size()> magic{};
    if (input.read(magic.data(), static_cast<int>(magic.size())) != static_cast<int>(magic.size())
        || magic != kMagic)
    {
        setError(errorMessage, "Audio state asset has an invalid signature");
        return false;
    }

    const auto version = input.readInt();
    const auto channels = input.readInt();
    const auto samples = input.readInt();
    const auto storedDecodedBytes = input.readInt();
    const auto storedCrc = static_cast<std::uint32_t>(input.readInt());
    const auto compressedBytes = input.readInt();

    std::size_t decodedBytes = 0;
    if (version != kVersion
        || !computeDecodedByteCount(channels, samples, decodedBytes)
        || storedDecodedBytes <= 0
        || static_cast<std::size_t>(storedDecodedBytes) != decodedBytes
        || compressedBytes <= 0
        || static_cast<std::size_t>(compressedBytes) != encoded.getSize() - static_cast<std::size_t>(kHeaderBytes))
    {
        setError(errorMessage, "Audio state asset header is invalid or exceeds configured limits");
        return false;
    }

    juce::MemoryInputStream compressedInput(
        static_cast<const std::uint8_t*>(encoded.getData()) + kHeaderBytes,
        static_cast<std::size_t>(compressedBytes),
        false);
    juce::GZIPDecompressorInputStream unzipper(
        &compressedInput,
        false,
        juce::GZIPDecompressorInputStream::gzipFormat,
        static_cast<juce::int64>(decodedBytes));

    juce::MemoryBlock decoded(decodedBytes);
    auto totalRead = std::size_t{ 0 };
    while (totalRead < decodedBytes)
    {
        const auto remaining = decodedBytes - totalRead;
        const auto chunk = static_cast<int>(juce::jmin<std::size_t>(remaining, 1024u * 1024u));
        const auto read = unzipper.read(static_cast<std::uint8_t*>(decoded.getData()) + totalRead, chunk);
        if (read <= 0)
            break;
        totalRead += static_cast<std::size_t>(read);
    }

    std::uint8_t trailingDecodedByte = 0;
    const auto trailingDecodedBytes = totalRead == decodedBytes
        ? unzipper.read(&trailingDecodedByte, 1)
        : 0;

    if (totalRead != decodedBytes)
    {
        setError(errorMessage, "Audio state asset decoded fewer bytes than its header declares");
        return false;
    }

    if (trailingDecodedBytes != 0)
    {
        setError(errorMessage, "Audio state asset contains trailing decoded data beyond its declared size");
        return false;
    }

    if (crc32(decoded.getData(), decoded.getSize()) != storedCrc)
    {
        setError(errorMessage, "Audio state asset failed its decoded-data checksum");
        return false;
    }

    const auto* decodedFloats = static_cast<const float*>(decoded.getData());
    const auto totalFloats = decodedBytes / sizeof(float);
    for (std::size_t index = 0; index < totalFloats; ++index)
    {
        if (!std::isfinite(decodedFloats[index]))
        {
            setError(errorMessage, "Audio state asset contains a non-finite sample");
            return false;
        }
    }

    juce::AudioBuffer<float> restored(channels, samples);
    for (auto channel = 0; channel < channels; ++channel)
    {
        std::memcpy(restored.getWritePointer(channel),
                    decodedFloats + (static_cast<std::size_t>(channel) * static_cast<std::size_t>(samples)),
                    static_cast<std::size_t>(samples) * sizeof(float));
    }

    destination = std::move(restored);
    return true;
}

bool decodeMonoAudioStateAsset(const juce::MemoryBlock& encoded,
                               std::vector<float>& destination,
                               juce::String* const errorMessage)
{
    if (errorMessage != nullptr)
        errorMessage->clear();

    juce::AudioBuffer<float> buffer;
    juce::String decodeError;
    if (!decodeAudioStateAsset(encoded, buffer, &decodeError))
    {
        setError(errorMessage, decodeError.isNotEmpty()
            ? decodeError
            : juce::String("Mono audio state asset could not be decoded"));
        return false;
    }

    if (buffer.getNumChannels() != 1)
    {
        setError(errorMessage, "Audio state asset is not mono");
        return false;
    }

    std::vector<float> restored(static_cast<std::size_t>(buffer.getNumSamples()));
    std::memcpy(restored.data(), buffer.getReadPointer(0), restored.size() * sizeof(float));
    destination = std::move(restored);
    return true;
}
} // namespace audiocity::plugin
