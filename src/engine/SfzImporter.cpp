#include "SfzImporter.h"

#include "AudioFileSupport.h"
#include "ImportCancellation.h"

#include <juce_audio_formats/juce_audio_formats.h>

#include <algorithm>
#include <cctype>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <utility>

namespace audiocity::engine
{
namespace
{
struct PreprocessedLine
{
    juce::String text;
    juce::File file;
    int line = 0;
};

using OpcodeMap = std::map<std::string, juce::String>;
using Defines = std::map<std::string, juce::String>;

void addDiagnostic(std::vector<SfzDiagnostic>& diagnostics,
                   const SfzDiagnostic::Severity severity,
                   const juce::String& message,
                   const juce::File& file,
                   const int line)
{
    diagnostics.push_back({ severity, message.toStdString(), file.getFullPathName().toStdString(), line });
}

[[nodiscard]] std::string lowerAscii(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(), [](const unsigned char c)
    {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

[[nodiscard]] bool isKnownOpcode(const std::string& key) noexcept
{
    static const std::set<std::string> known
    {
        "default_path",
        "sample",
        "lokey",
        "hikey",
        "key",
        "pitch_keycenter",
        "lovel",
        "hivel",
        "transpose",
        "tune",
        "offset",
        "end",
        "loop_start",
        "loop_end",
        "loop_mode",
        "volume",
        "pan",
        "seq_position",
        "seq_length",
        "seq_mode",
        "xfin_lovel",
        "xfin_hivel",
        "xfout_lovel",
        "xfout_hivel",
        "off_by",
        "trigger"
    };

    return known.find(key) != known.end();
}

[[nodiscard]] bool isSupportedLoopModeValue(const std::string& value) noexcept
{
    return value == "loop_sustain"
        || value == "loop_continuous"
        || value == "one_shot"
        || value == "no_loop";
}

[[nodiscard]] bool isSupportedTriggerValue(const std::string& value) noexcept
{
    return value == "attack"
        || value == "release";
}

[[nodiscard]] bool isSupportedSeqModeValue(const std::string& value) noexcept
{
    return value == "random"
        || value == "round_robin"
        || value == "sequence"
        || value == "sequential";
}

void validateOpcodeValue(std::vector<SfzDiagnostic>& diagnostics,
                         const std::string& key,
                         const juce::String& value,
                         const juce::File& file,
                         const int line)
{
    const auto normalized = lowerAscii(value.trim().toStdString());
    if (normalized.empty())
        return;

    if (key == "loop_mode" && !isSupportedLoopModeValue(normalized))
    {
        addDiagnostic(diagnostics,
            SfzDiagnostic::Severity::warning,
            "Unsupported SFZ loop_mode value " + value.trim(),
            file,
            line);
    }
    else if (key == "trigger" && !isSupportedTriggerValue(normalized))
    {
        addDiagnostic(diagnostics,
            SfzDiagnostic::Severity::warning,
            "Unsupported SFZ trigger value " + value.trim(),
            file,
            line);
    }
    else if (key == "seq_mode" && !isSupportedSeqModeValue(normalized))
    {
        addDiagnostic(diagnostics,
            SfzDiagnostic::Severity::warning,
            "Unsupported SFZ seq_mode value " + value.trim(),
            file,
            line);
    }
}

[[nodiscard]] juce::String stripLineComment(const juce::String& line)
{
    const auto commentStart = line.indexOf("//");
    return commentStart >= 0 ? line.substring(0, commentStart) : line;
}

[[nodiscard]] juce::String expandVariables(const juce::String& input,
                                           const Defines& defines,
                                           std::vector<SfzDiagnostic>& diagnostics,
                                           const juce::File& file,
                                           const int line)
{
    const auto text = input.toStdString();
    std::string expanded;
    expanded.reserve(text.size());

    for (std::size_t index = 0; index < text.size();)
    {
        if (text[index] != '$')
        {
            expanded.push_back(text[index]);
            ++index;
            continue;
        }

        auto end = index + 1;
        while (end < text.size())
        {
            const auto c = static_cast<unsigned char>(text[end]);
            if (!std::isalnum(c) && text[end] != '_')
                break;

            ++end;
        }

        if (end == index + 1)
        {
            expanded.push_back(text[index]);
            ++index;
            continue;
        }

        const auto name = text.substr(index, end - index);
        const auto found = defines.find(name);
        if (found == defines.end())
        {
            addDiagnostic(diagnostics,
                SfzDiagnostic::Severity::warning,
                "Unknown SFZ variable " + juce::String(name),
                file,
                line);
            expanded.append(name);
        }
        else
        {
            expanded.append(found->second.toStdString());
        }

        index = end;
    }

    return juce::String(expanded);
}

[[nodiscard]] juce::File resolveAgainstRoot(const juce::File& rootDirectory, const juce::String& path)
{
    juce::File candidate(path);
    if (juce::File::isAbsolutePath(path))
        return candidate;

    return rootDirectory.getChildFile(path);
}

void preprocessFile(const juce::File& file,
                    const juce::File& rootDirectory,
                    Defines& defines,
                    std::vector<juce::File>& includeStack,
                    std::vector<PreprocessedLine>& output,
                    std::vector<SfzDiagnostic>& diagnostics)
{
    if (isImportCancellationRequested())
        return;

    for (const auto& stacked : includeStack)
    {
        if (stacked == file)
        {
            addDiagnostic(diagnostics,
                SfzDiagnostic::Severity::error,
                "SFZ include cycle detected",
                file,
                0);
            return;
        }
    }

    if (!file.existsAsFile())
    {
        addDiagnostic(diagnostics,
            SfzDiagnostic::Severity::error,
            "SFZ file does not exist",
            file,
            0);
        return;
    }

    includeStack.push_back(file);

    juce::StringArray lines;
    juce::MemoryBlock fileBytes;
    if (!readFileInCancellableChunks(file, fileBytes))
    {
        includeStack.pop_back();
        return;
    }
    lines.addLines(juce::String::fromUTF8(static_cast<const char*>(fileBytes.getData()),
                                          static_cast<int>(fileBytes.getSize())));

    for (int lineIndex = 0; lineIndex < lines.size(); ++lineIndex)
    {
        if (isImportCancellationRequested())
            break;

        const auto lineNumber = lineIndex + 1;
        const auto rawLine = stripLineComment(lines[lineIndex]);
        const auto trimmed = rawLine.trim();

        if (trimmed.startsWith("#define"))
        {
            const auto rest = trimmed.substring(7).trim();
            const auto firstSpace = rest.indexOfAnyOf(" \t");
            if (firstSpace <= 0)
            {
                addDiagnostic(diagnostics,
                    SfzDiagnostic::Severity::warning,
                    "Malformed SFZ #define directive",
                    file,
                    lineNumber);
                continue;
            }

            const auto name = rest.substring(0, firstSpace).trim().toStdString();
            const auto value = expandVariables(rest.substring(firstSpace).trim(), defines, diagnostics, file, lineNumber);
            if (!name.empty() && name.front() == '$')
                defines[name] = value;
            else
                addDiagnostic(diagnostics,
                    SfzDiagnostic::Severity::warning,
                    "SFZ #define names should start with $",
                    file,
                    lineNumber);
            continue;
        }

        if (trimmed.startsWith("#include"))
        {
            const auto firstQuote = trimmed.indexOfChar('"');
            const auto secondQuote = firstQuote >= 0 ? trimmed.indexOfChar(firstQuote + 1, '"') : -1;
            if (firstQuote < 0 || secondQuote <= firstQuote)
            {
                addDiagnostic(diagnostics,
                    SfzDiagnostic::Severity::error,
                    "Malformed SFZ #include directive",
                    file,
                    lineNumber);
                continue;
            }

            const auto includePath = expandVariables(
                trimmed.substring(firstQuote + 1, secondQuote),
                defines,
                diagnostics,
                file,
                lineNumber);
            const auto includeFile = resolveAgainstRoot(rootDirectory, includePath);
            if (!includeFile.existsAsFile())
            {
                addDiagnostic(diagnostics,
                    SfzDiagnostic::Severity::error,
                    "Missing SFZ include file " + includePath,
                    file,
                    lineNumber);
                continue;
            }

            preprocessFile(includeFile, rootDirectory, defines, includeStack, output, diagnostics);
            continue;
        }

        output.push_back({ expandVariables(rawLine, defines, diagnostics, file, lineNumber), file, lineNumber });
    }

    includeStack.pop_back();
}

[[nodiscard]] std::optional<int> parseInt(const OpcodeMap& opcodes, const std::string& key)
{
    const auto found = opcodes.find(key);
    if (found == opcodes.end())
        return std::nullopt;

    const auto trimmed = found->second.trim();
    if (!trimmed.containsOnly("-0123456789"))
        return std::nullopt;

    return trimmed.getIntValue();
}

[[nodiscard]] std::optional<float> parseFloat(const OpcodeMap& opcodes, const std::string& key)
{
    const auto found = opcodes.find(key);
    if (found == opcodes.end())
        return std::nullopt;

    const auto value = found->second.trim().getFloatValue();
    return std::isfinite(value) ? std::optional<float>{ value } : std::nullopt;
}

[[nodiscard]] std::optional<int> parseMidiNoteValue(const juce::String& value)
{
    const auto trimmed = value.trim();
    if (trimmed.containsOnly("-0123456789"))
        return clampMidiNote(trimmed.getIntValue());

    const auto upper = trimmed.toUpperCase();
    static constexpr const char* noteNames[] =
    {
        "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"
    };

    for (int note = 0; note < 12; ++note)
    {
        const juce::String noteName(noteNames[note]);
        if (!upper.startsWith(noteName))
            continue;

        const auto octavePart = upper.substring(noteName.length()).trim();
        if (!octavePart.containsOnly("-0123456789"))
            continue;

        return clampMidiNote(((octavePart.getIntValue() + 1) * 12) + note);
    }

    return std::nullopt;
}

[[nodiscard]] std::optional<int> parseMidiNote(const OpcodeMap& opcodes, const std::string& key)
{
    const auto found = opcodes.find(key);
    if (found == opcodes.end())
        return std::nullopt;

    return parseMidiNoteValue(found->second);
}

template <typename Map>
void overlayOpcodes(OpcodeMap& target, const Map& source)
{
    for (const auto& [key, value] : source)
        target[key] = value;
}

[[nodiscard]] MidiRange parseKeyRange(const OpcodeMap& opcodes)
{
    if (const auto key = parseMidiNote(opcodes, "key"); key.has_value())
        return MidiRange::single(*key);

    const auto low = parseMidiNote(opcodes, "lokey").value_or(kMidiNoteMin);
    const auto high = parseMidiNote(opcodes, "hikey").value_or(kMidiNoteMax);
    return MidiRange::fromUnordered(low, high);
}

[[nodiscard]] VelocityRange parseVelocityRange(const OpcodeMap& opcodes)
{
    const auto low = parseInt(opcodes, "lovel").value_or(kVelocityMin);
    const auto high = parseInt(opcodes, "hivel").value_or(kVelocityMax);
    return VelocityRange::fromUnordered(low, high);
}

[[nodiscard]] VelocityFadeRange parseVelocityFadeRange(const OpcodeMap& opcodes,
                                                       const std::string& lowKey,
                                                       const std::string& highKey)
{
    const auto low = parseInt(opcodes, lowKey);
    const auto high = parseInt(opcodes, highKey);
    if (!low.has_value() || !high.has_value())
        return VelocityFadeRange::disabled();

    return VelocityFadeRange::fromUnordered(*low, *high);
}

[[nodiscard]] int parseRootMidiNote(const OpcodeMap& opcodes, const int fallback)
{
    if (const auto pitchKeycenter = parseMidiNote(opcodes, "pitch_keycenter"); pitchKeycenter.has_value())
        return *pitchKeycenter;

    if (const auto key = parseMidiNote(opcodes, "key"); key.has_value())
        return *key;

    return clampMidiNote(fallback);
}

[[nodiscard]] float parsePan(const OpcodeMap& opcodes)
{
    auto pan = parseFloat(opcodes, "pan").value_or(0.0f);
    if (std::abs(pan) > 1.0f)
        pan /= 100.0f;

    return juce::jlimit(-1.0f, 1.0f, pan);
}

[[nodiscard]] RoundRobinMode parseRoundRobinMode(const OpcodeMap& opcodes)
{
    const auto found = opcodes.find("seq_mode");
    if (found == opcodes.end())
        return RoundRobinMode::ordered;

    const auto mode = lowerAscii(found->second.trim().toStdString());
    if (mode == "random")
        return RoundRobinMode::cycleRandom;

    return RoundRobinMode::ordered;
}

[[nodiscard]] ZoneLoopMode parseLoopMode(const OpcodeMap& opcodes)
{
    const auto found = opcodes.find("loop_mode");
    if (found == opcodes.end())
        return ZoneLoopMode::noLoop;

    const auto mode = lowerAscii(found->second.trim().toStdString());
    if (mode == "loop_sustain")
        return ZoneLoopMode::sustain;

    if (mode == "loop_continuous")
        return ZoneLoopMode::continuous;

    if (mode == "one_shot" || mode == "no_loop")
        return ZoneLoopMode::noLoop;

    return ZoneLoopMode::noLoop;
}

[[nodiscard]] ZoneTriggerMode parseTriggerMode(const OpcodeMap& opcodes)
{
    if (const auto loopMode = opcodes.find("loop_mode"); loopMode != opcodes.end())
    {
        if (lowerAscii(loopMode->second.trim().toStdString()) == "one_shot")
            return ZoneTriggerMode::oneShot;
    }

    if (const auto trigger = opcodes.find("trigger"); trigger != opcodes.end())
    {
        const auto triggerValue = lowerAscii(trigger->second.trim().toStdString());
        if (triggerValue == "release")
            return ZoneTriggerMode::release;

        if (triggerValue == "attack")
            return ZoneTriggerMode::gate;
    }

    return ZoneTriggerMode::gate;
}

[[nodiscard]] Group makeGroupFromOpcodes(const OpcodeMap& opcodes)
{
    Group group;
    group.keyRange = parseKeyRange(opcodes);
    group.velocityRange = parseVelocityRange(opcodes);
    group.velocityFadeIn = parseVelocityFadeRange(opcodes, "xfin_lovel", "xfin_hivel");
    group.velocityFadeOut = parseVelocityFadeRange(opcodes, "xfout_lovel", "xfout_hivel");
    group.gainDb = parseFloat(opcodes, "volume").value_or(0.0f);
    group.pan = parsePan(opcodes);
    group.roundRobinMode = parseRoundRobinMode(opcodes);
    group.triggerMode = parseTriggerMode(opcodes);
    group.chokeGroup = parseInt(opcodes, "off_by").value_or(0);
    return group;
}

[[nodiscard]] juce::File resolveSampleFile(const juce::File& rootDirectory,
                                           const OpcodeMap& controlOpcodes,
                                           const juce::String& sampleValue)
{
    juce::String defaultPath;
    if (const auto found = controlOpcodes.find("default_path"); found != controlOpcodes.end())
        defaultPath = found->second;

    const auto combined = defaultPath.isEmpty() ? sampleValue : defaultPath + sampleValue;
    return resolveAgainstRoot(rootDirectory, combined);
}

[[nodiscard]] int safeReaderLength(const juce::AudioFormatReader& reader) noexcept
{
    const auto maxInt = static_cast<juce::int64>(std::numeric_limits<int>::max());
    return static_cast<int>(juce::jlimit<juce::int64>(0, maxInt, reader.lengthInSamples));
}

struct ParserState
{
    Program program;
    std::vector<juce::AudioBuffer<float>>& sampleDataByAsset;
    OpcodeMap controlOpcodes;
    OpcodeMap globalOpcodes;
    OpcodeMap masterOpcodes;
    OpcodeMap groupOpcodes;
    OpcodeMap regionOpcodes;
    std::map<std::string, int> sampleAssetIndices;
    std::vector<SfzDiagnostic>& diagnostics;
    juce::AudioFormatManager& formatManager;
    juce::File rootDirectory;
    int currentGroupIndex = -1;
    bool hasActiveGroup = false;

    [[nodiscard]] OpcodeMap groupInheritedOpcodes() const
    {
        OpcodeMap combined;
        overlayOpcodes(combined, globalOpcodes);
        overlayOpcodes(combined, masterOpcodes);
        overlayOpcodes(combined, groupOpcodes);
        return combined;
    }

    [[nodiscard]] OpcodeMap regionInheritedOpcodes() const
    {
        auto combined = groupInheritedOpcodes();
        overlayOpcodes(combined, regionOpcodes);
        return combined;
    }

    [[nodiscard]] int ensureCurrentGroup()
    {
        if (!hasActiveGroup)
            return -1;

        if (currentGroupIndex >= 0)
            return currentGroupIndex;

        auto group = makeGroupFromOpcodes(groupInheritedOpcodes());
        group.name = "SFZ Group " + std::to_string(program.groups.size() + 1);
        if (group.roundRobinMode == RoundRobinMode::cycleRandom && group.roundRobinGroup <= 0)
            group.roundRobinGroup = static_cast<int>(program.groups.size()) + 1;
        program.groups.push_back(group);
        currentGroupIndex = static_cast<int>(program.groups.size() - 1);
        return currentGroupIndex;
    }

    [[nodiscard]] int getOrAddSampleAsset(const juce::File& sampleFile, const int rootMidiNote, const PreprocessedLine& line)
    {
        if (isImportCancellationRequested())
            return -1;

        auto openResult = audio_file::openReaderForFile(formatManager, sampleFile);
        auto reader = std::move(openResult.reader);
        const auto path = openResult.readableFile.existsAsFile()
            ? openResult.readableFile.getFullPathName().toStdString()
            : sampleFile.getFullPathName().toStdString();
        const auto found = sampleAssetIndices.find(path);
        if (found != sampleAssetIndices.end())
            return found->second;

        if (reader == nullptr)
        {
            addDiagnostic(diagnostics,
                SfzDiagnostic::Severity::error,
                openResult.errorMessage.isNotEmpty() ? openResult.errorMessage
                                                     : ("Unsupported or unreadable SFZ sample "
                                                        + sampleFile.getFullPathName()),
                line.file,
                line.line);
            return -1;
        }

        SampleAsset asset;
        asset.sourcePath = path;
        asset.displayName = sampleFile.getFileName().toStdString();
        asset.lengthSamples = safeReaderLength(*reader);
        asset.numChannels = static_cast<int>(reader->numChannels);
        asset.sampleRateHz = reader->sampleRate;
        asset.rootMidiNote = rootMidiNote;
        asset.bitDepth = static_cast<int>(reader->bitsPerSample);
        asset.embeddedInProgram = false;

        if (!asset.hasAudio())
        {
            addDiagnostic(diagnostics,
                SfzDiagnostic::Severity::error,
                "SFZ sample has no readable audio " + sampleFile.getFullPathName(),
                line.file,
                line.line);
            return -1;
        }

        juce::AudioBuffer<float> sampleData(asset.numChannels, asset.lengthSamples);
        if (!readAudioInCancellableChunks(*reader, sampleData, asset.lengthSamples))
        {
            addDiagnostic(diagnostics,
                SfzDiagnostic::Severity::error,
                "Could not decode SFZ sample " + sampleFile.getFullPathName(),
                line.file,
                line.line);
            return -1;
        }

        program.sampleAssets.push_back(asset);
        sampleDataByAsset.push_back(sampleData);
        const auto index = static_cast<int>(program.sampleAssets.size() - 1);
        sampleAssetIndices[path] = index;
        return index;
    }

    void finishRegion(const PreprocessedLine& line)
    {
        if (regionOpcodes.empty())
            return;

        const auto opcodes = regionInheritedOpcodes();
        const auto sampleOpcode = opcodes.find("sample");
        if (sampleOpcode == opcodes.end() || sampleOpcode->second.trim().isEmpty())
        {
            addDiagnostic(diagnostics,
                SfzDiagnostic::Severity::warning,
                "SFZ region skipped because it has no sample opcode",
                line.file,
                line.line);
            regionOpcodes.clear();
            return;
        }

        const auto rootMidiNote = parseRootMidiNote(opcodes, 60);
        const auto sampleFile = resolveSampleFile(rootDirectory, controlOpcodes, sampleOpcode->second.trim().unquoted());
        if (!sampleFile.existsAsFile())
        {
            addDiagnostic(diagnostics,
                SfzDiagnostic::Severity::error,
                "Missing SFZ sample " + sampleFile.getFullPathName(),
                line.file,
                line.line);
            regionOpcodes.clear();
            return;
        }

        const auto sampleAssetIndex = getOrAddSampleAsset(sampleFile, rootMidiNote, line);
        if (sampleAssetIndex < 0)
        {
            regionOpcodes.clear();
            return;
        }

        Zone zone;
        zone.sampleAssetIndex = sampleAssetIndex;
        zone.groupIndex = ensureCurrentGroup();
        zone.keyRange = parseKeyRange(opcodes);
        zone.velocityRange = parseVelocityRange(opcodes);
        zone.velocityFadeIn = parseVelocityFadeRange(opcodes, "xfin_lovel", "xfin_hivel");
        zone.velocityFadeOut = parseVelocityFadeRange(opcodes, "xfout_lovel", "xfout_hivel");
        zone.rootMidiNote = rootMidiNote;
        zone.sampleStart = juce::jmax(0, parseInt(opcodes, "offset").value_or(0));
        if (const auto sampleEnd = parseInt(opcodes, "end"); sampleEnd.has_value())
            zone.sampleEndExclusive = juce::jmax(zone.sampleStart + 1, *sampleEnd + 1);
        zone.loopStart = parseInt(opcodes, "loop_start").value_or(-1);
        const auto loopEnd = parseInt(opcodes, "loop_end").value_or(-1);
        zone.loopEndExclusive = loopEnd >= 0 ? loopEnd + 1 : -1;
        zone.gainDb = parseFloat(opcodes, "volume").value_or(0.0f);
        zone.pan = parsePan(opcodes);
        zone.tuneCents = parseFloat(opcodes, "tune").value_or(0.0f)
            + (static_cast<float>(parseInt(opcodes, "transpose").value_or(0)) * 100.0f);
        zone.roundRobinPosition = parseInt(opcodes, "seq_position").value_or(0);
        zone.roundRobinLength = juce::jmax(0, parseInt(opcodes, "seq_length").value_or(0));
        zone.roundRobinMode = parseRoundRobinMode(opcodes);
        if (zone.roundRobinPosition > 0 || zone.roundRobinMode == RoundRobinMode::cycleRandom)
            zone.roundRobinGroup = zone.groupIndex >= 0 ? zone.groupIndex + 1 : 1;
        zone.chokeGroup = parseInt(opcodes, "off_by").value_or(0);
        zone.loopMode = parseLoopMode(opcodes);
        zone.triggerMode = parseTriggerMode(opcodes);

        program.zones.push_back(zone);
        regionOpcodes.clear();
    }
};

enum class HeaderType
{
    none,
    control,
    global,
    master,
    group,
    region
};

[[nodiscard]] HeaderType parseHeaderType(const juce::String& header)
{
    const auto lower = header.trim().toLowerCase();
    if (lower == "control")
        return HeaderType::control;
    if (lower == "global")
        return HeaderType::global;
    if (lower == "master")
        return HeaderType::master;
    if (lower == "group")
        return HeaderType::group;
    if (lower == "region")
        return HeaderType::region;

    return HeaderType::none;
}

void setHeader(ParserState& state,
               HeaderType& currentHeader,
               const HeaderType nextHeader,
               const PreprocessedLine& line)
{
    if (currentHeader == HeaderType::region)
        state.finishRegion(line);

    currentHeader = nextHeader;

    if (nextHeader == HeaderType::global)
    {
        state.globalOpcodes.clear();
        state.masterOpcodes.clear();
        state.groupOpcodes.clear();
        state.currentGroupIndex = -1;
        state.hasActiveGroup = false;
    }
    else if (nextHeader == HeaderType::master)
    {
        state.masterOpcodes.clear();
        state.groupOpcodes.clear();
        state.currentGroupIndex = -1;
        state.hasActiveGroup = false;
    }
    else if (nextHeader == HeaderType::group)
    {
        state.groupOpcodes.clear();
        state.currentGroupIndex = -1;
        state.hasActiveGroup = true;
    }
    else if (nextHeader == HeaderType::region)
    {
        state.regionOpcodes.clear();
    }
}

[[nodiscard]] std::vector<std::pair<std::string, juce::String>> parseOpcodeTokens(const juce::String& text)
{
    const auto source = text.toStdString();
    std::vector<std::pair<std::string, juce::String>> tokens;
    std::size_t index = 0;

    while (index < source.size())
    {
        while (index < source.size() && std::isspace(static_cast<unsigned char>(source[index])))
            ++index;

        const auto keyStart = index;
        while (index < source.size() && source[index] != '=' && !std::isspace(static_cast<unsigned char>(source[index])))
            ++index;

        if (index >= source.size() || source[index] != '=')
        {
            while (index < source.size() && !std::isspace(static_cast<unsigned char>(source[index])))
                ++index;
            continue;
        }

        auto key = lowerAscii(source.substr(keyStart, index - keyStart));
        ++index;

        std::string value;
        if (index < source.size() && source[index] == '"')
        {
            ++index;
            while (index < source.size() && source[index] != '"')
                value.push_back(source[index++]);
            if (index < source.size() && source[index] == '"')
                ++index;
        }
        else
        {
            while (index < source.size() && !std::isspace(static_cast<unsigned char>(source[index])))
                value.push_back(source[index++]);
        }

        if (!key.empty())
            tokens.emplace_back(std::move(key), juce::String(value));
    }

    return tokens;
}

void applyOpcodeTokens(ParserState& state,
                       const HeaderType currentHeader,
                       const juce::String& text,
                       const PreprocessedLine& line)
{
    auto tokens = parseOpcodeTokens(text);
    OpcodeMap* target = nullptr;

    switch (currentHeader)
    {
        case HeaderType::control:
            target = &state.controlOpcodes;
            break;
        case HeaderType::global:
            target = &state.globalOpcodes;
            break;
        case HeaderType::master:
            target = &state.masterOpcodes;
            break;
        case HeaderType::group:
            target = &state.groupOpcodes;
            break;
        case HeaderType::region:
            target = &state.regionOpcodes;
            break;
        case HeaderType::none:
        default:
            return;
    }

    for (const auto& [key, value] : tokens)
    {
        if (!isKnownOpcode(key))
        {
            addDiagnostic(state.diagnostics,
                SfzDiagnostic::Severity::warning,
                "Unknown SFZ opcode " + juce::String(key),
                line.file,
                line.line);
        }

            validateOpcodeValue(state.diagnostics, key, value, line.file, line.line);

        (*target)[key] = value;
    }
}

void parsePreprocessedLines(ParserState& state, const std::vector<PreprocessedLine>& lines)
{
    auto currentHeader = HeaderType::none;
    PreprocessedLine lastLine;

    for (const auto& line : lines)
    {
        if (isImportCancellationRequested())
            break;

        lastLine = line;
        auto text = stripLineComment(line.text).trim();
        while (!text.isEmpty())
        {
            if (text.startsWithChar('<'))
            {
                const auto close = text.indexOfChar('>');
                if (close < 0)
                {
                    addDiagnostic(state.diagnostics,
                        SfzDiagnostic::Severity::warning,
                        "Malformed SFZ header",
                        line.file,
                        line.line);
                    break;
                }

                const auto header = parseHeaderType(text.substring(1, close));
                if (header == HeaderType::none)
                {
                    addDiagnostic(state.diagnostics,
                        SfzDiagnostic::Severity::warning,
                        "Unknown SFZ header " + text.substring(0, close + 1),
                        line.file,
                        line.line);
                }
                else
                {
                    setHeader(state, currentHeader, header, line);
                }

                text = text.substring(close + 1).trim();
                continue;
            }

            const auto nextHeader = text.indexOfChar('<');
            const auto opcodeText = nextHeader >= 0 ? text.substring(0, nextHeader) : text;
            applyOpcodeTokens(state, currentHeader, opcodeText, line);
            text = nextHeader >= 0 ? text.substring(nextHeader).trim() : juce::String();
        }
    }

    if (currentHeader == HeaderType::region)
        state.finishRegion(lastLine);
}
} // namespace

bool SfzImportResult::hasErrors() const noexcept
{
    for (const auto& diagnostic : diagnostics)
    {
        if (diagnostic.severity == SfzDiagnostic::Severity::error)
            return true;
    }

    return false;
}

SfzImportResult SfzImporter::importFile(const juce::File& sfzFile) const
{
    SfzImportResult result;
    result.program.name = sfzFile.getFileNameWithoutExtension().toStdString();

    const auto rootDirectory = sfzFile.getParentDirectory();
    Defines defines;
    std::vector<juce::File> includeStack;
    std::vector<PreprocessedLine> preprocessedLines;
    preprocessFile(sfzFile, rootDirectory, defines, includeStack, preprocessedLines, result.diagnostics);

    juce::AudioFormatManager formatManager;
    audio_file::registerAudioFormats(formatManager);

    ParserState state{ result.program, result.sampleDataByAsset, {}, {}, {}, {}, {}, {}, result.diagnostics, formatManager, rootDirectory };
    parsePreprocessedLines(state, preprocessedLines);
    result.program = std::move(state.program);
    return result;
}
} // namespace audiocity::engine
