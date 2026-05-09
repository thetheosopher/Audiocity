#include "Sf2Importer.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <limits>
#include <map>
#include <optional>

namespace audiocity::engine::sf2
{
bool ImportResult::hasErrors() const noexcept
{
    for (const auto& d : diagnostics)
        if (d.severity == ImportDiagnostic::Severity::error)
            return true;
    return false;
}

namespace
{
void addDiagnostic(std::vector<ImportDiagnostic>& diagnostics,
                   const ImportDiagnostic::Severity severity,
                   const juce::String& message)
{
    diagnostics.push_back({ severity, message.toStdString() });
}

constexpr juce::uint32 fourcc(const char a, const char b, const char c, const char d) noexcept
{
    return static_cast<juce::uint32>(static_cast<juce::uint8>(a))
         | (static_cast<juce::uint32>(static_cast<juce::uint8>(b)) << 8)
         | (static_cast<juce::uint32>(static_cast<juce::uint8>(c)) << 16)
         | (static_cast<juce::uint32>(static_cast<juce::uint8>(d)) << 24);
}

[[nodiscard]] juce::uint16 readU16LE(const juce::uint8* p) noexcept
{
    return static_cast<juce::uint16>(p[0]) | (static_cast<juce::uint16>(p[1]) << 8);
}

[[nodiscard]] juce::int16 readS16LE(const juce::uint8* p) noexcept
{
    return static_cast<juce::int16>(readU16LE(p));
}

[[nodiscard]] juce::uint32 readU32LE(const juce::uint8* p) noexcept
{
    return static_cast<juce::uint32>(p[0])
         | (static_cast<juce::uint32>(p[1]) << 8)
         | (static_cast<juce::uint32>(p[2]) << 16)
         | (static_cast<juce::uint32>(p[3]) << 24);
}

// Generator IDs from the SoundFont 2.04 spec.
enum GenOp : juce::uint16
{
    StartAddrsOffset = 0,
    EndAddrsOffset = 1,
    StartLoopAddrsOffset = 2,
    EndLoopAddrsOffset = 3,
    StartAddrsCoarseOffset = 4,
    Pan = 17,
    StartLoopAddrsCoarseOffset = 45,
    EndAddrsCoarseOffset = 12,
    EndLoopAddrsCoarseOffset = 50,
    InstrumentGen = 41,
    KeyRange = 43,
    VelRange = 44,
    InitialAttenuation = 48,
    CoarseTune = 51,
    FineTune = 52,
    SampleID = 53,
    SampleModes = 54,
    OverridingRootKey = 58
};

#pragma pack(push, 1)
struct PhdrRecord { char name[20]; juce::uint16 preset; juce::uint16 bank; juce::uint16 bagIdx; juce::uint32 library; juce::uint32 genre; juce::uint32 morph; };
struct BagRecord  { juce::uint16 genIdx; juce::uint16 modIdx; };
struct GenRecord  { juce::uint16 genOper; juce::uint16 amount; };
struct InstRecord { char name[20]; juce::uint16 bagIdx; };
struct ShdrRecord { char name[20]; juce::uint32 start; juce::uint32 end; juce::uint32 startLoop; juce::uint32 endLoop;
                    juce::uint32 sampleRate; juce::uint8 originalPitch; juce::int8 pitchCorrection;
                    juce::uint16 sampleLink; juce::uint16 sampleType; };
#pragma pack(pop)

static_assert(sizeof(PhdrRecord) == 38, "phdr record size");
static_assert(sizeof(BagRecord) == 4, "bag record size");
static_assert(sizeof(GenRecord) == 4, "gen record size");
static_assert(sizeof(InstRecord) == 22, "inst record size");
static_assert(sizeof(ShdrRecord) == 46, "shdr record size");

struct Chunk
{
    juce::uint32 id = 0;
    const juce::uint8* data = nullptr;
    size_t size = 0;
};

struct Sf2File
{
    const juce::uint8* smplData = nullptr; size_t smplBytes = 0;
    const juce::uint8* sm24Data = nullptr; size_t sm24Bytes = 0;
    const PhdrRecord* phdr = nullptr; size_t phdrCount = 0;
    const BagRecord* pbag = nullptr;   size_t pbagCount = 0;
    const GenRecord* pgen = nullptr;   size_t pgenCount = 0;
    const InstRecord* inst = nullptr;  size_t instCount = 0;
    const BagRecord* ibag = nullptr;   size_t ibagCount = 0;
    const GenRecord* igen = nullptr;   size_t igenCount = 0;
    const ShdrRecord* shdr = nullptr;  size_t shdrCount = 0;
};

[[nodiscard]] juce::String fixedString(const char* p, const size_t maxLen)
{
    size_t n = 0;
    while (n < maxLen && p[n] != 0) ++n;
    return juce::String::fromUTF8(p, static_cast<int>(n)).trim();
}

bool parseSf2Container(const juce::uint8* base, const size_t size, Sf2File& file,
                       std::vector<ImportDiagnostic>& diagnostics)
{
    if (size < 12) { addDiagnostic(diagnostics, ImportDiagnostic::Severity::error, "SF2 file too small"); return false; }
    if (readU32LE(base) != fourcc('R','I','F','F'))
    { addDiagnostic(diagnostics, ImportDiagnostic::Severity::error, "Not a RIFF file"); return false; }
    const auto riffSize = readU32LE(base + 4);
    if (riffSize + 8 > size)
    { addDiagnostic(diagnostics, ImportDiagnostic::Severity::error, "Truncated RIFF size"); return false; }
    if (readU32LE(base + 8) != fourcc('s','f','b','k'))
    { addDiagnostic(diagnostics, ImportDiagnostic::Severity::error, "Not a SoundFont 2 (sfbk) file"); return false; }

    size_t cursor = 12;
    const size_t end = static_cast<size_t>(riffSize) + 8;
    while (cursor + 8 <= end)
    {
        const auto chunkId = readU32LE(base + cursor);
        const auto chunkSize = readU32LE(base + cursor + 4);
        cursor += 8;
        if (cursor + chunkSize > end)
        { addDiagnostic(diagnostics, ImportDiagnostic::Severity::error, "Truncated chunk"); return false; }
        const auto chunkPayload = base + cursor;

        if (chunkId == fourcc('L','I','S','T'))
        {
            const auto listType = readU32LE(chunkPayload);
            size_t inner = 4;
            const size_t innerEnd = chunkSize;
            while (inner + 8 <= innerEnd)
            {
                const auto subId = readU32LE(chunkPayload + inner);
                const auto subSize = readU32LE(chunkPayload + inner + 4);
                inner += 8;
                if (inner + subSize > innerEnd)
                { addDiagnostic(diagnostics, ImportDiagnostic::Severity::error, "Truncated sub-chunk"); return false; }
                const auto subPayload = chunkPayload + inner;

                if (listType == fourcc('s','d','t','a'))
                {
                    if (subId == fourcc('s','m','p','l'))
                    {
                        file.smplData = subPayload;
                        file.smplBytes = subSize;
                    }
                    else if (subId == fourcc('s','m','2','4'))
                    {
                        file.sm24Data = subPayload;
                        file.sm24Bytes = subSize;
                    }
                }
                else if (listType == fourcc('p','d','t','a'))
                {
                    if (subId == fourcc('p','h','d','r')) { file.phdr = reinterpret_cast<const PhdrRecord*>(subPayload); file.phdrCount = subSize / sizeof(PhdrRecord); }
                    else if (subId == fourcc('p','b','a','g')) { file.pbag = reinterpret_cast<const BagRecord*>(subPayload); file.pbagCount = subSize / sizeof(BagRecord); }
                    else if (subId == fourcc('p','g','e','n')) { file.pgen = reinterpret_cast<const GenRecord*>(subPayload); file.pgenCount = subSize / sizeof(GenRecord); }
                    else if (subId == fourcc('i','n','s','t')) { file.inst = reinterpret_cast<const InstRecord*>(subPayload); file.instCount = subSize / sizeof(InstRecord); }
                    else if (subId == fourcc('i','b','a','g')) { file.ibag = reinterpret_cast<const BagRecord*>(subPayload); file.ibagCount = subSize / sizeof(BagRecord); }
                    else if (subId == fourcc('i','g','e','n')) { file.igen = reinterpret_cast<const GenRecord*>(subPayload); file.igenCount = subSize / sizeof(GenRecord); }
                    else if (subId == fourcc('s','h','d','r')) { file.shdr = reinterpret_cast<const ShdrRecord*>(subPayload); file.shdrCount = subSize / sizeof(ShdrRecord); }
                }

                inner += subSize;
                if (subSize & 1u) ++inner; // pad
            }
        }

        cursor += chunkSize;
        if (chunkSize & 1u) ++cursor; // pad
    }

    if (file.phdr == nullptr || file.pbag == nullptr || file.pgen == nullptr
        || file.inst == nullptr || file.ibag == nullptr || file.igen == nullptr
        || file.shdr == nullptr || file.smplData == nullptr)
    {
        addDiagnostic(diagnostics, ImportDiagnostic::Severity::error,
                      "SF2 file missing required pdta or sdta chunks");
        return false;
    }
    return true;
}

struct GenSet
{
    std::map<juce::uint16, juce::uint16> values; // raw 16-bit amount, interpretation depends on gen op
    bool keyRangeSet = false; int keyLo = 0, keyHi = 127;
    bool velRangeSet = false; int velLo = 0, velHi = 127;
    int sampleId = -1;
    int instId = -1;
    bool hasSampleId = false;
    bool hasInstId = false;

    void apply(const GenRecord& gen)
    {
        const auto op = gen.genOper;
        if (op == KeyRange)
        {
            keyRangeSet = true;
            keyLo = static_cast<int>(gen.amount & 0xFFu);
            keyHi = static_cast<int>((gen.amount >> 8) & 0xFFu);
        }
        else if (op == VelRange)
        {
            velRangeSet = true;
            velLo = static_cast<int>(gen.amount & 0xFFu);
            velHi = static_cast<int>((gen.amount >> 8) & 0xFFu);
        }
        else if (op == SampleID)
        {
            sampleId = static_cast<int>(gen.amount);
            hasSampleId = true;
        }
        else if (op == InstrumentGen)
        {
            instId = static_cast<int>(gen.amount);
            hasInstId = true;
        }
        else
        {
            values[op] = gen.amount;
        }
    }

    [[nodiscard]] juce::int16 signedValue(const juce::uint16 op, const juce::int16 fallback) const
    {
        const auto it = values.find(op);
        if (it == values.end()) return fallback;
        return static_cast<juce::int16>(it->second);
    }

    [[nodiscard]] juce::uint16 unsignedValue(const juce::uint16 op, const juce::uint16 fallback) const
    {
        const auto it = values.find(op);
        if (it == values.end()) return fallback;
        return it->second;
    }

    void mergePresetOverlay(const GenSet& presetGlobal, const GenSet& presetZone)
    {
        // Preset-level generators add to instrument-level (per SF2 spec).
        auto applyAdditive = [this](const GenSet& src)
        {
            for (const auto& [op, amount] : src.values)
            {
                if (op == InstrumentGen) continue;
                const auto existing = values.find(op);
                if (existing == values.end())
                {
                    values[op] = amount;
                }
                else
                {
                    // Add as signed 16-bit.
                    const auto sum = static_cast<juce::int32>(static_cast<juce::int16>(existing->second))
                                   + static_cast<juce::int32>(static_cast<juce::int16>(amount));
                    const auto clamped = juce::jlimit<juce::int32>(std::numeric_limits<juce::int16>::min(),
                                                                   std::numeric_limits<juce::int16>::max(), sum);
                    existing->second = static_cast<juce::uint16>(static_cast<juce::int16>(clamped));
                }
            }
            if (src.keyRangeSet)
            {
                if (!keyRangeSet) { keyLo = src.keyLo; keyHi = src.keyHi; keyRangeSet = true; }
                else { keyLo = juce::jmax(keyLo, src.keyLo); keyHi = juce::jmin(keyHi, src.keyHi); }
            }
            if (src.velRangeSet)
            {
                if (!velRangeSet) { velLo = src.velLo; velHi = src.velHi; velRangeSet = true; }
                else { velLo = juce::jmax(velLo, src.velLo); velHi = juce::jmin(velHi, src.velHi); }
            }
        };
        applyAdditive(presetGlobal);
        applyAdditive(presetZone);
    }
};

struct InstrumentZoneSpec
{
    GenSet gens;
    int sampleIndex = -1;
};

void collectZoneGens(const Sf2File& f, const bool isPreset, const size_t bagStart, const size_t bagEnd,
                     std::vector<GenSet>& zonesOut, GenSet& globalOut, bool& hasGlobalOut)
{
    const auto* bags = isPreset ? f.pbag : f.ibag;
    const auto bagCount = isPreset ? f.pbagCount : f.ibagCount;
    const auto* gens = isPreset ? f.pgen : f.igen;
    const auto genCount = isPreset ? f.pgenCount : f.igenCount;

    hasGlobalOut = false;
    for (size_t bagIdx = bagStart; bagIdx < bagEnd && bagIdx + 1 < bagCount; ++bagIdx)
    {
        const auto genStart = bags[bagIdx].genIdx;
        const auto genEnd = bags[bagIdx + 1].genIdx;
        if (genStart > genCount || genEnd > genCount || genEnd < genStart)
            continue;
        GenSet gset;
        for (size_t g = genStart; g < genEnd; ++g)
            gset.apply(gens[g]);

        const bool zoneTerminator = isPreset ? gset.hasInstId : gset.hasSampleId;
        if (zoneTerminator)
        {
            zonesOut.push_back(std::move(gset));
        }
        else if (!hasGlobalOut)
        {
            // First non-terminating zone is the global zone.
            globalOut = std::move(gset);
            hasGlobalOut = true;
        }
    }
}

[[nodiscard]] juce::AudioBuffer<float> decodeSample(const Sf2File& f, const ShdrRecord& shdr,
                                                    std::vector<ImportDiagnostic>& diagnostics)
{
    juce::AudioBuffer<float> buffer(1, 0);
    if (shdr.end <= shdr.start)
    {
        addDiagnostic(diagnostics, ImportDiagnostic::Severity::warning,
                      "SF2 sample has empty range: " + fixedString(shdr.name, 20));
        return buffer;
    }
    const auto sampleCount = static_cast<size_t>(shdr.end - shdr.start);
    const auto byteStart = static_cast<size_t>(shdr.start) * 2u;
    if (byteStart + sampleCount * 2u > f.smplBytes)
    {
        addDiagnostic(diagnostics, ImportDiagnostic::Severity::error,
                      "SF2 sample exceeds smpl chunk: " + fixedString(shdr.name, 20));
        return buffer;
    }

    const bool has24 = (f.sm24Data != nullptr) && (static_cast<size_t>(shdr.start) + sampleCount <= f.sm24Bytes);

    buffer.setSize(1, static_cast<int>(sampleCount), false, true, false);
    auto* dst = buffer.getWritePointer(0);
    const auto* src16 = f.smplData + byteStart;
    constexpr float scale16 = 1.0f / 32768.0f;
    constexpr float scale24 = 1.0f / 8388608.0f;
    for (size_t i = 0; i < sampleCount; ++i)
    {
        const auto s16 = static_cast<juce::int16>(static_cast<juce::uint16>(src16[i * 2])
                                                  | (static_cast<juce::uint16>(src16[i * 2 + 1]) << 8));
        if (has24)
        {
            const auto low = f.sm24Data[static_cast<size_t>(shdr.start) + i];
            const auto s24 = (static_cast<juce::int32>(s16) << 8) | static_cast<juce::int32>(low);
            dst[i] = static_cast<float>(s24) * scale24;
        }
        else
        {
            dst[i] = static_cast<float>(s16) * scale16;
        }
    }
    return buffer;
}

void importSinglePreset(const Sf2File& f, const size_t presetIndex, ImportResult& result)
{
    if (presetIndex + 1 >= f.phdrCount)
    {
        addDiagnostic(result.diagnostics, ImportDiagnostic::Severity::error, "SF2 preset index out of range");
        return;
    }

    const auto& presetHdr = f.phdr[presetIndex];
    const auto& nextPresetHdr = f.phdr[presetIndex + 1];
    result.program.name = fixedString(presetHdr.name, 20).toStdString();

    GenSet presetGlobal; bool hasPresetGlobal = false;
    std::vector<GenSet> presetZones;
    collectZoneGens(f, /*isPreset*/ true,
                    presetHdr.bagIdx, nextPresetHdr.bagIdx,
                    presetZones, presetGlobal, hasPresetGlobal);

    std::map<int, int> sampleIdToAssetIndex; // shdr index -> SampleAsset index

    for (const auto& presetZone : presetZones)
    {
        if (!presetZone.hasInstId) continue;
        const auto instId = presetZone.instId;
        if (instId < 0 || static_cast<size_t>(instId) + 1 >= f.instCount) continue;

        const auto& inst = f.inst[instId];
        const auto& nextInst = f.inst[instId + 1];

        Group group;
        group.name = fixedString(inst.name, 20).toStdString();
        result.program.groups.push_back(group);
        const auto groupIndex = static_cast<int>(result.program.groups.size() - 1);

        GenSet instGlobal; bool hasInstGlobal = false;
        std::vector<GenSet> instZones;
        collectZoneGens(f, /*isPreset*/ false,
                        inst.bagIdx, nextInst.bagIdx,
                        instZones, instGlobal, hasInstGlobal);

        for (auto& zoneGens : instZones)
        {
            if (!zoneGens.hasSampleId) continue;

            // Merge instrument global generators as defaults below the zone.
            GenSet effective = zoneGens;
            if (hasInstGlobal)
            {
                for (const auto& [op, val] : instGlobal.values)
                    if (effective.values.find(op) == effective.values.end())
                        effective.values[op] = val;
                if (!effective.keyRangeSet && instGlobal.keyRangeSet)
                { effective.keyLo = instGlobal.keyLo; effective.keyHi = instGlobal.keyHi; effective.keyRangeSet = true; }
                if (!effective.velRangeSet && instGlobal.velRangeSet)
                { effective.velLo = instGlobal.velLo; effective.velHi = instGlobal.velHi; effective.velRangeSet = true; }
            }
            // Then overlay preset generators (additive per spec).
            effective.mergePresetOverlay(hasPresetGlobal ? presetGlobal : GenSet{}, presetZone);

            const auto sampleId = effective.sampleId;
            if (sampleId < 0 || static_cast<size_t>(sampleId) >= f.shdrCount - 1) // last is terminal EOS
            {
                addDiagnostic(result.diagnostics, ImportDiagnostic::Severity::warning,
                              "SF2 zone references invalid sampleID");
                continue;
            }

            int assetIndex = -1;
            const auto cached = sampleIdToAssetIndex.find(sampleId);
            if (cached != sampleIdToAssetIndex.end())
            {
                assetIndex = cached->second;
            }
            else
            {
                const auto& shdr = f.shdr[sampleId];
                auto sampleData = decodeSample(f, shdr, result.diagnostics);
                if (sampleData.getNumSamples() == 0)
                    continue;

                SampleAsset asset;
                asset.sourcePath = "";
                asset.displayName = fixedString(shdr.name, 20).toStdString();
                asset.lengthSamples = sampleData.getNumSamples();
                asset.numChannels = 1;
                asset.sampleRateHz = static_cast<double>(shdr.sampleRate);
                asset.rootMidiNote = static_cast<int>(shdr.originalPitch);
                asset.bitDepth = (f.sm24Data != nullptr) ? 24 : 16;
                asset.embeddedInProgram = true;

                result.program.sampleAssets.push_back(asset);
                result.sampleDataByAsset.push_back(std::move(sampleData));
                assetIndex = static_cast<int>(result.program.sampleAssets.size() - 1);
                sampleIdToAssetIndex[sampleId] = assetIndex;
            }

            const auto& shdr = f.shdr[sampleId];

            Zone zone;
            zone.sampleAssetIndex = assetIndex;
            zone.groupIndex = groupIndex;

            // Key/Vel range.
            if (effective.keyRangeSet)
                zone.keyRange = MidiRange::fromUnordered(effective.keyLo, effective.keyHi);
            if (effective.velRangeSet)
                zone.velocityRange = VelocityRange::fromUnordered(effective.velLo, effective.velHi);

            // Root key.
            const auto overridingRoot = effective.signedValue(OverridingRootKey, -1);
            zone.rootMidiNote = (overridingRoot >= 0)
                ? clampMidiNote(static_cast<int>(overridingRoot))
                : clampMidiNote(static_cast<int>(shdr.originalPitch));

            // Tune.
            const auto coarseTune = effective.signedValue(CoarseTune, 0);
            const auto fineTune = effective.signedValue(FineTune, 0);
            const auto pitchCorrection = static_cast<int>(shdr.pitchCorrection);
            zone.tuneCents = static_cast<float>(static_cast<int>(fineTune) + pitchCorrection);
            zone.rootMidiNote = clampMidiNote(zone.rootMidiNote - static_cast<int>(coarseTune));

            // Pan: -500..500 in tenths of a percent -> -1..1.
            zone.pan = juce::jlimit(-1.0f, 1.0f, static_cast<float>(effective.signedValue(Pan, 0)) / 500.0f);

            // Initial attenuation: centibels (positive lowers volume) -> dB.
            const auto attenCb = effective.signedValue(InitialAttenuation, 0);
            zone.gainDb = -static_cast<float>(attenCb) / 10.0f;

            // Sample window offsets.
            const auto startOffset = static_cast<int>(effective.signedValue(StartAddrsOffset, 0))
                                   + static_cast<int>(effective.signedValue(StartAddrsCoarseOffset, 0)) * 32768;
            const auto endOffset = static_cast<int>(effective.signedValue(EndAddrsOffset, 0))
                                 + static_cast<int>(effective.signedValue(EndAddrsCoarseOffset, 0)) * 32768;
            const auto sampleLen = static_cast<int>(shdr.end - shdr.start);
            zone.sampleStart = juce::jlimit(0, sampleLen, startOffset);
            zone.sampleEndExclusive = juce::jlimit(zone.sampleStart, sampleLen, sampleLen + endOffset);

            // Loop modes.
            const auto modes = effective.unsignedValue(SampleModes, 0);
            const int relLoopStart = static_cast<int>(shdr.startLoop) - static_cast<int>(shdr.start)
                                   + static_cast<int>(effective.signedValue(StartLoopAddrsOffset, 0));
            const int relLoopEnd = static_cast<int>(shdr.endLoop) - static_cast<int>(shdr.start)
                                 + static_cast<int>(effective.signedValue(EndLoopAddrsOffset, 0));
            if (modes == 1u)
            {
                zone.loopMode = ZoneLoopMode::continuous;
                zone.loopStart = juce::jmax(0, relLoopStart);
                zone.loopEndExclusive = juce::jlimit(zone.loopStart + 1, sampleLen, relLoopEnd);
            }
            else if (modes == 3u)
            {
                zone.loopMode = ZoneLoopMode::sustain;
                zone.loopStart = juce::jmax(0, relLoopStart);
                zone.loopEndExclusive = juce::jlimit(zone.loopStart + 1, sampleLen, relLoopEnd);
            }
            else
            {
                zone.loopMode = ZoneLoopMode::noLoop;
            }

            zone.triggerMode = ZoneTriggerMode::gate;

            result.program.zones.push_back(zone);
        }
    }

    if (result.program.zones.empty())
        addDiagnostic(result.diagnostics, ImportDiagnostic::Severity::error,
                      "SF2 preset produced no playable zones: " + juce::String(result.program.name));
}

void enumeratePresets(const Sf2File& f, std::vector<PresetInfo>& presets)
{
    if (f.phdrCount == 0) return;
    // The last record is EOP (terminator).
    for (size_t i = 0; i + 1 < f.phdrCount; ++i)
    {
        PresetInfo p;
        p.name = fixedString(f.phdr[i].name, 20);
        p.bank = static_cast<int>(f.phdr[i].bank);
        p.program = static_cast<int>(f.phdr[i].preset);
        presets.push_back(p);
    }
    // Sort by (bank, program) so the "first preset" is deterministic.
    std::sort(presets.begin(), presets.end(), [](const PresetInfo& a, const PresetInfo& b)
    {
        if (a.bank != b.bank) return a.bank < b.bank;
        return a.program < b.program;
    });
}
} // namespace

ImportResult importFile(const juce::File& sf2File)
{
    return importFilePreset(sf2File, 0);
}

ImportResult importFilePreset(const juce::File& sf2File, const int presetIndex)
{
    ImportResult result;

    if (!sf2File.existsAsFile())
    {
        addDiagnostic(result.diagnostics, ImportDiagnostic::Severity::error,
                      "SoundFont 2 file not found: " + sf2File.getFullPathName());
        return result;
    }

    juce::MemoryBlock blob;
    if (!sf2File.loadFileAsData(blob))
    {
        addDiagnostic(result.diagnostics, ImportDiagnostic::Severity::error,
                      "Could not read SoundFont 2 file: " + sf2File.getFullPathName());
        return result;
    }

    Sf2File parsed;
    if (!parseSf2Container(reinterpret_cast<const juce::uint8*>(blob.getData()), blob.getSize(),
                           parsed, result.diagnostics))
        return result;

    enumeratePresets(parsed, result.availablePresets);
    if (result.availablePresets.empty())
    {
        addDiagnostic(result.diagnostics, ImportDiagnostic::Severity::error, "SoundFont 2 file has no presets");
        return result;
    }

    if (presetIndex < 0 || static_cast<size_t>(presetIndex) >= result.availablePresets.size())
    {
        addDiagnostic(result.diagnostics, ImportDiagnostic::Severity::error,
                      "SoundFont 2 preset index out of range: " + juce::String(presetIndex));
        return result;
    }

    // Find the phdr index for the chosen preset (matching bank+program from sorted list).
    const auto& chosen = result.availablePresets[static_cast<size_t>(presetIndex)];
    size_t phdrPick = 0;
    for (size_t i = 0; i + 1 < parsed.phdrCount; ++i)
    {
        if (static_cast<int>(parsed.phdr[i].bank) == chosen.bank
            && static_cast<int>(parsed.phdr[i].preset) == chosen.program)
        {
            phdrPick = i;
            break;
        }
    }
    result.chosenPresetIndex = presetIndex;
    importSinglePreset(parsed, phdrPick, result);
    return result;
}

juce::String buildImportSummary(const ImportResult& result, const bool imported)
{
    int errorCount = 0;
    int warningCount = 0;
    for (const auto& d : result.diagnostics)
    {
        if (d.severity == ImportDiagnostic::Severity::error)
            ++errorCount;
        else
            ++warningCount;
    }

    juce::String summary = imported
        ? ("SF2 imported preset \"" + juce::String(result.program.name) + "\": "
           + juce::String(static_cast<int>(result.program.zones.size())) + " zones / "
           + juce::String(static_cast<int>(result.program.sampleAssets.size())) + " samples")
        : juce::String("SF2 import failed");

    summary += " (" + juce::String(static_cast<int>(result.availablePresets.size())) + " presets available)";

    if (errorCount > 0) summary += " (" + juce::String(errorCount) + " errors)";
    if (warningCount > 0) summary += " (" + juce::String(warningCount) + " warnings)";

    for (const auto& d : result.diagnostics)
    {
        summary += "\n";
        summary += d.severity == ImportDiagnostic::Severity::error ? "Error: " : "Warning: ";
        summary += juce::String(d.message);
    }

    return summary;
}
} // namespace audiocity::engine::sf2
