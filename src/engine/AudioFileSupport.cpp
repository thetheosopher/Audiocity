#include "AudioFileSupport.h"
#include "ImportCancellation.h"

#include <juce_core/juce_core.h>

#include <array>
#include <cstdint>
#include <cstring>
#include <limits>
#include <optional>
#include <vector>

namespace audiocity::engine::audio_file
{
namespace
{
constexpr auto kNcwConverterEnv = "AUDIOCITY_NCW_CONVERTER_COMMAND";
constexpr int kNcwConverterTimeoutMs = 300000;
constexpr std::uint64_t kFnvOffsetBasis = 14695981039346656037ull;
constexpr std::uint64_t kFnvPrime = 1099511628211ull;
constexpr double kWavePitchFractionUnit = 4294967296.0;

[[nodiscard]] std::uint64_t fnv1a64(const juce::String& text) noexcept
{
    std::uint64_t hash = kFnvOffsetBasis;
    const auto utf8 = text.toRawUTF8();
    for (const auto* byte = reinterpret_cast<const unsigned char*>(utf8); *byte != 0; ++byte)
    {
        hash ^= static_cast<std::uint64_t>(*byte);
        hash *= kFnvPrime;
    }
    return hash;
}

[[nodiscard]] std::uint32_t readLeU32(const std::uint8_t* data) noexcept
{
    return static_cast<std::uint32_t>(data[0])
        | (static_cast<std::uint32_t>(data[1]) << 8)
        | (static_cast<std::uint32_t>(data[2]) << 16)
        | (static_cast<std::uint32_t>(data[3]) << 24);
}

[[nodiscard]] std::uint16_t readBeU16(const std::uint8_t* data) noexcept
{
    return static_cast<std::uint16_t>((static_cast<std::uint16_t>(data[0]) << 8)
        | static_cast<std::uint16_t>(data[1]));
}

[[nodiscard]] std::uint32_t readBeU32(const std::uint8_t* data) noexcept
{
    return (static_cast<std::uint32_t>(data[0]) << 24)
        | (static_cast<std::uint32_t>(data[1]) << 16)
        | (static_cast<std::uint32_t>(data[2]) << 8)
        | static_cast<std::uint32_t>(data[3]);
}

[[nodiscard]] bool readAt(juce::FileInputStream& input,
                          const juce::int64 position,
                          void* destination,
                          const int numBytes)
{
    if (position < 0)
        return false;

    input.setPosition(position);
    return input.getPosition() == position && input.read(destination, numBytes) == numBytes;
}

void appendWarning(juce::String& warnings, const juce::String& warning)
{
    if (warnings.isNotEmpty())
        warnings += "; ";

    warnings += warning;
}

[[nodiscard]] juce::String quoteForCommand(const juce::String& text)
{
    return "\"" + text.replace("\"", "\\\"") + "\"";
}

[[nodiscard]] juce::File getNcwCacheDirectory()
{
    return juce::File::getSpecialLocation(juce::File::tempDirectory)
        .getChildFile("Audiocity")
        .getChildFile("ncw-cache");
}

[[nodiscard]] juce::File getCachedNcwWavFile(const juce::File& sourceFile)
{
    const auto fingerprint = sourceFile.getFullPathName().toLowerCase()
        + "|" + juce::String(static_cast<juce::int64>(sourceFile.getSize()))
        + "|" + juce::String(static_cast<juce::int64>(sourceFile.getLastModificationTime().toMilliseconds()));
    auto hashHex = juce::String::toHexString(static_cast<juce::int64>(fnv1a64(fingerprint)));
    hashHex = hashHex.paddedLeft('0', 16);
    return getNcwCacheDirectory().getChildFile(hashHex + ".wav");
}

[[nodiscard]] juce::String buildConverterCommand(const juce::String& commandTemplate,
                                                 const juce::File& inputFile,
                                                 const juce::File& outputFile)
{
    const auto quotedInput = quoteForCommand(inputFile.getFullPathName());
    const auto quotedOutput = quoteForCommand(outputFile.getFullPathName());
    const auto trimmed = commandTemplate.trim();

    if (trimmed.contains("{input}") || trimmed.contains("{output}"))
        return trimmed.replace("{input}", quotedInput).replace("{output}", quotedOutput);

    return trimmed + " " + quotedInput + " " + quotedOutput;
}

[[nodiscard]] juce::String decorateConverterFailure(const juce::String& prefix,
                                                    const juce::String& processOutput)
{
    if (processOutput.isEmpty())
        return prefix;

    return prefix + ": " + processOutput.substring(0, 240);
}

[[nodiscard]] juce::File ensureDecodedNcwFile(const juce::File& sourceFile,
                                              juce::String& errorMessage)
{
    const auto cacheDirectory = getNcwCacheDirectory();
    if (!cacheDirectory.isDirectory() && !cacheDirectory.createDirectory())
    {
        errorMessage = "NCW decode failed: could not create cache folder " + cacheDirectory.getFullPathName();
        return {};
    }

    const auto cachedWav = getCachedNcwWavFile(sourceFile);
    if (cachedWav.existsAsFile() && cachedWav.getSize() > 0)
        return cachedWav;

    const auto commandTemplate = juce::SystemStats::getEnvironmentVariable(kNcwConverterEnv, {}).trim();
    if (commandTemplate.isEmpty())
    {
        errorMessage = "NCW support requires the " + juce::String(kNcwConverterEnv)
            + " environment variable so Audiocity can convert .ncw files to cached PCM";
        return {};
    }

    const auto temporaryOutput = cachedWav.getSiblingFile(cachedWav.getFileNameWithoutExtension() + ".tmp.wav");
    temporaryOutput.deleteFile();

    juce::ChildProcess childProcess;
    const auto command = buildConverterCommand(commandTemplate, sourceFile, temporaryOutput);
    if (!childProcess.start(command))
    {
        errorMessage = "NCW decode failed: could not start converter command";
        return {};
    }

    if (!childProcess.waitForProcessToFinish(kNcwConverterTimeoutMs))
    {
        childProcess.kill();
        errorMessage = "NCW decode failed: converter timed out";
        temporaryOutput.deleteFile();
        return {};
    }

    const auto processOutput = childProcess.readAllProcessOutput().trim();
    if (childProcess.getExitCode() != 0)
    {
        errorMessage = decorateConverterFailure(
            "NCW decode failed: converter exited with code " + juce::String(childProcess.getExitCode()),
            processOutput);
        temporaryOutput.deleteFile();
        return {};
    }

    if (!temporaryOutput.existsAsFile() || temporaryOutput.getSize() <= 0)
    {
        errorMessage = decorateConverterFailure(
            "NCW decode failed: converter did not produce a readable WAV",
            processOutput);
        temporaryOutput.deleteFile();
        return {};
    }

    if (cachedWav.existsAsFile())
        cachedWav.deleteFile();

    if (!temporaryOutput.moveFileTo(cachedWav))
    {
        if (!temporaryOutput.copyFileTo(cachedWav))
        {
            errorMessage = "NCW decode failed: could not move converted WAV into cache";
            temporaryOutput.deleteFile();
            return {};
        }

        temporaryOutput.deleteFile();
    }

    return cachedWav;
}
} // namespace

void registerAudioFormats(juce::AudioFormatManager& formatManager)
{
    formatManager.registerBasicFormats();
}

bool isNcwConverterAvailable()
{
    return juce::SystemStats::getEnvironmentVariable(kNcwConverterEnv, {}).trim().isNotEmpty();
}

EmbeddedSamplerMetadata readWaveSamplerMetadata(const juce::File& sourceFile,
                                                const juce::int64 decodedFrameCount)
{
    EmbeddedSamplerMetadata result;
    if (!sourceFile.getFileExtension().equalsIgnoreCase(".wav"))
        return result;

    juce::FileInputStream input(sourceFile);
    if (!input.openedOk())
    {
        result.errorMessage = "Could not open WAVE sampler metadata from " + sourceFile.getFullPathName();
        return result;
    }

    const auto fileLength = input.getTotalLength();
    if (fileLength < 12)
        return result;

    std::uint8_t root[12]{};
    if (!readAt(input, 0, root, static_cast<int>(sizeof(root))))
    {
        result.errorMessage = "Could not read the WAVE root header from " + sourceFile.getFullPathName();
        return result;
    }

    if (std::memcmp(root, "RF64", 4) == 0 || std::memcmp(root, "BW64", 4) == 0)
    {
        appendWarning(result.warningMessage, "RF64/BW64 sampler metadata is not supported yet");
        return result;
    }

    if (std::memcmp(root, "RIFF", 4) != 0 || std::memcmp(root + 8, "WAVE", 4) != 0)
        return result;

    const auto riffSize = static_cast<std::uint64_t>(readLeU32(root + 4));
    const auto riffEnd = static_cast<std::uint64_t>(8) + riffSize;
    if (riffSize < 4 || riffEnd > static_cast<std::uint64_t>(fileLength))
    {
        result.errorMessage = "Invalid RIFF size in WAVE file " + sourceFile.getFullPathName();
        return result;
    }

    const auto fail = [&result, &sourceFile](const juce::String& detail)
    {
        result.errorMessage = "Invalid WAVE sampler metadata in "
            + sourceFile.getFullPathName() + ": " + detail;
    };

    auto cursor = static_cast<std::uint64_t>(12);
    while (cursor < riffEnd)
    {
        if (isImportCancellationRequested())
        {
            result.errorMessage = "WAVE sampler metadata read cancelled";
            return result;
        }

        if (riffEnd - cursor < 8)
        {
            fail("truncated chunk header");
            return result;
        }

        std::uint8_t chunkHeader[8]{};
        if (!readAt(input, static_cast<juce::int64>(cursor), chunkHeader, static_cast<int>(sizeof(chunkHeader))))
        {
            fail("could not read chunk header");
            return result;
        }

        const auto chunkSize = static_cast<std::uint64_t>(readLeU32(chunkHeader + 4));
        const auto payloadStart = cursor + 8;
        if (chunkSize > riffEnd - payloadStart)
        {
            fail("chunk payload exceeds the RIFF boundary");
            return result;
        }

        if (std::memcmp(chunkHeader, "smpl", 4) == 0)
        {
            if (result.metadataChunkPresent)
            {
                appendWarning(result.warningMessage, "additional WAVE smpl chunks were ignored");
            }
            else
            {
                result.metadataChunkPresent = true;
                if (chunkSize < 36)
                {
                    fail("smpl chunk is shorter than its fixed header");
                    return result;
                }

                std::uint8_t smplHeader[36]{};
                if (!readAt(input, static_cast<juce::int64>(payloadStart),
                            smplHeader, static_cast<int>(sizeof(smplHeader))))
                {
                    fail("could not read smpl header");
                    return result;
                }

                const auto loopCount = static_cast<std::uint64_t>(readLeU32(smplHeader + 28));
                const auto samplerDataBytes = static_cast<std::uint64_t>(readLeU32(smplHeader + 32));
                const auto availableAfterHeader = chunkSize - 36;
                if (loopCount > availableAfterHeader / 24)
                {
                    fail("smpl loop records exceed the chunk payload");
                    return result;
                }

                const auto loopBytes = loopCount * 24;
                if (samplerDataBytes > availableAfterHeader - loopBytes)
                {
                    fail("smpl sampler-specific data exceeds the chunk payload");
                    return result;
                }

                const auto unityNote = readLeU32(smplHeader + 12);
                if (unityNote <= 127)
                {
                    result.rootMidiNote = static_cast<int>(unityNote);
                    const auto pitchFraction = readLeU32(smplHeader + 16);
                    result.defaultTuneCents = static_cast<float>(
                        (static_cast<double>(pitchFraction) / kWavePitchFractionUnit) * 100.0);
                }
                else
                    appendWarning(result.warningMessage, "WAVE MIDI unity note is outside the MIDI key range");

                if (loopCount > 1)
                    appendWarning(result.warningMessage, "only the first WAVE sampler loop can be imported");

                if (loopCount > 0)
                {
                    std::uint8_t loop[24]{};
                    if (!readAt(input, static_cast<juce::int64>(payloadStart + 36),
                                loop, static_cast<int>(sizeof(loop))))
                    {
                        fail("could not read the first smpl loop record");
                        return result;
                    }

                    const auto loopType = readLeU32(loop + 4);
                    const auto loopStart = readLeU32(loop + 8);
                    const auto loopEndInclusive = readLeU32(loop + 12);
                    const auto loopFraction = readLeU32(loop + 16);
                    const auto playCount = readLeU32(loop + 20);
                    const auto endFitsModel = loopEndInclusive
                        < static_cast<std::uint32_t>((std::numeric_limits<int>::max)());
                    const auto startFitsModel = loopStart
                        <= static_cast<std::uint32_t>((std::numeric_limits<int>::max)());
                    const auto loopFitsAudio = decodedFrameCount <= 0
                        || static_cast<juce::int64>(loopEndInclusive) < decodedFrameCount;

                    if (loopType != 0 || loopFraction != 0 || playCount != 0)
                    {
                        appendWarning(result.warningMessage,
                                      "only integer forward WAVE loops with unlimited play count are supported");
                    }
                    else if (loopEndInclusive < loopStart || !startFitsModel || !endFitsModel || !loopFitsAudio)
                    {
                        appendWarning(result.warningMessage, "WAVE sampler loop is outside the decoded audio range");
                    }
                    else
                    {
                        result.defaultLoopStart = static_cast<int>(loopStart);
                        result.defaultLoopEndExclusive = static_cast<int>(loopEndInclusive + 1);
                    }
                }
            }
        }

        const auto payloadEnd = payloadStart + chunkSize;
        const auto paddingBytes = chunkSize & 1u;
        if (paddingBytes > riffEnd - payloadEnd)
        {
            fail("missing odd-chunk alignment byte");
            return result;
        }

        cursor = payloadEnd + paddingBytes;
    }

    return result;
}

EmbeddedSamplerMetadata readAiffSamplerMetadata(const juce::File& sourceFile,
                                                const juce::int64 decodedFrameCount)
{
    EmbeddedSamplerMetadata result;
    const auto extension = sourceFile.getFileExtension().toLowerCase();
    if (extension != ".aif" && extension != ".aiff" && extension != ".aifc")
        return result;

    juce::FileInputStream input(sourceFile);
    if (!input.openedOk())
    {
        result.errorMessage = "Could not open AIFF sampler metadata from " + sourceFile.getFullPathName();
        return result;
    }

    const auto fileLength = input.getTotalLength();
    if (fileLength < 12)
        return result;

    std::uint8_t root[12]{};
    if (!readAt(input, 0, root, static_cast<int>(sizeof(root))))
    {
        result.errorMessage = "Could not read the AIFF root header from " + sourceFile.getFullPathName();
        return result;
    }

    if (std::memcmp(root, "FORM", 4) != 0
        || (std::memcmp(root + 8, "AIFF", 4) != 0 && std::memcmp(root + 8, "AIFC", 4) != 0))
    {
        return result;
    }

    const auto formSize = static_cast<std::uint64_t>(readBeU32(root + 4));
    const auto formEnd = static_cast<std::uint64_t>(8) + formSize;
    if (formSize < 4 || formEnd > static_cast<std::uint64_t>(fileLength))
    {
        result.errorMessage = "Invalid FORM size in AIFF file " + sourceFile.getFullPathName();
        return result;
    }

    const auto fail = [&result, &sourceFile](const juce::String& detail)
    {
        result.errorMessage = "Invalid AIFF sampler metadata in "
            + sourceFile.getFullPathName() + ": " + detail;
    };

    struct Marker
    {
        std::uint16_t id = 0;
        std::uint32_t frame = 0;
    };

    constexpr std::uint64_t kMaximumMarkerCount = 65536;
    std::vector<Marker> markers;
    std::array<std::uint8_t, 20> instrument{};
    auto instrumentPresent = false;
    auto markChunkSeen = false;
    auto instrumentChunkSeen = false;
    auto markerOutsideAudio = false;

    auto cursor = static_cast<std::uint64_t>(12);
    while (cursor < formEnd)
    {
        if (isImportCancellationRequested())
        {
            result.errorMessage = "AIFF sampler metadata read cancelled";
            return result;
        }

        if (formEnd - cursor < 8)
        {
            fail("truncated chunk header");
            return result;
        }

        std::uint8_t chunkHeader[8]{};
        if (!readAt(input, static_cast<juce::int64>(cursor), chunkHeader, static_cast<int>(sizeof(chunkHeader))))
        {
            fail("could not read chunk header");
            return result;
        }

        const auto chunkSize = static_cast<std::uint64_t>(readBeU32(chunkHeader + 4));
        const auto payloadStart = cursor + 8;
        if (chunkSize > formEnd - payloadStart)
        {
            fail("chunk payload exceeds the FORM boundary");
            return result;
        }

        if (std::memcmp(chunkHeader, "MARK", 4) == 0)
        {
            if (markChunkSeen)
            {
                appendWarning(result.warningMessage, "additional AIFF MARK chunks were ignored");
            }
            else
            {
                markChunkSeen = true;
                if (chunkSize < 2)
                {
                    fail("MARK chunk is shorter than its marker count");
                    return result;
                }

                std::uint8_t countBytes[2]{};
                if (!readAt(input, static_cast<juce::int64>(payloadStart),
                            countBytes, static_cast<int>(sizeof(countBytes))))
                {
                    fail("could not read MARK marker count");
                    return result;
                }

                const auto markerCount = static_cast<std::uint64_t>(readBeU16(countBytes));
                if (markerCount > kMaximumMarkerCount || markerCount > (chunkSize - 2) / 7)
                {
                    fail("MARK marker count exceeds the chunk or implementation limit");
                    return result;
                }

                markers.reserve(static_cast<std::size_t>(markerCount));
                const auto payloadEnd = payloadStart + chunkSize;
                auto markerCursor = payloadStart + 2;
                for (std::uint64_t markerIndex = 0; markerIndex < markerCount; ++markerIndex)
                {
                    if (payloadEnd - markerCursor < 7)
                    {
                        fail("truncated MARK record");
                        return result;
                    }

                    std::uint8_t markerHeader[7]{};
                    if (!readAt(input, static_cast<juce::int64>(markerCursor),
                                markerHeader, static_cast<int>(sizeof(markerHeader))))
                    {
                        fail("could not read MARK record");
                        return result;
                    }

                    const auto markerNameLength = static_cast<std::uint64_t>(markerHeader[6]);
                    const auto recordBytes = static_cast<std::uint64_t>(7) + markerNameLength;
                    const auto paddedRecordBytes = recordBytes + (recordBytes & 1u);
                    if (paddedRecordBytes > payloadEnd - markerCursor)
                    {
                        fail("MARK Pascal name exceeds the chunk payload");
                        return result;
                    }

                    const auto markerFrame = readBeU32(markerHeader + 2);
                    markers.push_back({ readBeU16(markerHeader), markerFrame });
                    if (decodedFrameCount > 0
                        && static_cast<juce::int64>(markerFrame) > decodedFrameCount)
                    {
                        markerOutsideAudio = true;
                    }

                    markerCursor += paddedRecordBytes;
                }

                if (markerCursor != payloadEnd)
                {
                    fail("MARK marker count does not consume the chunk payload");
                    return result;
                }
            }
        }
        else if (std::memcmp(chunkHeader, "INST", 4) == 0)
        {
            if (chunkSize < instrument.size())
            {
                fail("INST chunk is shorter than its fixed payload");
                return result;
            }

            if (instrumentChunkSeen)
            {
                appendWarning(result.warningMessage, "additional AIFF INST chunks were ignored");
            }
            else
            {
                instrumentChunkSeen = true;
                instrumentPresent = true;
                if (!readAt(input, static_cast<juce::int64>(payloadStart),
                            instrument.data(), static_cast<int>(instrument.size())))
                {
                    fail("could not read INST payload");
                    return result;
                }

                if (chunkSize > instrument.size())
                    appendWarning(result.warningMessage, "AIFF INST extension bytes were ignored");
            }
        }

        const auto payloadEnd = payloadStart + chunkSize;
        const auto paddingBytes = chunkSize & 1u;
        if (paddingBytes > formEnd - payloadEnd)
        {
            fail("missing odd-chunk alignment byte");
            return result;
        }

        cursor = payloadEnd + paddingBytes;
    }

    if (!instrumentPresent)
        return result;

    result.metadataChunkPresent = true;
    if (instrument[0] <= 127)
        result.rootMidiNote = static_cast<int>(instrument[0]);
    else
        appendWarning(result.warningMessage, "AIFF INST base note is outside the MIDI key range");

    const auto rawDetune = static_cast<int>(instrument[1]);
    result.defaultTuneCents = static_cast<float>(rawDetune < 128 ? rawDetune : rawDetune - 256);

    const auto lowNote = static_cast<int>(instrument[2]);
    const auto highNote = static_cast<int>(instrument[3]);
    const auto lowVelocity = static_cast<int>(instrument[4]);
    const auto highVelocity = static_cast<int>(instrument[5]);
    const auto rawGain = static_cast<int>(readBeU16(instrument.data() + 6));
    const auto gain = rawGain < 0x8000 ? rawGain : rawGain - 0x10000;
    if (lowNote != 0 || highNote != 127 || lowVelocity != 0 || highVelocity != 127 || gain != 0)
    {
        appendWarning(result.warningMessage,
                      "AIFF INST note/velocity ranges and gain are not applied to the sample zone");
    }

    if (markerOutsideAudio)
        appendWarning(result.warningMessage, "AIFF MARK contains positions beyond the decoded audio");

    const auto sustainMode = readBeU16(instrument.data() + 8);
    const auto sustainBeginId = readBeU16(instrument.data() + 10);
    const auto sustainEndId = readBeU16(instrument.data() + 12);
    const auto releaseMode = readBeU16(instrument.data() + 14);
    if (releaseMode != 0)
        appendWarning(result.warningMessage, "AIFF release loops are not imported");

    if (sustainMode == 1)
    {
        const auto resolveMarker = [&markers](const std::uint16_t markerId)
            -> std::optional<std::uint32_t>
        {
            std::optional<std::uint32_t> frame;
            for (const auto& marker : markers)
            {
                if (marker.id != markerId)
                    continue;

                if (frame.has_value())
                    return std::nullopt;
                frame = marker.frame;
            }

            return frame;
        };

        const auto loopStart = resolveMarker(sustainBeginId);
        const auto loopEndExclusive = resolveMarker(sustainEndId);
        if (!loopStart.has_value() || !loopEndExclusive.has_value())
        {
            appendWarning(result.warningMessage,
                          "AIFF sustain loop references missing or ambiguous MARK IDs");
        }
        else if (*loopStart >= *loopEndExclusive
                 || *loopStart > static_cast<std::uint32_t>((std::numeric_limits<int>::max)())
                 || *loopEndExclusive > static_cast<std::uint32_t>((std::numeric_limits<int>::max)())
                 || (decodedFrameCount > 0
                     && static_cast<juce::int64>(*loopEndExclusive) > decodedFrameCount))
        {
            appendWarning(result.warningMessage, "AIFF sustain loop is outside the decoded audio range");
        }
        else
        {
            // AIFF marker-end positions use the selected libsndfile-compatible exclusive convention.
            result.defaultLoopStart = static_cast<int>(*loopStart);
            result.defaultLoopEndExclusive = static_cast<int>(*loopEndExclusive);
        }
    }
    else if (sustainMode == 2)
    {
        appendWarning(result.warningMessage, "AIFF ping-pong sustain loops are not supported");
    }
    else if (sustainMode != 0)
    {
        appendWarning(result.warningMessage, "AIFF sustain loop mode is unsupported");
    }

    return result;
}

EmbeddedSamplerMetadata readEmbeddedSamplerMetadata(const juce::File& sourceFile,
                                                    const juce::int64 decodedFrameCount)
{
    const auto extension = sourceFile.getFileExtension().toLowerCase();
    if (extension == ".wav")
        return readWaveSamplerMetadata(sourceFile, decodedFrameCount);

    if (extension == ".aif" || extension == ".aiff" || extension == ".aifc")
        return readAiffSamplerMetadata(sourceFile, decodedFrameCount);

    return {};
}

ReaderOpenResult openReaderForFile(juce::AudioFormatManager& formatManager,
                                   const juce::File& sourceFile)
{
    ReaderOpenResult result;
    if (!sourceFile.existsAsFile())
    {
        result.errorMessage = "Audio sample not found: " + sourceFile.getFullPathName();
        return result;
    }

    result.readableFile = sourceFile;
    result.reader.reset(formatManager.createReaderFor(sourceFile));
    if (result.reader != nullptr)
        return result;

    if (!sourceFile.getFileExtension().equalsIgnoreCase(".ncw"))
    {
        result.errorMessage = "Unsupported audio format: " + sourceFile.getFullPathName();
        return result;
    }

    juce::String conversionError;
    const auto decodedFile = ensureDecodedNcwFile(sourceFile, conversionError);
    if (!decodedFile.existsAsFile())
    {
        result.errorMessage = conversionError;
        result.readableFile = {};
        return result;
    }

    result.readableFile = decodedFile;
    result.reader.reset(formatManager.createReaderFor(decodedFile));
    if (result.reader == nullptr)
        result.errorMessage = "NCW decode failed: converted WAV was not readable " + decodedFile.getFullPathName();

    return result;
}
} // namespace audiocity::engine::audio_file
