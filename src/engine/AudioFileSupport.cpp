#include "AudioFileSupport.h"

#include <juce_core/juce_core.h>

#include <cstdint>

namespace audiocity::engine::audio_file
{
namespace
{
constexpr auto kNcwConverterEnv = "AUDIOCITY_NCW_CONVERTER_COMMAND";
constexpr int kNcwConverterTimeoutMs = 300000;
constexpr std::uint64_t kFnvOffsetBasis = 14695981039346656037ull;
constexpr std::uint64_t kFnvPrime = 1099511628211ull;

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