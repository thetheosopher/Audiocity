#include "XmlMultisampleImporterUtils.h"

#include "AudioFileSupport.h"
#include "ImportCancellation.h"

#include <algorithm>
#include <limits>
#include <memory>

namespace audiocity::engine::xml_multi
{
int parseMidiNoteText(const juce::String& raw, int defaultValue)
{
    const auto trimmed = raw.trim();
    if (trimmed.isEmpty())
        return defaultValue;

    if (trimmed.containsOnly("0123456789-+ "))
        return clampMidiNote(trimmed.getIntValue());

    int pitchClass = -1;
    switch (trimmed[0])
    {
        case 'C': case 'c': pitchClass = 0; break;
        case 'D': case 'd': pitchClass = 2; break;
        case 'E': case 'e': pitchClass = 4; break;
        case 'F': case 'f': pitchClass = 5; break;
        case 'G': case 'g': pitchClass = 7; break;
        case 'A': case 'a': pitchClass = 9; break;
        case 'B': case 'b': pitchClass = 11; break;
        default: return defaultValue;
    }

    int idx = 1;
    if (idx < trimmed.length())
    {
        const auto a = trimmed[idx];
        if (a == '#')                                 { pitchClass = (pitchClass + 1) % 12; ++idx; }
        else if (a == 'b' || a == 'B')                { pitchClass = (pitchClass + 11) % 12; ++idx; }
    }
    if (idx >= trimmed.length()) return defaultValue;

    const auto octText = trimmed.substring(idx);
    if (octText.isEmpty() || !octText.containsOnly("0123456789-+"))
        return defaultValue;

    const auto octave = octText.getIntValue();
    return clampMidiNote((octave + 1) * 12 + pitchClass);
}

juce::File resolveSamplePath(const juce::String& rawPath, const juce::File& presetFolder)
{
    if (rawPath.isEmpty()) return {};

    const auto normalized = rawPath.replaceCharacter('\\', '/');
    juce::File abs(normalized);
    if (juce::File::isAbsolutePath(normalized) && abs.existsAsFile())
        return abs;

    const auto rel = presetFolder.getChildFile(normalized);
    if (rel.existsAsFile()) return rel;

    const auto base = juce::File::createFileWithoutCheckingPath(normalized).getFileName();
    const auto siblingSamples = presetFolder.getChildFile("Samples").getChildFile(base);
    if (siblingSamples.existsAsFile()) return siblingSamples;

    auto parent = presetFolder.getParentDirectory();
    for (int i = 0; i < 2 && parent.exists(); ++i)
    {
        const auto cand = parent.getChildFile(normalized);
        if (cand.existsAsFile()) return cand;
        const auto candInSamples = parent.getChildFile("Samples").getChildFile(base);
        if (candInSamples.existsAsFile()) return candInSamples;
        parent = parent.getParentDirectory();
    }

    return {};
}

bool isSafeArchiveRelativePath(const juce::String& rawPath)
{
    const auto normalized = rawPath.trim().replaceCharacter('\\', '/');
    if (normalized.isEmpty())
        return false;

    if (normalized.startsWithChar('/') || normalized.startsWith("//") || juce::File::isAbsolutePath(normalized))
        return false;

    if (normalized.containsChar(':'))
        return false;

    juce::StringArray segments;
    segments.addTokens(normalized, "/", {});
    for (const auto& segment : segments)
    {
        const auto trimmed = segment.trim();
        if (trimmed.isEmpty() || trimmed == "." || trimmed == "..")
            return false;
    }

    return true;
}

int loadSampleAssetFromFile(juce::AudioFormatManager& fm,
                            const juce::File& audioFile,
                            const int rootMidiNote,
                            Program& program,
                            std::vector<juce::AudioBuffer<float>>& sampleData,
                            std::vector<Diagnostic>& diagnostics,
                            const juce::String& humanLabel)
{
    if (isImportCancellationRequested())
        return -1;

    if (!audioFile.existsAsFile())
    {
        addDiagnostic(diagnostics, Diagnostic::Severity::error,
                      "Audio sample not found: " + humanLabel);
        return -1;
    }

    auto openResult = audio_file::openReaderForFile(fm, audioFile);
    auto reader = std::move(openResult.reader);
    if (reader == nullptr)
    {
        addDiagnostic(diagnostics, Diagnostic::Severity::error,
                      openResult.errorMessage.isNotEmpty() ? openResult.errorMessage
                                                           : ("Unsupported audio format: " + humanLabel));
        return -1;
    }

    SampleAsset asset;
    asset.sourcePath = openResult.readableFile.getFullPathName().toStdString();
    asset.displayName = audioFile.getFileName().toStdString();
    asset.lengthSamples = static_cast<int>(juce::jlimit<juce::int64>(0, std::numeric_limits<int>::max(), reader->lengthInSamples));
    asset.numChannels = static_cast<int>(reader->numChannels);
    asset.sampleRateHz = reader->sampleRate;
    asset.rootMidiNote = rootMidiNote;
    asset.bitDepth = static_cast<int>(reader->bitsPerSample);
    asset.embeddedInProgram = false;

    if (!asset.hasAudio())
    {
        addDiagnostic(diagnostics, Diagnostic::Severity::error,
                      "Audio sample has no readable samples: " + humanLabel);
        return -1;
    }

    juce::AudioBuffer<float> buffer(asset.numChannels, asset.lengthSamples);
    if (!readAudioInCancellableChunks(*reader, buffer, asset.lengthSamples))
    {
        addDiagnostic(diagnostics, Diagnostic::Severity::error,
                      "Could not decode audio sample: " + humanLabel);
        return -1;
    }

    program.sampleAssets.push_back(asset);
    sampleData.push_back(std::move(buffer));
    return static_cast<int>(program.sampleAssets.size() - 1);
}

juce::String buildGenericSummary(const juce::String& formatLabel,
                                 const Program& program,
                                 const std::vector<Diagnostic>& diagnostics,
                                 const bool imported)
{
    int errors = 0, warnings = 0;
    for (const auto& d : diagnostics)
    {
        if (d.severity == Diagnostic::Severity::error) ++errors;
        else ++warnings;
    }

    juce::String summary;
    if (imported)
        summary << formatLabel << " imported: "
                << static_cast<int>(program.zones.size()) << " zone(s), "
                << static_cast<int>(program.sampleAssets.size()) << " sample(s)";
    else
        summary << formatLabel << " import failed";

    if (warnings > 0)
        summary << " (" << warnings << " warning" << (warnings == 1 ? "" : "s") << ")";
    if (errors > 0)
        summary << " (" << errors << " error" << (errors == 1 ? "" : "s") << ")";

    for (size_t i = 0; i < diagnostics.size() && i < 3; ++i)
        summary << "\n  - " << juce::String(diagnostics[i].message);
    if (diagnostics.size() > 3)
        summary << "\n  ... " << static_cast<int>(diagnostics.size() - 3) << " more";

    return summary;
}
} // namespace audiocity::engine::xml_multi
