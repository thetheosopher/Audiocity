#include "DecentSamplerExporter.h"

#include <juce_audio_formats/juce_audio_formats.h>

#include <algorithm>
#include <cmath>

namespace audiocity::engine::dspreset_export
{
namespace
{
constexpr auto kAudiocityChokeGroupAttribute = "audiocityChokeGroup";
constexpr auto kAudiocityChokeGroupTagPrefix = "audiocity_choke_";

[[nodiscard]] juce::String relativeOrAbsolutePath(const juce::File& target, const juce::File& presetFile)
{
    auto normalised = target.getRelativePathFrom(presetFile.getParentDirectory())
        .replaceCharacter('\\', '/');
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

[[nodiscard]] juce::String formatDb(float value)
{
    return juce::String(value, 2) + "dB";
}

bool writeSampleBufferAsWav(const juce::File& destWav,
                            const juce::AudioBuffer<float>& buffer,
                            double sampleRateHz)
{
    if (buffer.getNumChannels() <= 0 || buffer.getNumSamples() <= 0)
        return false;

    const auto sampleRate = sampleRateHz > 0.0 ? sampleRateHz : 44100.0;
    destWav.deleteFile();

    std::unique_ptr<juce::FileOutputStream> stream(destWav.createOutputStream());
    if (stream == nullptr)
        return false;

    juce::WavAudioFormat wav;
    std::unique_ptr<juce::AudioFormatWriter> writer(
        wav.createWriterFor(stream.get(),
                            sampleRate,
                            static_cast<unsigned int>(buffer.getNumChannels()),
                            16,
                            {},
                            0));
    if (writer == nullptr)
        return false;
    stream.release();

    return writer->writeFromAudioSampleBuffer(buffer, 0, buffer.getNumSamples());
}

void addDiagnostic(std::vector<ExportDiagnostic>& diagnostics,
                   ExportDiagnostic::Severity severity,
                   const juce::String& message)
{
    diagnostics.push_back({ severity, message.toStdString() });
}

[[nodiscard]] MidiRange intersectRanges(const MidiRange& first, const MidiRange& second)
{
    return { juce::jmax(first.low, second.low), juce::jmin(first.high, second.high) };
}

[[nodiscard]] VelocityRange intersectRanges(const VelocityRange& first, const VelocityRange& second)
{
    return { juce::jmax(first.low, second.low), juce::jmin(first.high, second.high) };
}

[[nodiscard]] int effectiveRoundRobinGroup(const Zone& zone, const Group* group)
{
    if (zone.roundRobinGroup > 0)
        return zone.roundRobinGroup;
    if (group != nullptr && group->roundRobinGroup > 0)
        return group->roundRobinGroup;
    return 0;
}

[[nodiscard]] RoundRobinMode effectiveRoundRobinMode(const Zone& zone, const Group* group)
{
    if (zone.roundRobinMode == RoundRobinMode::cycleRandom)
        return RoundRobinMode::cycleRandom;
    if (group != nullptr && group->roundRobinMode == RoundRobinMode::cycleRandom)
        return RoundRobinMode::cycleRandom;
    return RoundRobinMode::ordered;
}

[[nodiscard]] int effectiveChokeGroup(const Zone& zone, const Group* group)
{
    if (zone.chokeGroup > 0)
        return zone.chokeGroup;
    if (group != nullptr && group->chokeGroup > 0)
        return group->chokeGroup;
    return 0;
}

[[nodiscard]] juce::String makeChokeGroupTag(const int chokeGroup)
{
    return juce::String(kAudiocityChokeGroupTagPrefix) + juce::String(chokeGroup);
}

} // namespace

ExportResult exportProgramToDecentSampler(const juce::File& destPreset,
                                          const Program& program,
                                          const std::vector<juce::AudioBuffer<float>>& sampleDataByAsset,
                                          const ExportOptions& options)
{
    ExportResult result;
    result.writtenFile = destPreset;

    if (destPreset == juce::File{})
    {
        addDiagnostic(result.diagnostics,
                      ExportDiagnostic::Severity::error,
                      "DecentSampler export: destination path is empty");
        return result;
    }

    if (program.sampleAssets.size() != sampleDataByAsset.size())
    {
        addDiagnostic(result.diagnostics,
                      ExportDiagnostic::Severity::error,
                      "DecentSampler export: sample-data vector size does not match program asset count");
        return result;
    }

    const auto parent = destPreset.getParentDirectory();
    if (parent != juce::File{} && !parent.exists())
    {
        const auto createResult = parent.createDirectory();
        if (createResult.failed())
        {
            addDiagnostic(result.diagnostics,
                          ExportDiagnostic::Severity::error,
                          "DecentSampler export: failed to create destination folder: "
                              + createResult.getErrorMessage());
            return result;
        }
    }

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
                    addDiagnostic(result.diagnostics,
                                  ExportDiagnostic::Severity::error,
                                  "DecentSampler export: failed to create samples folder: "
                                      + createResult.getErrorMessage());
                    return result;
                }
            }

            juce::String stem = originalFile != juce::File{}
                ? originalFile.getFileNameWithoutExtension()
                : juce::String::fromUTF8(asset.displayName.c_str());
            stem = sanitizeFileNameStem(stem, static_cast<int>(i));

            juce::File candidate = samplesFolder.getChildFile(stem + ".wav");
            int suffix = 2;
            while (candidate.exists())
            {
                candidate = samplesFolder.getChildFile(stem + "_" + juce::String(suffix) + ".wav");
                ++suffix;
            }

            bool wrote = false;
            if (haveBuffer)
                wrote = writeSampleBufferAsWav(candidate, buffer, asset.sampleRateHz);
            else if (originalExists)
                wrote = originalFile.copyFileTo(candidate);

            if (!wrote)
            {
                addDiagnostic(result.diagnostics,
                              ExportDiagnostic::Severity::error,
                              "DecentSampler export: failed to write sample asset "
                                  + juce::String(static_cast<int>(i + 1)));
                return result;
            }

            resolvedSamplePaths[i] = relativeOrAbsolutePath(candidate, destPreset);
            ++result.copiedSampleCount;
        }
        else if (originalExists)
        {
            resolvedSamplePaths[i] = relativeOrAbsolutePath(originalFile, destPreset);
        }
        else if (!asset.sourcePath.empty())
        {
            resolvedSamplePaths[i] = juce::String::fromUTF8(asset.sourcePath.c_str())
                .replaceCharacter('\\', '/');
            addDiagnostic(result.diagnostics,
                          ExportDiagnostic::Severity::warning,
                          "DecentSampler export: sample asset "
                              + juce::String(static_cast<int>(i + 1))
                              + " could not be resolved on disk; keeping original path reference");
        }
        else
        {
            addDiagnostic(result.diagnostics,
                          ExportDiagnostic::Severity::warning,
                          "DecentSampler export: sample asset "
                              + juce::String(static_cast<int>(i + 1))
                              + " has no source path; affected zone(s) will be skipped");
        }
    }

    if (samplesFolder.exists() && result.copiedSampleCount > 0)
        result.samplesFolder = samplesFolder;

    juce::XmlElement root("DecentSampler");
    const juce::String presetName = options.libraryDisplayName.empty()
        ? juce::String::fromUTF8(program.name.c_str())
        : juce::String::fromUTF8(options.libraryDisplayName.c_str());
    if (presetName.isNotEmpty())
        root.setAttribute("name", presetName);

    juce::XmlElement* groupsNode = nullptr;
    std::vector<juce::XmlElement*> emittedGroups(program.groups.size(), nullptr);
    std::vector<int> groupRoundRobinLengths(program.groups.size(), 0);

    for (const auto& zone : program.zones)
    {
        if (program.isZoneGroupIndexValid(zone)
            && zone.groupIndex >= 0
            && static_cast<std::size_t>(zone.groupIndex) < groupRoundRobinLengths.size()
            && zone.roundRobinLength > groupRoundRobinLengths[static_cast<std::size_t>(zone.groupIndex)])
        {
            groupRoundRobinLengths[static_cast<std::size_t>(zone.groupIndex)] = zone.roundRobinLength;
        }
    }

    auto ensureGroupNode = [&](const int groupIndex) -> juce::XmlElement*
    {
        if (groupIndex < 0 || static_cast<std::size_t>(groupIndex) >= program.groups.size())
            return nullptr;

        auto*& groupNode = emittedGroups[static_cast<std::size_t>(groupIndex)];
        if (groupNode != nullptr)
            return groupNode;

        if (groupsNode == nullptr)
            groupsNode = root.createNewChildElement("groups");

        const auto& group = program.groups[static_cast<std::size_t>(groupIndex)];
        groupNode = groupsNode->createNewChildElement("group");

        const auto groupName = juce::String::fromUTF8(group.name.c_str()).trim();
        if (groupName.isNotEmpty())
            groupNode->setAttribute("name", groupName);
        if (std::abs(group.gainDb) > 0.0001f)
            groupNode->setAttribute("volume", formatDb(group.gainDb));
        if (std::abs(group.pan) > 0.0001f)
            groupNode->setAttribute("pan", formatFloatShort(group.pan * 100.0f, 1));

        if (group.roundRobinGroup > 0 || group.roundRobinMode != RoundRobinMode::ordered)
        {
            groupNode->setAttribute("seqMode",
                                    group.roundRobinMode == RoundRobinMode::cycleRandom ? "random"
                                                                                         : "round_robin");
            const auto roundRobinLength = groupRoundRobinLengths[static_cast<std::size_t>(groupIndex)];
            if (roundRobinLength > 1)
                groupNode->setAttribute("seqLength", roundRobinLength);
        }

        if (group.triggerMode == ZoneTriggerMode::release)
        {
            groupNode->setAttribute("trigger", "release");
        }
        else if (group.triggerMode == ZoneTriggerMode::oneShot)
        {
            addDiagnostic(result.diagnostics,
                          ExportDiagnostic::Severity::warning,
                          "DecentSampler export: one-shot group trigger is not supported and was omitted for group "
                              + juce::String(groupIndex + 1));
        }

        if (group.chokeGroup > 0)
            groupNode->setAttribute(kAudiocityChokeGroupAttribute, group.chokeGroup);

        return groupNode;
    };

    for (std::size_t zoneIndex = 0; zoneIndex < program.zones.size(); ++zoneIndex)
    {
        const auto& zone = program.zones[zoneIndex];
        if (!program.isZoneSampleIndexValid(zone))
        {
            addDiagnostic(result.diagnostics,
                          ExportDiagnostic::Severity::warning,
                          "DecentSampler export: zone "
                              + juce::String(static_cast<int>(zoneIndex + 1))
                              + " has no valid sample asset; skipped");
            continue;
        }

        const auto& samplePath = resolvedSamplePaths[static_cast<std::size_t>(zone.sampleAssetIndex)];
        if (samplePath.isEmpty())
        {
            addDiagnostic(result.diagnostics,
                          ExportDiagnostic::Severity::warning,
                          "DecentSampler export: zone "
                              + juce::String(static_cast<int>(zoneIndex + 1))
                              + " skipped because its sample asset has no usable path");
            continue;
        }

        const Group* group = program.isZoneGroupIndexValid(zone) && zone.groupIndex >= 0
            ? &program.groups[static_cast<std::size_t>(zone.groupIndex)]
            : nullptr;

        const auto effectiveKeyRange = group != nullptr ? intersectRanges(zone.keyRange, group->keyRange) : zone.keyRange;
        const auto effectiveVelocityRange = group != nullptr ? intersectRanges(zone.velocityRange, group->velocityRange)
                                                             : zone.velocityRange;
        if (!effectiveKeyRange.isValid() || !effectiveVelocityRange.isValid())
        {
            addDiagnostic(result.diagnostics,
                          ExportDiagnostic::Severity::warning,
                          "DecentSampler export: zone "
                              + juce::String(static_cast<int>(zoneIndex + 1))
                              + " has an empty effective key or velocity range; skipped");
            continue;
        }

        if (zone.velocityFadeIn.isEnabled() || zone.velocityFadeOut.isEnabled()
            || (group != nullptr && (group->velocityFadeIn.isEnabled() || group->velocityFadeOut.isEnabled())))
        {
            addDiagnostic(result.diagnostics,
                          ExportDiagnostic::Severity::warning,
                          "DecentSampler export: velocity fades are not supported and were omitted for zone "
                              + juce::String(static_cast<int>(zoneIndex + 1)));
        }

        auto* sampleParent = &root;
        if (group != nullptr)
        {
            if (auto* groupNode = ensureGroupNode(zone.groupIndex))
                sampleParent = groupNode;
        }

        auto* sampleNode = sampleParent->createNewChildElement("sample");
        sampleNode->setAttribute("path", samplePath);
        sampleNode->setAttribute("rootNote", zone.rootMidiNote);
        sampleNode->setAttribute("loNote", effectiveKeyRange.low);
        sampleNode->setAttribute("hiNote", effectiveKeyRange.high);
        sampleNode->setAttribute("loVel", effectiveVelocityRange.low);
        sampleNode->setAttribute("hiVel", effectiveVelocityRange.high);

        if (zone.chokeGroup > 0)
            sampleNode->setAttribute(kAudiocityChokeGroupAttribute, zone.chokeGroup);

        if (const auto chokeGroup = effectiveChokeGroup(zone, group); chokeGroup > 0)
        {
            const auto chokeTag = makeChokeGroupTag(chokeGroup);
            sampleNode->setAttribute("tags", chokeTag);
            sampleNode->setAttribute("silencedByTags", chokeTag);
        }

        if (zone.sampleStart > 0)
            sampleNode->setAttribute("start", zone.sampleStart);
        if (zone.sampleEndExclusive > 0)
            sampleNode->setAttribute("end", zone.sampleEndExclusive);

        if (std::abs(zone.tuneCents) > 0.0001f)
            sampleNode->setAttribute("tuning", formatFloatShort(zone.tuneCents / 100.0f));

        if (std::abs(zone.gainDb) > 0.0001f)
            sampleNode->setAttribute("volume", formatDb(zone.gainDb));

        if (std::abs(zone.pan) > 0.0001f)
            sampleNode->setAttribute("pan", formatFloatShort(zone.pan * 100.0f, 1));

        const auto roundRobinGroup = effectiveRoundRobinGroup(zone, group);
        const auto roundRobinMode = effectiveRoundRobinMode(zone, group);
        const bool hasRoundRobin = roundRobinGroup > 0
            || zone.roundRobinPosition > 0
            || zone.roundRobinLength > 1
            || roundRobinMode != RoundRobinMode::ordered;
        if (hasRoundRobin)
        {
            if (zone.roundRobinPosition > 0)
            {
                sampleNode->setAttribute("seqMode",
                                         roundRobinMode == RoundRobinMode::cycleRandom ? "random"
                                                                                        : "round_robin");
                sampleNode->setAttribute("seqPosition", juce::jmax(1, zone.roundRobinPosition));
                if (zone.roundRobinLength > 1)
                    sampleNode->setAttribute("seqLength", zone.roundRobinLength);
            }
            else
            {
                addDiagnostic(result.diagnostics,
                              ExportDiagnostic::Severity::warning,
                              "DecentSampler export: round-robin data requires explicit seqPosition values and was omitted for zone "
                                  + juce::String(static_cast<int>(zoneIndex + 1)));
            }
        }

        if (zone.loopMode != ZoneLoopMode::noLoop
            && zone.loopStart >= 0
            && zone.loopEndExclusive > zone.loopStart)
        {
            sampleNode->setAttribute("loopEnabled", "true");
            sampleNode->setAttribute("loopStart", zone.loopStart);
            sampleNode->setAttribute("loopEnd", zone.loopEndExclusive);
            if (zone.loopMode == ZoneLoopMode::sustain)
            {
                addDiagnostic(result.diagnostics,
                              ExportDiagnostic::Severity::warning,
                              "DecentSampler export: sustain loops are exported as continuous loops for zone "
                                  + juce::String(static_cast<int>(zoneIndex + 1)));
            }
        }

        if (zone.triggerMode == ZoneTriggerMode::release)
        {
            sampleNode->setAttribute("trigger", "release");
        }
        else if (zone.triggerMode == ZoneTriggerMode::oneShot)
        {
            addDiagnostic(result.diagnostics,
                          ExportDiagnostic::Severity::warning,
                          "DecentSampler export: one-shot trigger is not supported and was omitted for zone "
                              + juce::String(static_cast<int>(zoneIndex + 1)));
        }

        ++result.writtenSampleCount;
    }

    if (!destPreset.replaceWithText(root.toString(), false, false, "\n"))
    {
        addDiagnostic(result.diagnostics,
                      ExportDiagnostic::Severity::error,
                      "DecentSampler export: failed to write preset XML to disk");
    }

    return result;
}
} // namespace audiocity::engine::dspreset_export