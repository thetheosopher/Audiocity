#include "PluginProcessor.h"

#include "PluginEditor.h"
#include "../engine/sfz/SfzImport.h"

namespace
{
constexpr auto kPatchRoot = "AudiocityPatch";
constexpr auto kSamplePath = "samplePath";
constexpr auto kRootMidiNote = "rootMidiNote";

constexpr auto kAmpAttack = "ampAttack";
constexpr auto kAmpDecay = "ampDecay";
constexpr auto kAmpSustain = "ampSustain";
constexpr auto kAmpRelease = "ampRelease";

constexpr auto kFilterAttack = "filterAttack";
constexpr auto kFilterDecay = "filterDecay";
constexpr auto kFilterSustain = "filterSustain";
constexpr auto kFilterRelease = "filterRelease";
constexpr auto kFilterBaseCutoff = "filterBaseCutoff";
constexpr auto kFilterEnvAmount = "filterEnvAmount";
constexpr auto kRrMode = "rrMode";
constexpr auto kPlaybackMode = "playbackMode";
constexpr auto kPreloadSamples = "preloadSamples";
constexpr auto kMonoMode = "monoMode";
constexpr auto kLegatoMode = "legatoMode";
constexpr auto kGlideSeconds = "glideSeconds";
constexpr auto kChokeGroup = "chokeGroup";
constexpr auto kLoopStart = "loopStart";
constexpr auto kLoopEnd = "loopEnd";
constexpr auto kSfzLoopMode = "sfzLoopMode";

constexpr auto kBrowserState = "BrowserState";
constexpr auto kWatchedFolder = "WatchedFolder";
constexpr auto kFavorite = "Favorite";
constexpr auto kRecent = "Recent";
constexpr auto kPath = "path";
}

AudiocityAudioProcessor::AudiocityAudioProcessor()
    : AudioProcessor(BusesProperties().withOutput("Output", juce::AudioChannelSet::stereo(), true))
{
}

void AudiocityAudioProcessor::prepareToPlay(const double sampleRate, const int samplesPerBlock)
{
    engine_.prepare(sampleRate, samplesPerBlock, getTotalNumOutputChannels());
}

void AudiocityAudioProcessor::releaseResources()
{
    engine_.release();
}

bool AudiocityAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    const auto& mainOut = layouts.getMainOutputChannelSet();
    return mainOut == juce::AudioChannelSet::mono() || mainOut == juce::AudioChannelSet::stereo();
}

void AudiocityAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;

    const auto numInputChannels = getTotalNumInputChannels();
    const auto numOutputChannels = getTotalNumOutputChannels();

    for (auto channel = numInputChannels; channel < numOutputChannels; ++channel)
        buffer.clear(channel, 0, buffer.getNumSamples());

    if (previewStopRequested_.exchange(false))
        engine_.noteOff(previewMidiNote_, 0);

    if (previewStartRequested_.exchange(false))
    {
        engine_.noteOff(previewMidiNote_, 0);
        engine_.noteOn(previewMidiNote_, 0.85f, 0);
    }

    engine_.render(buffer, midiMessages);
}

juce::AudioProcessorEditor* AudiocityAudioProcessor::createEditor()
{
    return new AudiocityAudioProcessorEditor(*this);
}

void AudiocityAudioProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    auto state = juce::ValueTree(kPatchRoot);

    state.setProperty(kSamplePath, engine_.getSamplePath(), nullptr);
    state.setProperty(kRootMidiNote, engine_.getRootMidiNote(), nullptr);

    const auto amp = engine_.getAmpEnvelope();
    state.setProperty(kAmpAttack, amp.attackSeconds, nullptr);
    state.setProperty(kAmpDecay, amp.decaySeconds, nullptr);
    state.setProperty(kAmpSustain, amp.sustainLevel, nullptr);
    state.setProperty(kAmpRelease, amp.releaseSeconds, nullptr);

    const auto filterAdsr = engine_.getFilterEnvelope();
    state.setProperty(kFilterAttack, filterAdsr.attackSeconds, nullptr);
    state.setProperty(kFilterDecay, filterAdsr.decaySeconds, nullptr);
    state.setProperty(kFilterSustain, filterAdsr.sustainLevel, nullptr);
    state.setProperty(kFilterRelease, filterAdsr.releaseSeconds, nullptr);

    const auto filter = engine_.getFilterSettings();
    state.setProperty(kFilterBaseCutoff, filter.baseCutoffHz, nullptr);
    state.setProperty(kFilterEnvAmount, filter.envAmountHz, nullptr);
    state.setProperty(kRrMode, getRoundRobinMode() == RoundRobinMode::random ? 1 : 0, nullptr);
    state.setProperty(kPlaybackMode,
        getPlaybackMode() == PlaybackMode::oneShot ? 1 : (getPlaybackMode() == PlaybackMode::loop ? 2 : 0),
        nullptr);
    state.setProperty(kPreloadSamples, getPreloadSamples(), nullptr);
    state.setProperty(kMonoMode, getMonoMode() ? 1 : 0, nullptr);
    state.setProperty(kLegatoMode, getLegatoMode() ? 1 : 0, nullptr);
    state.setProperty(kGlideSeconds, getGlideSeconds(), nullptr);
    state.setProperty(kChokeGroup, getChokeGroup(), nullptr);
    state.setProperty(kLoopStart, engine_.getLoopStart(), nullptr);
    state.setProperty(kLoopEnd, engine_.getLoopEnd(), nullptr);
    state.setProperty(kSfzLoopMode,
        engine_.getSfzLoopMode() == audiocity::engine::EngineCore::SfzLoopMode::loopSustain ? 1
            : (engine_.getSfzLoopMode() == audiocity::engine::EngineCore::SfzLoopMode::loopContinuous ? 2 : 0),
        nullptr);

    juce::ValueTree browserState(kBrowserState);

    for (const auto& folderPath : browserIndex_.getWatchedFolders())
    {
        juce::ValueTree node(kWatchedFolder);
        node.setProperty(kPath, folderPath, nullptr);
        browserState.appendChild(node, nullptr);
    }

    for (const auto& favoritePath : browserIndex_.getFavoritePaths())
    {
        juce::ValueTree node(kFavorite);
        node.setProperty(kPath, favoritePath, nullptr);
        browserState.appendChild(node, nullptr);
    }

    for (const auto& recentPath : browserIndex_.getRecentPaths(128))
    {
        juce::ValueTree node(kRecent);
        node.setProperty(kPath, recentPath, nullptr);
        browserState.appendChild(node, nullptr);
    }

    state.appendChild(browserState, nullptr);

    if (auto xml = state.createXml())
        copyXmlToBinary(*xml, destData);
}

void AudiocityAudioProcessor::setStateInformation(const void* data, const int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xml(getXmlFromBinary(data, sizeInBytes));
    if (xml == nullptr)
        return;

    const auto state = juce::ValueTree::fromXml(*xml);
    if (!state.isValid() || !state.hasType(kPatchRoot))
        return;

    const auto samplePath = state.getProperty(kSamplePath).toString();
    if (samplePath.isNotEmpty())
        loadSampleFromFile(juce::File(samplePath));

    engine_.setRootMidiNote(static_cast<int>(state.getProperty(kRootMidiNote, engine_.getRootMidiNote())));

    auto amp = engine_.getAmpEnvelope();
    amp.attackSeconds = static_cast<float>(state.getProperty(kAmpAttack, amp.attackSeconds));
    amp.decaySeconds = static_cast<float>(state.getProperty(kAmpDecay, amp.decaySeconds));
    amp.sustainLevel = static_cast<float>(state.getProperty(kAmpSustain, amp.sustainLevel));
    amp.releaseSeconds = static_cast<float>(state.getProperty(kAmpRelease, amp.releaseSeconds));
    engine_.setAmpEnvelope(amp);

    auto filterAdsr = engine_.getFilterEnvelope();
    filterAdsr.attackSeconds = static_cast<float>(state.getProperty(kFilterAttack, filterAdsr.attackSeconds));
    filterAdsr.decaySeconds = static_cast<float>(state.getProperty(kFilterDecay, filterAdsr.decaySeconds));
    filterAdsr.sustainLevel = static_cast<float>(state.getProperty(kFilterSustain, filterAdsr.sustainLevel));
    filterAdsr.releaseSeconds = static_cast<float>(state.getProperty(kFilterRelease, filterAdsr.releaseSeconds));
    engine_.setFilterEnvelope(filterAdsr);

    auto filter = engine_.getFilterSettings();
    filter.baseCutoffHz = static_cast<float>(state.getProperty(kFilterBaseCutoff, filter.baseCutoffHz));
    filter.envAmountHz = static_cast<float>(state.getProperty(kFilterEnvAmount, filter.envAmountHz));
    engine_.setFilterSettings(filter);

    setRoundRobinMode(static_cast<int>(state.getProperty(kRrMode, 0)) == 1 ? RoundRobinMode::random : RoundRobinMode::ordered);
    const auto playbackMode = static_cast<int>(state.getProperty(kPlaybackMode, 0));
    setPlaybackMode(playbackMode == 1 ? PlaybackMode::oneShot : (playbackMode == 2 ? PlaybackMode::loop : PlaybackMode::gate));
    setPreloadSamples(static_cast<int>(state.getProperty(kPreloadSamples, getPreloadSamples())));
    setMonoMode(static_cast<int>(state.getProperty(kMonoMode, getMonoMode() ? 1 : 0)) == 1);
    setLegatoMode(static_cast<int>(state.getProperty(kLegatoMode, getLegatoMode() ? 1 : 0)) == 1);
    setGlideSeconds(static_cast<float>(state.getProperty(kGlideSeconds, getGlideSeconds())));
    setChokeGroup(static_cast<int>(state.getProperty(kChokeGroup, getChokeGroup())));
    engine_.setLoopPoints(static_cast<int>(state.getProperty(kLoopStart, engine_.getLoopStart())),
        static_cast<int>(state.getProperty(kLoopEnd, engine_.getLoopEnd())));
    const auto sfzLoopMode = static_cast<int>(state.getProperty(kSfzLoopMode, 0));
    engine_.setSfzLoopMode(sfzLoopMode == 1 ? audiocity::engine::EngineCore::SfzLoopMode::loopSustain
        : (sfzLoopMode == 2 ? audiocity::engine::EngineCore::SfzLoopMode::loopContinuous
                            : audiocity::engine::EngineCore::SfzLoopMode::noLoop));

    const auto browserState = state.getChildWithName(kBrowserState);
    if (browserState.isValid())
    {
        juce::StringArray watchedFolders;
        juce::StringArray favorites;
        juce::StringArray recent;

        for (int i = 0; i < browserState.getNumChildren(); ++i)
        {
            const auto child = browserState.getChild(i);
            const auto path = child.getProperty(kPath).toString();
            if (path.isEmpty())
                continue;

            if (child.hasType(kWatchedFolder))
                watchedFolders.add(path);
            else if (child.hasType(kFavorite))
                favorites.add(path);
            else if (child.hasType(kRecent))
                recent.add(path);
        }

        if (!watchedFolders.isEmpty())
            browserIndex_.setWatchedFolders(watchedFolders);

        browserIndex_.setFavoritePaths(favorites);
        browserIndex_.setRecentPaths(recent);
    }
}

bool AudiocityAudioProcessor::loadSampleFromFile(const juce::File& file)
{
    return engine_.loadSampleFromFile(file);
}

juce::String AudiocityAudioProcessor::getLoadedSamplePath() const
{
    return engine_.getSamplePath();
}

bool AudiocityAudioProcessor::importSfzFile(const juce::File& file)
{
    audiocity::engine::sfz::Importer importer;
    sfzProgram_ = importer.importFromFile(file);
    zoneSelector_.setZones(sfzProgram_.zones);

    if (!sfzProgram_.zones.empty())
    {
        const auto& zone = sfzProgram_.zones.front();
        engine_.setRootMidiNote(zone.pitchKeycenter);

        engine_.loadSampleFromFile(juce::File(zone.resolvedSamplePath));
        engine_.setLoopPoints(zone.loopStart, zone.loopEnd);

        if (zone.loopMode == "loop_continuous")
        {
            engine_.setSfzLoopMode(audiocity::engine::EngineCore::SfzLoopMode::loopContinuous);
            engine_.setPlaybackMode(audiocity::engine::EngineCore::PlaybackMode::loop);
        }
        else if (zone.loopMode == "loop_sustain")
        {
            engine_.setSfzLoopMode(audiocity::engine::EngineCore::SfzLoopMode::loopSustain);
            engine_.setPlaybackMode(audiocity::engine::EngineCore::PlaybackMode::loop);
        }
        else
        {
            engine_.setSfzLoopMode(audiocity::engine::EngineCore::SfzLoopMode::noLoop);
            engine_.setPlaybackMode(audiocity::engine::EngineCore::PlaybackMode::gate);
        }
    }

    return !sfzProgram_.zones.empty();
}

const std::vector<audiocity::engine::sfz::Zone>& AudiocityAudioProcessor::getImportedZones() const noexcept
{
    return sfzProgram_.zones;
}

const std::vector<audiocity::engine::sfz::Diagnostic>& AudiocityAudioProcessor::getImportDiagnostics() const noexcept
{
    return sfzProgram_.diagnostics;
}

bool AudiocityAudioProcessor::updateImportedZoneLoopPoints(const int zoneIndex, const int loopStart, const int loopEnd)
{
    if (zoneIndex < 0 || zoneIndex >= static_cast<int>(sfzProgram_.zones.size()))
        return false;

    auto& zone = sfzProgram_.zones[static_cast<std::size_t>(zoneIndex)];
    zone.loopStart = juce::jmax(0, loopStart);
    zone.loopEnd = juce::jmax(zone.loopStart + 1, loopEnd);

    zoneSelector_.setZones(sfzProgram_.zones);

    if (zoneIndex == 0)
        engine_.setLoopPoints(zone.loopStart, zone.loopEnd);

    return true;
}

void AudiocityAudioProcessor::setRoundRobinMode(const RoundRobinMode mode) noexcept
{
    zoneSelector_.setRoundRobinMode(mode);
}

AudiocityAudioProcessor::RoundRobinMode AudiocityAudioProcessor::getRoundRobinMode() const noexcept
{
    return zoneSelector_.getRoundRobinMode();
}

void AudiocityAudioProcessor::setPlaybackMode(const PlaybackMode mode) noexcept
{
    engine_.setPlaybackMode(mode);
}

AudiocityAudioProcessor::PlaybackMode AudiocityAudioProcessor::getPlaybackMode() const noexcept
{
    return engine_.getPlaybackMode();
}

bool AudiocityAudioProcessor::startPreviewFromPath(const juce::String& path)
{
    const juce::File file(path);
    if (!loadSampleFromFile(file))
        return false;

    browserIndex_.markRecent(path);
    previewStartRequested_.store(true);
    previewStopRequested_.store(false);
    previewPlaying_.store(true);
    return true;
}

void AudiocityAudioProcessor::stopPreview()
{
    previewStopRequested_.store(true);
    previewPlaying_.store(false);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new AudiocityAudioProcessor();
}
