#include "BinaryMultisampleImporters.h"
#include "ImportCancellation.h"

#include <juce_audio_formats/juce_audio_formats.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <limits>
#include <memory>

namespace audiocity::engine
{
using xml_multi::Diagnostic;
using xml_multi::addDiagnostic;
using xml_multi::buildGenericSummary;
using xml_multi::loadSampleAssetFromFile;
using xml_multi::parseMidiNoteText;
using xml_multi::resolveSamplePath;

namespace
{
[[nodiscard]] juce::String trimNullTerminated(const char* data, int maxLen)
{
    int len = 0;
    while (len < maxLen && data[len] != '\0') ++len;
    return juce::String::fromUTF8(data, len).trim();
}

[[nodiscard]] uint32_t readBE32(const uint8_t* p)
{
    return (static_cast<uint32_t>(p[0]) << 24) | (static_cast<uint32_t>(p[1]) << 16)
         | (static_cast<uint32_t>(p[2]) << 8)  |  static_cast<uint32_t>(p[3]);
}
[[nodiscard]] uint32_t readLE32(const uint8_t* p)
{
    return  static_cast<uint32_t>(p[0])        | (static_cast<uint32_t>(p[1]) << 8)
         | (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
}
[[nodiscard]] uint16_t readLE16(const uint8_t* p)
{
    return static_cast<uint16_t>(p[0] | (p[1] << 8));
}
} // namespace

// =====================================================================================
// Expert Sleepers disting EX (.dexpreset) - simple text key=value
// =====================================================================================
//   The disting EX writes preset files with "Key: Value" lines and a sample-list block.
//   We accept either:
//     - Lines beginning with "Sample N file: <relative>" and matching key/root metadata
//     - A simple "[Sample N]" INI section with file=, root=, lokey=, hikey=, lovel=, hivel=
//   plus an optional global Name=. Real EX presets are short text; values are forgiving.
namespace distingex
{
namespace
{
struct PendingZone
{
    juce::String fileRef;
    int rootNote = 60;
    int lowKey = 0;
    int highKey = 127;
    int lowVel = 1;
    int highVel = 127;
    float gainDb = 0.0f;
    float pan = 0.0f;
    float tuneCents = 0.0f;
    bool hasValues = false;
};

void flushZone(PendingZone& zone, juce::AudioFormatManager& fm, const juce::File& folder,
               ImportResult& result, int groupIndex)
{
    if (!zone.hasValues || zone.fileRef.isEmpty())
        return;

    const auto resolved = resolveSamplePath(zone.fileRef, folder);
    const int idx = loadSampleAssetFromFile(fm, resolved, zone.rootNote, result.program,
                                            result.sampleDataByAsset, result.diagnostics, zone.fileRef);
    if (idx < 0) { zone = {}; return; }

    Zone z;
    z.sampleAssetIndex = idx;
    z.groupIndex = groupIndex;
    z.rootMidiNote = zone.rootNote;
    z.keyRange = MidiRange::fromUnordered(zone.lowKey, zone.highKey);
    z.velocityRange = VelocityRange::fromUnordered(zone.lowVel, zone.highVel);
    z.gainDb = zone.gainDb;
    z.pan = juce::jlimit(-1.0f, 1.0f, zone.pan);
    z.tuneCents = zone.tuneCents;
    result.program.zones.push_back(z);
    zone = {};
}
} // namespace

ImportResult importFile(const juce::File& file)
{
    ImportResult result;
    if (isImportCancellationRequested())
        return result;
    if (!file.existsAsFile())
    {
        addDiagnostic(result.diagnostics, Diagnostic::Severity::error,
                      "disting EX preset not found: " + file.getFullPathName());
        return result;
    }

    juce::StringArray lines;
    juce::MemoryBlock textBytes;
    if (!readFileInCancellableChunks(file, textBytes))
        return result;
    lines.addLines(juce::String::fromUTF8(static_cast<const char*>(textBytes.getData()),
                                          static_cast<int>(textBytes.getSize())));
    if (lines.isEmpty())
    {
        addDiagnostic(result.diagnostics, Diagnostic::Severity::error,
                      "disting EX preset is empty");
        return result;
    }

    juce::AudioFormatManager fm;
    fm.registerBasicFormats();
    const auto folder = file.getParentDirectory();
    result.program.name = file.getFileNameWithoutExtension().toStdString();

    Group g;
    g.name = "Samples";
    result.program.groups.push_back(g);
    const int groupIndex = 0;

    PendingZone current;

    auto setKv = [&](const juce::String& k, const juce::String& v)
    {
        const auto key = k.toLowerCase().trim();
        const auto val = v.trim();
        if (key == "name") result.program.name = val.toStdString();
        else if (key == "file" || key == "filename" || key == "sample" || key.endsWith("file"))
        { current.fileRef = val; current.hasValues = true; }
        else if (key == "root" || key == "rootkey" || key == "rootnote")
        { current.rootNote = parseMidiNoteText(val, 60); current.hasValues = true; }
        else if (key == "lowkey" || key == "lokey" || key == "lownote")
        { current.lowKey = parseMidiNoteText(val, 0); current.hasValues = true; }
        else if (key == "highkey" || key == "hikey" || key == "highnote")
        { current.highKey = parseMidiNoteText(val, 127); current.hasValues = true; }
        else if (key == "lowvel" || key == "lovel")
        { current.lowVel = juce::jlimit(0, 127, val.getIntValue()); current.hasValues = true; }
        else if (key == "highvel" || key == "hivel")
        { current.highVel = juce::jlimit(0, 127, val.getIntValue()); current.hasValues = true; }
        else if (key == "gain" || key == "gaindb" || key == "level")
        { current.gainDb = (float) val.getDoubleValue(); current.hasValues = true; }
        else if (key == "pan")
        { current.pan = (float) val.getDoubleValue(); current.hasValues = true; }
        else if (key == "tune" || key == "tunecents" || key == "fine")
        { current.tuneCents += (float) val.getDoubleValue(); current.hasValues = true; }
        else if (key == "coarse" || key == "transpose")
        { current.tuneCents += 100.0f * (float) val.getDoubleValue(); current.hasValues = true; }
    };

    for (auto rawLine : lines)
    {
        if (isImportCancellationRequested())
            return result;

        auto line = rawLine.trim();
        if (line.isEmpty() || line.startsWithChar('#') || line.startsWithChar(';'))
            continue;

        if (line.startsWithChar('[') && line.endsWithChar(']'))
        {
            flushZone(current, fm, folder, result, groupIndex);
            continue;
        }

        // "Sample 3 file: foo.wav"
        if (line.startsWithIgnoreCase("Sample "))
        {
            const auto colon = line.indexOfChar(':');
            const auto eq = line.indexOfChar('=');
            const auto sep = (colon >= 0 && (eq < 0 || colon < eq)) ? colon : eq;
            if (sep < 0) continue;
            const auto head = line.substring(0, sep);
            const auto val = line.substring(sep + 1);
            // Detect the trailing keyword in head (e.g. "Sample 3 file")
            const auto tokens = juce::StringArray::fromTokens(head, " \t", "");
            if (tokens.size() >= 3)
            {
                static int lastIndex = -1;
                const auto sampleIndex = tokens[1].getIntValue();
                if (sampleIndex != lastIndex)
                {
                    flushZone(current, fm, folder, result, groupIndex);
                    lastIndex = sampleIndex;
                }
                setKv(tokens[2], val);
                continue;
            }
        }

        const auto eq = line.indexOfChar('=');
        const auto colon = line.indexOfChar(':');
        const auto sep = (colon >= 0 && (eq < 0 || colon < eq)) ? colon : eq;
        if (sep < 0) continue;
        setKv(line.substring(0, sep), line.substring(sep + 1));
    }
    flushZone(current, fm, folder, result, groupIndex);

    if (result.program.zones.empty())
        addDiagnostic(result.diagnostics, Diagnostic::Severity::error,
                      "disting EX preset produced no playable zones");
    return result;
}

juce::String buildImportSummary(const ImportResult& r, const bool imported)
{
    return buildGenericSummary("disting EX preset", r.program, r.diagnostics, imported);
}
} // namespace distingex

// =====================================================================================
// Korg KMP / KSF
// =====================================================================================
//   KMP layout (chunked, 4-char ID + 4-byte big-endian size):
//     "MSP1" 18 bytes:  16 byte name | 1 byte numSamples | 1 byte (used)
//     "RLP1" 18 * numSamples bytes per sample:
//        1 byte topKey (highest note covered by this sample)
//        1 byte originalKey (root pitch)
//        1 byte tune (signed cents)
//        1 byte level (signed dB)
//        1 byte pan (signed -64..63)
//        1 byte reserved
//        12 byte filename (8.3 like "msf001.KSF")
//     "NAME" optional pretty name
//   We honour the KMP key-split semantic: each entry's keyRange is (prevTopKey+1 .. topKey).
//   Samples are loaded with JUCE; a sibling .wav (same stem) is also accepted because some
//   tools convert .ksf -> .wav next to the .kmp.
namespace korgkmp
{
namespace
{
constexpr int kMaxKmpSampleEntries = 128;
}

ImportResult importFile(const juce::File& file)
{
    ImportResult result;
    if (!file.existsAsFile())
    {
        addDiagnostic(result.diagnostics, Diagnostic::Severity::error,
                      "Korg KMP not found: " + file.getFullPathName());
        return result;
    }

    juce::MemoryBlock data;
    if (!readFileInCancellableChunks(file, data) || data.getSize() < 16)
    {
        addDiagnostic(result.diagnostics, Diagnostic::Severity::error,
                      "Korg KMP read failed or file too short");
        return result;
    }

    const auto* bytes = static_cast<const uint8_t*>(data.getData());
    const auto totalSize = static_cast<size_t>(data.getSize());

    juce::AudioFormatManager fm;
    fm.registerBasicFormats();
    const auto folder = file.getParentDirectory();
    result.program.name = file.getFileNameWithoutExtension().toStdString();

    Group g; g.name = "KMP";
    result.program.groups.push_back(g);
    const int groupIndex = 0;

    int numSamples = 0;
    juce::String multisampleName;
    struct KmpEntry
    {
        int topKey = 127;
        int rootKey = 60;
        int tuneCents = 0;
        int levelDb = 0;
        int pan = 0;
        juce::String filename;
    };
    std::vector<KmpEntry> entries;

    size_t pos = 0;
    while (pos + 8 <= totalSize)
    {
        if (isImportCancellationRequested())
            return result;

        char id[5] = {};
        std::memcpy(id, bytes + pos, 4);
        const auto chunkSize = readBE32(bytes + pos + 4);
        pos += 8;
        if (chunkSize > totalSize - pos)
        {
            addDiagnostic(result.diagnostics, Diagnostic::Severity::warning,
                          "Truncated KMP chunk: " + juce::String(id));
            break;
        }
        const auto* p = bytes + pos;

        if (std::strncmp(id, "MSP1", 4) == 0 && chunkSize >= 18)
        {
            multisampleName = trimNullTerminated(reinterpret_cast<const char*>(p), 16);
            numSamples = juce::jlimit(0, 128, static_cast<int>(p[16]));
        }
        else if (std::strncmp(id, "NAME", 4) == 0 && chunkSize > 0)
        {
            const auto n = trimNullTerminated(reinterpret_cast<const char*>(p), static_cast<int>(chunkSize));
            if (n.isNotEmpty()) result.program.name = n.toStdString();
        }
        else if (std::strncmp(id, "RLP1", 4) == 0)
        {
            const int declaredCount = static_cast<int>(chunkSize / 18);
            const int countLimit = numSamples > 0 ? numSamples : kMaxKmpSampleEntries;
            const int count = juce::jmin(declaredCount, countLimit);
            if (declaredCount > count)
                addDiagnostic(result.diagnostics, Diagnostic::Severity::warning,
                              "KMP RLP1 entry count exceeds declared sample count - extra entries ignored");
            for (int i = 0; i < count; ++i)
            {
                if (isImportCancellationRequested())
                    return result;

                const auto* e = p + i * 18;
                KmpEntry k;
                k.topKey   = juce::jlimit(0, 127, static_cast<int>(e[0]));
                k.rootKey  = juce::jlimit(0, 127, static_cast<int>(e[1]));
                k.tuneCents = static_cast<int>(static_cast<int8_t>(e[2]));
                k.levelDb  = static_cast<int>(static_cast<int8_t>(e[3]));
                k.pan      = static_cast<int>(static_cast<int8_t>(e[4]));
                k.filename = trimNullTerminated(reinterpret_cast<const char*>(e + 6), 12);
                entries.push_back(k);
            }
        }
        pos += chunkSize;
    }

    if (multisampleName.isNotEmpty()) result.program.name = multisampleName.toStdString();
    if (numSamples == 0 && !entries.empty()) numSamples = static_cast<int>(entries.size());
    if (entries.empty())
    {
        addDiagnostic(result.diagnostics, Diagnostic::Severity::error,
                      "Korg KMP has no RLP1 sample entries");
        return result;
    }

    int prevTop = -1;
    for (const auto& e : entries)
    {
        if (isImportCancellationRequested())
            return result;

        if (e.filename.isEmpty())
        {
            addDiagnostic(result.diagnostics, Diagnostic::Severity::warning,
                          "KMP entry missing filename - skipped");
            continue;
        }
        // Try as-is, then with .wav swapped (some tools deliver WAV alongside .KMP).
        auto resolved = resolveSamplePath(e.filename, folder);
        if (!resolved.existsAsFile())
        {
            const auto stem = juce::File(e.filename).getFileNameWithoutExtension();
            resolved = resolveSamplePath(stem + ".wav", folder);
            if (!resolved.existsAsFile())
                resolved = resolveSamplePath(stem + ".aif", folder);
        }
        const int assetIndex = loadSampleAssetFromFile(fm, resolved, e.rootKey, result.program,
                                                       result.sampleDataByAsset, result.diagnostics, e.filename);
        if (assetIndex < 0) { prevTop = e.topKey; continue; }

        Zone z;
        z.sampleAssetIndex = assetIndex;
        z.groupIndex = groupIndex;
        z.rootMidiNote = e.rootKey;
        const int low = juce::jlimit(0, 127, prevTop + 1);
        const int high = juce::jlimit(low, 127, e.topKey);
        z.keyRange = MidiRange::fromUnordered(low, high);
        z.tuneCents = static_cast<float>(e.tuneCents);
        z.gainDb = static_cast<float>(e.levelDb);
        z.pan = juce::jlimit(-1.0f, 1.0f, static_cast<float>(e.pan) / 64.0f);
        result.program.zones.push_back(z);
        prevTop = e.topKey;
    }

    if (result.program.zones.empty())
        addDiagnostic(result.diagnostics, Diagnostic::Severity::error,
                      "Korg KMP produced no playable zones");
    return result;
}

juce::String buildImportSummary(const ImportResult& r, const bool imported)
{
    return buildGenericSummary("Korg KMP", r.program, r.diagnostics, imported);
}
} // namespace korgkmp

// =====================================================================================
// Logic EXS24 (.exs)
// =====================================================================================
//   Chunk-based binary; each chunk:
//     int32 magic ('SOBT' / 'TBOS' depending on endianness)  - we just key off the size+type
//     uint32 size (excluding header)
//     uint32 typeCode  (0x01000101 = header, 0x01000102 = zone, 0x01000103 = group,
//                       0x01000104 = sample, 0x01000105 = parameter)
//     uint32 unused
//     uint32 unused
//     char[64] name
//     ... payload of (size - 84) bytes
//   We support little-endian files (the common case on modern macOS). Byte offsets in the
//   payload are based on the public EXS24 reverse-engineering write-ups.
namespace exs24
{
namespace
{
constexpr size_t kExsHeaderSize = 84;
constexpr uint32_t kTypeHeader  = 0x01000101u;
constexpr uint32_t kTypeZone    = 0x01000102u;
constexpr uint32_t kTypeGroup   = 0x01000103u;
constexpr uint32_t kTypeSample  = 0x01000104u;

struct ExsZone
{
    juce::String name;
    int rootNote = 60;
    int lowKey = 0;
    int highKey = 127;
    int lowVel = 1;
    int highVel = 127;
    int sampleStart = 0;
    int sampleEnd = -1;
    int loopStart = -1;
    int loopEnd = -1;
    bool loopOn = false;
    int sampleIndex = -1;
    float pan = 0.0f;
    int volumeOffset = 0;
    int coarseTune = 0;
    int fineTune = 0;
};

struct ExsSample
{
    juce::String name;
    juce::String fileName;
    juce::String filePath;
};

[[nodiscard]] juce::String readFixedString(const uint8_t* p, int len)
{
    return trimNullTerminated(reinterpret_cast<const char*>(p), len);
}
} // namespace

ImportResult importFile(const juce::File& file)
{
    ImportResult result;
    if (isImportCancellationRequested())
        return result;

    if (isImportCancellationRequested())
        return result;
    if (!file.existsAsFile())
    {
        addDiagnostic(result.diagnostics, Diagnostic::Severity::error,
                      "EXS24 instrument not found: " + file.getFullPathName());
        return result;
    }

    juce::MemoryBlock data;
    if (!readFileInCancellableChunks(file, data) || data.getSize() < kExsHeaderSize)
    {
        addDiagnostic(result.diagnostics, Diagnostic::Severity::error,
                      "EXS24 read failed or file too short");
        return result;
    }

    const auto* bytes = static_cast<const uint8_t*>(data.getData());
    const size_t total = data.getSize();

    juce::AudioFormatManager fm;
    fm.registerBasicFormats();
    const auto folder = file.getParentDirectory();
    result.program.name = file.getFileNameWithoutExtension().toStdString();

    Group g; g.name = "EXS24";
    result.program.groups.push_back(g);
    const int groupIndex = 0;

    std::vector<ExsZone> zones;
    std::vector<ExsSample> samples;

    size_t pos = 0;
    while (pos + kExsHeaderSize <= total)
    {
        if (isImportCancellationRequested())
            return result;

        const uint32_t size = readLE32(bytes + pos + 4);
        const uint32_t type = readLE32(bytes + pos + 8);
        const auto chunkPayloadSize = static_cast<size_t>(size);
        const auto chunkName = readFixedString(bytes + pos + 20, 64);
        if (chunkPayloadSize > total - pos - kExsHeaderSize)
        {
            addDiagnostic(result.diagnostics, Diagnostic::Severity::warning,
                          "Truncated EXS24 chunk: " + chunkName);
            break;
        }

        const auto fullChunk = kExsHeaderSize + chunkPayloadSize;
        const auto* payload = bytes + pos + kExsHeaderSize;

        switch (type & 0x0FFFFFFFu) // tolerant of high bits used as flags
        {
            case kTypeHeader & 0x0FFFFFFFu:
                if (chunkName.isNotEmpty()) result.program.name = chunkName.toStdString();
                break;

            case kTypeZone & 0x0FFFFFFFu:
            {
                if (chunkPayloadSize >= 64)
                {
                    ExsZone z;
                    z.name = chunkName;
                    // Public reverse-engineering: offsets within zone payload
                    z.rootNote   = juce::jlimit(0, 127, static_cast<int>(payload[5]));
                    z.lowKey     = juce::jlimit(0, 127, static_cast<int>(payload[6]));
                    z.highKey    = juce::jlimit(0, 127, static_cast<int>(payload[7]));
                    z.lowVel     = juce::jlimit(0, 127, static_cast<int>(payload[10]));
                    z.highVel    = juce::jlimit(0, 127, static_cast<int>(payload[11]));
                    z.sampleStart = static_cast<int>(readLE32(payload + 12));
                    z.sampleEnd   = static_cast<int>(readLE32(payload + 16));
                    z.loopStart   = static_cast<int>(readLE32(payload + 20));
                    z.loopEnd     = static_cast<int>(readLE32(payload + 24));
                    z.loopOn      = (payload[1] & 0x01) != 0;
                    z.coarseTune  = static_cast<int>(static_cast<int8_t>(payload[164 < (int) chunkPayloadSize ? 164 : 0]));
                    z.fineTune    = static_cast<int>(static_cast<int8_t>(payload[165 < (int) chunkPayloadSize ? 165 : 0]));
                    z.pan         = juce::jlimit(-1.0f, 1.0f,
                                                  static_cast<float>(static_cast<int8_t>(payload[166 < (int) chunkPayloadSize ? 166 : 0])) / 100.0f);
                    z.volumeOffset = static_cast<int>(static_cast<int8_t>(payload[167 < (int) chunkPayloadSize ? 167 : 0]));
                    if (chunkPayloadSize >= 184)
                        z.sampleIndex = static_cast<int>(readLE32(payload + 180));
                    zones.push_back(z);
                }
                break;
            }

            case kTypeSample & 0x0FFFFFFFu:
            {
                ExsSample s;
                s.name = chunkName;
                if (chunkPayloadSize >= 64)
                    s.fileName = readFixedString(payload, 64);
                if (s.fileName.isEmpty()) s.fileName = chunkName;
                if (chunkPayloadSize >= 256 + 64)
                    s.filePath = readFixedString(payload + 256, 256);
                samples.push_back(s);
                break;
            }

            case kTypeGroup & 0x0FFFFFFFu:
            default:
                break;
        }

        pos += fullChunk;
    }

    if (zones.empty())
    {
        addDiagnostic(result.diagnostics, Diagnostic::Severity::error,
                      "EXS24 file has no zone chunks");
        return result;
    }

    // Build playable zones, mapping sampleIndex -> sample asset.
    std::vector<int> sampleToAsset(samples.size(), -1);

    auto resolveSampleFile = [&](const ExsSample& s) -> juce::File
    {
        if (s.filePath.isNotEmpty())
        {
            const auto absolute = resolveSamplePath(s.filePath, folder);
            if (absolute.existsAsFile()) return absolute;
        }
        if (s.fileName.isEmpty()) return {};
        // Logic stores samples in a sibling "Sampler Files" folder by convention.
        const auto candidates = std::array<juce::File, 4> {
            folder.getChildFile(s.fileName),
            folder.getChildFile("Samples").getChildFile(s.fileName),
            folder.getChildFile("Sampler Files").getChildFile(s.fileName),
            folder.getParentDirectory().getChildFile("Samples").getChildFile(s.fileName)
        };
        for (const auto& c : candidates)
            if (c.existsAsFile()) return c;
        return resolveSamplePath(s.fileName, folder);
    };

    for (const auto& z : zones)
    {
        if (isImportCancellationRequested())
            return result;

        if (z.sampleIndex < 0 || static_cast<size_t>(z.sampleIndex) >= samples.size())
        {
            addDiagnostic(result.diagnostics, Diagnostic::Severity::warning,
                          "EXS24 zone references missing sample index " + juce::String(z.sampleIndex));
            continue;
        }

        if (sampleToAsset[static_cast<size_t>(z.sampleIndex)] < 0)
        {
            const auto& s = samples[static_cast<size_t>(z.sampleIndex)];
            const auto resolved = resolveSampleFile(s);
            const int assetIndex = loadSampleAssetFromFile(fm, resolved, z.rootNote, result.program,
                                                           result.sampleDataByAsset, result.diagnostics,
                                                           s.fileName.isEmpty() ? s.name : s.fileName);
            sampleToAsset[static_cast<size_t>(z.sampleIndex)] = assetIndex;
        }
        const int assetIndex = sampleToAsset[static_cast<size_t>(z.sampleIndex)];
        if (assetIndex < 0) continue;

        Zone out;
        out.sampleAssetIndex = assetIndex;
        out.groupIndex = groupIndex;
        out.rootMidiNote = z.rootNote;
        out.keyRange = MidiRange::fromUnordered(z.lowKey, z.highKey);
        out.velocityRange = VelocityRange::fromUnordered(z.lowVel, z.highVel);
        out.sampleStart = juce::jmax(0, z.sampleStart);
        if (z.sampleEnd > z.sampleStart) out.sampleEndExclusive = z.sampleEnd;
        if (z.loopOn && z.loopEnd > z.loopStart && z.loopStart >= 0)
        {
            out.loopStart = z.loopStart;
            out.loopEndExclusive = z.loopEnd;
            out.loopMode = ZoneLoopMode::continuous;
        }
        out.pan = z.pan;
        out.tuneCents = 100.0f * static_cast<float>(z.coarseTune) + static_cast<float>(z.fineTune);
        if (z.volumeOffset != 0) out.gainDb = static_cast<float>(z.volumeOffset);
        result.program.zones.push_back(out);
    }

    if (result.program.zones.empty())
        addDiagnostic(result.diagnostics, Diagnostic::Severity::error,
                      "EXS24 file produced no playable zones");
    return result;
}

juce::String buildImportSummary(const ImportResult& r, const bool imported)
{
    return buildGenericSummary("Logic EXS24 instrument", r.program, r.diagnostics, imported);
}
} // namespace exs24

// =====================================================================================
// Propellerhead NN-XT (.sxt)
// =====================================================================================
//   The .sxt format is a Reason proprietary binary container. It is partially documented
//   by community reverse engineering (PowerPC big-endian "JIFF"-like chunks). Audiocity
//   does not yet have a verified parser, so this importer surfaces an actionable
//   diagnostic instead of guessing. Once a verified spec or fixture is available, the
//   real parser plugs in here.
namespace nnxt
{
ImportResult importFile(const juce::File& file)
{
    ImportResult result;
    if (isImportCancellationRequested())
        return result;

    if (!file.existsAsFile())
    {
        addDiagnostic(result.diagnostics, Diagnostic::Severity::error,
                      "NN-XT preset not found: " + file.getFullPathName());
        return result;
    }

    juce::MemoryBlock head;
    {
        juce::FileInputStream fis(file);
        if (!fis.openedOk())
        {
            addDiagnostic(result.diagnostics, Diagnostic::Severity::error,
                          "Could not open NN-XT preset");
            return result;
        }
        head.setSize(juce::jmin<juce::int64>(64, file.getSize()), true);
        fis.read(head.getData(), static_cast<int>(head.getSize()));
    }

    const auto headTxt = juce::String::fromUTF8(static_cast<const char*>(head.getData()),
                                                static_cast<int>(head.getSize()));
    if (!headTxt.contains("CAT ") && !headTxt.contains("PROP") && !headTxt.contains("NN-XT"))
    {
        addDiagnostic(result.diagnostics, Diagnostic::Severity::error,
                      "File does not look like a Propellerhead NN-XT (.sxt) preset");
        return result;
    }

    addDiagnostic(result.diagnostics, Diagnostic::Severity::error,
                  "NN-XT (.sxt) import is recognised but not yet supported. "
                  "Re-export the patch to SFZ or DecentSampler from Reason and re-import.");
    return result;
}

juce::String buildImportSummary(const ImportResult& r, const bool imported)
{
    return buildGenericSummary("Reason NN-XT instrument", r.program, r.diagnostics, imported);
}
} // namespace nnxt

} // namespace audiocity::engine
