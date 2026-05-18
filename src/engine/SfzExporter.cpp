#include "SfzExporter.h"

#include <juce_audio_formats/juce_audio_formats.h>

#include <algorithm>
#include <cmath>

namespace audiocity::engine::sfz_export
{
namespace
{
[[nodiscard]] juce::String relativeOrAbsolutePath(const juce::File& target, const juce::File& sfzFile)
{
    // Reference relative to the SFZ when target sits under the SFZ's parent;
    // otherwise fall back to the absolute path so the SFZ still resolves.
    const auto rel = target.getRelativePathFrom(sfzFile.getParentDirectory());

    // getRelativePathFrom may return a path with ".." segments which most SFZ
    // hosts accept, but on Windows it uses backslashes; SFZ canonicalises to
    // forward slashes.
    auto normalised = rel.replaceCharacter('\\', '/');
    if (normalised.isEmpty())
        return target.getFullPathName().replaceCharacter('\\', '/');
    return normalised;
}

[[nodiscard]] juce::String sanitizeFileNameStem(const juce::String& stem, int fallbackIndex)
{
    juce::String cleaned;
    cleaned.preallocateBytes(static_cast<std::size_t>(stem.length()));
    for (auto c : stem)
    {
        if (juce::CharacterFunctions::isLetterOrDigit(c) || c == '-' || c == '_')
            cleaned << c;
        else
            cleaned << '_';
    }
    cleaned = cleaned.trim();
    if (cleaned.isEmpty())
        cleaned = "Sample_" + juce::String(fallbackIndex + 1);
    return cleaned;
}

[[nodiscard]] juce::String formatFloatShort(float value, int decimals = 3)
{
    return juce::String(value, decimals);
}

[[nodiscard]] juce::String loopModeOpcode(ZoneLoopMode mode)
{
    switch (mode)
    {
        case ZoneLoopMode::sustain:    return "loop_sustain";
        case ZoneLoopMode::continuous: return "loop_continuous";
        case ZoneLoopMode::noLoop:
        default:                       return "no_loop";
    }
}

bool writeSampleBufferAsWav(const juce::File& destWav,
                            const juce::AudioBuffer<float>& buffer,
                            double sampleRateHz)
{
    if (buffer.getNumChannels() <= 0 || buffer.getNumSamples() <= 0)
        return false;
    const auto sr = sampleRateHz > 0.0 ? sampleRateHz : 44100.0;

    destWav.deleteFile();
    std::unique_ptr<juce::FileOutputStream> stream(destWav.createOutputStream());
    if (stream == nullptr)
        return false;

    juce::WavAudioFormat wav;
    std::unique_ptr<juce::AudioFormatWriter> writer(
        wav.createWriterFor(stream.get(), sr, static_cast<unsigned int>(buffer.getNumChannels()), 16, {}, 0));
    if (writer == nullptr)
        return false;
    stream.release();

    return writer->writeFromAudioSampleBuffer(buffer, 0, buffer.getNumSamples());
}
} // namespace

ExportResult exportProgramToSfz(const juce::File& destSfz,
                                const Program& program,
                                const std::vector<juce::AudioBuffer<float>>& sampleDataByAsset,
                                const ExportOptions& options)
{
    ExportResult result;
    result.writtenFile = destSfz;

    if (destSfz == juce::File{})
    {
        result.diagnostics.push_back({ ExportDiagnostic::Severity::error,
                                       "SFZ export: destination path is empty" });
        return result;
    }

    if (program.sampleAssets.size() != sampleDataByAsset.size())
    {
        result.diagnostics.push_back({ ExportDiagnostic::Severity::error,
                                       "SFZ export: sample-data vector size does not match program asset count" });
        return result;
    }

    const auto parent = destSfz.getParentDirectory();
    if (parent != juce::File{} && !parent.exists())
    {
        const auto createResult = parent.createDirectory();
        if (createResult.failed())
        {
            result.diagnostics.push_back({ ExportDiagnostic::Severity::error,
                                           "SFZ export: failed to create destination folder: "
                                               + createResult.getErrorMessage().toStdString() });
            return result;
        }
    }

    // Resolve sample paths first so each <region> can reference a stable string.
    std::vector<juce::String> resolvedSamplePaths(program.sampleAssets.size());
    juce::File samplesFolder;

    if (options.copySamples)
    {
        const auto subfolderName = options.samplesSubfolderName.empty()
            ? juce::String("Samples")
            : juce::String::fromUTF8(options.samplesSubfolderName.c_str());
        samplesFolder = parent.getChildFile(subfolderName);
    }

    for (std::size_t i = 0; i < program.sampleAssets.size(); ++i)
    {
        const auto& asset = program.sampleAssets[i];
        const auto& buffer = sampleDataByAsset[i];
        const juce::File originalFile(juce::String::fromUTF8(asset.sourcePath.c_str()));

        const bool haveBuffer = buffer.getNumChannels() > 0 && buffer.getNumSamples() > 0;
        const bool originalExists = originalFile != juce::File{} && originalFile.existsAsFile();

        if (options.copySamples && (haveBuffer || originalExists))
        {
            if (!samplesFolder.exists())
            {
                const auto createResult = samplesFolder.createDirectory();
                if (createResult.failed())
                {
                    result.diagnostics.push_back({ ExportDiagnostic::Severity::error,
                                                   "SFZ export: failed to create samples folder: "
                                                       + createResult.getErrorMessage().toStdString() });
                    return result;
                }
            }

            juce::String stem = originalFile != juce::File{}
                ? originalFile.getFileNameWithoutExtension()
                : juce::String::fromUTF8(asset.displayName.c_str());
            stem = sanitizeFileNameStem(stem, static_cast<int>(i));
            // Avoid collisions when multiple assets share a stem.
            juce::File candidate = samplesFolder.getChildFile(stem + ".wav");
            int suffix = 2;
            while (candidate.exists())
            {
                candidate = samplesFolder.getChildFile(stem + "_" + juce::String(suffix) + ".wav");
                ++suffix;
            }

            bool wrote = false;
            if (haveBuffer)
            {
                wrote = writeSampleBufferAsWav(candidate, buffer, asset.sampleRateHz);
            }
            else if (originalExists)
            {
                wrote = originalFile.copyFileTo(candidate);
            }

            if (!wrote)
            {
                result.diagnostics.push_back({ ExportDiagnostic::Severity::error,
                                               "SFZ export: failed to write sample asset "
                                                   + std::to_string(i + 1) });
                return result;
            }

            resolvedSamplePaths[i] = relativeOrAbsolutePath(candidate, destSfz);
            ++result.copiedSampleCount;
        }
        else if (originalExists)
        {
            resolvedSamplePaths[i] = relativeOrAbsolutePath(originalFile, destSfz);
        }
        else if (!asset.sourcePath.empty())
        {
            // Keep author's original reference even if we cannot resolve it; SFZ host
            // may locate it via default_path search rules.
            resolvedSamplePaths[i] = juce::String::fromUTF8(asset.sourcePath.c_str())
                                         .replaceCharacter('\\', '/');
            result.diagnostics.push_back({ ExportDiagnostic::Severity::warning,
                                           "SFZ export: sample asset "
                                               + std::to_string(i + 1)
                                               + " has no audio buffer and source path could not be located" });
        }
        else
        {
            resolvedSamplePaths[i].clear();
            result.diagnostics.push_back({ ExportDiagnostic::Severity::warning,
                                           "SFZ export: sample asset "
                                               + std::to_string(i + 1)
                                               + " has no source path; region(s) referencing it will be skipped" });
        }
    }

    if (samplesFolder.exists() && result.copiedSampleCount > 0)
        result.samplesFolder = samplesFolder;

    juce::String out;
    out.preallocateBytes(static_cast<std::size_t>(512 + 96 * program.zones.size()));

    const juce::String banner = options.libraryDisplayName.empty()
        ? juce::String::fromUTF8(program.name.c_str())
        : juce::String::fromUTF8(options.libraryDisplayName.c_str());
    out << "// Audiocity SFZ export\n";
    out << "// Library: " << (banner.isEmpty() ? juce::String("(unnamed)") : banner) << "\n";
    out << "// Regions: " << juce::String(static_cast<int>(program.zones.size())) << "\n\n";

    out << "<control>\n";
    if (result.copiedSampleCount > 0)
    {
        const auto subfolderName = options.samplesSubfolderName.empty()
            ? juce::String("Samples")
            : juce::String::fromUTF8(options.samplesSubfolderName.c_str());
        out << "default_path=" << subfolderName << "/\n";
    }
    out << '\n';

    out << "<global>\n\n";

    for (std::size_t zoneIndex = 0; zoneIndex < program.zones.size(); ++zoneIndex)
    {
        const auto& zone = program.zones[zoneIndex];
        if (zone.sampleAssetIndex < 0
            || static_cast<std::size_t>(zone.sampleAssetIndex) >= program.sampleAssets.size())
        {
            result.diagnostics.push_back({ ExportDiagnostic::Severity::warning,
                                           "SFZ export: zone "
                                               + std::to_string(zoneIndex + 1)
                                               + " has no valid sample asset; skipped" });
            continue;
        }

        const auto& samplePath = resolvedSamplePaths[static_cast<std::size_t>(zone.sampleAssetIndex)];
        if (samplePath.isEmpty())
        {
            result.diagnostics.push_back({ ExportDiagnostic::Severity::warning,
                                           "SFZ export: zone "
                                               + std::to_string(zoneIndex + 1)
                                               + " skipped because its sample asset has no usable path" });
            continue;
        }

        const auto& asset = program.sampleAssets[static_cast<std::size_t>(zone.sampleAssetIndex)];

        out << "<region>\n";

        // For copied samples we already set default_path so emit basename only;
        // for absolute or out-of-tree refs we emit the full resolved string.
        juce::String sampleFieldValue = samplePath;
        if (result.copiedSampleCount > 0 && samplesFolder != juce::File{})
        {
            const auto subfolderName = options.samplesSubfolderName.empty()
                ? juce::String("Samples")
                : juce::String::fromUTF8(options.samplesSubfolderName.c_str());
            const auto prefix = subfolderName + "/";
            if (sampleFieldValue.startsWith(prefix))
                sampleFieldValue = sampleFieldValue.substring(prefix.length());
        }

        out << "sample=" << sampleFieldValue << '\n';
        out << "pitch_keycenter=" << juce::String(zone.rootMidiNote) << '\n';
        out << "lokey=" << juce::String(zone.keyRange.low)
            << " hikey=" << juce::String(zone.keyRange.high) << '\n';
        out << "lovel=" << juce::String(zone.velocityRange.low)
            << " hivel=" << juce::String(zone.velocityRange.high) << '\n';

        if (zone.velocityFadeIn.isEnabled())
            out << "xfin_lovel=" << juce::String(zone.velocityFadeIn.low)
                << " xfin_hivel=" << juce::String(zone.velocityFadeIn.high) << '\n';
        if (zone.velocityFadeOut.isEnabled())
            out << "xfout_lovel=" << juce::String(zone.velocityFadeOut.low)
                << " xfout_hivel=" << juce::String(zone.velocityFadeOut.high) << '\n';

        if (std::fabs(zone.tuneCents) > 0.0001f)
            out << "tune=" << juce::String(static_cast<int>(std::lround(zone.tuneCents))) << '\n';
        if (std::fabs(zone.gainDb) > 0.0001f)
            out << "volume=" << formatFloatShort(zone.gainDb, 2) << '\n';
        if (std::fabs(zone.pan) > 0.0001f)
            out << "pan=" << formatFloatShort(zone.pan * 100.0f, 1) << '\n';

        if (zone.sampleStart > 0)
            out << "offset=" << juce::String(zone.sampleStart) << '\n';
        if (zone.sampleEndExclusive > 0)
            out << "end=" << juce::String(zone.sampleEndExclusive - 1) << '\n';

        out << "loop_mode=" << loopModeOpcode(zone.loopMode) << '\n';
        if (zone.loopMode != ZoneLoopMode::noLoop
            && zone.loopStart >= 0
            && zone.loopEndExclusive > zone.loopStart)
        {
            out << "loop_start=" << juce::String(zone.loopStart)
                << " loop_end=" << juce::String(zone.loopEndExclusive - 1) << '\n';
        }

        if (zone.triggerMode == ZoneTriggerMode::release)
            out << "trigger=release\n";
        else if (zone.triggerMode == ZoneTriggerMode::oneShot)
            out << "loop_mode=one_shot\n";

        if (zone.chokeGroup > 0)
            out << "group=" << juce::String(zone.chokeGroup)
                << " off_by=" << juce::String(zone.chokeGroup) << '\n';

        if (zone.roundRobinLength > 1)
        {
            out << "seq_length=" << juce::String(zone.roundRobinLength)
                << " seq_position=" << juce::String(juce::jmax(1, zone.roundRobinPosition + 1)) << '\n';
            if (zone.roundRobinMode == RoundRobinMode::cycleRandom)
                out << "seq_mode=random\n";
        }

        // Bonus metadata so re-importers can recreate the asset's root note
        // even if the underlying WAV has none.
        if (asset.rootMidiNote != zone.rootMidiNote)
            out << "// asset_root_note=" << juce::String(asset.rootMidiNote) << '\n';

        out << '\n';
        ++result.writtenRegionCount;
    }

    if (!destSfz.replaceWithText(out, false, false, "\n"))
    {
        result.diagnostics.push_back({ ExportDiagnostic::Severity::error,
                                       "SFZ export: failed to write SFZ text to disk" });
    }

    return result;
}
} // namespace audiocity::engine::sfz_export
