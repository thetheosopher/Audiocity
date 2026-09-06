#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_core/juce_core.h>

#include <atomic>
#include <limits>
#include <memory>
#include <vector>

namespace audiocity::engine
{
/** Installs a cancellation flag for importer work on the current worker thread.

    Importers keep their public APIs stable while sharing one explicit, job-scoped
    cancellation source. Calls made without a scope remain non-cancellable.
*/
class ImportCancellationScope final
{
public:
    explicit ImportCancellationScope(const std::atomic<bool>* cancellationFlag) noexcept
        : previous_(current_)
    {
        current_ = cancellationFlag;
    }

    ~ImportCancellationScope() noexcept
    {
        current_ = previous_;
    }

    ImportCancellationScope(const ImportCancellationScope&) = delete;
    ImportCancellationScope& operator=(const ImportCancellationScope&) = delete;

    [[nodiscard]] static bool isCancellationRequested() noexcept
    {
        return current_ != nullptr && current_->load(std::memory_order_acquire);
    }

private:
    const std::atomic<bool>* previous_ = nullptr;
    inline static thread_local const std::atomic<bool>* current_ = nullptr;
};

[[nodiscard]] inline bool isImportCancellationRequested() noexcept
{
    return ImportCancellationScope::isCancellationRequested();
}

template <typename AudioReader>
[[nodiscard]] bool readAudioInCancellableChunks(AudioReader& reader,
                                                juce::AudioBuffer<float>& destination,
                                                const int totalSamples,
                                                const int chunkSize = 65536)
{
    const auto safeChunkSize = juce::jmax(1, chunkSize);
    for (auto position = 0; position < totalSamples; position += safeChunkSize)
    {
        if (isImportCancellationRequested())
            return false;

        const auto samplesThisChunk = juce::jmin(safeChunkSize, totalSamples - position);
        if (!reader.read(&destination, position, samplesThisChunk, position, true, true))
            return false;
    }

    return !isImportCancellationRequested();
}

[[nodiscard]] inline bool readFileInCancellableChunks(const juce::File& file,
                                                      juce::MemoryBlock& destination,
                                                      const int chunkSize = 1024 * 1024)
{
    auto input = file.createInputStream();
    if (input == nullptr)
        return false;

    const auto totalBytes = input->getTotalLength();
    if (totalBytes < 0
        || static_cast<juce::uint64>(totalBytes) > static_cast<juce::uint64>((std::numeric_limits<std::size_t>::max)()))
    {
        return false;
    }

    destination.setSize(static_cast<std::size_t>(totalBytes), false);
    auto* bytes = static_cast<char*>(destination.getData());
    const auto safeChunkSize = juce::jmax(1, chunkSize);
    for (juce::int64 position = 0; position < totalBytes;)
    {
        if (isImportCancellationRequested())
        {
            destination.setSize(0);
            return false;
        }

        const auto bytesThisChunk = static_cast<int>(
            juce::jmin<juce::int64>(safeChunkSize, totalBytes - position));
        if (input->read(bytes + static_cast<std::size_t>(position), bytesThisChunk) != bytesThisChunk)
        {
            destination.setSize(0);
            return false;
        }
        position += bytesThisChunk;
    }

    return !isImportCancellationRequested();
}

[[nodiscard]] inline bool readStreamInCancellableChunks(juce::InputStream& input,
                                                         juce::MemoryBlock& destination,
                                                         const juce::int64 maximumBytes,
                                                         const int chunkSize = 1024 * 1024)
{
    destination.setSize(0);
    const auto safeChunkSize = juce::jmax(1, chunkSize);
    std::vector<char> chunk(static_cast<std::size_t>(safeChunkSize));

    while (!input.isExhausted())
    {
        if (isImportCancellationRequested())
        {
            destination.setSize(0);
            return false;
        }

        const auto remaining = maximumBytes - static_cast<juce::int64>(destination.getSize());
        if (remaining <= 0)
            return false;

        const auto requested = static_cast<int>(juce::jmin<juce::int64>(safeChunkSize, remaining));
        const auto bytesRead = input.read(chunk.data(), requested);
        if (bytesRead <= 0)
            break;
        destination.append(chunk.data(), static_cast<std::size_t>(bytesRead));
    }

    return !isImportCancellationRequested();
}

[[nodiscard]] inline std::unique_ptr<juce::XmlElement> parseXmlTextCancellable(
    const juce::String& text)
{
    if (isImportCancellationRequested())
        return {};

    auto parsed = juce::parseXML(text);
    if (isImportCancellationRequested())
        return {};
    return parsed;
}

[[nodiscard]] inline std::unique_ptr<juce::XmlElement> parseXmlFileCancellable(
    const juce::File& file,
    const juce::int64 maximumBytes = 64 * 1024 * 1024)
{
    if (file.getSize() < 0 || file.getSize() > maximumBytes)
        return {};

    juce::MemoryBlock bytes;
    if (!readFileInCancellableChunks(file, bytes) || isImportCancellationRequested())
        return {};

    return parseXmlTextCancellable(juce::String::fromUTF8(
        static_cast<const char*>(bytes.getData()), static_cast<int>(bytes.getSize())));
}
}
