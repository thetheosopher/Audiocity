#include "SfzImport.h"

#include <algorithm>
#include <array>
#include <map>
#include <set>

namespace audiocity::engine::sfz
{
namespace
{
struct SourceLine
{
    juce::String text;
    juce::String filePath;
    int line = 0;
};

struct PreprocessResult
{
    std::vector<SourceLine> lines;
    std::vector<Diagnostic> diagnostics;
};

using OpcodeMap = std::map<juce::String, juce::String>;

void addDiagnostic(std::vector<Diagnostic>& diagnostics,
    const DiagnosticSeverity severity,
    const juce::String& filePath,
    const int line,
    const juce::String& message)
{
    diagnostics.push_back({ severity, filePath, line, message });
}

juce::String stripComment(const juce::String& line)
{
    const auto commentPos = line.indexOf("//");
    if (commentPos >= 0)
        return line.substring(0, commentPos);

    return line;
}

juce::String expandDefines(const juce::String& value,
    const std::map<juce::String, juce::String>& defines,
    std::vector<Diagnostic>& diagnostics,
    const juce::String& filePath,
    const int line)
{
    juce::String expanded = value;

    int cursor = 0;
    while (cursor < expanded.length())
    {
        if (expanded[cursor] != '$')
        {
            ++cursor;
            continue;
        }

        int end = cursor + 1;
        while (end < expanded.length())
        {
            const auto c = expanded[end];
            if (!juce::CharacterFunctions::isLetterOrDigit(c) && c != '_')
                break;
            ++end;
        }

        const auto key = expanded.substring(cursor, end);
        const auto found = defines.find(key);

        if (found == defines.end())
        {
            addDiagnostic(diagnostics, DiagnosticSeverity::warning, filePath, line,
                "Unknown variable '" + key + "', leaving literal text.");
            cursor = end;
            continue;
        }

        expanded = expanded.replaceSection(cursor, end - cursor, found->second);
        cursor += found->second.length();
    }

    return expanded;
}

void preprocessFile(const juce::File& file,
    const juce::File& rootDir,
    std::map<juce::String, juce::String>& defines,
    std::set<juce::String>& includeStack,
    PreprocessResult& result)
{
    const auto canonicalPath = file.getFullPathName();

    if (includeStack.find(canonicalPath) != includeStack.end())
    {
        addDiagnostic(result.diagnostics, DiagnosticSeverity::error, canonicalPath, 0,
            "Include cycle detected, include skipped.");
        return;
    }

    if (!file.existsAsFile())
    {
        addDiagnostic(result.diagnostics, DiagnosticSeverity::error, canonicalPath, 0,
            "Missing include file.");
        return;
    }

    includeStack.insert(canonicalPath);

    juce::StringArray rawLines;
    rawLines.addLines(file.loadFileAsString());

    for (int lineIndex = 0; lineIndex < rawLines.size(); ++lineIndex)
    {
        const auto lineNumber = lineIndex + 1;
        auto line = stripComment(rawLines[lineIndex]).trim();

        if (line.isEmpty())
            continue;

        if (line.startsWith("#define"))
        {
            const auto rest = line.fromFirstOccurrenceOf("#define", false, false).trim();
            const auto name = rest.upToFirstOccurrenceOf(" ", false, false).trim();
            const auto value = rest.fromFirstOccurrenceOf(name, false, false).trim();

            if (!name.startsWith("$"))
            {
                addDiagnostic(result.diagnostics, DiagnosticSeverity::warning, canonicalPath, lineNumber,
                    "Malformed #define directive, expected '$NAME'.");
                continue;
            }

            defines[name] = value;
            continue;
        }

        if (line.startsWith("#include"))
        {
            auto includeToken = line.fromFirstOccurrenceOf("#include", false, false).trim();
            includeToken = includeToken.unquoted();
            includeToken = expandDefines(includeToken, defines, result.diagnostics, canonicalPath, lineNumber);

            const auto includePath = rootDir.getChildFile(includeToken).getFullPathName();
            preprocessFile(juce::File(includePath), rootDir, defines, includeStack, result);
            continue;
        }

        line = expandDefines(line, defines, result.diagnostics, canonicalPath, lineNumber);
        result.lines.push_back({ line, canonicalPath, lineNumber });
    }

    includeStack.erase(canonicalPath);
}

int parseIntOr(const juce::String& value, const int fallback)
{
    if (value.trim().isEmpty())
        return fallback;

    return value.getIntValue();
}

bool isSupportedOpcode(const juce::String& key)
{
    static const std::array<juce::String, 16> supported = {
        "default_path", "sample", "lokey", "hikey", "key", "pitch_keycenter",
        "lovel", "hivel", "transpose", "tune", "offset", "loop_start", "loop_end",
        "loop_mode", "group", "seq_group"
    };

    return std::find(supported.begin(), supported.end(), key) != supported.end();
}

void mergeOpcodes(OpcodeMap& target, const OpcodeMap& source)
{
    for (const auto& [key, value] : source)
        target[key] = value;
}

Zone buildZone(const OpcodeMap& merged)
{
    Zone zone;
    zone.sourceSample = merged.count("sample") > 0 ? merged.at("sample") : juce::String{};
    zone.key = merged.count("key") > 0 ? parseIntOr(merged.at("key"), -1) : -1;
    zone.lowKey = merged.count("lokey") > 0 ? parseIntOr(merged.at("lokey"), 0) : 0;
    zone.highKey = merged.count("hikey") > 0 ? parseIntOr(merged.at("hikey"), 127) : 127;
    zone.pitchKeycenter = merged.count("pitch_keycenter") > 0 ? parseIntOr(merged.at("pitch_keycenter"), 60) : 60;
    zone.lowVelocity = merged.count("lovel") > 0 ? parseIntOr(merged.at("lovel"), 0) : 0;
    zone.highVelocity = merged.count("hivel") > 0 ? parseIntOr(merged.at("hivel"), 127) : 127;
    zone.transpose = merged.count("transpose") > 0 ? parseIntOr(merged.at("transpose"), 0) : 0;
    zone.tuneCents = merged.count("tune") > 0 ? parseIntOr(merged.at("tune"), 0) : 0;
    zone.offset = merged.count("offset") > 0 ? parseIntOr(merged.at("offset"), 0) : 0;
    zone.loopStart = merged.count("loop_start") > 0 ? parseIntOr(merged.at("loop_start"), 0) : 0;
    zone.loopEnd = merged.count("loop_end") > 0 ? parseIntOr(merged.at("loop_end"), 0) : 0;
    zone.loopMode = merged.count("loop_mode") > 0 ? merged.at("loop_mode") : "no_loop";
    zone.rrGroup = merged.count("seq_group") > 0 ? parseIntOr(merged.at("seq_group"), 0)
        : (merged.count("group") > 0 ? parseIntOr(merged.at("group"), 0) : 0);

    if (zone.key >= 0)
    {
        zone.lowKey = zone.key;
        zone.highKey = zone.key;

        if (merged.count("pitch_keycenter") == 0)
            zone.pitchKeycenter = zone.key;
    }

    return zone;
}
}

Program Importer::importFromFile(const juce::File& sfzFile) const
{
    Program program;

    PreprocessResult preprocessed;
    std::map<juce::String, juce::String> defines;
    std::set<juce::String> includeStack;
    preprocessFile(sfzFile, sfzFile.getParentDirectory(), defines, includeStack, preprocessed);

    program.diagnostics = preprocessed.diagnostics;

    enum class Header
    {
        control,
        global,
        master,
        group,
        region
    };

    Header currentHeader = Header::global;
    OpcodeMap controlOps;
    OpcodeMap globalOps;
    OpcodeMap masterOps;
    OpcodeMap groupOps;
    OpcodeMap regionOps;

    auto finalizeRegion = [&](const juce::String& filePath, const int line)
    {
        if (regionOps.empty())
            return;

        OpcodeMap merged;
        mergeOpcodes(merged, globalOps);
        mergeOpcodes(merged, masterOps);
        mergeOpcodes(merged, groupOps);
        mergeOpcodes(merged, regionOps);

        for (const auto& [key, value] : merged)
        {
            juce::ignoreUnused(value);
            if (!isSupportedOpcode(key))
            {
                addDiagnostic(program.diagnostics, DiagnosticSeverity::warning, filePath, line,
                    "Unsupported opcode ignored: " + key);
            }
        }

        if (merged.count("sample") == 0)
        {
            addDiagnostic(program.diagnostics, DiagnosticSeverity::warning, filePath, line,
                "Region missing sample opcode; region skipped.");
            regionOps.clear();
            return;
        }

        Zone zone = buildZone(merged);
        const auto defaultPath = controlOps.count("default_path") > 0 ? controlOps.at("default_path") : juce::String{};
        const auto rootDir = sfzFile.getParentDirectory();
        juce::File sampleFile(zone.sourceSample);

        if (!juce::File::isAbsolutePath(zone.sourceSample))
        {
            auto baseDir = rootDir;
            if (defaultPath.isNotEmpty())
                baseDir = baseDir.getChildFile(defaultPath);

            sampleFile = baseDir.getChildFile(zone.sourceSample);
        }

        if (!sampleFile.existsAsFile())
        {
            addDiagnostic(program.diagnostics, DiagnosticSeverity::error, filePath, line,
                "Missing sample file: " + sampleFile.getFullPathName());
            regionOps.clear();
            return;
        }

        zone.resolvedSamplePath = sampleFile.getFullPathName();

        const auto zoneIndex = static_cast<int>(program.zones.size());
        program.zones.push_back(zone);

        const auto existingGroup = std::find_if(program.groups.begin(), program.groups.end(),
            [&](const Group& group) { return group.rrGroup == zone.rrGroup; });

        if (existingGroup == program.groups.end())
        {
            Group newGroup;
            newGroup.rrGroup = zone.rrGroup;
            newGroup.zoneIndices.push_back(zoneIndex);
            program.groups.push_back(newGroup);
        }
        else
        {
            existingGroup->zoneIndices.push_back(zoneIndex);
        }

        regionOps.clear();
    };

    for (const auto& line : preprocessed.lines)
    {
        auto tokens = juce::StringArray::fromTokens(line.text, " \t", "");
        if (tokens.isEmpty())
            continue;

        for (int tokenIndex = 0; tokenIndex < tokens.size(); ++tokenIndex)
        {
            auto token = tokens[tokenIndex].trim();
            if (token.isEmpty())
                continue;

            if (token.startsWith("<") && token.endsWith(">"))
            {
                if (currentHeader == Header::region)
                    finalizeRegion(line.filePath, line.line);

                if (token == "<control>")
                    currentHeader = Header::control;
                else if (token == "<global>")
                    currentHeader = Header::global;
                else if (token == "<master>")
                {
                    currentHeader = Header::master;
                    masterOps.clear();
                }
                else if (token == "<group>")
                {
                    currentHeader = Header::group;
                    groupOps.clear();
                }
                else if (token == "<region>")
                {
                    currentHeader = Header::region;
                    regionOps.clear();
                }

                continue;
            }

            if (!token.containsChar('='))
                continue;

            const auto key = token.upToFirstOccurrenceOf("=", false, false).trim();
            const auto value = token.fromFirstOccurrenceOf("=", false, false).trim();

            switch (currentHeader)
            {
                case Header::control: controlOps[key] = value; break;
                case Header::global: globalOps[key] = value; break;
                case Header::master: masterOps[key] = value; break;
                case Header::group: groupOps[key] = value; break;
                case Header::region: regionOps[key] = value; break;
            }
        }
    }

    if (currentHeader == Header::region)
        finalizeRegion(sfzFile.getFullPathName(), 0);

    return program;
}
}
