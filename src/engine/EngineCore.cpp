#include "EngineCore.h"

#include "AudioFileSupport.h"

#include "RexLoader.h"

#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <mutex>
#include <optional>

namespace audiocity::engine
{
namespace
{
constexpr float kPi = 3.14159265358979323846f;
constexpr int kRenderControlBlockSize = 16;

float wrapPhase(const float phase) noexcept
{
    auto wrapped = phase - std::floor(phase);
    if (wrapped < 0.0f)
        wrapped += 1.0f;
    return wrapped;
}

float advanceWrappedPhase(const float phase, const float increment, const int samples) noexcept
{
    return wrapPhase(phase + increment * static_cast<float>(samples));
}

float interpolateSpanValue(const float start, const float end, const int index, const int count) noexcept
{
    if (count <= 1)
        return end;

    return start + (end - start) * (static_cast<float>(index) / static_cast<float>(count - 1));
}

float computeLfoWave(const audiocity::engine::EngineCore::FilterSettings::LfoShape shape,
                     const float phase) noexcept
{
    const auto p = phase - std::floor(phase);
    switch (shape)
    {
        case audiocity::engine::EngineCore::FilterSettings::LfoShape::triangle:
            return 1.0f - 4.0f * std::abs(p - 0.5f);
        case audiocity::engine::EngineCore::FilterSettings::LfoShape::square:
            return p < 0.5f ? 1.0f : -1.0f;
        case audiocity::engine::EngineCore::FilterSettings::LfoShape::sawUp:
            return (2.0f * p) - 1.0f;
        case audiocity::engine::EngineCore::FilterSettings::LfoShape::sawDown:
            return 1.0f - (2.0f * p);
        case audiocity::engine::EngineCore::FilterSettings::LfoShape::sine:
        default:
            return std::sin(2.0f * kPi * p);
    }
}

float semitonesToRatio(const float semitones) noexcept
{
    return std::pow(2.0f, semitones / 12.0f);
}

float centsToRatio(const float cents) noexcept
{
    return semitonesToRatio(cents / 100.0f);
}

struct ModulationContribution
{
    float pitchCents = 0.0f;
    float filterHz = 0.0f;
    float ampDelta = 0.0f;
};

struct ModulationSourceValue
{
    const audiocity::engine::EngineCore::ModulationRoute* route = nullptr;
    float value = 0.0f;
};

void accumulateModulationContribution(ModulationContribution& contribution,
                                      const audiocity::engine::EngineCore::ModulationRoute& route,
                                      const float sourceValue) noexcept
{
    contribution.pitchCents += route.toPitchCents * sourceValue;
    contribution.filterHz += route.toFilterHz * sourceValue;
    contribution.ampDelta += route.toAmp * sourceValue;
}

template <std::size_t SourceCount>
void accumulateModulationSources(ModulationContribution& contribution,
                                 const std::array<ModulationSourceValue, SourceCount>& sources) noexcept
{
    for (const auto& source : sources)
    {
        if (source.route != nullptr)
            accumulateModulationContribution(contribution, *source.route, source.value);
    }
}

void clampModulationRoute(audiocity::engine::EngineCore::ModulationRoute& target,
                          const audiocity::engine::EngineCore::ModulationRoute& source) noexcept
{
    target.toPitchCents = juce::jlimit(-1200.0f, 1200.0f, source.toPitchCents);
    target.toFilterHz = juce::jlimit(-20000.0f, 20000.0f, source.toFilterHz);
    target.toAmp = juce::jlimit(-1.0f, 1.0f, source.toAmp);
}

template <std::size_t RouteCount>
void clampModulationRoutes(const std::array<audiocity::engine::EngineCore::ModulationRoute*, RouteCount>& targets,
                           const std::array<const audiocity::engine::EngineCore::ModulationRoute*, RouteCount>& sources) noexcept
{
    for (std::size_t index = 0; index < RouteCount; ++index)
        clampModulationRoute(*targets[index], *sources[index]);
}

float unitHashToFloat(std::uint32_t x) noexcept
{
    x ^= x >> 16;
    x *= 0x7feb352dU;
    x ^= x >> 15;
    x *= 0x846ca68bU;
    x ^= x >> 16;
    return static_cast<float>(x) * (1.0f / 4294967295.0f);
}

float deterministicBipolarFromNoteAndOrder(const int noteNumber, const std::uint64_t startOrder) noexcept
{
    const auto seedA = static_cast<std::uint32_t>(noteNumber * 2654435761u);
    const auto seedB = static_cast<std::uint32_t>((startOrder & 0xffffffffull) ^ (startOrder >> 32));
    const auto unit = unitHashToFloat(seedA ^ seedB ^ 0x9e3779b9U);
    return (2.0f * unit) - 1.0f;
}

std::optional<juce::String> getMetadataValueCaseInsensitive(const juce::StringPairArray& metadata,
                                                            const juce::String& key)
{
    const auto keys = metadata.getAllKeys();
    for (int i = 0; i < keys.size(); ++i)
    {
        if (keys[i].equalsIgnoreCase(key))
            return metadata.getValue(keys[i], {});
    }

    return std::nullopt;
}

std::optional<int> parseMidiNoteFromMetadataString(const juce::String& raw)
{
    const auto trimmed = raw.trim();
    if (trimmed.isEmpty())
        return std::nullopt;

    if (trimmed.containsOnly("-0123456789"))
        return juce::jlimit(0, 127, trimmed.getIntValue());

    const juce::String upper = trimmed.toUpperCase();
    static constexpr const char* noteNames[] =
    {
        "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"
    };

    for (int n = 0; n < 12; ++n)
    {
        const juce::String noteName(noteNames[n]);
        if (!upper.startsWith(noteName))
            continue;

        const auto octavePart = upper.substring(noteName.length()).trim();
        if (!octavePart.containsOnly("-0123456789"))
            continue;

        const auto octave = octavePart.getIntValue();
        const auto midi = (octave + 2) * 12 + n;
        return juce::jlimit(0, 127, midi);
    }

    return std::nullopt;
}

std::optional<int> findEmbeddedRootMidiNote(const juce::StringPairArray& metadata)
{
    static const juce::StringArray candidateKeys
    {
        "MidiUnityNote",
        "RootNote",
        "ACID Root Note",
        "AcidRootNote",
        "acidrootnote"
    };

    for (const auto& key : candidateKeys)
    {
        const auto maybeValue = getMetadataValueCaseInsensitive(metadata, key);
        if (!maybeValue.has_value())
            continue;

        const auto parsed = parseMidiNoteFromMetadataString(*maybeValue);
        if (parsed.has_value())
            return parsed;
    }

    return std::nullopt;
}

std::optional<double> findEmbeddedTempoBpm(const juce::StringPairArray& metadata)
{
    static const juce::StringArray candidateKeys
    {
        "Tempo",
        "BPM",
        "ACID Tempo",
        "AcidTempo",
        "acidtempo"
    };

    for (const auto& key : candidateKeys)
    {
        const auto maybeValue = getMetadataValueCaseInsensitive(metadata, key);
        if (!maybeValue.has_value())
            continue;

        auto numeric = maybeValue->trim().retainCharacters("0123456789.");
        if (numeric.isEmpty())
            continue;

        const auto tempo = numeric.getDoubleValue();
        if (tempo > 0.0)
            return tempo;
    }

    return std::nullopt;
}

std::optional<int> findEmbeddedLoopPoint(const juce::StringPairArray& metadata,
                                         const juce::StringArray& candidateKeys)
{
    for (const auto& key : candidateKeys)
    {
        const auto maybeValue = getMetadataValueCaseInsensitive(metadata, key);
        if (!maybeValue.has_value())
            continue;

        const auto trimmed = maybeValue->trim();
        if (trimmed.isEmpty() || !trimmed.containsOnly("-0123456789"))
            continue;

        return trimmed.getIntValue();
    }

    return std::nullopt;
}

std::optional<std::pair<int, int>> findEmbeddedLoopRange(const juce::StringPairArray& metadata)
{
    static const juce::StringArray loopStartKeys
    {
        "Loop0Start",
        "loop0start"
    };

    static const juce::StringArray loopEndKeys
    {
        "Loop0End",
        "loop0end"
    };

    static const juce::StringArray loopLengthKeys
    {
        "Loop0Length",
        "loop0length"
    };

    const auto maybeStart = findEmbeddedLoopPoint(metadata, loopStartKeys);
    if (!maybeStart.has_value())
        return std::nullopt;

    auto maybeEnd = findEmbeddedLoopPoint(metadata, loopEndKeys);
    const auto maybeLength = findEmbeddedLoopPoint(metadata, loopLengthKeys);
    if (!maybeEnd.has_value() && maybeLength.has_value() && *maybeLength > 0)
        maybeEnd = *maybeStart + *maybeLength - 1;

    if (!maybeEnd.has_value())
        return std::nullopt;

    if (*maybeStart < 0 || *maybeEnd <= *maybeStart)
        return std::nullopt;

    return std::pair<int, int>{ *maybeStart, *maybeEnd };
}

std::uint32_t readLeU32(const std::uint8_t* data) noexcept
{
    return static_cast<std::uint32_t>(data[0])
        | (static_cast<std::uint32_t>(data[1]) << 8)
        | (static_cast<std::uint32_t>(data[2]) << 16)
        | (static_cast<std::uint32_t>(data[3]) << 24);
}

std::optional<std::pair<int, int>> findEmbeddedLoopRangeFromWavSmplChunk(const juce::File& file)
{
    juce::MemoryBlock bytes;
    if (!file.loadFileAsData(bytes))
        return std::nullopt;

    const auto* data = static_cast<const std::uint8_t*>(bytes.getData());
    const auto size = bytes.getSize();
    if (size < 12)
        return std::nullopt;

    if (std::memcmp(data + 0, "RIFF", 4) != 0 || std::memcmp(data + 8, "WAVE", 4) != 0)
        return std::nullopt;

    std::size_t cursor = 12;
    while (cursor + 8 <= size)
    {
        const auto* chunkHeader = data + cursor;
        const auto chunkSize = static_cast<std::size_t>(readLeU32(chunkHeader + 4));
        const auto chunkDataOffset = cursor + 8;

        if (chunkDataOffset + chunkSize > size)
            break;

        if (std::memcmp(chunkHeader, "smpl", 4) == 0)
        {
            if (chunkSize < 60)
                return std::nullopt;

            const auto* smpl = data + chunkDataOffset;
            const auto loopCount = readLeU32(smpl + 28);
            if (loopCount == 0)
                return std::nullopt;

            const auto* loop = smpl + 36;
            const auto loopStart = static_cast<int>(readLeU32(loop + 8));
            const auto loopEnd = static_cast<int>(readLeU32(loop + 12));
            if (loopStart < 0 || loopEnd <= loopStart)
                return std::nullopt;

            return std::pair<int, int>{ loopStart, loopEnd };
        }

        cursor = chunkDataOffset + chunkSize + (chunkSize & 1u);
    }

    return std::nullopt;
}

juce::String detectLoopFormatBadge(const juce::File& file, const juce::StringPairArray& metadata)
{
    const auto ext = file.getFileExtension().toLowerCase();
    const auto isWav = (ext == ".wav");

    const auto embeddedLoopRange = findEmbeddedLoopRange(metadata);
    const auto fallbackSmplLoopRange = embeddedLoopRange.has_value()
        ? std::optional<std::pair<int, int>>{}
        : (isWav ? findEmbeddedLoopRangeFromWavSmplChunk(file)
                 : std::optional<std::pair<int, int>>{});

    if (!embeddedLoopRange.has_value() && !fallbackSmplLoopRange.has_value())
        return {};

    if (ext == ".wav")
        return "Acidized";

    if (ext == ".aif" || ext == ".aiff")
        return "Apple Loop";

    return {};
}

EngineCore::AdsrSettings defaultAmpEnvelopeForLoadedSample() noexcept
{
    return {};
}

EngineCore::AmpLfoSettings defaultAmpLfoSettingsForLoadedSample() noexcept
{
    return {};
}

EngineCore::PitchLfoSettings defaultPitchLfoSettingsForLoadedSample() noexcept
{
    return {};
}

EngineCore::AdsrSettings defaultFilterEnvelopeForLoadedSample() noexcept
{
    return { 0.001f, 0.120f, 0.0f, 0.100f };
}

EngineCore::FilterSettings defaultFilterSettingsForLoadedSample() noexcept
{
    EngineCore::FilterSettings settings;
    settings.baseCutoffHz = 18000.0f;
    settings.envAmountHz = 0.0f;
    settings.resonance = 0.0f;
    settings.mode = EngineCore::FilterSettings::Mode::lowPass12;
    settings.keyTracking = 0.0f;
    settings.velocityAmountHz = 0.0f;
    settings.lfoRateHz = 0.0f;
    settings.lfoRateKeyTracking = 0.0f;
    settings.lfoAmountHz = 0.0f;
    settings.lfoAmountKeyTracking = 0.0f;
    settings.lfoStartPhaseDegrees = 0.0f;
    settings.lfoStartPhaseRandomDegrees = 0.0f;
    settings.lfoFadeInMs = 0.0f;
    settings.lfoKeytrackLinear = false;
    settings.lfoUnipolar = false;
    settings.lfoShape = EngineCore::FilterSettings::LfoShape::sine;
    settings.lfoRetrigger = true;
    settings.lfoTempoSync = false;
    settings.lfoRateKeytrackInTempoSync = true;
    settings.lfoSyncDivision = 6;
    return settings;
}
}

struct SampleStreamSource
{
    virtual ~SampleStreamSource() = default;
    [[nodiscard]] virtual int getNumChannels() const noexcept = 0;
    [[nodiscard]] virtual int getNumSamples() const noexcept = 0;
    [[nodiscard]] virtual float readSample(int channel, int index) const noexcept = 0;
    virtual void requestPrime(int streamIndex) const noexcept { juce::ignoreUnused(streamIndex); }
    [[nodiscard]] virtual bool servicePendingPrime() const { return false; }
    [[nodiscard]] virtual int getPrimeRequestCount() const noexcept { return 0; }
    [[nodiscard]] virtual int getPrimeCacheHitCount() const noexcept { return 0; }
    [[nodiscard]] virtual int getPrimeCacheMissCount() const noexcept { return 0; }
    [[nodiscard]] virtual int getPrimeServiceCount() const noexcept { return 0; }
};

struct MemorySampleStreamSource final : SampleStreamSource
{
    explicit MemorySampleStreamSource(juce::AudioBuffer<float> dataToOwn)
        : data(std::move(dataToOwn))
    {
    }

    [[nodiscard]] int getNumChannels() const noexcept override
    {
        return data.getNumChannels();
    }

    [[nodiscard]] int getNumSamples() const noexcept override
    {
        return data.getNumSamples();
    }

    [[nodiscard]] float readSample(const int channel, const int index) const noexcept override
    {
        if (index < 0 || index >= data.getNumSamples() || data.getNumChannels() <= 0)
            return 0.0f;

        const auto clampedChannel = juce::jlimit(0, data.getNumChannels() - 1, channel);
        return data.getSample(clampedChannel, index);
    }

    juce::AudioBuffer<float> data;
};

constexpr int kDiskStreamCachePageCount = 4;

struct StreamCacheWindow
{
    juce::AudioBuffer<float> data;
    int startSample = 0;

    [[nodiscard]] bool contains(const int sampleIndex) const noexcept
    {
        return sampleIndex >= startSample && sampleIndex < (startSample + data.getNumSamples());
    }
};

struct StreamCacheState
{
    std::array<StreamCacheWindow, kDiskStreamCachePageCount> pages{};
    int pageCount = 0;

    [[nodiscard]] const StreamCacheWindow* findPageContainingSample(const int sampleIndex) const noexcept
    {
        for (int pageIndex = 0; pageIndex < pageCount; ++pageIndex)
        {
            const auto& page = pages[pageIndex];
            if (page.contains(sampleIndex))
                return &page;
        }

        return nullptr;
    }

    [[nodiscard]] bool contains(const int sampleIndex) const noexcept
    {
        return findPageContainingSample(sampleIndex) != nullptr;
    }
};

struct DiskSampleStreamSource final : SampleStreamSource
{
    DiskSampleStreamSource(const juce::File& backingFile,
                           juce::AudioBuffer<float> seedTail,
                           const int absoluteStreamStartSample)
        : file(backingFile),
          totalChannels(juce::jmax(1, seedTail.getNumChannels())),
          totalSamples(seedTail.getNumSamples()),
          streamStartSample(juce::jmax(0, absoluteStreamStartSample)),
          primeWindowSamples(totalSamples > 0
                            ? juce::jmin(totalSamples,
                                         juce::jmax(512,
                                                    juce::jmin(4096, juce::jmax(0, absoluteStreamStartSample))))
              : 0)
    {
        formatManager.registerBasicFormats();

        if (const auto initialState = seedInitialCacheState(seedTail))
            cacheState.store(initialState, std::memory_order_release);
    }

    [[nodiscard]] int getNumChannels() const noexcept override
    {
        return totalChannels;
    }

    [[nodiscard]] int getNumSamples() const noexcept override
    {
        return totalSamples;
    }

    [[nodiscard]] float readSample(const int channel, const int index) const noexcept override
    {
        if (index < 0 || index >= totalSamples || totalChannels <= 0)
            return 0.0f;

        const auto clampedChannel = juce::jlimit(0, totalChannels - 1, channel);
        const auto currentCache = cacheState.load(std::memory_order_acquire);
        if (currentCache != nullptr)
            if (const auto* page = currentCache->findPageContainingSample(index); page != nullptr)
                return page->data.getSample(juce::jmin(clampedChannel, page->data.getNumChannels() - 1),
                                            index - page->startSample);

        return 0.0f;
    }

    void requestPrime(const int streamIndex) const noexcept override
    {
        if (totalSamples <= 0 || primeWindowSamples <= 0)
            return;

        ++primeRequestCount;

        const auto clampedIndex = juce::jlimit(0, totalSamples - 1, streamIndex);
        const auto currentCache = cacheState.load(std::memory_order_acquire);
        if (currentCache != nullptr && currentCache->contains(clampedIndex))
        {
            ++primeCacheHitCount;
            return;
        }

        ++primeCacheMissCount;
        pendingPrimeWindowStart.store(alignPrimeWindowStart(clampedIndex), std::memory_order_release);
    }

    [[nodiscard]] bool servicePendingPrime() const override
    {
        const auto requestedWindowStart = pendingPrimeWindowStart.exchange(-1, std::memory_order_acq_rel);
        if (requestedWindowStart < 0)
            return false;

        const auto currentCache = cacheState.load(std::memory_order_acquire);
        if (currentCache != nullptr && currentCache->contains(requestedWindowStart))
            return false;

        const auto nextCache = loadCacheWindow(requestedWindowStart);
        if (nextCache == nullptr)
            return false;

        cacheState.store(mergeCacheWindow(currentCache, *nextCache), std::memory_order_release);
        ++primeServiceCount;
        return true;
    }

    [[nodiscard]] int getPrimeRequestCount() const noexcept override
    {
        return primeRequestCount.load(std::memory_order_relaxed);
    }

    [[nodiscard]] int getPrimeCacheHitCount() const noexcept override
    {
        return primeCacheHitCount.load(std::memory_order_relaxed);
    }

    [[nodiscard]] int getPrimeCacheMissCount() const noexcept override
    {
        return primeCacheMissCount.load(std::memory_order_relaxed);
    }

    [[nodiscard]] int getPrimeServiceCount() const noexcept override
    {
        return primeServiceCount.load(std::memory_order_relaxed);
    }

private:
    [[nodiscard]] int alignPrimeWindowStart(const int streamIndex) const noexcept
    {
        if (primeWindowSamples <= 0 || totalSamples <= 0)
            return -1;

        const auto clampedIndex = juce::jlimit(0, totalSamples - 1, streamIndex);
        const auto alignedStart = (clampedIndex / primeWindowSamples) * primeWindowSamples;
        return juce::jlimit(0, juce::jmax(0, totalSamples - primeWindowSamples), alignedStart);
    }

    [[nodiscard]] std::shared_ptr<const StreamCacheState> seedInitialCacheState(const juce::AudioBuffer<float>& seedTail) const
    {
        if (totalSamples <= 0 || primeWindowSamples <= 0 || seedTail.getNumChannels() <= 0)
            return {};

        auto state = std::make_shared<StreamCacheState>();
        auto& page = state->pages[state->pageCount++];
        const auto windowSamples = juce::jmin(primeWindowSamples, seedTail.getNumSamples());
        page.startSample = 0;
        page.data.setSize(totalChannels, windowSamples, false, true, true);
        page.data.clear();

        for (int channel = 0; channel < totalChannels; ++channel)
        {
            const auto sourceChannel = juce::jmin(channel, seedTail.getNumChannels() - 1);
            page.data.copyFrom(channel, 0, seedTail, sourceChannel, 0, windowSamples);
        }

        return state;
    }

    [[nodiscard]] std::shared_ptr<const StreamCacheWindow> loadCacheWindow(const int requestedWindowStart) const
    {
        if (totalSamples <= 0 || primeWindowSamples <= 0)
            return {};

        const auto windowStart = juce::jlimit(0, juce::jmax(0, totalSamples - primeWindowSamples), requestedWindowStart);
        const auto windowSamples = juce::jmin(primeWindowSamples, totalSamples - windowStart);
        if (windowSamples <= 0)
            return {};

        std::lock_guard<std::mutex> lock(readerMutex);
        auto* currentReader = getOrCreateReaderLocked();
        if (currentReader == nullptr)
            return {};

        juce::AudioBuffer<float> fileData(static_cast<int>(currentReader->numChannels), windowSamples);
        if (!currentReader->read(&fileData,
                                 0,
                                 windowSamples,
                                 static_cast<juce::int64>(streamStartSample + windowStart),
                                 true,
                                 true))
        {
            return {};
        }

        auto cache = std::make_shared<StreamCacheWindow>();
        cache->startSample = windowStart;
        cache->data.setSize(totalChannels, windowSamples, false, true, true);
        cache->data.clear();

        if (totalChannels == 1 && fileData.getNumChannels() > 1)
        {
            for (int sampleIndex = 0; sampleIndex < windowSamples; ++sampleIndex)
            {
                float sum = 0.0f;
                for (int channel = 0; channel < fileData.getNumChannels(); ++channel)
                    sum += fileData.getSample(channel, sampleIndex);

                cache->data.setSample(0, sampleIndex, sum / static_cast<float>(fileData.getNumChannels()));
            }

            return cache;
        }

        for (int channel = 0; channel < totalChannels; ++channel)
        {
            const auto sourceChannel = juce::jmin(channel, fileData.getNumChannels() - 1);
            cache->data.copyFrom(channel, 0, fileData, sourceChannel, 0, windowSamples);
        }

        return cache;
    }

    [[nodiscard]] juce::AudioFormatReader* getOrCreateReaderLocked() const
    {
        if (reader == nullptr && file.existsAsFile())
            reader.reset(formatManager.createReaderFor(file));

        if (reader == nullptr || reader->numChannels <= 0)
            return nullptr;

        return reader.get();
    }

    [[nodiscard]] std::shared_ptr<const StreamCacheState> mergeCacheWindow(
        const std::shared_ptr<const StreamCacheState>& currentState,
        const StreamCacheWindow& nextWindow) const
    {
        auto merged = std::make_shared<StreamCacheState>();
        merged->pages[merged->pageCount++] = nextWindow;

        if (currentState == nullptr)
            return merged;

        for (int pageIndex = 0; pageIndex < currentState->pageCount; ++pageIndex)
        {
            const auto& existingPage = currentState->pages[pageIndex];
            if (existingPage.startSample == nextWindow.startSample)
                continue;

            if (merged->pageCount >= kDiskStreamCachePageCount)
                break;

            merged->pages[merged->pageCount++] = existingPage;
        }

        return merged;
    }

    juce::File file;
    int totalChannels = 0;
    int totalSamples = 0;
    int streamStartSample = 0;
    int primeWindowSamples = 0;
    mutable juce::AudioFormatManager formatManager;
    mutable std::unique_ptr<juce::AudioFormatReader> reader;
    mutable std::mutex readerMutex;
    mutable std::atomic<int> pendingPrimeWindowStart{ -1 };
    mutable std::atomic<int> primeRequestCount{ 0 };
    mutable std::atomic<int> primeCacheHitCount{ 0 };
    mutable std::atomic<int> primeCacheMissCount{ 0 };
    mutable std::atomic<int> primeServiceCount{ 0 };
    mutable std::atomic<std::shared_ptr<const StreamCacheState>> cacheState{};
};

struct EngineCore::SampleSegments
{
    juce::AudioBuffer<float> preloadData;
    std::shared_ptr<const SampleStreamSource> streamSource;
    juce::String backingFilePath;

    [[nodiscard]] int getStreamNumChannels() const noexcept
    {
        return streamSource != nullptr ? streamSource->getNumChannels() : 0;
    }

    [[nodiscard]] int getStreamNumSamples() const noexcept
    {
        return streamSource != nullptr ? streamSource->getNumSamples() : 0;
    }

    void requestPrimeForAbsoluteSample(const int absoluteSampleIndex) const noexcept
    {
        if (streamSource == nullptr)
            return;

        streamSource->requestPrime(juce::jmax(0, absoluteSampleIndex - preloadData.getNumSamples()));
    }

    [[nodiscard]] bool servicePendingPrime() const
    {
        return streamSource != nullptr && streamSource->servicePendingPrime();
    }

    [[nodiscard]] int getPrimeRequestCount() const noexcept
    {
        return streamSource != nullptr ? streamSource->getPrimeRequestCount() : 0;
    }

    [[nodiscard]] int getPrimeCacheHitCount() const noexcept
    {
        return streamSource != nullptr ? streamSource->getPrimeCacheHitCount() : 0;
    }

    [[nodiscard]] int getPrimeCacheMissCount() const noexcept
    {
        return streamSource != nullptr ? streamSource->getPrimeCacheMissCount() : 0;
    }

    [[nodiscard]] int getPrimeServiceCount() const noexcept
    {
        return streamSource != nullptr ? streamSource->getPrimeServiceCount() : 0;
    }
};

struct EngineCore::ProgramAudioSnapshot
{
    std::array<std::shared_ptr<const SampleSegments>, ProgramSnapshot::maxSampleAssets> sampleSegments{};
    std::size_t sampleAssetCount = 0;

    [[nodiscard]] const SampleSegments* getSampleSegments(const int sampleAssetIndex) const noexcept
    {
        if (sampleAssetIndex < 0 || static_cast<std::size_t>(sampleAssetIndex) >= sampleAssetCount)
            return nullptr;

        const auto& segments = sampleSegments[static_cast<std::size_t>(sampleAssetIndex)];
        return segments != nullptr ? segments.get() : nullptr;
    }
};

void EngineCore::prepare(const double sampleRate, const int maxSamplesPerBlock, const int outputChannels) noexcept
{
    sampleRate_ = sampleRate;
    maxSamplesPerBlock_ = maxSamplesPerBlock;
    outputChannels_ = outputChannels;

    voicePool_.prepare(maxSamplesPerBlock_);
    pendingEventCount_ = 0;
    resetRoundRobinCursors();
    globalFilterLfoPhase_ = 0.0f;
    globalAmpLfoPhase_ = 0.0f;
    globalPitchLfoPhase_ = 0.0f;
    autopanPhase_ = 0.0f;
    currentPitchBendSemitones_ = 0.0f;

    juce::dsp::ProcessSpec spec;
    spec.sampleRate = sampleRate_;
    spec.maximumBlockSize = static_cast<juce::uint32>(juce::jmax(1, maxSamplesPerBlock_));
    spec.numChannels = 1;

    auto voiceFilterSpec = spec;
    voiceFilterSpec.numChannels = 2;

    for (auto& voice : voices_)
    {
        voice.filterA.prepare(voiceFilterSpec);
        voice.filterB.prepare(voiceFilterSpec);
        voice.filterA.reset();
        voice.filterB.reset();
    }

    applyEnvelopeParamsToVoices();
    applyFilterParamsToVoices();
    reverb_.setSampleRate(sampleRate_);
    updateReverbParameters();

    for (auto& filter : dcBlockFilters_)
    {
        filter.prepare(spec);
        filter.setType(juce::dsp::StateVariableTPTFilterType::highpass);
        filter.setResonance(0.7071f);
        filter.setCutoffFrequency(juce::jlimit(5.0f, 20.0f, dcFilterSettings_.cutoffHz));
        filter.reset();
    }

    const auto maxDelaySamples = juce::jmax(1, static_cast<int>(std::ceil(sampleRate_ * 4.0)));
    delayBuffer_.setSize(2, maxDelaySamples, false, true, true);
    delayBuffer_.clear();
    mixBuffer_.setSize(2, juce::jmax(1, maxSamplesPerBlock_), false, true, true);
    mixBuffer_.clear();
    voiceScratchBuffer_.setSize(2, juce::jmax(1, maxSamplesPerBlock_), false, true, true);
    voiceScratchBuffer_.clear();
    modulationScratchBuffer_.setSize(4, juce::jmax(1, maxSamplesPerBlock_), false, true, true);
    modulationScratchBuffer_.clear();
    delayWritePos_ = 0;

    if (getTotalSampleLength() == 0)
        generateFallbackSample();
}

void EngineCore::release() noexcept
{
    stopAllVoicesImmediate();
    voicePool_.reset();
    pendingEventCount_ = 0;
    resetRoundRobinCursors();
    currentPitchBendSemitones_ = 0.0f;
    delayBuffer_.clear();
    mixBuffer_.clear();
    voiceScratchBuffer_.clear();
    modulationScratchBuffer_.clear();
    delayWritePos_ = 0;
    autopanPhase_ = 0.0f;
    for (auto& filter : dcBlockFilters_)
        filter.reset();
}

void EngineCore::setReverbMix(const float mix) noexcept
{
    reverbMix_ = juce::jlimit(0.0f, 1.0f, mix);
    updateReverbParameters();
}

void EngineCore::setDelaySettings(const DelaySettings& settings) noexcept
{
    delaySettings_.timeMs = juce::jlimit(1.0f, 2000.0f, settings.timeMs);
    delaySettings_.feedback = juce::jlimit(0.0f, 0.95f, settings.feedback);
    delaySettings_.mix = juce::jlimit(0.0f, 1.0f, settings.mix);
    delaySettings_.tempoSync = settings.tempoSync;
}

void EngineCore::setDcFilterSettings(const DcFilterSettings& settings) noexcept
{
    dcFilterSettings_.enabled = settings.enabled;
    dcFilterSettings_.cutoffHz = juce::jlimit(5.0f, 20.0f, settings.cutoffHz);

    const auto cutoff = dcFilterSettings_.cutoffHz;
    for (auto& filter : dcBlockFilters_)
        filter.setCutoffFrequency(cutoff);
}

void EngineCore::setAutopanSettings(const AutopanSettings& settings) noexcept
{
    autopanSettings_.rateHz = juce::jlimit(0.01f, 20.0f, settings.rateHz);
    autopanSettings_.depth = juce::jlimit(0.0f, 1.0f, settings.depth);
}

void EngineCore::setSaturationSettings(const SaturationSettings& settings) noexcept
{
    saturationSettings_.drive = juce::jlimit(0.0f, 1.0f, settings.drive);
    saturationSettings_.mode = settings.mode;
}

int EngineCore::getLoadedPreloadSamples() const noexcept
{
    const auto programSnapshot = programSnapshot_.load(std::memory_order_acquire);
    const auto programAudioSnapshot = programAudioSnapshot_.load(std::memory_order_acquire);
    if (programSnapshot != nullptr)
        return programAudioSnapshot != nullptr ? countProgramSegmentSamples(*programAudioSnapshot, true) : 0;

    const auto segments = getSampleSegmentsSnapshot();
    return segments != nullptr ? segments->preloadData.getNumSamples() : 0;
}

int EngineCore::getLoadedStreamSamples() const noexcept
{
    const auto programSnapshot = programSnapshot_.load(std::memory_order_acquire);
    const auto programAudioSnapshot = programAudioSnapshot_.load(std::memory_order_acquire);
    if (programSnapshot != nullptr)
        return programAudioSnapshot != nullptr ? countProgramSegmentSamples(*programAudioSnapshot, false) : 0;

    const auto segments = getSampleSegmentsSnapshot();
    return segments != nullptr ? segments->getStreamNumSamples() : 0;
}

bool EngineCore::loadSampleFromFile(const juce::File& file)
{
    if (!file.existsAsFile())
        return false;

    const auto ext = file.getFileExtension().toLowerCase();
    if (ext == ".rex" || ext == ".rx2")
    {
        rex::DecodedLoop decoded;
        if (!rex::decodeFile(file, decoded))
            return false;

        if (decoded.audio.getNumSamples() <= 0 || decoded.audio.getNumChannels() <= 0)
            return false;

        const auto rootNote = rootMidiNote_;
        setSampleData(decoded.audio, decoded.sampleRateHz, rootNote);
        setAmpEnvelope(defaultAmpEnvelopeForLoadedSample());
        setAmpLfoSettings(defaultAmpLfoSettingsForLoadedSample());
        setPitchLfoSettings(defaultPitchLfoSettingsForLoadedSample());
        setFilterEnvelope(defaultFilterEnvelopeForLoadedSample());
        setFilterSettings(defaultFilterSettingsForLoadedSample());
        displaySampleData_ = decoded.audio;
        loadedSampleBitDepth_ = -1;
        loadedMetadataRootMidiNote_ = -1;
        loadedMetadataTempoBpm_ = 0.0;
        samplePath_ = file.getFullPathName();
        loadedSampleLoopFormatBadge_ = "REX";
        clearProgram();

        const auto fullEnd = juce::jmax(0, getTotalSampleLength() - 1);
        setSampleWindow(0, fullEnd);
        setLoopPoints(0, fullEnd);
        setPlaybackMode(PlaybackMode::loop);
        return true;
    }

    juce::AudioFormatManager formatManager;
    audio_file::registerAudioFormats(formatManager);

    auto openResult = audio_file::openReaderForFile(formatManager, file);
    auto reader = std::move(openResult.reader);
    if (reader == nullptr)
        return false;

    const auto lengthInSamples = static_cast<int>(reader->lengthInSamples);
    if (lengthInSamples <= 0)
        return false;

    juce::AudioBuffer<float> loaded(static_cast<int>(reader->numChannels), lengthInSamples);
    if (!reader->read(&loaded, 0, lengthInSamples, 0, true, true))
        return false;

    juce::AudioBuffer<float> mono(1, lengthInSamples);

    for (int sampleIndex = 0; sampleIndex < lengthInSamples; ++sampleIndex)
    {
        float sum = 0.0f;

        for (int channel = 0; channel < static_cast<int>(reader->numChannels); ++channel)
            sum += loaded.getSample(channel, sampleIndex);

        mono.setSample(0, sampleIndex, sum / static_cast<float>(reader->numChannels));
    }

    // Read embedded root note from WAV smpl chunk or AIFF INST chunk
    int embeddedRootNote = rootMidiNote_;
    const auto& metadata = reader->metadataValues;
    const auto loadedLoopFormatBadge = detectLoopFormatBadge(file, metadata);
    const auto parsedRootNote = findEmbeddedRootMidiNote(metadata);
    if (parsedRootNote.has_value())
        embeddedRootNote = *parsedRootNote;
    loadedMetadataRootMidiNote_ = parsedRootNote.has_value() ? *parsedRootNote : -1;
    const auto parsedTempoBpm = findEmbeddedTempoBpm(metadata);
    loadedMetadataTempoBpm_ = parsedTempoBpm.has_value() ? *parsedTempoBpm : 0.0;

    setSampleDataInternal(mono, reader->sampleRate, embeddedRootNote, openResult.readableFile);
    setAmpEnvelope(defaultAmpEnvelopeForLoadedSample());
    setAmpLfoSettings(defaultAmpLfoSettingsForLoadedSample());
    setPitchLfoSettings(defaultPitchLfoSettingsForLoadedSample());
    setFilterEnvelope(defaultFilterEnvelopeForLoadedSample());
    setFilterSettings(defaultFilterSettingsForLoadedSample());
    displaySampleData_ = loaded;
    loadedSampleBitDepth_ = static_cast<int>(reader->bitsPerSample);
    samplePath_ = file.getFullPathName();
    loadedSampleLoopFormatBadge_ = {};
    clearProgram();

    auto embeddedLoopRange = findEmbeddedLoopRange(metadata);
    if (!embeddedLoopRange.has_value() && ext == ".wav")
        embeddedLoopRange = findEmbeddedLoopRangeFromWavSmplChunk(file);

    if (embeddedLoopRange.has_value())
    {
        const auto [embeddedLoopStart, embeddedLoopEnd] = *embeddedLoopRange;
        const auto fullEnd = juce::jmax(0, getTotalSampleLength() - 1);
        setSampleWindow(0, fullEnd);
        setLoopPoints(embeddedLoopStart, embeddedLoopEnd);
        setPlaybackMode(PlaybackMode::loop);
        loadedSampleLoopFormatBadge_ = loadedLoopFormatBadge;
    }
    else
    {
        // No embedded loop metadata: reset loop region to full file
        const auto fullEnd = juce::jmax(0, getTotalSampleLength() - 1);
        setSampleWindow(0, fullEnd);
        setLoopPoints(0, fullEnd);
    }

    return true;
}

bool EngineCore::isRexRuntimeAvailable() const noexcept
{
    return rex::isRuntimeAvailable();
}

void EngineCore::setSampleData(const juce::AudioBuffer<float>& sampleData, const double sampleRate, const int rootNote) noexcept
{
    setSampleDataInternal(sampleData, sampleRate, rootNote, {});
}

void EngineCore::setSampleDataInternal(const juce::AudioBuffer<float>& sampleData,
                                      const double sampleRate,
                                      const int rootNote,
                                      const juce::File& backingFile) noexcept
{
    displaySampleData_ = sampleData;
    loadedSampleLoopFormatBadge_ = {};
    loadedSampleBitDepth_ = -1;
    loadedMetadataRootMidiNote_ = -1;
    loadedMetadataTempoBpm_ = 0.0;

    auto monoSample = sampleData;
    sampleDataRate_ = sampleRate > 0.0 ? sampleRate : sampleRate_;
    setRootMidiNote(rootNote);

    if (monoSample.getNumChannels() > 1)
    {
        juce::AudioBuffer<float> mono(1, monoSample.getNumSamples());

        for (int sampleIndex = 0; sampleIndex < monoSample.getNumSamples(); ++sampleIndex)
        {
            float sum = 0.0f;

            for (int channel = 0; channel < monoSample.getNumChannels(); ++channel)
                sum += monoSample.getSample(channel, sampleIndex);

            mono.setSample(0, sampleIndex, sum / static_cast<float>(monoSample.getNumChannels()));
        }

        monoSample = mono;
    }

    rebuildSampleSegments(monoSample, backingFile);

    if (getTotalSampleLength() <= 0)
        generateFallbackSample();

    setSampleWindow(0, getTotalSampleLength() - 1);
    setFadeSamples(0, 0);
    reversePlayback_ = false;
    setLoopPoints(0, getTotalSampleLength() - 1);
}

void EngineCore::setProgram(const Program& program)
{
    resetRoundRobinCursors();
    programAudioSnapshot_.store(nullptr, std::memory_order_release);
    programSnapshot_.store(
        std::make_shared<const ProgramSnapshot>(ProgramSnapshot::fromProgram(program)),
        std::memory_order_release);
}

void EngineCore::setProgram(const Program& program, const std::vector<juce::AudioBuffer<float>>& sampleDataByAsset)
{
    resetRoundRobinCursors();
    auto metadata = ProgramSnapshot::fromProgram(program);
    auto audio = std::make_shared<ProgramAudioSnapshot>();
    audio->sampleAssetCount = metadata.sampleAssetCount;

    const auto sampleCount = sampleDataByAsset.size() < metadata.sampleAssetCount
        ? sampleDataByAsset.size()
        : metadata.sampleAssetCount;

    for (std::size_t assetIndex = 0; assetIndex < sampleCount; ++assetIndex)
    {
        const auto& source = sampleDataByAsset[assetIndex];
        if (source.getNumChannels() <= 0 || source.getNumSamples() <= 0)
            continue;

        auto& asset = metadata.sampleAssets[assetIndex];
        asset.lengthSamples = source.getNumSamples();
        asset.numChannels = source.getNumChannels();
        if (asset.sampleRateHz <= 0.0)
            asset.sampleRateHz = sampleDataRate_ > 0.0 ? sampleDataRate_ : sampleRate_;

        const auto backingFilePath = juce::String::fromUTF8(program.sampleAssets[assetIndex].sourcePath.c_str());
        audio->sampleSegments[assetIndex] = buildSampleSegments(source, preloadSamples_, juce::File(backingFilePath));
    }

    for (std::size_t zoneIndex = 0; zoneIndex < metadata.zoneCount; ++zoneIndex)
    {
        const auto& zone = metadata.zones[zoneIndex];
        if (!metadata.isZoneSampleIndexValid(zone))
            continue;

        const auto* segments = audio->getSampleSegments(zone.sampleAssetIndex);
        if (segments == nullptr)
            continue;

        segments->requestPrimeForAbsoluteSample(zone.sampleStart);
        const auto primed = segments->servicePendingPrime();
        juce::ignoreUnused(primed);
    }

    programAudioSnapshot_.store(audio, std::memory_order_release);
    programSnapshot_.store(std::make_shared<const ProgramSnapshot>(metadata), std::memory_order_release);
}

void EngineCore::clearProgram() noexcept
{
    resetRoundRobinCursors();
    programSnapshot_.store(nullptr, std::memory_order_release);
    programAudioSnapshot_.store(nullptr, std::memory_order_release);
}

bool EngineCore::hasProgram() const noexcept
{
    return programSnapshot_.load(std::memory_order_acquire) != nullptr;
}

int EngineCore::getProgramZoneCount() const noexcept
{
    const auto snapshot = programSnapshot_.load(std::memory_order_acquire);
    return snapshot != nullptr ? static_cast<int>(snapshot->zoneCount) : 0;
}

void EngineCore::setPreloadSamples(const int preloadSamples) noexcept
{
    preloadSamples_ = juce::jmax(256, preloadSamples);
    auto rebuiltSingleSampleSegments = false;

    const auto segments = getSampleSegmentsSnapshot();
    if (segments != nullptr && getTotalSampleLength(*segments) > 0)
    {
        rebuildSampleSegments(materializeSampleData(*segments), juce::File(segments->backingFilePath));
        rebuiltSingleSampleSegments = true;
    }

    const auto programAudioSnapshot = programAudioSnapshot_.load(std::memory_order_acquire);
    if (programAudioSnapshot != nullptr)
    {
        ++segmentRebuildCount_;
        programAudioSnapshot_.store(rebuildProgramAudioSnapshot(*programAudioSnapshot), std::memory_order_release);
    }

    if (rebuiltSingleSampleSegments)
    {
        setSampleWindow(sampleWindowStart_, sampleWindowEnd_);
        setFadeSamples(fadeInSamples_, fadeOutSamples_);
        setLoopPoints(loopStartSample_, loopEndSample_);
    }
}

void EngineCore::serviceStreamPriming()
{
    const auto programSnapshot = programSnapshot_.load(std::memory_order_acquire);
    if (programSnapshot != nullptr)
    {
        const auto programAudioSnapshot = programAudioSnapshot_.load(std::memory_order_acquire);
        if (programAudioSnapshot == nullptr)
            return;

        for (std::size_t assetIndex = 0; assetIndex < programAudioSnapshot->sampleAssetCount; ++assetIndex)
        {
            if (const auto* segments = programAudioSnapshot->getSampleSegments(static_cast<int>(assetIndex)); segments != nullptr)
            {
                const auto serviced = segments->servicePendingPrime();
                juce::ignoreUnused(serviced);
            }
        }

        return;
    }

    if (const auto segments = getSampleSegmentsSnapshot())
    {
        const auto serviced = segments->servicePendingPrime();
        juce::ignoreUnused(serviced);
    }
}

int EngineCore::getStreamPrimeRequestCount() const noexcept
{
    const auto programSnapshot = programSnapshot_.load(std::memory_order_acquire);
    if (programSnapshot != nullptr)
    {
        auto total = 0;
        const auto programAudioSnapshot = programAudioSnapshot_.load(std::memory_order_acquire);
        if (programAudioSnapshot == nullptr)
            return 0;

        for (std::size_t assetIndex = 0; assetIndex < programAudioSnapshot->sampleAssetCount; ++assetIndex)
        {
            if (const auto* segments = programAudioSnapshot->getSampleSegments(static_cast<int>(assetIndex)); segments != nullptr)
                total += segments->getPrimeRequestCount();
        }

        return total;
    }

    const auto segments = getSampleSegmentsSnapshot();
    return segments != nullptr ? segments->getPrimeRequestCount() : 0;
}

int EngineCore::getStreamPrimeCacheHitCount() const noexcept
{
    const auto programSnapshot = programSnapshot_.load(std::memory_order_acquire);
    if (programSnapshot != nullptr)
    {
        auto total = 0;
        const auto programAudioSnapshot = programAudioSnapshot_.load(std::memory_order_acquire);
        if (programAudioSnapshot == nullptr)
            return 0;

        for (std::size_t assetIndex = 0; assetIndex < programAudioSnapshot->sampleAssetCount; ++assetIndex)
        {
            if (const auto* segments = programAudioSnapshot->getSampleSegments(static_cast<int>(assetIndex)); segments != nullptr)
                total += segments->getPrimeCacheHitCount();
        }

        return total;
    }

    const auto segments = getSampleSegmentsSnapshot();
    return segments != nullptr ? segments->getPrimeCacheHitCount() : 0;
}

int EngineCore::getStreamPrimeCacheMissCount() const noexcept
{
    const auto programSnapshot = programSnapshot_.load(std::memory_order_acquire);
    if (programSnapshot != nullptr)
    {
        auto total = 0;
        const auto programAudioSnapshot = programAudioSnapshot_.load(std::memory_order_acquire);
        if (programAudioSnapshot == nullptr)
            return 0;

        for (std::size_t assetIndex = 0; assetIndex < programAudioSnapshot->sampleAssetCount; ++assetIndex)
        {
            if (const auto* segments = programAudioSnapshot->getSampleSegments(static_cast<int>(assetIndex)); segments != nullptr)
                total += segments->getPrimeCacheMissCount();
        }

        return total;
    }

    const auto segments = getSampleSegmentsSnapshot();
    return segments != nullptr ? segments->getPrimeCacheMissCount() : 0;
}

int EngineCore::getStreamPrimeServiceCount() const noexcept
{
    const auto programSnapshot = programSnapshot_.load(std::memory_order_acquire);
    if (programSnapshot != nullptr)
    {
        auto total = 0;
        const auto programAudioSnapshot = programAudioSnapshot_.load(std::memory_order_acquire);
        if (programAudioSnapshot == nullptr)
            return 0;

        for (std::size_t assetIndex = 0; assetIndex < programAudioSnapshot->sampleAssetCount; ++assetIndex)
        {
            if (const auto* segments = programAudioSnapshot->getSampleSegments(static_cast<int>(assetIndex)); segments != nullptr)
                total += segments->getPrimeServiceCount();
        }

        return total;
    }

    const auto segments = getSampleSegmentsSnapshot();
    return segments != nullptr ? segments->getPrimeServiceCount() : 0;
}

int EngineCore::getLoadedSampleLength() const noexcept
{
    return getTotalSampleLength();
}

int EngineCore::getLoadedSampleChannels() const noexcept
{
    return juce::jmax(1, displaySampleData_.getNumChannels());
}

void EngineCore::setSampleWindow(const int startSample, const int endSample) noexcept
{
    const auto totalSamples = getTotalSampleLength();
    const auto maxValid = juce::jmax(0, totalSamples - 1);

    sampleWindowStart_ = juce::jlimit(0, maxValid, startSample);
    sampleWindowEnd_ = juce::jlimit(0, maxValid, endSample);

    if (sampleWindowEnd_ <= sampleWindowStart_)
        sampleWindowEnd_ = maxValid;
}

void EngineCore::setFadeSamples(const int fadeInSamples, const int fadeOutSamples) noexcept
{
    const auto maxFade = juce::jmax(0, getEffectivePlaybackLength() - 1);
    fadeInSamples_ = juce::jlimit(0, maxFade, fadeInSamples);
    fadeOutSamples_ = juce::jlimit(0, maxFade, fadeOutSamples);
}

void EngineCore::noteOn(const int noteNumber, const float velocity, const int sampleOffsetInBlock) noexcept
{
    enqueuePendingEvent(EventType::noteOn,
        juce::jlimit(0, 127, noteNumber),
        juce::jlimit(0.0f, 1.0f, velocity),
        juce::jmax(0, sampleOffsetInBlock));
}

void EngineCore::noteOff(const int noteNumber, const int sampleOffsetInBlock) noexcept
{
    const auto accepted = enqueuePendingEvent(EventType::noteOff,
        juce::jlimit(0, 127, noteNumber),
        0.0f,
        juce::jmax(0, sampleOffsetInBlock));

    if (!accepted)
        releaseVoicesForNote(juce::jlimit(0, 127, noteNumber));
}

void EngineCore::pitchBend(const int pitchWheelValue, const int sampleOffsetInBlock) noexcept
{
    const auto clamped = juce::jlimit(0, 16383, pitchWheelValue);
    const auto normalized = (static_cast<float>(clamped) - 8192.0f) / 8192.0f;
    enqueuePendingEvent(EventType::pitchBend,
        0,
        juce::jlimit(-1.0f, 1.0f, normalized),
        juce::jmax(0, sampleOffsetInBlock));
}

void EngineCore::channelPressure(const int pressureValue, const int sampleOffsetInBlock) noexcept
{
    enqueuePendingEvent(EventType::channelPressure,
        0,
        static_cast<float>(juce::jlimit(0, 127, pressureValue)) / 127.0f,
        juce::jmax(0, sampleOffsetInBlock));
}

void EngineCore::render(float** outputs, const int numChannels, const int numSamples) noexcept
{
    if (outputs == nullptr || numChannels <= 0 || numSamples <= 0)
        return;

    const auto segments = getSampleSegmentsSnapshot();
    if (segments == nullptr)
        return;

    for (int channel = 0; channel < numChannels; ++channel)
        juce::FloatVectorOperations::clear(outputs[channel], numSamples);

    if (numSamples > mixBuffer_.getNumSamples()
        || numSamples > voiceScratchBuffer_.getNumSamples()
        || numSamples > modulationScratchBuffer_.getNumSamples())
    {
        return;
    }

    auto* mixLeft = mixBuffer_.getWritePointer(0);
    auto* mixRight = mixBuffer_.getWritePointer(1);
    juce::FloatVectorOperations::clear(mixLeft, numSamples);
    juce::FloatVectorOperations::clear(mixRight, numSamples);

    const auto programSnapshot = programSnapshot_.load(std::memory_order_acquire);
    const auto* program = programSnapshot != nullptr && programSnapshot->hasPlayableZones()
        ? programSnapshot.get()
        : nullptr;
    const auto programAudioSnapshot = programAudioSnapshot_.load(std::memory_order_acquire);
    const auto* programAudio = programAudioSnapshot.get();

    sortPendingEventsByOffset();

    auto segmentStart = 0;
    while (segmentStart < numSamples)
    {
        flushPendingEventsAtOffset(segmentStart, program, programAudio);

        const auto segmentEnd = findNextPendingEventOffset(segmentStart, numSamples);
        const auto segmentSamples = segmentEnd - segmentStart;
        if (segmentSamples > 0)
        {
            fillGlobalModulationBuffers(segmentSamples);
            renderActiveVoices(segmentStart, segmentSamples, *segments, programAudio);
        }

        segmentStart = segmentEnd;
    }

    for (int sampleIndex = 0; sampleIndex < numSamples; ++sampleIndex)
    {
        mixLeft[sampleIndex] = processSaturationSample(mixLeft[sampleIndex]);
        mixRight[sampleIndex] = processSaturationSample(mixRight[sampleIndex]);
    }

    auto* monoScratch = voiceScratchBuffer_.getWritePointer(0);

    if (numChannels == 1)
    {
        juce::FloatVectorOperations::copy(monoScratch, mixLeft, numSamples);
        juce::FloatVectorOperations::add(monoScratch, mixRight, numSamples);
        juce::FloatVectorOperations::multiply(monoScratch, 0.5f, numSamples);
        juce::FloatVectorOperations::copy(outputs[0], monoScratch, numSamples);
    }
    else
    {
        juce::FloatVectorOperations::copy(outputs[0], mixLeft, numSamples);
        juce::FloatVectorOperations::copy(outputs[1], mixRight, numSamples);

        if (numChannels > 2)
        {
            juce::FloatVectorOperations::copy(monoScratch, mixLeft, numSamples);
            juce::FloatVectorOperations::add(monoScratch, mixRight, numSamples);
            juce::FloatVectorOperations::multiply(monoScratch, 0.5f, numSamples);

            for (int channel = 2; channel < numChannels; ++channel)
                juce::FloatVectorOperations::copy(outputs[channel], monoScratch, numSamples);
        }
    }

    if (numChannels >= 2)
    {
        const auto clampedPan = juce::jlimit(-1.0f, 1.0f, pan_);
        const auto autopanRateHz = juce::jlimit(0.01f, 20.0f, autopanSettings_.rateHz);
        const auto autopanDepth = juce::jlimit(0.0f, 1.0f, autopanSettings_.depth);
        const auto phaseInc = autopanRateHz / static_cast<float>(sampleRate_);

        for (int sampleIndex = 0; sampleIndex < numSamples; ++sampleIndex)
        {
            const auto autopanOffset = std::sin(2.0f * juce::MathConstants<float>::pi * autopanPhase_) * autopanDepth;
            const auto effectivePan = juce::jlimit(-1.0f, 1.0f, clampedPan + autopanOffset);
            const auto leftGain = effectivePan > 0.0f ? (1.0f - effectivePan) : 1.0f;
            const auto rightGain = effectivePan < 0.0f ? (1.0f + effectivePan) : 1.0f;

            outputs[0][sampleIndex] *= leftGain;
            outputs[1][sampleIndex] *= rightGain;

            autopanPhase_ += phaseInc;
            if (autopanPhase_ >= 1.0f)
                autopanPhase_ -= std::floor(autopanPhase_);
        }
    }

    if (reverbMix_ > 0.0001f)
    {
        if (numChannels >= 2)
            reverb_.processStereo(outputs[0], outputs[1], numSamples);
        else if (numChannels == 1)
            reverb_.processMono(outputs[0], numSamples);
    }

    processDelay(outputs, numChannels, numSamples);
    processDcFilter(outputs, numChannels, numSamples);

    if (masterVolume_ < 0.9999f)
    {
        for (int channel = 0; channel < numChannels; ++channel)
            juce::FloatVectorOperations::multiply(outputs[channel], masterVolume_, numSamples);
    }

    pendingEventCount_ = 0;
}

int EngineCore::findNextPendingEventOffset(const int currentOffset, const int blockEnd) const noexcept
{
    auto nextOffset = blockEnd;
    for (int eventIndex = 0; eventIndex < pendingEventCount_; ++eventIndex)
    {
        const auto eventOffset = pendingEvents_[static_cast<std::size_t>(eventIndex)].offset;
        if (eventOffset > currentOffset && eventOffset < nextOffset)
            nextOffset = eventOffset;
    }

    return nextOffset;
}

int EngineCore::getGlobalModulationBlockSize(const int remainingSamples) const noexcept
{
    const auto isSmoothLfoShape = [](const FilterSettings::LfoShape shape) noexcept
    {
        return shape == FilterSettings::LfoShape::sine
            || shape == FilterSettings::LfoShape::triangle;
    };

    const auto globalFilterLfoActive = !filterSettings_.lfoRetrigger
        && filterSettings_.lfoRateHz > 0.0f
        && std::abs(filterSettings_.lfoAmountHz) > 0.0001f;
    const auto ampLfoActive = ampLfoSettings_.rateHz > 0.0f
        && ampLfoSettings_.depth > 0.0001f;

    if ((globalFilterLfoActive && !isSmoothLfoShape(filterSettings_.lfoShape))
        || (ampLfoActive && !isSmoothLfoShape(ampLfoSettings_.shape)))
    {
        return 1;
    }

    return juce::jmax(1, juce::jmin(kRenderControlBlockSize, remainingSamples));
}

EngineCore::GlobalModulationSpan EngineCore::advanceGlobalModulationSpan(const int numSamples) noexcept
{
    GlobalModulationSpan span;
    if (numSamples <= 0)
        return span;

    ModulationContribution globalModulation;
    const std::array<ModulationSourceValue, 2 + kMacroControlCount> globalModulationSources{{
        { &modulationRoutingSettings_.modWheel, currentModWheelValue_ },
        { &modulationRoutingSettings_.aftertouch, currentAftertouchValue_ },
        { &modulationRoutingSettings_.macros[0], macroControlValues_[0] },
        { &modulationRoutingSettings_.macros[1], macroControlValues_[1] }
    }};
    accumulateModulationSources(globalModulation, globalModulationSources);

    const auto lastSampleIndex = juce::jmax(0, numSamples - 1);
    const auto pitchBendRatio = semitonesToRatio(currentPitchBendSemitones_);
    const auto pitchModSemitones = globalModulation.pitchCents / 100.0f;
    const auto hasActivePitchLfo = pitchLfoSettings_.rateHz > 0.0f && pitchLfoSettings_.depthCents > 0.0001f;
    if (hasActivePitchLfo)
    {
        const auto phaseIncrement = pitchLfoSettings_.rateHz / static_cast<float>(sampleRate_);
        const auto startWave = computeLfoWave(FilterSettings::LfoShape::sine, globalPitchLfoPhase_);
        const auto endWave = computeLfoWave(FilterSettings::LfoShape::sine,
            advanceWrappedPhase(globalPitchLfoPhase_, phaseIncrement, lastSampleIndex));
        span.start.combinedPitchRatio = pitchBendRatio
            * semitonesToRatio(pitchModSemitones + (pitchLfoSettings_.depthCents / 100.0f) * startWave);
        span.end.combinedPitchRatio = pitchBendRatio
            * semitonesToRatio(pitchModSemitones + (pitchLfoSettings_.depthCents / 100.0f) * endWave);
        globalPitchLfoPhase_ = advanceWrappedPhase(globalPitchLfoPhase_, phaseIncrement, numSamples);
    }
    else
    {
        span.start.combinedPitchRatio = pitchBendRatio * semitonesToRatio(pitchModSemitones);
        span.end.combinedPitchRatio = span.start.combinedPitchRatio;
    }

    span.start.filterLfoActive = filterSettings_.lfoRateHz > 0.0f && std::abs(filterSettings_.lfoAmountHz) > 0.0001f;
    span.end.filterLfoActive = span.start.filterLfoActive;
    if (span.start.filterLfoActive)
    {
        const auto phaseIncrement = filterSettings_.lfoRateHz / static_cast<float>(sampleRate_);
        span.start.filterLfoValue = computeLfoWave(filterSettings_.lfoShape, globalFilterLfoPhase_);
        span.end.filterLfoValue = computeLfoWave(filterSettings_.lfoShape,
            advanceWrappedPhase(globalFilterLfoPhase_, phaseIncrement, lastSampleIndex));
        globalFilterLfoPhase_ = advanceWrappedPhase(globalFilterLfoPhase_, phaseIncrement, numSamples);
    }

    const auto hasActiveAmpLfo = ampLfoSettings_.rateHz > 0.0f && ampLfoSettings_.depth > 0.0001f;
    if (hasActiveAmpLfo)
    {
        const auto phaseIncrement = ampLfoSettings_.rateHz / static_cast<float>(sampleRate_);
        const auto startWave = computeLfoWave(ampLfoSettings_.shape, globalAmpLfoPhase_);
        const auto endWave = computeLfoWave(ampLfoSettings_.shape,
            advanceWrappedPhase(globalAmpLfoPhase_, phaseIncrement, lastSampleIndex));
        const auto startUnipolar = 0.5f * (startWave + 1.0f);
        const auto endUnipolar = 0.5f * (endWave + 1.0f);
        span.start.ampLfoGain = (1.0f - ampLfoSettings_.depth) + (ampLfoSettings_.depth * startUnipolar);
        span.end.ampLfoGain = (1.0f - ampLfoSettings_.depth) + (ampLfoSettings_.depth * endUnipolar);
        globalAmpLfoPhase_ = advanceWrappedPhase(globalAmpLfoPhase_, phaseIncrement, numSamples);
    }

    span.start.ampLfoGain *= juce::jmax(0.0f, 1.0f + globalModulation.ampDelta);
    span.end.ampLfoGain *= juce::jmax(0.0f, 1.0f + globalModulation.ampDelta);
    span.start.filterCutoffOffsetHz = globalModulation.filterHz;
    span.end.filterCutoffOffsetHz = globalModulation.filterHz;

    return span;
}

void EngineCore::fillGlobalModulationBuffers(const int numSamples) noexcept
{
    auto* pitchRatios = modulationScratchBuffer_.getWritePointer(0);
    auto* ampGains = modulationScratchBuffer_.getWritePointer(1);
    auto* filterOffsets = modulationScratchBuffer_.getWritePointer(2);
    auto* filterLfoValues = modulationScratchBuffer_.getWritePointer(3);

    auto sampleIndex = 0;
    while (sampleIndex < numSamples)
    {
        const auto chunkSize = getGlobalModulationBlockSize(numSamples - sampleIndex);
        const auto span = advanceGlobalModulationSpan(chunkSize);

        for (int chunkSample = 0; chunkSample < chunkSize; ++chunkSample)
        {
            const auto writeIndex = sampleIndex + chunkSample;
            pitchRatios[writeIndex] = interpolateSpanValue(
                span.start.combinedPitchRatio,
                span.end.combinedPitchRatio,
                chunkSample,
                chunkSize);
            ampGains[writeIndex] = interpolateSpanValue(
                span.start.ampLfoGain,
                span.end.ampLfoGain,
                chunkSample,
                chunkSize);
            filterOffsets[writeIndex] = interpolateSpanValue(
                span.start.filterCutoffOffsetHz,
                span.end.filterCutoffOffsetHz,
                chunkSample,
                chunkSize);
            filterLfoValues[writeIndex] = interpolateSpanValue(
                span.start.filterLfoValue,
                span.end.filterLfoValue,
                chunkSample,
                chunkSize);
        }

        sampleIndex += chunkSize;
    }
}

EngineCore::ModulationValueSpan EngineCore::advanceVoiceRetriggeredFilterLfoSpan(VoiceState& voice,
                                                                                  const int numSamples) noexcept
{
    ModulationValueSpan span;
    if (numSamples <= 0)
        return span;

    const auto filterLfoActive = filterSettings_.lfoRetrigger
        && filterSettings_.lfoRateHz > 0.0f
        && std::abs(filterSettings_.lfoAmountHz) > 0.0001f;
    if (!filterLfoActive)
        return span;

    auto lfoRateKeyTrackRatio = 1.0f;
    if (!filterSettings_.lfoTempoSync || filterSettings_.lfoRateKeytrackInTempoSync)
    {
        const auto noteSemitoneOffset = static_cast<float>(voice.noteNumber - voice.rootMidiNote);
        if (filterSettings_.lfoKeytrackLinear)
        {
            lfoRateKeyTrackRatio = juce::jmax(0.0f,
                1.0f + (noteSemitoneOffset / 12.0f) * filterSettings_.lfoRateKeyTracking);
        }
        else
        {
            lfoRateKeyTrackRatio = std::pow(2.0f,
                (noteSemitoneOffset / 12.0f) * filterSettings_.lfoRateKeyTracking);
        }
    }

    const auto trackedLfoRateHz = juce::jlimit(0.0f, 40.0f,
        filterSettings_.lfoRateHz * lfoRateKeyTrackRatio);
    const auto phaseIncrement = trackedLfoRateHz / static_cast<float>(sampleRate_);
    const auto lastSampleIndex = juce::jmax(0, numSamples - 1);
    span.start = computeLfoWave(filterSettings_.lfoShape, voice.filterLfoPhase);
    span.end = computeLfoWave(filterSettings_.lfoShape,
        advanceWrappedPhase(voice.filterLfoPhase, phaseIncrement, lastSampleIndex));
    voice.filterLfoPhase = advanceWrappedPhase(voice.filterLfoPhase, phaseIncrement, numSamples);

    if (filterSettings_.lfoUnipolar)
    {
        span.start = 0.5f * (span.start + 1.0f);
        span.end = 0.5f * (span.end + 1.0f);
    }

    if (voice.filterLfoFadeSamplesTotal > 0 && voice.filterLfoFadeSamplesRemaining > 0)
    {
        const auto remainingStart = voice.filterLfoFadeSamplesRemaining;
        const auto remainingEnd = juce::jmax(0, remainingStart - lastSampleIndex);
        const auto total = static_cast<float>(voice.filterLfoFadeSamplesTotal);
        const auto startScale = juce::jlimit(0.0f, 1.0f,
            1.0f - (static_cast<float>(remainingStart) / total));
        const auto endScale = juce::jlimit(0.0f, 1.0f,
            1.0f - (static_cast<float>(remainingEnd) / total));
        span.start *= startScale;
        span.end *= endScale;
        voice.filterLfoFadeSamplesRemaining = juce::jmax(0, remainingStart - numSamples);
    }

    return span;
}

EngineCore::ModulationValueSpan EngineCore::advanceVoiceGlideSpan(VoiceState& voice,
                                                                  const int numSamples) noexcept
{
    ModulationValueSpan span;
    if (numSamples <= 0)
    {
        span.start = voice.sampleIncrement;
        span.end = voice.sampleIncrement;
        return span;
    }

    if (voice.glideSamplesRemaining <= 0)
    {
        span.start = voice.sampleIncrement;
        span.end = voice.sampleIncrement;
        return span;
    }

    const auto glideStep = (voice.glideTargetIncrement - voice.sampleIncrement)
        / static_cast<float>(voice.glideSamplesRemaining);
    span.start = voice.sampleIncrement + glideStep;
    span.end = voice.sampleIncrement + glideStep * static_cast<float>(numSamples);
    voice.sampleIncrement = span.end;
    voice.glideSamplesRemaining = juce::jmax(0, voice.glideSamplesRemaining - numSamples);
    if (voice.glideSamplesRemaining == 0)
    {
        voice.sampleIncrement = voice.glideTargetIncrement;
        span.end = voice.sampleIncrement;
    }

    return span;
}

int EngineCore::getVoiceRenderBlockSize(const VoiceState& voice, const int remainingSamples) const noexcept
{
    auto chunkSize = juce::jmax(1, juce::jmin(kRenderControlBlockSize, remainingSamples));
    const auto retriggeredFilterLfoActive = filterSettings_.lfoRetrigger
        && filterSettings_.lfoRateHz > 0.0f
        && std::abs(filterSettings_.lfoAmountHz) > 0.0001f;
    const auto smoothRetriggeredShape = filterSettings_.lfoShape == FilterSettings::LfoShape::sine
        || filterSettings_.lfoShape == FilterSettings::LfoShape::triangle;

    if (retriggeredFilterLfoActive && !smoothRetriggeredShape)
        chunkSize = 1;

    if (voice.glideSamplesRemaining > 0)
        chunkSize = juce::jmin(chunkSize, voice.glideSamplesRemaining);

    if (retriggeredFilterLfoActive && voice.filterLfoFadeSamplesRemaining > 0)
        chunkSize = juce::jmin(chunkSize, voice.filterLfoFadeSamplesRemaining);

    return juce::jmax(1, chunkSize);
}

void EngineCore::renderActiveVoices(const int startSample,
                                    const int numSamples,
                                    const SampleSegments& fallbackSegments,
                                    const ProgramAudioSnapshot* const programAudio) noexcept
{
    auto* mixLeft = mixBuffer_.getWritePointer(0) + startSample;
    auto* mixRight = mixBuffer_.getWritePointer(1) + startSample;
    const auto* globalPitchRatios = modulationScratchBuffer_.getReadPointer(0);
    const auto* globalAmpGains = modulationScratchBuffer_.getReadPointer(1);
    const auto* globalFilterOffsets = modulationScratchBuffer_.getReadPointer(2);
    const auto* globalFilterLfoValues = modulationScratchBuffer_.getReadPointer(3);
    const auto requestLookaheadPrime = startSample == 0;
    const auto filterLfoActive = filterSettings_.lfoRateHz > 0.0f && std::abs(filterSettings_.lfoAmountHz) > 0.0001f;
    const auto useRetriggeredFilterLfo = filterLfoActive && filterSettings_.lfoRetrigger;
    const auto useUnipolarFilterLfo = filterLfoActive && filterSettings_.lfoUnipolar;
    const auto useSecondFilterStage = filterSettings_.mode == FilterSettings::Mode::lowPass24
        || filterSettings_.mode == FilterSettings::Mode::highPass24;
    const auto useNotchOutput = filterSettings_.mode == FilterSettings::Mode::notch12;
    const auto resonanceQ = computeFilterResonanceQ();
    const auto maxCutoffHz = static_cast<float>(sampleRate_ * 0.45);

    for (int voiceIndex = 0; voiceIndex < static_cast<int>(VoicePool::maxVoices); ++voiceIndex)
    {
        if (!voicePool_.isActive(voiceIndex))
            continue;

        auto& voice = voices_[static_cast<std::size_t>(voiceIndex)];
        const auto* voiceSegments = programAudio != nullptr
            ? programAudio->getSampleSegments(voice.sampleAssetIndex)
            : nullptr;
        if (voiceSegments == nullptr)
            voiceSegments = &fallbackSegments;

        if (voiceSegments == nullptr)
        {
            voicePool_.stopVoiceAtIndex(voiceIndex);
            voice.noteNumber = -1;
            voice.noteHeld = false;
            voice.ampEnvelope.reset();
            voice.filterEnvelope.reset();
            continue;
        }

        const auto totalSampleLength = getTotalSampleLength(*voiceSegments);
        if (totalSampleLength <= 0)
        {
            voicePool_.stopVoiceAtIndex(voiceIndex);
            voice.noteNumber = -1;
            voice.noteHeld = false;
            voice.ampEnvelope.reset();
            voice.filterEnvelope.reset();
            continue;
        }

        const auto useZoneWindow = voice.sampleEndExclusive > voice.sampleStart;
        const auto requestedSampleStart = useZoneWindow ? voice.sampleStart : sampleWindowStart_;
        const auto requestedSampleEnd = useZoneWindow ? (voice.sampleEndExclusive - 1) : sampleWindowEnd_;
        const auto playbackStartSampleIndex = juce::jlimit(0, totalSampleLength - 1, requestedSampleStart);
        const auto playbackEndSampleIndex = juce::jlimit(playbackStartSampleIndex, totalSampleLength - 1, requestedSampleEnd);
        const auto sampleLength = playbackEndSampleIndex - playbackStartSampleIndex + 1;
        if (sampleLength <= 1)
        {
            voicePool_.stopVoiceAtIndex(voiceIndex);
            voice.noteNumber = -1;
            voice.noteHeld = false;
            voice.ampEnvelope.reset();
            voice.filterEnvelope.reset();
            continue;
        }

        const auto sampleLengthMinusOne = sampleLength - 1;
        const auto maxSamplePosition = static_cast<float>(sampleLengthMinusOne);
        const auto reversePlayback = reversePlayback_;
        const auto preloadSamples = voiceSegments->preloadData.getNumSamples();
        const auto preloadChannelCount = voiceSegments->preloadData.getNumChannels();
        const auto streamChannelCount = voiceSegments->getStreamNumChannels();
        const auto preloadRightChannel = preloadChannelCount > 1 ? 1 : 0;
        const auto streamRightChannel = streamChannelCount > 1 ? 1 : 0;
        const auto* preloadLeftData = preloadChannelCount > 0 ? voiceSegments->preloadData.getReadPointer(0) : nullptr;
        const auto* preloadRightData = preloadChannelCount > 0 ? voiceSegments->preloadData.getReadPointer(preloadRightChannel) : nullptr;
        const auto hasStreamSource = voiceSegments->streamSource != nullptr && streamChannelCount > 0;
        const auto fadeInScale = fadeInSamples_ > 0 ? 1.0f / static_cast<float>(fadeInSamples_) : 0.0f;
        const auto fadeOutScale = fadeOutSamples_ > 0 ? 1.0f / static_cast<float>(fadeOutSamples_) : 0.0f;
        const auto fadeInLimit = static_cast<float>(fadeInSamples_);
        const auto fadeOutStart = static_cast<float>(juce::jmax(0, sampleLengthMinusOne - fadeOutSamples_));

        const auto mapPlaybackIndexToVoiceSampleIndex = [&](const int playbackIndex) noexcept
        {
            return reversePlayback
            ? (playbackEndSampleIndex - playbackIndex)
            : (playbackStartSampleIndex + playbackIndex);
        };

        const auto mapVoiceSampleIndexToPlaybackIndex = [&](const int sampleIndex) noexcept
        {
            const auto clampedSample = juce::jlimit(playbackStartSampleIndex, playbackEndSampleIndex, sampleIndex);
            return reversePlayback
                ? (playbackEndSampleIndex - clampedSample)
                : (clampedSample - playbackStartSampleIndex);
        };

        const auto readMappedSample = [&](const int playbackIndex, const int channel) noexcept
        {
            const auto absoluteIndex = mapPlaybackIndexToVoiceSampleIndex(playbackIndex);

            if (absoluteIndex < preloadSamples)
            {
                const auto* preloadData = channel == 0 ? preloadLeftData : preloadRightData;
                return preloadData != nullptr ? preloadData[absoluteIndex] : 0.0f;
            }

            if (hasStreamSource)
                return voiceSegments->streamSource->readSample(channel == 0 ? 0 : streamRightChannel, absoluteIndex - preloadSamples);

            return 0.0f;
        };

        const auto computeVoiceEditGain = [&](const float playbackPosition) noexcept
        {
            auto gain = 1.0f;

            if (fadeInSamples_ > 0 && playbackPosition < fadeInLimit)
                gain = playbackPosition * fadeInScale;

            if (fadeOutSamples_ > 0 && playbackPosition >= fadeOutStart)
            {
                const auto remaining = maxSamplePosition - playbackPosition;
                const auto fadeOutGain = remaining * fadeOutScale;
                if (fadeOutGain < gain)
                    gain = fadeOutGain;
            }

            return gain;
        };

        auto effectiveLoopStart = 0;
        auto effectiveLoopEnd = sampleLengthMinusOne;
        const auto hasProgramZone = voice.zoneIndex >= 0;
        const auto zoneLoopEnabled = hasProgramZone && voice.loopMode != ZoneLoopMode::noLoop;
        const auto globalLoopEnabled = !hasProgramZone && playbackMode_ == PlaybackMode::loop;
        const auto loopEnabled = zoneLoopEnabled || globalLoopEnabled;

        if (loopEnabled)
        {
            const auto useZoneLoopRange = zoneLoopEnabled && voice.loopEndExclusive > voice.loopStart;
            if (useZoneLoopRange || globalLoopEnabled)
            {
                const auto loopStart = useZoneLoopRange ? voice.loopStart : loopStartSample_;
                const auto loopEnd = useZoneLoopRange ? (voice.loopEndExclusive - 1) : loopEndSample_;
                const auto rawLoopStart = mapVoiceSampleIndexToPlaybackIndex(loopStart);
                const auto rawLoopEnd = mapVoiceSampleIndexToPlaybackIndex(loopEnd);

                effectiveLoopStart = juce::jlimit(0, sampleLengthMinusOne, juce::jmin(rawLoopStart, rawLoopEnd));
                effectiveLoopEnd = juce::jlimit(0, sampleLengthMinusOne, juce::jmax(rawLoopStart, rawLoopEnd));
                if (effectiveLoopEnd <= effectiveLoopStart)
                {
                    effectiveLoopStart = 0;
                    effectiveLoopEnd = sampleLengthMinusOne;
                }
            }
        }

        const auto shouldLoopNow = globalLoopEnabled
            ? voice.noteHeld
            : zoneLoopEnabled && (voice.noteHeld || voice.loopMode == ZoneLoopMode::continuous);
        const auto loopLength = juce::jmax(1, effectiveLoopEnd - effectiveLoopStart + 1);
        const auto crossfadeSamples = shouldLoopNow && loopCrossfadeSamples_ > 0
            ? juce::jlimit(0, juce::jmax(0, loopLength / 2), loopCrossfadeSamples_)
            : 0;
        const auto crossfadeStart = static_cast<float>(effectiveLoopEnd - crossfadeSamples);
        const auto crossfadeScale = crossfadeSamples > 0 ? 1.0f / static_cast<float>(crossfadeSamples) : 0.0f;

        if (requestLookaheadPrime && voiceSegments->getStreamNumSamples() > 0)
        {
            const auto lookaheadSamples = juce::jmax(512,
                juce::jmin(4096, juce::jmax(0, voiceSegments->preloadData.getNumSamples())));
            const auto lookaheadPlaybackIndex = juce::jmin(sampleLengthMinusOne,
                static_cast<int>(voice.samplePosition) + lookaheadSamples);
            const auto lookaheadSampleIndex = mapPlaybackIndexToVoiceSampleIndex(lookaheadPlaybackIndex);
            voiceSegments->requestPrimeForAbsoluteSample(lookaheadSampleIndex);
        }

        const auto mappedVelocity = mapVelocity(voice.velocity);
        ModulationContribution velocityModulation;
        const std::array<ModulationSourceValue, 1> voiceModulationSources{{
            { &modulationRoutingSettings_.velocity, voice.velocity }
        }};
        accumulateModulationSources(velocityModulation, voiceModulationSources);

        const auto velocityAmpGain = mappedVelocity * juce::jmax(0.0f, 1.0f + velocityModulation.ampDelta);
        const auto velocityPitchRatio = centsToRatio(velocityModulation.pitchCents);
        const auto zoneLeftGain = voice.zoneGain * voice.zonePanLeftGain;
        const auto zoneRightGain = voice.zoneGain * voice.zonePanRightGain;
        const auto noteNumber = voice.noteNumber >= 0 ? voice.noteNumber : voice.rootMidiNote;
        const auto noteSemitoneOffset = static_cast<float>(noteNumber - voice.rootMidiNote);
        const auto lfoAmountKeyTrackRatio = filterSettings_.lfoKeytrackLinear
            ? juce::jmax(0.0f, 1.0f + (noteSemitoneOffset / 12.0f) * filterSettings_.lfoAmountKeyTracking)
            : std::pow(2.0f, (noteSemitoneOffset / 12.0f) * filterSettings_.lfoAmountKeyTracking);
        const auto keyTrackRatio = std::pow(2.0f, (noteSemitoneOffset / 12.0f) * filterSettings_.keyTracking);
        const auto filterLfoDepthHz = filterSettings_.lfoAmountHz * lfoAmountKeyTrackRatio;
        auto& filterA = voice.filterA;
        auto& filterB = voice.filterB;

        if (voice.lastFilterResonanceQ != resonanceQ)
        {
            filterA.setResonance(resonanceQ);

            if (useSecondFilterStage)
                filterB.setResonance(resonanceQ);

            voice.lastFilterResonanceQ = resonanceQ;
        }

        auto renderVoiceWithReader = [&](auto&& readVoiceSample) noexcept
        {
            auto localOffset = 0;
            auto voiceStopped = false;
            while (localOffset < numSamples && !voiceStopped)
            {
                const auto chunkSize = getVoiceRenderBlockSize(voice, numSamples - localOffset);
                const auto glideSpan = advanceVoiceGlideSpan(voice, chunkSize);
                const auto retriggeredFilterLfoSpan = advanceVoiceRetriggeredFilterLfoSpan(voice, chunkSize);
                const auto chunkLerpScale = chunkSize > 1 ? 1.0f / static_cast<float>(chunkSize - 1) : 0.0f;
                auto glideIncrement = glideSpan.start;
                const auto glideIncrementStep = (glideSpan.end - glideSpan.start) * chunkLerpScale;
                auto retriggeredFilterLfoValue = retriggeredFilterLfoSpan.start;
                const auto retriggeredFilterLfoStep = (retriggeredFilterLfoSpan.end - retriggeredFilterLfoSpan.start) * chunkLerpScale;

                for (int sampleIndex = 0; sampleIndex < chunkSize; ++sampleIndex)
                {
                    const auto bufferIndex = localOffset + sampleIndex;

                    if (voice.samplePosition >= maxSamplePosition)
                    {
                        if (shouldLoopNow)
                        {
                            voice.samplePosition = static_cast<float>(effectiveLoopStart);
                        }
                        else
                        {
                            voice.samplePosition = maxSamplePosition;

                            if (voice.noteHeld)
                            {
                                voice.noteHeld = false;
                                voice.ampEnvelope.noteOff();
                                voice.filterEnvelope.noteOff();
                            }

                            if (!voice.ampEnvelope.isActive())
                            {
                                voicePool_.stopVoiceAtIndex(voiceIndex);
                                voice.noteNumber = -1;
                                voice.noteHeld = false;
                                voice.ampEnvelope.reset();
                                voice.filterEnvelope.reset();
                                voiceStopped = true;
                                break;
                            }
                        }
                    }

                    if (shouldLoopNow && voice.samplePosition >= static_cast<float>(effectiveLoopEnd))
                    {
                        while (voice.samplePosition >= static_cast<float>(effectiveLoopEnd))
                            voice.samplePosition -= static_cast<float>(loopLength);

                        while (voice.samplePosition < static_cast<float>(effectiveLoopStart))
                            voice.samplePosition += static_cast<float>(loopLength);
                    }

                    const auto ampLevel = voice.ampEnvelope.getNextSample()
                        * velocityAmpGain
                        * globalAmpGains[bufferIndex];
                    if (!voice.ampEnvelope.isActive())
                    {
                        voicePool_.stopVoiceAtIndex(voiceIndex);
                        voice.noteNumber = -1;
                        voice.noteHeld = false;
                        voice.filterEnvelope.reset();
                        voiceStopped = true;
                        break;
                    }

                    auto lfoValue = 0.0f;
                    if (filterLfoActive)
                    {
                        if (useRetriggeredFilterLfo)
                        {
                            lfoValue = retriggeredFilterLfoValue;
                            retriggeredFilterLfoValue += retriggeredFilterLfoStep;
                        }
                        else
                        {
                            lfoValue = globalFilterLfoValues[bufferIndex];
                            if (useUnipolarFilterLfo)
                                lfoValue = 0.5f * (lfoValue + 1.0f);

                            if (voice.filterLfoFadeSamplesTotal > 0 && voice.filterLfoFadeSamplesRemaining > 0)
                            {
                                const auto remaining = static_cast<float>(voice.filterLfoFadeSamplesRemaining);
                                const auto total = static_cast<float>(voice.filterLfoFadeSamplesTotal);
                                const auto depthScale = juce::jlimit(0.0f, 1.0f, 1.0f - (remaining / total));
                                lfoValue *= depthScale;
                                --voice.filterLfoFadeSamplesRemaining;
                            }
                        }
                    }

                    auto rawLeft = readVoiceSample(voice.samplePosition, 0);
                    auto rawRight = readVoiceSample(voice.samplePosition, 1);
                    if (crossfadeSamples > 0 && voice.samplePosition >= crossfadeStart)
                    {
                        const auto progress = (voice.samplePosition - crossfadeStart) * crossfadeScale;
                        const auto headPosition = static_cast<float>(effectiveLoopStart)
                            + progress * static_cast<float>(crossfadeSamples);
                        const auto headLeft = readVoiceSample(headPosition, 0);
                        const auto headRight = readVoiceSample(headPosition, 1);
                        rawLeft = rawLeft + (headLeft - rawLeft) * progress;
                        rawRight = rawRight + (headRight - rawRight) * progress;
                    }

                    const auto filterEnvValue = voice.filterEnvelope.getNextSample();
                    auto cutoff = filterSettings_.baseCutoffHz
                        + filterEnvValue * filterSettings_.envAmountHz
                        + voice.velocity * filterSettings_.velocityAmountHz
                        + lfoValue * filterLfoDepthHz
                        + globalFilterOffsets[bufferIndex]
                        + velocityModulation.filterHz;
                    cutoff *= keyTrackRatio;
                    cutoff = juce::jlimit(20.0f, maxCutoffHz, cutoff);

                    if (voice.lastFilterCutoffHz != cutoff)
                    {
                        filterA.setCutoffFrequency(cutoff);

                        if (useSecondFilterStage)
                            filterB.setCutoffFrequency(cutoff);

                        voice.lastFilterCutoffHz = cutoff;
                    }

                    auto filteredLeft = filterA.processSample(0, rawLeft);
                    auto filteredRight = filterA.processSample(1, rawRight);

                    if (useNotchOutput)
                    {
                        filteredLeft = rawLeft - filteredLeft;
                        filteredRight = rawRight - filteredRight;
                    }

                    if (useSecondFilterStage)
                    {
                        filteredLeft = filterB.processSample(0, filteredLeft);
                        filteredRight = filterB.processSample(1, filteredRight);
                    }

                    mixLeft[bufferIndex] += filteredLeft * ampLevel * zoneLeftGain;
                    mixRight[bufferIndex] += filteredRight * ampLevel * zoneRightGain;

                    voice.lastAmpLevel = ampLevel;
                    voicePool_.setCurrentLevel(voiceIndex, ampLevel);

                    voice.samplePosition += glideIncrement
                        * globalPitchRatios[bufferIndex]
                        * velocityPitchRatio;
                    glideIncrement += glideIncrementStep;
                }

                localOffset += chunkSize;
            }
        };

        const auto renderCpuSample = [&](const float position, const int channel) noexcept
        {
            const auto sampleIndex = static_cast<int>(position);
            return readMappedSample(sampleIndex, channel) * computeVoiceEditGain(position);
        };
        const auto renderFidelitySample = [&](const float position, const int channel) noexcept
        {
            const auto sampleIndex = static_cast<int>(position);
            const auto nextIndex = juce::jmin(sampleIndex + 1, sampleLengthMinusOne);
            const auto fraction = position - static_cast<float>(sampleIndex);
            const auto sampleA = readMappedSample(sampleIndex, channel);
            const auto sampleB = readMappedSample(nextIndex, channel);
            return (sampleA + (sampleB - sampleA) * fraction) * computeVoiceEditGain(position);
        };
        const auto renderUltraSample = [&](const float position, const int channel) noexcept
        {
            return readSampleWindowedSinc(
                *voiceSegments,
                position,
                voice.sampleStart,
                voice.sampleEndExclusive,
                channel) * computeVoiceEditGain(position);
        };

        switch (qualityTier_)
        {
            case QualityTier::cpu:
                renderVoiceWithReader(renderCpuSample);
                break;
            case QualityTier::ultra:
                renderVoiceWithReader(renderUltraSample);
                break;
            case QualityTier::fidelity:
            default:
                renderVoiceWithReader(renderFidelitySample);
                break;
        }
    }
}

void EngineCore::render(juce::AudioBuffer<float>& audioBuffer, const juce::MidiBuffer& midiBuffer) noexcept
{
    const auto numSamples = audioBuffer.getNumSamples();
    const auto numChannels = audioBuffer.getNumChannels();

    audioBuffer.clear();

    for (const auto metadata : midiBuffer)
    {
        const auto message = metadata.getMessage();
        const auto offset = juce::jlimit(0, juce::jmax(0, numSamples - 1), metadata.samplePosition);

        if (message.isNoteOn())
            noteOn(message.getNoteNumber(), message.getFloatVelocity(), offset);
        else if (message.isNoteOff())
            noteOff(message.getNoteNumber(), offset);
        else if (message.isPitchWheel())
            pitchBend(message.getPitchWheelValue(), offset);
        else if (message.isChannelPressure())
        {
            enqueuePendingEvent(EventType::channelPressure,
                0,
                static_cast<float>(juce::jlimit(0, 127, message.getChannelPressureValue())) / 127.0f,
                offset);
        }
        else if (message.isAftertouch())
        {
            enqueuePendingEvent(EventType::channelPressure,
                0,
                static_cast<float>(juce::jlimit(0, 127, message.getAfterTouchValue())) / 127.0f,
                offset);
        }
        else if (message.isController() && message.getControllerNumber() == 1)
        {
            enqueuePendingEvent(EventType::controller,
                1,
                static_cast<float>(juce::jlimit(0, 127, message.getControllerValue())) / 127.0f,
                offset);
        }
    }

    std::array<float*, 32> outputPointers{};
    const auto clampedChannels = juce::jmin(numChannels, static_cast<int>(outputPointers.size()));

    for (int channel = 0; channel < clampedChannels; ++channel)
        outputPointers[static_cast<std::size_t>(channel)] = audioBuffer.getWritePointer(channel);

    render(outputPointers.data(), clampedChannels, numSamples);
}

void EngineCore::panic() noexcept
{
    stopAllVoicesImmediate();
    pendingEventCount_ = 0;
    currentPitchBendSemitones_ = 0.0f;
    currentModWheelValue_ = 0.0f;
    currentAftertouchValue_ = 0.0f;
    delayBuffer_.clear();
    delayWritePos_ = 0;
    autopanPhase_ = 0.0f;
    for (auto& filter : dcBlockFilters_)
        filter.reset();
}

void EngineCore::setAmpEnvelope(const AdsrSettings& settings) noexcept
{
    ampEnvelopeSettings_ = settings;
    applyEnvelopeParamsToVoices();
}

void EngineCore::setAmpLfoSettings(const AmpLfoSettings& settings) noexcept
{
    ampLfoSettings_.rateHz = juce::jlimit(0.0f, 40.0f, settings.rateHz);
    ampLfoSettings_.depth = juce::jlimit(0.0f, 1.0f, settings.depth);
    ampLfoSettings_.shape = settings.shape;
}

void EngineCore::setPitchLfoSettings(const PitchLfoSettings& settings) noexcept
{
    pitchLfoSettings_.rateHz = juce::jlimit(0.0f, 40.0f, settings.rateHz);
    pitchLfoSettings_.depthCents = juce::jlimit(0.0f, 100.0f, settings.depthCents);
}

void EngineCore::setModulationRoutingSettings(const ModulationRoutingSettings& settings) noexcept
{
    const std::array<ModulationRoute*, 3 + kMacroControlCount> targetRoutes{{
        &modulationRoutingSettings_.modWheel,
        &modulationRoutingSettings_.aftertouch,
        &modulationRoutingSettings_.velocity,
        &modulationRoutingSettings_.macros[0],
        &modulationRoutingSettings_.macros[1]
    }};
    const std::array<const ModulationRoute*, 3 + kMacroControlCount> sourceRoutes{{
        &settings.modWheel,
        &settings.aftertouch,
        &settings.velocity,
        &settings.macros[0],
        &settings.macros[1]
    }};
    clampModulationRoutes(targetRoutes, sourceRoutes);
}

void EngineCore::setMacroControlValues(const MacroControlValues& values) noexcept
{
    for (int macroIndex = 0; macroIndex < kMacroControlCount; ++macroIndex)
    {
        macroControlValues_[static_cast<std::size_t>(macroIndex)] = juce::jlimit(0.0f, 1.0f,
            values[static_cast<std::size_t>(macroIndex)]);
    }
}

void EngineCore::setFilterEnvelope(const AdsrSettings& settings) noexcept
{
    filterEnvelopeSettings_ = settings;
    applyEnvelopeParamsToVoices();
}

void EngineCore::setFilterSettings(const FilterSettings& settings) noexcept
{
    filterSettings_.baseCutoffHz = juce::jlimit(20.0f, 20000.0f, settings.baseCutoffHz);
    filterSettings_.envAmountHz = juce::jlimit(-12000.0f, 12000.0f, settings.envAmountHz);
    filterSettings_.resonance = juce::jlimit(0.0f, 1.0f, settings.resonance);
    filterSettings_.mode = settings.mode;
    filterSettings_.keyTracking = juce::jlimit(-1.0f, 2.0f, settings.keyTracking);
    filterSettings_.velocityAmountHz = juce::jlimit(-12000.0f, 12000.0f, settings.velocityAmountHz);
    filterSettings_.lfoRateHz = juce::jlimit(0.0f, 40.0f, settings.lfoRateHz);
    filterSettings_.lfoRateKeyTracking = juce::jlimit(-1.0f, 2.0f, settings.lfoRateKeyTracking);
    filterSettings_.lfoAmountHz = juce::jlimit(-20000.0f, 20000.0f, settings.lfoAmountHz);
    filterSettings_.lfoAmountKeyTracking = juce::jlimit(-1.0f, 2.0f, settings.lfoAmountKeyTracking);
    filterSettings_.lfoStartPhaseDegrees = juce::jlimit(0.0f, 360.0f, settings.lfoStartPhaseDegrees);
    filterSettings_.lfoStartPhaseRandomDegrees = juce::jlimit(0.0f, 180.0f, settings.lfoStartPhaseRandomDegrees);
    filterSettings_.lfoFadeInMs = juce::jlimit(0.0f, 5000.0f, settings.lfoFadeInMs);
    filterSettings_.lfoKeytrackLinear = settings.lfoKeytrackLinear;
    filterSettings_.lfoUnipolar = settings.lfoUnipolar;
    filterSettings_.lfoShape = settings.lfoShape;
    filterSettings_.lfoRetrigger = settings.lfoRetrigger;
    filterSettings_.lfoTempoSync = settings.lfoTempoSync;
    filterSettings_.lfoRateKeytrackInTempoSync = settings.lfoRateKeytrackInTempoSync;
    filterSettings_.lfoSyncDivision = juce::jlimit(0, 11, settings.lfoSyncDivision);
    applyFilterParamsToVoices();
}

void EngineCore::setRootMidiNote(const int rootMidiNote) noexcept
{
    rootMidiNote_ = juce::jlimit(0, 127, rootMidiNote);
}

void EngineCore::setLoopPoints(const int loopStart, const int loopEnd) noexcept
{
    const auto sampleLength = getTotalSampleLength();
    const auto maxValid = juce::jmax(0, sampleLength - 1);

    loopStartSample_ = juce::jlimit(0, maxValid, loopStart);
    loopEndSample_ = juce::jlimit(0, maxValid, loopEnd);

    if (loopEndSample_ <= loopStartSample_)
        loopEndSample_ = maxValid;

    setLoopCrossfadeSamples(loopCrossfadeSamples_);
}

void EngineCore::setPolyphonyLimit(const int voices) noexcept
{
    const auto clamped = juce::jlimit(1, static_cast<int>(VoicePool::maxVoices), voices);
    voicePool_.setVoiceLimit(clamped);

    for (int i = clamped; i < static_cast<int>(VoicePool::maxVoices); ++i)
    {
        auto& voice = voices_[static_cast<std::size_t>(i)];
        voice.noteHeld = false;
        voice.releaseOnNoteOff = true;
        voice.lastAmpLevel = 0.0f;
        voice.filterA.reset();
        voice.filterB.reset();
        voice.filterLfoPhase = 0.0f;
        voice.filterLfoFadeSamplesTotal = 0;
        voice.filterLfoFadeSamplesRemaining = 0;
        voice.glideSamplesRemaining = 0;
        voice.ampEnvelope.reset();
        voice.filterEnvelope.reset();
    }
}

void EngineCore::setLoopCrossfadeSamples(const int crossfadeSamples) noexcept
{
    const auto loopLength = juce::jmax(0, loopEndSample_ - loopStartSample_ + 1);
    const auto maxCrossfade = juce::jmax(0, loopLength / 2);
    loopCrossfadeSamples_ = juce::jlimit(0, maxCrossfade, crossfadeSamples);
}

int EngineCore::activeVoiceCount() const noexcept
{
    return voicePool_.activeVoiceCount();
}

bool EngineCore::enqueuePendingEvent(const EventType type,
                                     const int noteNumber,
                                     const float velocity,
                                     const int sampleOffsetInBlock) noexcept
{
    const auto capacity = static_cast<int>(pendingEvents_.size());
    const auto clampedOffset = juce::jmax(0, sampleOffsetInBlock);

    if (pendingEventCount_ < capacity)
    {
        auto& event = pendingEvents_[static_cast<std::size_t>(pendingEventCount_++)];
        event.type = type;
        event.data = noteNumber;
        event.value = velocity;
        event.offset = clampedOffset;
        return true;
    }

    if (type == EventType::noteOn)
        return false;

    int replaceIndex = -1;
    int latestOffset = std::numeric_limits<int>::min();
    for (int index = 0; index < pendingEventCount_; ++index)
    {
        if (pendingEvents_[static_cast<std::size_t>(index)].type != EventType::noteOn)
            continue;

        const auto eventOffset = pendingEvents_[static_cast<std::size_t>(index)].offset;
        if (eventOffset >= latestOffset)
        {
            latestOffset = eventOffset;
            replaceIndex = index;
        }
    }

    if (replaceIndex < 0)
        return false;

    auto& event = pendingEvents_[static_cast<std::size_t>(replaceIndex)];
    event.type = type;
    event.data = noteNumber;
    event.value = velocity;
    event.offset = clampedOffset;
    return true;
}

void EngineCore::sortPendingEventsByOffset() noexcept
{
    for (int index = 1; index < pendingEventCount_; ++index)
    {
        const auto event = pendingEvents_[static_cast<std::size_t>(index)];
        auto insertionIndex = index;

        while (insertionIndex > 0
            && pendingEvents_[static_cast<std::size_t>(insertionIndex - 1)].offset > event.offset)
        {
            pendingEvents_[static_cast<std::size_t>(insertionIndex)] =
                pendingEvents_[static_cast<std::size_t>(insertionIndex - 1)];
            --insertionIndex;
        }

        pendingEvents_[static_cast<std::size_t>(insertionIndex)] = event;
    }
}

int EngineCore::stealCount() const noexcept
{
    return voicePool_.stealCount();
}

void EngineCore::resetStealCount() noexcept
{
    voicePool_.resetStealCount();
}

bool EngineCore::isNoteActive(const int noteNumber) const noexcept
{
    return voicePool_.isNoteActive(noteNumber);
}

EngineCore::VoicePlaybackStates EngineCore::getVoicePlaybackStates() const noexcept
{
    VoicePlaybackStates states{};
    const auto segments = getSampleSegmentsSnapshot();
    if (segments == nullptr)
        return states;

    const auto programAudioSnapshot = programAudioSnapshot_.load(std::memory_order_acquire);
    const auto* programAudio = programAudioSnapshot.get();

    for (int voiceIndex = 0; voiceIndex < static_cast<int>(VoicePool::maxVoices); ++voiceIndex)
    {
        if (!voicePool_.isActive(voiceIndex))
            continue;

        const auto& voice = voices_[static_cast<std::size_t>(voiceIndex)];
        const auto playbackIndex = static_cast<int>(std::floor(voice.samplePosition));
        const auto* voiceSegments = programAudio != nullptr
            ? programAudio->getSampleSegments(voice.sampleAssetIndex)
            : nullptr;
        if (voiceSegments == nullptr)
            voiceSegments = segments.get();

        auto& state = states[static_cast<std::size_t>(voiceIndex)];
        state.active = true;
        state.zoneIndex = voice.zoneIndex;
        state.sampleAssetIndex = voice.sampleAssetIndex;
        state.sampleIndex = voiceSegments != nullptr
            ? mapPlaybackIndexToSampleIndex(*voiceSegments, playbackIndex, voice.sampleStart, voice.sampleEndExclusive)
            : -1;
    }

    return states;
}

void EngineCore::generateFallbackSample() noexcept
{
    constexpr int sampleLength = 4096;
    juce::AudioBuffer<float> fallback(1, sampleLength);

    for (int sampleIndex = 0; sampleIndex < sampleLength; ++sampleIndex)
    {
        const auto phase = 2.0f * kPi * static_cast<float>(sampleIndex) * 220.0f / static_cast<float>(sampleRate_);
        fallback.setSample(0, sampleIndex, 0.2f * std::sin(phase));
    }

    setSampleData(fallback, sampleRate_, rootMidiNote_);
    samplePath_.clear();
}

void EngineCore::resetRoundRobinCursors() noexcept
{
    for (auto& cursor : roundRobinCursors_)
    {
        cursor.group = 0;
        cursor.nextStep = 0;
    }
}

std::uint32_t EngineCore::consumeRoundRobinStep(const int roundRobinGroup) noexcept
{
    if (roundRobinGroup <= 0)
        return 0;

    for (auto& cursor : roundRobinCursors_)
    {
        if (cursor.group == roundRobinGroup)
        {
            const auto step = cursor.nextStep;
            ++cursor.nextStep;
            return step;
        }
    }

    for (auto& cursor : roundRobinCursors_)
    {
        if (cursor.group == 0)
        {
            cursor.group = roundRobinGroup;
            cursor.nextStep = 1;
            return 0;
        }
    }

    return 0;
}

int EngineCore::chooseProgramZoneIndex(const ProgramSnapshot& programSnapshot,
                                       const int note,
                                       const int velocity) noexcept
{
    const auto firstZoneIndex = programSnapshot.findFirstMatchingZoneIndex(note, velocity);
    if (firstZoneIndex < 0)
        return -1;

    const auto& firstZone = programSnapshot.zones[static_cast<std::size_t>(firstZoneIndex)];
    const auto roundRobinGroup = programSnapshot.getZoneRoundRobinGroup(firstZone);
    if (roundRobinGroup <= 0)
        return firstZoneIndex;

    const auto roundRobinMode = programSnapshot.getZoneRoundRobinMode(firstZone);
    const auto step = consumeRoundRobinStep(roundRobinGroup);
    const auto selectedZoneIndex = programSnapshot.findRoundRobinMatchingZoneIndex(
        note,
        velocity,
        roundRobinGroup,
        step,
        roundRobinMode);

    return selectedZoneIndex >= 0 ? selectedZoneIndex : firstZoneIndex;
}

int EngineCore::chooseProgramZoneIndices(const ProgramSnapshot& programSnapshot,
                                         const int note,
                                         const int velocity,
                                         const bool releaseTriggerOnly,
                                         int* const zoneIndices,
                                         const int maxZoneIndices) noexcept
{
    if (zoneIndices == nullptr || maxZoneIndices <= 0)
        return 0;

    std::array<int, ProgramSnapshot::maxGroups> handledRoundRobinGroups{};
    auto handledRoundRobinGroupCount = 0;
    auto selectedCount = 0;

    auto addSelectedZone = [&](const int zoneIndex) noexcept
    {
        if (zoneIndex < 0 || selectedCount >= maxZoneIndices)
            return;

        zoneIndices[selectedCount++] = zoneIndex;
    };

    auto hasHandledRoundRobinGroup = [&](const int roundRobinGroup) noexcept
    {
        for (int index = 0; index < handledRoundRobinGroupCount; ++index)
        {
            if (handledRoundRobinGroups[static_cast<std::size_t>(index)] == roundRobinGroup)
                return true;
        }

        return false;
    };

    auto markRoundRobinGroupHandled = [&](const int roundRobinGroup) noexcept
    {
        if (handledRoundRobinGroupCount >= static_cast<int>(handledRoundRobinGroups.size()))
            return;

        handledRoundRobinGroups[static_cast<std::size_t>(handledRoundRobinGroupCount++)] = roundRobinGroup;
    };

    auto matchesTriggerPhase = [&](const ProgramSnapshot::ZoneRef& zone) noexcept
    {
        const auto triggerMode = programSnapshot.getZoneTriggerMode(zone);
        return releaseTriggerOnly
            ? triggerMode == ZoneTriggerMode::release
            : triggerMode != ZoneTriggerMode::release;
    };

    for (std::size_t zoneIndex = 0; zoneIndex < programSnapshot.zoneCount; ++zoneIndex)
    {
        if (!programSnapshot.isZonePlayableMatch(zoneIndex, note, velocity))
            continue;

        const auto& zone = programSnapshot.zones[zoneIndex];
        if (!matchesTriggerPhase(zone))
            continue;

        const auto roundRobinGroup = programSnapshot.getZoneRoundRobinGroup(zone);
        if (roundRobinGroup <= 0)
        {
            addSelectedZone(static_cast<int>(zoneIndex));
            continue;
        }

        if (hasHandledRoundRobinGroup(roundRobinGroup))
            continue;

        markRoundRobinGroupHandled(roundRobinGroup);
        const auto step = consumeRoundRobinStep(roundRobinGroup);
        std::array<int, ProgramSnapshot::maxZones> candidateIndices{};
        auto candidateCount = 0;
        for (std::size_t candidateZoneIndex = 0; candidateZoneIndex < programSnapshot.zoneCount; ++candidateZoneIndex)
        {
            if (!programSnapshot.isZonePlayableMatch(candidateZoneIndex, note, velocity))
                continue;

            const auto& candidateZone = programSnapshot.zones[candidateZoneIndex];
            if (!matchesTriggerPhase(candidateZone)
                || programSnapshot.getZoneRoundRobinGroup(candidateZone) != roundRobinGroup)
            {
                continue;
            }

            candidateIndices[static_cast<std::size_t>(candidateCount++)] = static_cast<int>(candidateZoneIndex);
        }

        if (candidateCount <= 0)
            continue;

        if (candidateCount == 1)
        {
            addSelectedZone(candidateIndices[0]);
            continue;
        }

        const auto roundRobinMode = programSnapshot.getZoneRoundRobinMode(zone);
        if (roundRobinMode == RoundRobinMode::cycleRandom)
        {
            const auto selectedIndex = ProgramSnapshot::cycleRandomCandidateIndex(
                roundRobinGroup,
                step,
                static_cast<std::size_t>(candidateCount));
            addSelectedZone(candidateIndices[selectedIndex]);
            continue;
        }

        auto roundRobinLength = 0;
        for (int candidateIndex = 0; candidateIndex < candidateCount; ++candidateIndex)
        {
            const auto& candidateZone = programSnapshot.zones[static_cast<std::size_t>(candidateIndices[candidateIndex])];
            const auto candidateLength = programSnapshot.getZoneRoundRobinLength(candidateZone);
            if (candidateLength > roundRobinLength)
                roundRobinLength = candidateLength;
        }

        if (roundRobinLength > 0)
        {
            const auto targetPosition = static_cast<int>(step % static_cast<std::uint32_t>(roundRobinLength)) + 1;
            for (int candidateIndex = 0; candidateIndex < candidateCount; ++candidateIndex)
            {
                const auto resolvedZoneIndex = candidateIndices[candidateIndex];
                if (programSnapshot.zones[static_cast<std::size_t>(resolvedZoneIndex)].roundRobinPosition == targetPosition)
                {
                    addSelectedZone(resolvedZoneIndex);
                    break;
                }
            }
            continue;
        }

        std::sort(candidateIndices.begin(), candidateIndices.begin() + candidateCount,
            [&programSnapshot](const int left, const int right)
            {
                return programSnapshot.roundRobinSortsBefore(
                    static_cast<std::size_t>(left),
                    static_cast<std::size_t>(right));
            });
        addSelectedZone(candidateIndices[static_cast<std::size_t>(step % static_cast<std::uint32_t>(candidateCount))]);
    }

    return selectedCount;
}

void EngineCore::flushPendingEventsAtOffset(const int offset,
                                           const ProgramSnapshot* const programSnapshot,
                                           const ProgramAudioSnapshot* const programAudioSnapshot) noexcept
{
    for (int eventIndex = 0; eventIndex < pendingEventCount_; ++eventIndex)
    {
        const auto& event = pendingEvents_[static_cast<std::size_t>(eventIndex)];

        if (event.offset != offset)
            continue;

        auto startResolvedVoice = [&](const int selectedZoneIndex, const int existingVoiceIndex) noexcept
        {
            VoiceStartContext voiceContext;
            voiceContext.noteNumber = event.data;
            voiceContext.velocity = event.value;
            voiceContext.rootMidiNote = rootMidiNote_;
            voiceContext.releaseOnNoteOff = playbackMode_ != PlaybackMode::oneShot;
            voiceContext.sourceSampleRateHz = sampleDataRate_;
            const auto midiVelocity = juce::jlimit(0, 127, static_cast<int>(std::round(event.value * 127.0f)));

            if (programSnapshot != nullptr)
            {
                if (selectedZoneIndex < 0 || static_cast<std::size_t>(selectedZoneIndex) >= programSnapshot->zoneCount)
                    return false;

                const auto& zone = programSnapshot->zones[static_cast<std::size_t>(selectedZoneIndex)];
                voiceContext.rootMidiNote = zone.rootMidiNote;
                voiceContext.zoneIndex = selectedZoneIndex;
                voiceContext.sampleAssetIndex = zone.sampleAssetIndex;
                voiceContext.zoneGain = std::pow(10.0f, programSnapshot->getZoneGainDb(zone) / 20.0f)
                    * programSnapshot->getZoneVelocityGain(zone, midiVelocity);
                const auto zonePan = programSnapshot->getZonePan(zone);
                voiceContext.zonePanLeftGain = zonePan > 0.0f ? (1.0f - zonePan) : 1.0f;
                voiceContext.zonePanRightGain = zonePan < 0.0f ? (1.0f + zonePan) : 1.0f;
                voiceContext.zoneTuneCents = zone.tuneCents;
                voiceContext.chokeGroup = programSnapshot->getZoneChokeGroup(zone);
                voiceContext.sampleStart = zone.sampleStart;
                voiceContext.sampleEndExclusive = zone.sampleEndExclusive;
                voiceContext.loopStart = zone.loopStart;
                voiceContext.loopEndExclusive = zone.loopEndExclusive;
                voiceContext.loopMode = zone.loopMode;
                voiceContext.releaseOnNoteOff = programSnapshot->getZoneTriggerMode(zone) == ZoneTriggerMode::gate;

                if (programSnapshot->isZoneSampleIndexValid(zone))
                {
                    const auto& asset = programSnapshot->sampleAssets[static_cast<std::size_t>(voiceContext.sampleAssetIndex)];
                    voiceContext.sourceSampleRateHz = asset.sampleRateHz > 0.0
                        ? asset.sampleRateHz
                        : voiceContext.sourceSampleRateHz;
                }

                if (programAudioSnapshot != nullptr
                    && programAudioSnapshot->getSampleSegments(voiceContext.sampleAssetIndex) == nullptr)
                {
                    return false;
                }

                if (programAudioSnapshot != nullptr)
                    if (const auto* streamSegments = programAudioSnapshot->getSampleSegments(voiceContext.sampleAssetIndex); streamSegments != nullptr)
                        streamSegments->requestPrimeForAbsoluteSample(voiceContext.sampleStart);
            }
            else if (const auto streamSegments = getSampleSegmentsSnapshot())
            {
                streamSegments->requestPrimeForAbsoluteSample(sampleWindowStart_);
            }

            if (existingVoiceIndex >= 0)
            {
                retargetVoiceLegato(existingVoiceIndex, voiceContext);
                return true;
            }

            const auto voiceIndex = voicePool_.startVoiceForNote(event.data);
            startVoice(voiceIndex, voiceContext);
            return voiceIndex >= 0;
        };

        if (event.type == EventType::noteOn)
        {

            if (programSnapshot != nullptr)
            {
                std::array<int, ProgramSnapshot::maxZones> selectedZoneIndices{};
                const auto midiVelocity = juce::jlimit(0, 127, static_cast<int>(std::round(event.value * 127.0f)));
                const auto selectedZoneCount = chooseProgramZoneIndices(
                    *programSnapshot,
                    event.data,
                    midiVelocity,
                    false,
                    selectedZoneIndices.data(),
                    static_cast<int>(selectedZoneIndices.size()));

                if (selectedZoneCount <= 0)
                    continue;

                for (int selectedIndex = 0; selectedIndex < selectedZoneCount; ++selectedIndex)
                {
                    const auto selectedZoneIndex = selectedZoneIndices[static_cast<std::size_t>(selectedIndex)];
                    const auto& zone = programSnapshot->zones[static_cast<std::size_t>(selectedZoneIndex)];
                    stopVoicesInChokeGroupImmediate(programSnapshot->getZoneChokeGroup(zone));
                }

                stopVoicesForNoteImmediate(event.data);

                if (monoMode_)
                {
                    const auto activeIndex = voicePool_.firstActiveVoiceIndex();
                    if (activeIndex >= 0 && legatoMode_)
                    {
                        startResolvedVoice(selectedZoneIndices[0], activeIndex);
                        continue;
                    }

                    stopAllVoicesImmediate();
                    startResolvedVoice(selectedZoneIndices[0], -1);
                    continue;
                }

                for (int selectedIndex = 0; selectedIndex < selectedZoneCount; ++selectedIndex)
                    startResolvedVoice(selectedZoneIndices[static_cast<std::size_t>(selectedIndex)], -1);

                continue;
            }

            stopVoicesForNoteImmediate(event.data);

            if (monoMode_)
            {
                const auto activeIndex = voicePool_.firstActiveVoiceIndex();
                if (activeIndex >= 0 && legatoMode_)
                {
                    startResolvedVoice(-1, activeIndex);
                    continue;
                }

                stopAllVoicesImmediate();
            }

            startResolvedVoice(-1, -1);
        }
        else if (event.type == EventType::noteOff)
        {
            releaseVoicesForNote(event.data);

            if (programSnapshot == nullptr)
                continue;

            std::array<int, ProgramSnapshot::maxZones> selectedZoneIndices{};
            const auto midiVelocity = juce::jlimit(0, 127, static_cast<int>(std::round(event.value * 127.0f)));
            const auto selectedZoneCount = chooseProgramZoneIndices(
                *programSnapshot,
                event.data,
                midiVelocity,
                true,
                selectedZoneIndices.data(),
                static_cast<int>(selectedZoneIndices.size()));

            for (int selectedIndex = 0; selectedIndex < selectedZoneCount; ++selectedIndex)
            {
                const auto selectedZoneIndex = selectedZoneIndices[static_cast<std::size_t>(selectedIndex)];
                const auto& zone = programSnapshot->zones[static_cast<std::size_t>(selectedZoneIndex)];
                stopVoicesInChokeGroupImmediate(programSnapshot->getZoneChokeGroup(zone));
                startResolvedVoice(selectedZoneIndex, -1);
            }
        }
        else
        {
            if (event.type == EventType::pitchBend)
            {
                currentPitchBendSemitones_ = juce::jlimit(-pitchBendRangeSemitones_, pitchBendRangeSemitones_,
                    event.value * pitchBendRangeSemitones_);
            }
            else if (event.type == EventType::channelPressure)
            {
                currentAftertouchValue_ = juce::jlimit(0.0f, 1.0f, event.value);
            }
            else if (event.data == 1)
            {
                currentModWheelValue_ = juce::jlimit(0.0f, 1.0f, event.value);
            }
        }
    }
}

void EngineCore::applyVoiceStartContext(VoiceState& voice, const VoiceStartContext& context) noexcept
{
    voice.velocity = context.velocity;
    voice.noteNumber = context.noteNumber;
    voice.noteHeld = true;
    voice.releaseOnNoteOff = context.releaseOnNoteOff;
    voice.rootMidiNote = context.rootMidiNote;
    voice.zoneIndex = context.zoneIndex;
    voice.sampleAssetIndex = context.sampleAssetIndex;
    voice.zoneGain = context.zoneGain;
    voice.zonePanLeftGain = context.zonePanLeftGain;
    voice.zonePanRightGain = context.zonePanRightGain;
    voice.zoneTuneCents = context.zoneTuneCents;
    voice.chokeGroup = context.chokeGroup;
    voice.sampleStart = context.sampleStart;
    voice.sampleEndExclusive = context.sampleEndExclusive;
    voice.loopStart = context.loopStart;
    voice.loopEndExclusive = context.loopEndExclusive;
    voice.loopMode = context.loopMode;
}

void EngineCore::startVoice(const int voiceIndex, const VoiceStartContext& context) noexcept
{
    if (voiceIndex < 0 || voiceIndex >= static_cast<int>(VoicePool::maxVoices))
        return;

    auto& voice = voices_[static_cast<std::size_t>(voiceIndex)];
    const auto targetIncrement = computeSampleIncrementForNote(
        context.noteNumber,
        context.rootMidiNote,
        context.sourceSampleRateHz,
        context.zoneTuneCents);

    voice.samplePosition = 0.0f;
    voice.sampleIncrement = targetIncrement;
    voice.glideTargetIncrement = targetIncrement;
    voice.glideSamplesRemaining = 0;
    voice.lastAmpLevel = 0.0f;
    applyVoiceStartContext(voice, context);
    voice.filterA.reset();
    voice.filterB.reset();
    voice.lastFilterCutoffHz = -1.0f;
    voice.lastFilterResonanceQ = -1.0f;
    if (filterSettings_.lfoRetrigger)
    {
        auto startPhase = filterSettings_.lfoStartPhaseDegrees;
        if (filterSettings_.lfoStartPhaseRandomDegrees > 0.0f)
        {
            const auto startOrder = voicePool_.startOrderAt(voiceIndex);
            const auto bipolar = deterministicBipolarFromNoteAndOrder(context.noteNumber, startOrder);
            startPhase += bipolar * filterSettings_.lfoStartPhaseRandomDegrees;
        }

        auto phaseNorm = startPhase / 360.0f;
        phaseNorm -= std::floor(phaseNorm);
        voice.filterLfoPhase = phaseNorm;
    }
    else
    {
        voice.filterLfoPhase = globalFilterLfoPhase_;
    }
    voice.filterLfoFadeSamplesTotal = static_cast<int>(std::round(
        (filterSettings_.lfoFadeInMs / 1000.0f) * static_cast<float>(sampleRate_)));
    voice.filterLfoFadeSamplesRemaining = voice.filterLfoFadeSamplesTotal;

    voice.ampEnvelope.reset();
    voice.ampEnvelope.noteOn();

    voice.filterEnvelope.reset();
    voice.filterEnvelope.noteOn();
}

void EngineCore::retargetVoiceLegato(const int voiceIndex, const VoiceStartContext& context) noexcept
{
    if (voiceIndex < 0 || voiceIndex >= static_cast<int>(VoicePool::maxVoices))
        return;

    auto& voice = voices_[static_cast<std::size_t>(voiceIndex)];
    const auto targetIncrement = computeSampleIncrementForNote(
        context.noteNumber,
        context.rootMidiNote,
        context.sourceSampleRateHz,
        context.zoneTuneCents);

    applyVoiceStartContext(voice, context);
    voicePool_.setNoteAtIndex(voiceIndex, context.noteNumber);

    const auto glideSamples = static_cast<int>(std::round(glideSeconds_ * static_cast<float>(sampleRate_)));
    if (glideSamples > 0)
    {
        voice.glideTargetIncrement = targetIncrement;
        voice.glideSamplesRemaining = glideSamples;
    }
    else
    {
        voice.sampleIncrement = targetIncrement;
        voice.glideTargetIncrement = targetIncrement;
        voice.glideSamplesRemaining = 0;
    }
}

void EngineCore::stopAllVoicesImmediate() noexcept
{
    voicePool_.stopAllVoices();

    for (auto& voice : voices_)
    {
        voice.noteNumber = -1;
        voice.noteHeld = false;
        voice.releaseOnNoteOff = true;
        voice.lastAmpLevel = 0.0f;
        voice.rootMidiNote = rootMidiNote_;
        voice.zoneIndex = -1;
        voice.sampleAssetIndex = -1;
        voice.zoneGain = 1.0f;
        voice.zonePanLeftGain = 1.0f;
        voice.zonePanRightGain = 1.0f;
        voice.zoneTuneCents = 0.0f;
        voice.chokeGroup = 0;
        voice.sampleStart = 0;
        voice.sampleEndExclusive = -1;
        voice.loopStart = -1;
        voice.loopEndExclusive = -1;
        voice.loopMode = ZoneLoopMode::noLoop;
        voice.filterA.reset();
        voice.filterB.reset();
        voice.filterLfoPhase = 0.0f;
        voice.filterLfoFadeSamplesTotal = 0;
        voice.filterLfoFadeSamplesRemaining = 0;
        voice.glideSamplesRemaining = 0;
        voice.ampEnvelope.reset();
        voice.filterEnvelope.reset();
    }
}

void EngineCore::stopVoicesForNoteImmediate(const int noteNumber) noexcept
{
    std::array<int, VoicePool::maxVoices> indices{};
    const auto count = voicePool_.findActiveVoicesForNote(noteNumber, indices.data(), static_cast<int>(indices.size()));

    for (int index = 0; index < count; ++index)
    {
        const auto voiceIndex = indices[static_cast<std::size_t>(index)];
        voicePool_.stopVoiceAtIndex(voiceIndex);

        auto& voice = voices_[static_cast<std::size_t>(voiceIndex)];
        voice.noteNumber = -1;
        voice.noteHeld = false;
        voice.releaseOnNoteOff = true;
        voice.lastAmpLevel = 0.0f;
        voice.rootMidiNote = rootMidiNote_;
        voice.zoneIndex = -1;
        voice.sampleAssetIndex = -1;
        voice.zoneGain = 1.0f;
        voice.zonePanLeftGain = 1.0f;
        voice.zonePanRightGain = 1.0f;
        voice.zoneTuneCents = 0.0f;
        voice.chokeGroup = 0;
        voice.sampleStart = 0;
        voice.sampleEndExclusive = -1;
        voice.loopStart = -1;
        voice.loopEndExclusive = -1;
        voice.loopMode = ZoneLoopMode::noLoop;
        voice.filterA.reset();
        voice.filterB.reset();
        voice.filterLfoPhase = 0.0f;
        voice.filterLfoFadeSamplesTotal = 0;
        voice.filterLfoFadeSamplesRemaining = 0;
        voice.glideSamplesRemaining = 0;
        voice.ampEnvelope.reset();
        voice.filterEnvelope.reset();
    }
}

void EngineCore::stopVoicesInChokeGroupImmediate(const int chokeGroup) noexcept
{
    if (chokeGroup <= 0)
        return;

    for (int voiceIndex = 0; voiceIndex < static_cast<int>(VoicePool::maxVoices); ++voiceIndex)
    {
        if (!voicePool_.isActive(voiceIndex))
            continue;

        auto& voice = voices_[static_cast<std::size_t>(voiceIndex)];
        if (voice.chokeGroup != chokeGroup)
            continue;

        voicePool_.stopVoiceAtIndex(voiceIndex);
        voice.noteNumber = -1;
        voice.noteHeld = false;
        voice.releaseOnNoteOff = true;
        voice.lastAmpLevel = 0.0f;
        voice.rootMidiNote = rootMidiNote_;
        voice.zoneIndex = -1;
        voice.sampleAssetIndex = -1;
        voice.zoneGain = 1.0f;
        voice.zonePanLeftGain = 1.0f;
        voice.zonePanRightGain = 1.0f;
        voice.zoneTuneCents = 0.0f;
        voice.chokeGroup = 0;
        voice.sampleStart = 0;
        voice.sampleEndExclusive = -1;
        voice.loopStart = -1;
        voice.loopEndExclusive = -1;
        voice.loopMode = ZoneLoopMode::noLoop;
        voice.filterA.reset();
        voice.filterB.reset();
        voice.filterLfoPhase = 0.0f;
        voice.filterLfoFadeSamplesTotal = 0;
        voice.filterLfoFadeSamplesRemaining = 0;
        voice.glideSamplesRemaining = 0;
        voice.ampEnvelope.reset();
        voice.filterEnvelope.reset();
    }
}

void EngineCore::releaseVoicesForNote(const int noteNumber) noexcept
{
    std::array<int, VoicePool::maxVoices> indices{};
    const auto count = voicePool_.findActiveVoicesForNote(noteNumber, indices.data(), static_cast<int>(indices.size()));

    if (count <= 0)
        return;

    auto releaseVoiceByIndex = [this](const int voiceIndex)
    {
        auto& voice = voices_[static_cast<std::size_t>(voiceIndex)];

        if (!voice.releaseOnNoteOff)
            return;

        voice.noteHeld = false;

        voice.ampEnvelope.noteOff();
        voice.filterEnvelope.noteOff();
    };

    for (int index = 0; index < count; ++index)
        releaseVoiceByIndex(indices[static_cast<std::size_t>(index)]);
}

void EngineCore::applyEnvelopeParamsToVoices() noexcept
{
    juce::ADSR::Parameters ampParameters;
    ampParameters.attack = juce::jmax(0.0001f, ampEnvelopeSettings_.attackSeconds);
    ampParameters.decay = juce::jmax(0.0001f, ampEnvelopeSettings_.decaySeconds);
    ampParameters.sustain = juce::jlimit(0.0f, 1.0f, ampEnvelopeSettings_.sustainLevel);
    ampParameters.release = juce::jmax(0.0001f, ampEnvelopeSettings_.releaseSeconds);

    juce::ADSR::Parameters filterParameters;
    filterParameters.attack = juce::jmax(0.0001f, filterEnvelopeSettings_.attackSeconds);
    filterParameters.decay = juce::jmax(0.0001f, filterEnvelopeSettings_.decaySeconds);
    filterParameters.sustain = juce::jlimit(0.0f, 1.0f, filterEnvelopeSettings_.sustainLevel);
    filterParameters.release = juce::jmax(0.0001f, filterEnvelopeSettings_.releaseSeconds);

    for (auto& voice : voices_)
    {
        voice.ampEnvelope.setSampleRate(sampleRate_);
        voice.ampEnvelope.setParameters(ampParameters);

        voice.filterEnvelope.setSampleRate(sampleRate_);
        voice.filterEnvelope.setParameters(filterParameters);
    }
}

void EngineCore::applyFilterParamsToVoices() noexcept
{
    auto juceType = juce::dsp::StateVariableTPTFilterType::lowpass;
    switch (filterSettings_.mode)
    {
        case FilterSettings::Mode::highPass12:
        case FilterSettings::Mode::highPass24:
            juceType = juce::dsp::StateVariableTPTFilterType::highpass;
            break;
        case FilterSettings::Mode::bandPass12:
            juceType = juce::dsp::StateVariableTPTFilterType::bandpass;
            break;
        case FilterSettings::Mode::notch12:
            juceType = juce::dsp::StateVariableTPTFilterType::bandpass;
            break;
        case FilterSettings::Mode::lowPass12:
        case FilterSettings::Mode::lowPass24:
        default:
            juceType = juce::dsp::StateVariableTPTFilterType::lowpass;
            break;
    }

    const auto defaultCutoff = juce::jlimit(20.0f, static_cast<float>(sampleRate_ * 0.45), filterSettings_.baseCutoffHz);
    const auto q = juce::jlimit(0.5f, 20.0f, 0.5f + filterSettings_.resonance * 19.5f);

    for (auto& voice : voices_)
    {
        voice.filterA.setType(juceType);
        voice.filterB.setType(juceType);
        voice.filterA.setCutoffFrequency(defaultCutoff);
        voice.filterB.setCutoffFrequency(defaultCutoff);
        voice.filterA.setResonance(q);
        voice.filterB.setResonance(q);

        voice.lastFilterCutoffHz = -1.0f;
        voice.lastFilterResonanceQ = -1.0f;
    }
}

float EngineCore::computeSampleIncrementForNote(const int noteNumber) const noexcept
{
    return computeSampleIncrementForNote(noteNumber, rootMidiNote_);
}

float EngineCore::computeSampleIncrementForNote(const int noteNumber, const int rootMidiNote) const noexcept
{
    return computeSampleIncrementForNote(noteNumber, rootMidiNote, sampleDataRate_);
}

float EngineCore::computeSampleIncrementForNote(const int noteNumber,
                                                const int rootMidiNote,
                                                const double sourceSampleRateHz) const noexcept
{
    return computeSampleIncrementForNote(noteNumber, rootMidiNote, sourceSampleRateHz, 0.0f);
}

float EngineCore::computeSampleIncrementForNote(const int noteNumber,
                                                const int rootMidiNote,
                                                const double sourceSampleRateHz,
                                                const float zoneTuneCents) const noexcept
{
    const auto semitoneOffset = static_cast<float>(noteNumber - rootMidiNote)
        + coarseTuneSemitones_
        + ((fineTuneCents_ + zoneTuneCents) / 100.0f);
    const auto pitchRatio = std::pow(2.0f, semitoneOffset / 12.0f);
    const auto sourceToOutputRatio = sourceSampleRateHz > 0.0 ? sourceSampleRateHz / sampleRate_ : 1.0;
    return static_cast<float>(pitchRatio * sourceToOutputRatio);
}

float EngineCore::readSampleLinear(const SampleSegments& segments, const float position) const noexcept
{
    return readSampleLinear(segments, position, sampleWindowStart_, sampleWindowEnd_ + 1);
}

float EngineCore::readSampleLinear(const SampleSegments& segments,
                                   const float position,
                                   const int sampleStart,
                                   const int sampleEndExclusive) const noexcept
{
    return readSampleLinear(segments, position, sampleStart, sampleEndExclusive, 0);
}

float EngineCore::readSampleLinear(const SampleSegments& segments,
                                   const float position,
                                   const int sampleStart,
                                   const int sampleEndExclusive,
                                   const int channel) const noexcept
{
    const auto sampleLength = getEffectivePlaybackLength(segments, sampleStart, sampleEndExclusive);

    if (sampleLength <= 1)
        return 0.0f;

    const auto clampedPosition = juce::jlimit(0.0f, static_cast<float>(sampleLength - 1), position);
    const auto sampleIndex = static_cast<int>(clampedPosition);
    const auto mappedIndex = mapPlaybackIndexToSampleIndex(segments, sampleIndex, sampleStart, sampleEndExclusive);
    const auto editGain = computeEditGain(clampedPosition, sampleLength);

    if (qualityTier_ == QualityTier::cpu)
        return readSampleAt(segments, mappedIndex, channel) * editGain;

    if (qualityTier_ == QualityTier::ultra)
        return readSampleWindowedSinc(segments, clampedPosition, sampleStart, sampleEndExclusive, channel) * editGain;

    const auto nextIndex = juce::jmin(sampleIndex + 1, sampleLength - 1);
    const auto fraction = clampedPosition - static_cast<float>(sampleIndex);

    const auto sampleA = readSampleAt(segments, mappedIndex, channel);
    const auto sampleB = readSampleAt(
        segments,
        mapPlaybackIndexToSampleIndex(segments, nextIndex, sampleStart, sampleEndExclusive),
        channel);

    return (sampleA + (sampleB - sampleA) * fraction) * editGain;
}

float EngineCore::readSampleCubic(const SampleSegments& segments, const float position) const noexcept
{
    return readSampleCubic(segments, position, sampleWindowStart_, sampleWindowEnd_ + 1);
}

float EngineCore::readSampleCubic(const SampleSegments& segments,
                                  const float position,
                                  const int sampleStart,
                                  const int sampleEndExclusive) const noexcept
{
    return readSampleCubic(segments, position, sampleStart, sampleEndExclusive, 0);
}

float EngineCore::readSampleCubic(const SampleSegments& segments,
                                  const float position,
                                  const int sampleStart,
                                  const int sampleEndExclusive,
                                  const int channel) const noexcept
{
    const auto sampleLength = getEffectivePlaybackLength(segments, sampleStart, sampleEndExclusive);
    if (sampleLength <= 1)
        return 0.0f;

    const auto clampedPosition = juce::jlimit(0.0f, static_cast<float>(sampleLength - 1), position);
    const auto i1 = static_cast<int>(std::floor(clampedPosition));
    const auto i0 = juce::jmax(0, i1 - 1);
    const auto i2 = juce::jmin(sampleLength - 1, i1 + 1);
    const auto i3 = juce::jmin(sampleLength - 1, i1 + 2);
    const auto t = clampedPosition - static_cast<float>(i1);

    const auto y0 = readSampleAt(segments, mapPlaybackIndexToSampleIndex(segments, i0, sampleStart, sampleEndExclusive), channel);
    const auto y1 = readSampleAt(segments, mapPlaybackIndexToSampleIndex(segments, i1, sampleStart, sampleEndExclusive), channel);
    const auto y2 = readSampleAt(segments, mapPlaybackIndexToSampleIndex(segments, i2, sampleStart, sampleEndExclusive), channel);
    const auto y3 = readSampleAt(segments, mapPlaybackIndexToSampleIndex(segments, i3, sampleStart, sampleEndExclusive), channel);

    const auto a0 = -0.5f * y0 + 1.5f * y1 - 1.5f * y2 + 0.5f * y3;
    const auto a1 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
    const auto a2 = -0.5f * y0 + 0.5f * y2;
    const auto a3 = y1;

    return ((a0 * t + a1) * t + a2) * t + a3;
}

float EngineCore::readSampleWindowedSinc(const SampleSegments& segments, const float position) const noexcept
{
    return readSampleWindowedSinc(segments, position, sampleWindowStart_, sampleWindowEnd_ + 1);
}

float EngineCore::readSampleWindowedSinc(const SampleSegments& segments,
                                         const float position,
                                         const int sampleStart,
                                         const int sampleEndExclusive) const noexcept
{
    return readSampleWindowedSinc(segments, position, sampleStart, sampleEndExclusive, 0);
}

float EngineCore::readSampleWindowedSinc(const SampleSegments& segments,
                                         const float position,
                                         const int sampleStart,
                                         const int sampleEndExclusive,
                                         const int channel) const noexcept
{
    constexpr int radius = 4;
    constexpr double pi = juce::MathConstants<double>::pi;

    const auto sampleLength = getEffectivePlaybackLength(segments, sampleStart, sampleEndExclusive);
    if (sampleLength <= 1)
        return 0.0f;

    const auto clampedPosition = juce::jlimit(0.0f, static_cast<float>(sampleLength - 1), position);
    const auto centerIndex = static_cast<int>(std::floor(clampedPosition));

    auto sincWeight = [](const double distance) noexcept
    {
        if (std::abs(distance) < 1.0e-9)
            return 1.0;

        return std::sin(pi * distance) / (pi * distance);
    };

    auto lanczosWeight = [&](const double distance) noexcept
    {
        const auto absoluteDistance = std::abs(distance);
        if (absoluteDistance >= static_cast<double>(radius))
            return 0.0;

        return sincWeight(distance) * sincWeight(distance / static_cast<double>(radius));
    };

    double weightedSum = 0.0;
    double weightSum = 0.0;
    for (int tap = -radius + 1; tap <= radius; ++tap)
    {
        const auto sampleIndex = juce::jlimit(0, sampleLength - 1, centerIndex + tap);
        const auto distance = static_cast<double>(clampedPosition) - static_cast<double>(centerIndex + tap);
        const auto weight = lanczosWeight(distance);
        if (std::abs(weight) <= 1.0e-9)
            continue;

        const auto mappedIndex = mapPlaybackIndexToSampleIndex(segments, sampleIndex, sampleStart, sampleEndExclusive);
        weightedSum += static_cast<double>(readSampleAt(segments, mappedIndex, channel)) * weight;
        weightSum += weight;
    }

    if (std::abs(weightSum) <= 1.0e-9)
    {
        const auto mappedIndex = mapPlaybackIndexToSampleIndex(segments, centerIndex, sampleStart, sampleEndExclusive);
        return readSampleAt(segments, mappedIndex, channel);
    }

    return static_cast<float>(weightedSum / weightSum);
}

float EngineCore::readSampleLinear(const float position) const noexcept
{
    const auto segments = getSampleSegmentsSnapshot();
    if (segments == nullptr)
        return 0.0f;

    return readSampleLinear(*segments, position);
}

void EngineCore::rebuildSampleSegments(const juce::AudioBuffer<float>& monoSampleData) noexcept
{
    rebuildSampleSegments(monoSampleData, {});
}

void EngineCore::rebuildSampleSegments(const juce::AudioBuffer<float>& monoSampleData,
                                       const juce::File& backingFile) noexcept
{
    ++segmentRebuildCount_;

    sampleSegments_.store(buildSampleSegments(monoSampleData, preloadSamples_, backingFile), std::memory_order_release);
}

std::shared_ptr<const EngineCore::SampleSegments> EngineCore::getSampleSegmentsSnapshot() const noexcept
{
    return sampleSegments_.load(std::memory_order_acquire);
}

std::shared_ptr<const EngineCore::SampleSegments> EngineCore::buildSampleSegments(
    const juce::AudioBuffer<float>& monoSampleData,
    const int preloadSamples,
    const juce::File& backingFile) noexcept
{
    auto segments = std::make_shared<SampleSegments>();

    const auto totalSamples = monoSampleData.getNumSamples();
    const auto sourceChannels = monoSampleData.getNumChannels();
    const auto channels = juce::jmax(1, sourceChannels);
    const auto clampedPreload = juce::jlimit(0, totalSamples, preloadSamples);
    const auto streamSamples = juce::jmax(0, totalSamples - clampedPreload);

    segments->preloadData.setSize(channels, clampedPreload, false, true, true);
    segments->preloadData.clear();

    juce::AudioBuffer<float> streamData(channels, streamSamples);
    streamData.clear();

    for (int channel = 0; channel < channels; ++channel)
    {
        if (sourceChannels <= 0)
            continue;

        const auto sourceChannel = juce::jmin(channel, sourceChannels - 1);

        if (clampedPreload > 0)
            segments->preloadData.copyFrom(channel, 0, monoSampleData, sourceChannel, 0, clampedPreload);

        if (streamSamples > 0)
            streamData.copyFrom(channel, 0, monoSampleData, sourceChannel, clampedPreload, streamSamples);
    }

    if (streamSamples > 0)
    {
        if (backingFile.existsAsFile())
        {
            segments->backingFilePath = backingFile.getFullPathName();
            segments->streamSource = std::make_shared<DiskSampleStreamSource>(backingFile, std::move(streamData), clampedPreload);
        }
        else
        {
            segments->streamSource = std::make_shared<MemorySampleStreamSource>(std::move(streamData));
        }
    }

    return segments;
}

juce::AudioBuffer<float> EngineCore::materializeSampleData(const SampleSegments& segments) noexcept
{
    const auto preloadSamples = segments.preloadData.getNumSamples();
    const auto streamSamples = segments.getStreamNumSamples();
    const auto totalSamples = preloadSamples + streamSamples;
    const auto channels = juce::jmax(1,
        segments.preloadData.getNumChannels(),
        segments.getStreamNumChannels());

    juce::AudioBuffer<float> materialized(channels, totalSamples);
    materialized.clear();

    for (int channel = 0; channel < channels; ++channel)
    {
        if (preloadSamples > 0 && segments.preloadData.getNumChannels() > 0)
        {
            const auto sourceChannel = juce::jmin(channel, segments.preloadData.getNumChannels() - 1);
            materialized.copyFrom(channel, 0, segments.preloadData, sourceChannel, 0, preloadSamples);
        }

        if (streamSamples > 0 && segments.backingFilePath.isNotEmpty())
            continue;

        if (streamSamples > 0 && segments.streamSource != nullptr)
        {
            for (int sampleIndex = 0; sampleIndex < streamSamples; ++sampleIndex)
                materialized.setSample(channel,
                                       preloadSamples + sampleIndex,
                                       segments.streamSource->readSample(channel, sampleIndex));
        }
    }

    if (streamSamples > 0 && segments.backingFilePath.isNotEmpty())
    {
        juce::AudioFormatManager formatManager;
        formatManager.registerBasicFormats();

        if (std::unique_ptr<juce::AudioFormatReader> reader(formatManager.createReaderFor(juce::File(segments.backingFilePath)));
            reader != nullptr && reader->numChannels > 0)
        {
            juce::AudioBuffer<float> streamData(static_cast<int>(reader->numChannels), streamSamples);
            streamData.clear();

            if (reader->read(&streamData,
                             0,
                             streamSamples,
                             static_cast<juce::int64>(preloadSamples),
                             true,
                             true))
            {
                for (int channel = 0; channel < channels; ++channel)
                {
                    if (channels == 1 && streamData.getNumChannels() > 1)
                    {
                        for (int sampleIndex = 0; sampleIndex < streamSamples; ++sampleIndex)
                        {
                            float sum = 0.0f;
                            for (int sourceChannel = 0; sourceChannel < streamData.getNumChannels(); ++sourceChannel)
                                sum += streamData.getSample(sourceChannel, sampleIndex);

                            materialized.setSample(channel,
                                                   preloadSamples + sampleIndex,
                                                   sum / static_cast<float>(streamData.getNumChannels()));
                        }

                        continue;
                    }

                    const auto sourceChannel = juce::jmin(channel, streamData.getNumChannels() - 1);
                    materialized.copyFrom(channel, preloadSamples, streamData, sourceChannel, 0, streamSamples);
                }
            }
        }
    }

    return materialized;
}

std::shared_ptr<const EngineCore::ProgramAudioSnapshot> EngineCore::rebuildProgramAudioSnapshot(
    const ProgramAudioSnapshot& snapshot) const noexcept
{
    auto rebuilt = std::make_shared<ProgramAudioSnapshot>();
    rebuilt->sampleAssetCount = snapshot.sampleAssetCount;

    for (std::size_t assetIndex = 0; assetIndex < snapshot.sampleAssetCount; ++assetIndex)
    {
        const auto* segments = snapshot.getSampleSegments(static_cast<int>(assetIndex));
        if (segments == nullptr)
            continue;

        const auto source = materializeSampleData(*segments);
        if (source.getNumSamples() <= 0)
            continue;

        const auto backingFile = segments->backingFilePath.isNotEmpty() ? juce::File(segments->backingFilePath) : juce::File();
        rebuilt->sampleSegments[assetIndex] = buildSampleSegments(source, preloadSamples_, backingFile);
    }

    return rebuilt;
}

int EngineCore::countProgramSegmentSamples(const ProgramAudioSnapshot& snapshot, const bool usePreloadData) noexcept
{
    auto totalSamples = 0;
    for (std::size_t assetIndex = 0; assetIndex < snapshot.sampleAssetCount; ++assetIndex)
    {
        const auto* segments = snapshot.getSampleSegments(static_cast<int>(assetIndex));
        if (segments == nullptr)
            continue;

        totalSamples += usePreloadData
            ? segments->preloadData.getNumSamples()
            : segments->getStreamNumSamples();
    }

    return totalSamples;
}

int EngineCore::getTotalSampleLength(const SampleSegments& segments) const noexcept
{
    return segments.preloadData.getNumSamples() + segments.getStreamNumSamples();
}

int EngineCore::getTotalSampleLength() const noexcept
{
    const auto segments = getSampleSegmentsSnapshot();
    return segments != nullptr ? getTotalSampleLength(*segments) : 0;
}

int EngineCore::getEffectivePlaybackLength(const SampleSegments& segments) const noexcept
{
    return getEffectivePlaybackLength(segments, sampleWindowStart_, sampleWindowEnd_ + 1);
}

int EngineCore::getEffectivePlaybackLength(const SampleSegments& segments,
                                           const int sampleStart,
                                           const int sampleEndExclusive) const noexcept
{
    const auto totalLength = getTotalSampleLength(segments);
    if (totalLength <= 0)
        return 0;

    const auto useZoneWindow = sampleEndExclusive > sampleStart;
    const auto requestedStart = useZoneWindow ? sampleStart : sampleWindowStart_;
    const auto requestedEnd = useZoneWindow ? (sampleEndExclusive - 1) : sampleWindowEnd_;
    const auto clampedStart = juce::jlimit(0, totalLength - 1, requestedStart);
    const auto clampedEnd = juce::jlimit(clampedStart, totalLength - 1, requestedEnd);
    return clampedEnd - clampedStart + 1;
}

int EngineCore::getEffectivePlaybackLength() const noexcept
{
    const auto segments = getSampleSegmentsSnapshot();
    return segments != nullptr ? getEffectivePlaybackLength(*segments) : 0;
}

int EngineCore::mapPlaybackIndexToSampleIndex(const SampleSegments& segments, const int playbackIndex) const noexcept
{
    return mapPlaybackIndexToSampleIndex(segments, playbackIndex, sampleWindowStart_, sampleWindowEnd_ + 1);
}

int EngineCore::mapPlaybackIndexToSampleIndex(const SampleSegments& segments,
                                              const int playbackIndex,
                                              const int sampleStart,
                                              const int sampleEndExclusive) const noexcept
{
    const auto totalLength = getTotalSampleLength(segments);
    if (totalLength <= 0)
        return 0;

    const auto useZoneWindow = sampleEndExclusive > sampleStart;
    const auto requestedStart = useZoneWindow ? sampleStart : sampleWindowStart_;
    const auto requestedEnd = useZoneWindow ? (sampleEndExclusive - 1) : sampleWindowEnd_;
    const auto clampedStart = juce::jlimit(0, totalLength - 1, requestedStart);
    const auto clampedEnd = juce::jlimit(clampedStart, totalLength - 1, requestedEnd);
    const auto clampedPlayback = juce::jlimit(0, clampedEnd - clampedStart, playbackIndex);

    if (reversePlayback_)
        return clampedEnd - clampedPlayback;

    return clampedStart + clampedPlayback;
}

int EngineCore::mapSampleIndexToPlaybackIndex(const SampleSegments& segments,
                                              const int sampleIndex,
                                              const int sampleStart,
                                              const int sampleEndExclusive) const noexcept
{
    const auto totalLength = getTotalSampleLength(segments);
    if (totalLength <= 0)
        return 0;

    const auto useZoneWindow = sampleEndExclusive > sampleStart;
    const auto requestedStart = useZoneWindow ? sampleStart : sampleWindowStart_;
    const auto requestedEnd = useZoneWindow ? (sampleEndExclusive - 1) : sampleWindowEnd_;
    const auto clampedStart = juce::jlimit(0, totalLength - 1, requestedStart);
    const auto clampedEnd = juce::jlimit(clampedStart, totalLength - 1, requestedEnd);
    const auto clampedSample = juce::jlimit(clampedStart, clampedEnd, sampleIndex);

    if (reversePlayback_)
        return clampedEnd - clampedSample;

    return clampedSample - clampedStart;
}

float EngineCore::computeEditGain(const float playbackPosition, const int playbackLength) const noexcept
{
    if (playbackLength <= 1)
        return 0.0f;

    auto gain = 1.0f;

    if (fadeInSamples_ > 0 && playbackPosition < static_cast<float>(fadeInSamples_))
        gain = juce::jmin(gain, playbackPosition / static_cast<float>(fadeInSamples_));

    if (fadeOutSamples_ > 0)
    {
        const auto fadeOutStart = static_cast<float>(juce::jmax(0, playbackLength - fadeOutSamples_ - 1));
        if (playbackPosition >= fadeOutStart)
        {
            const auto remaining = static_cast<float>(playbackLength - 1) - playbackPosition;
            gain = juce::jmin(gain, remaining / static_cast<float>(fadeOutSamples_));
        }
    }

    return juce::jlimit(0.0f, 1.0f, gain);
}

float EngineCore::readSampleAt(const SampleSegments& segments, const int index) const noexcept
{
    return readSampleAt(segments, index, 0);
}

float EngineCore::readSampleAt(const SampleSegments& segments, const int index, const int channel) const noexcept
{
    if (index < 0)
        return 0.0f;

    const auto channelCount = juce::jmax(segments.preloadData.getNumChannels(), segments.getStreamNumChannels());
    if (channelCount <= 0)
        return 0.0f;

    const auto clampedChannel = juce::jlimit(0, channelCount - 1, channel);
    const auto preloadSamples = segments.preloadData.getNumSamples();

    if (index < preloadSamples)
        return segments.preloadData.getSample(clampedChannel, index);

    const auto streamIndex = index - preloadSamples;
    if (segments.streamSource != nullptr)
        return segments.streamSource->readSample(clampedChannel, streamIndex);

    return 0.0f;
}

float EngineCore::readSampleAt(const int index) const noexcept
{
    const auto segments = getSampleSegmentsSnapshot();
    if (segments == nullptr)
        return 0.0f;

    return readSampleAt(*segments, index);
}

std::vector<float> EngineCore::buildDisplayPeaks(const int maxPeaks) const noexcept
{
    const auto byChannel = buildDisplayPeaksByChannel(maxPeaks);
    if (byChannel.empty())
        return {};

    return byChannel.front();
}

std::vector<std::vector<float>> EngineCore::buildDisplayPeaksByChannel(const int maxPeaks) const noexcept
{
    const auto minMaxByChannel = buildDisplayMinMaxByChannel(maxPeaks);
    if (minMaxByChannel.empty())
        return {};

    std::vector<std::vector<float>> allPeaks(minMaxByChannel.size());
    for (std::size_t channel = 0; channel < minMaxByChannel.size(); ++channel)
    {
        const auto& minMax = minMaxByChannel[channel];
        auto& peaks = allPeaks[channel];
        peaks.resize(minMax.size(), 0.0f);
        for (std::size_t i = 0; i < minMax.size(); ++i)
        {
            const auto magnitude = juce::jmax(std::abs(minMax[i].minValue), std::abs(minMax[i].maxValue));
            peaks[i] = juce::jlimit(0.0f, 1.0f, magnitude);
        }
    }

    return allPeaks;
}

std::vector<std::vector<EngineCore::DisplayMinMax>> EngineCore::buildDisplayMinMaxByChannel(const int maxPeaks) const noexcept
{
    const auto channels = juce::jmax(1, displaySampleData_.getNumChannels());
    const auto total = juce::jmax(0, displaySampleData_.getNumSamples());
    if (total <= 0 || maxPeaks <= 0)
        return {};

    const auto peakCount = juce::jmax(1, juce::jmin(maxPeaks, total));
    std::vector<std::vector<DisplayMinMax>> allMinMax(static_cast<std::size_t>(channels),
        std::vector<DisplayMinMax>(static_cast<std::size_t>(peakCount)));

    for (int channel = 0; channel < channels; ++channel)
    {
        const auto* samples = displaySampleData_.getReadPointer(channel);
        for (int i = 0; i < peakCount; ++i)
        {
            // Use int64_t to avoid overflow when i * total exceeds INT_MAX
            const auto start = static_cast<int>((static_cast<int64_t>(i) * total) / peakCount);
            const auto endExclusive = juce::jmax(start + 1,
                static_cast<int>((static_cast<int64_t>(i + 1) * total) / peakCount));

            float minValue = 1.0f;
            float maxValue = -1.0f;
            for (int s = start; s < endExclusive; ++s)
            {
                minValue = juce::jmin(minValue, samples[s]);
                maxValue = juce::jmax(maxValue, samples[s]);
            }

            auto& bucket = allMinMax[static_cast<std::size_t>(channel)][static_cast<std::size_t>(i)];
            bucket.minValue = juce::jlimit(-1.0f, 1.0f, minValue);
            bucket.maxValue = juce::jlimit(-1.0f, 1.0f, maxValue);
        }
    }

    return allMinMax;
}

float EngineCore::computeFilterResonanceQ() const noexcept
{
    return juce::jlimit(0.5f, 20.0f, 0.5f + filterSettings_.resonance * 19.5f);
}

void EngineCore::processDelay(float** outputs, const int numChannels, const int numSamples) noexcept
{
    if (outputs == nullptr || numChannels <= 0 || numSamples <= 0)
        return;

    if (delaySettings_.mix <= 0.0001f || delayBuffer_.getNumSamples() <= 1)
        return;

    const auto effectiveTimeMs = delaySettings_.tempoSync
        ? computeSyncedDelayTimeMs(delaySettings_.timeMs)
        : delaySettings_.timeMs;

    const auto delaySamples = juce::jlimit(1, delayBuffer_.getNumSamples() - 1,
        static_cast<int>(std::round((effectiveTimeMs / 1000.0f) * static_cast<float>(sampleRate_))));

    int readPos = delayWritePos_ - delaySamples;
    while (readPos < 0)
        readPos += delayBuffer_.getNumSamples();

    const auto channelsToProcess = juce::jmin(numChannels, delayBuffer_.getNumChannels());
    const auto dry = 1.0f - delaySettings_.mix;
    const auto wet = delaySettings_.mix;
    const auto feedback = delaySettings_.feedback;

    for (int sample = 0; sample < numSamples; ++sample)
    {
        for (int channel = 0; channel < channelsToProcess; ++channel)
        {
            const auto input = outputs[channel][sample];
            const auto delayed = delayBuffer_.getSample(channel, readPos);
            outputs[channel][sample] = input * dry + delayed * wet;
            delayBuffer_.setSample(channel, delayWritePos_, input + delayed * feedback);
        }

        ++delayWritePos_;
        if (delayWritePos_ >= delayBuffer_.getNumSamples())
            delayWritePos_ = 0;

        ++readPos;
        if (readPos >= delayBuffer_.getNumSamples())
            readPos = 0;
    }
}

void EngineCore::processDcFilter(float** outputs, const int numChannels, const int numSamples) noexcept
{
    if (outputs == nullptr || numChannels <= 0 || numSamples <= 0)
        return;

    if (!dcFilterSettings_.enabled)
        return;

    const auto channelsToProcess = juce::jmin(numChannels, static_cast<int>(dcBlockFilters_.size()));
    for (int channel = 0; channel < channelsToProcess; ++channel)
    {
        auto& filter = dcBlockFilters_[static_cast<std::size_t>(channel)];
        auto* channelData = outputs[channel];
        for (int sample = 0; sample < numSamples; ++sample)
            channelData[sample] = filter.processSample(0, channelData[sample]);
    }
}

float EngineCore::processSaturationSample(const float sample) const noexcept
{
    const auto drive = juce::jlimit(0.0f, 1.0f, saturationSettings_.drive);
    if (drive <= 1.0e-5f)
        return sample;

    const auto driveGain = 1.0f + drive * 9.0f;
    const auto input = sample * driveGain;
    float shaped = input;

    switch (saturationSettings_.mode)
    {
        case SaturationSettings::Mode::hardClip:
            shaped = juce::jlimit(-1.0f, 1.0f, input);
            break;
        case SaturationSettings::Mode::tape:
            shaped = std::atan(input * 0.85f) * (2.0f / juce::MathConstants<float>::pi);
            break;
        case SaturationSettings::Mode::tube:
        {
            const auto cubic = input - (0.2f * input * input * input);
            shaped = std::tanh(cubic);
            break;
        }
        case SaturationSettings::Mode::softClip:
        default:
            shaped = std::tanh(input);
            break;
    }

    const auto outputGain = 1.0f / (1.0f + drive * 0.55f);
    return shaped * outputGain;
}

float EngineCore::computeSyncedDelayTimeMs(const float rawTimeMs) const noexcept
{
    const auto bpm = juce::jmax(1.0f, hostTempoBpm_);
    const auto beatMs = 60000.0f / bpm;
    constexpr std::array<float, 10> beatDivisions{
        0.25f,
        1.0f / 3.0f,
        0.5f,
        2.0f / 3.0f,
        0.75f,
        1.0f,
        1.5f,
        2.0f,
        3.0f,
        4.0f
    };

    auto bestMs = beatMs * beatDivisions[0];
    auto bestDistance = std::abs(bestMs - rawTimeMs);
    for (std::size_t index = 1; index < beatDivisions.size(); ++index)
    {
        const auto candidateMs = beatMs * beatDivisions[index];
        const auto candidateDistance = std::abs(candidateMs - rawTimeMs);
        if (candidateDistance < bestDistance)
        {
            bestDistance = candidateDistance;
            bestMs = candidateMs;
        }
    }

    return juce::jlimit(1.0f, 2000.0f, bestMs);
}

float EngineCore::mapVelocity(const float velocity) const noexcept
{
    const auto clamped = juce::jlimit(0.0f, 1.0f, velocity);
    switch (velocityCurve_)
    {
        case VelocityCurve::soft:
            return std::sqrt(clamped);
        case VelocityCurve::hard:
            return clamped * clamped;
        case VelocityCurve::linear:
        default:
            return clamped;
    }
}

void EngineCore::updateReverbParameters() noexcept
{
    juce::Reverb::Parameters params;
    params.roomSize = 0.55f;
    params.damping = 0.40f;
    params.wetLevel = reverbMix_;
    params.dryLevel = 1.0f - (reverbMix_ * 0.65f);
    params.width = 1.0f;
    params.freezeMode = 0.0f;
    reverb_.setParameters(params);
}
}
